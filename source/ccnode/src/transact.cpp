/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * transact.cpp
*/

#include "CCdef.h"
#include "transact.hpp"
#include "processtx.hpp"
#include "blockchain.hpp"
#include "commitments.hpp"
#include "dbconn.hpp"
#include "dbparamkeys.h"
#include "util.h"

#include <CCobjects.hpp>
#include <CCutil.h>
#include <CCticks.hpp>

#include <transaction.h>
#include <CCproof.h>
#include <Finally.hpp>

#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#include <blake2/blake2b.h>

#include <boost/date_time/posix_time/posix_time.hpp>

//#define JSON_ENDL	;
#define JSON_ENDL	<< endl;

#define TRANSACT_MAX_REQUEST_SIZE		64000
#define TRANSACT_MAX_REPLY_SIZE			64000

#define TRANSACT_QUERY_MAX_COMMITS		2

#define TRANSACT_READ_TIMEOUT			10
#define TRANSACT_VALIDATION_TIMEOUT		20

#define TRACE_TRANSACT	(g_params.trace_tx_server)

thread_local DbConn *tx_dbconn;

void TransactConnection::StartConnection()
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::StartConnection";

	if (SetTimer(TRANSACT_READ_TIMEOUT))
		return;

	Connection::StartConnection();
}

void TransactConnection::HandleReadComplete()
{
	if (m_nred < CC_MSG_HEADER_SIZE + TX_POW_SIZE)
	{
		static const string outbuf = "ERROR:unexpected short read";

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleReadComplete error short read " << m_nred << "; sending " << outbuf;

		WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	unsigned size = *(uint32_t*)m_pread;
	unsigned tag = *(uint32_t*)(m_pread + 4);

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleReadComplete read " << m_nred << " bytes msg size " << size << " tag " << tag;

	if (size < CC_MSG_HEADER_SIZE + TX_POW_SIZE || size > TRANSACT_MAX_REQUEST_SIZE)
	{
		static const string outbuf = "ERROR:message size field invalid";

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleReadComplete error invalid size " << size << "; sending " << outbuf;

		WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	unsigned clock_allowance;
	SmartBuf smartobj;

	switch (tag)
	{
	case CC_TAG_TX_QUERY_PARAMS:
		BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleReadComplete CC_TAG_TX_QUERY_PARAMS";

		clock_allowance = 0;
		break;

	case CC_TAG_TX_QUERY_ADDRESS:
	case CC_TAG_TX_QUERY_INPUTS:
	case CC_TAG_TX_QUERY_SERIAL:
		BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleReadComplete CC_TAG_TX_QUERY_ADDRESS/INPUTS/SERIAL";

		clock_allowance = 5*60;
		break;

	case CC_TAG_TX_WIRE:
	{
		BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleReadComplete CC_TAG_TX_WIRE";

		clock_allowance = 5*60;

		CCASSERT(CC_MSG_HEADER_SIZE == sizeof(CCObject::Header));

		smartobj = SmartBuf(size + sizeof(CCObject::Preamble));
		if (!smartobj)
		{
			BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleReadComplete error smartobj failed";

			return Stop();
		}

		auto obj = (CCObject*)smartobj.data();

		memcpy(obj->ObjPtr(), m_pread, m_nred);

		m_pread = obj->ObjPtr();

		break;
	}

	default:
		// note: this results in a forcible close because it doesn't read all of the data that was sent

		static const string outbuf = "ERROR:unrecognized message type";

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleReadComplete error unrecognized message tag " << tag << "; sending " << outbuf;

		WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	if (clock_allowance && tx_check_timestamp(*(uint64_t*)(m_pread + CC_MSG_HEADER_SIZE), clock_allowance))
	{
		char *outbuf = (char*)m_writebuf.data();

		#if ULONG_MAX == 0xffffffffffffffff
		sprintf(outbuf, "ERROR:invalid timestamp:%lu", _time64(NULL));
		#else
		sprintf(outbuf, "ERROR:invalid timestamp:%llu", _time64(NULL));
		#endif

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleMsgReadComplete error invalid timestamp; sending " << outbuf;

		WriteAsync("TransactConnection::HandleMsgReadComplete", boost::asio::buffer(outbuf, strlen(outbuf)),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	m_maxread = size;

	if (m_maxread > m_nred)
	{
		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleReadComplete queueing read size " << m_maxread - m_nred;

		ReadAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_exactly(m_maxread - m_nred),
				boost::bind(&TransactConnection::HandleMsgReadComplete, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, smartobj, AutoCount(this)));
	}
	else
	{
		HandleMsgReadComplete(boost::system::error_code(), 0, smartobj, AutoCount());	// don't need to increment op count
	}
}

void TransactConnection::HandleMsgReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter)
{
	bool sim_err = ((TEST_RANDOM_READ_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleMsgReadComplete simulating read error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleMsgReadComplete error " << e << " " << e.message() << "; read " << bytes_transferred << " total " << m_nred;

		return Stop();
	}

	m_nred += bytes_transferred;

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleMsgReadComplete read " << bytes_transferred << " total " << m_nred;

	unsigned size = *(uint32_t*)m_pread;
	unsigned tag = *(uint32_t*)(m_pread + 4);

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleMsgReadComplete read " << m_nred << " bytes msg size " << size << " tag " << tag;

	if (size != m_nred)
	{
		static const string outbuf = "ERROR:message size field does not match bytes received";

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleMsgReadComplete error size " << size << " mismatch " << m_nred << "; sending " << outbuf;

		WriteAsync("TransactConnection::HandleMsgReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	CCASSERT(size >= CC_MSG_HEADER_SIZE + TX_POW_SIZE);

	uint64_t proof_difficulty = 0;
	ccoid_t objhash;
	void *pobjhash = &objhash;

	switch (tag)
	{
	case CC_TAG_TX_QUERY_PARAMS:
		break;

	case CC_TAG_TX_QUERY_ADDRESS:
	case CC_TAG_TX_QUERY_INPUTS:
	case CC_TAG_TX_QUERY_SERIAL:
	{
		proof_difficulty = g_params.query_work_difficulty;
		const unsigned data_offset = CC_MSG_HEADER_SIZE + TX_POW_SIZE;
		auto rc = blake2b(&objhash, sizeof(objhash), NULL, 0, m_pread + data_offset, size - data_offset);
		CCASSERTZ(rc);
		break;
	}

	case CC_TAG_TX_WIRE:
	{
		proof_difficulty = g_params.tx_work_difficulty;
		auto obj = (CCObject*)smartobj.data();
		if (!obj->IsValid())
		{
			static const string outbuf = "ERROR:binary object not valid";

			BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleMsgReadComplete error object IsValid false; sending " << outbuf;

			WriteAsync("TransactConnection::HandleTx", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

			return;
		}

		obj->SetObjId();
		pobjhash = obj->OidPtr();

		#if TEST_SEQ_TX_OID
		static std::atomic<uint32_t> seq(1);
		*(uint32_t*)(obj->DataPtr()) = seq.fetch_add(1);
		#endif

		break;
	}

	default:
		CCASSERT(0);	// need to handle all tags passed by HandleReadComplete
	}

	if (proof_difficulty && tx_set_work((char*)m_pread, pobjhash, 0, TX_POW_NPROOFS, 1, proof_difficulty))
	{
		char *outbuf = (char*)m_writebuf.data();

		#if ULONG_MAX == 0xffffffffffffffff
		sprintf(outbuf, "ERROR:proof of work failed:%lu", proof_difficulty);
		#else
		sprintf(outbuf, "ERROR:proof of work failed:%llu", proof_difficulty);
		#endif

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleMsgReadComplete error proof of work failed; sending " << outbuf;

		WriteAsync("TransactConnection::HandleMsgReadComplete", boost::asio::buffer(outbuf, strlen(outbuf)),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	m_pread += CC_MSG_HEADER_SIZE + TX_POW_SIZE;
	size -= CC_MSG_HEADER_SIZE + TX_POW_SIZE;

	switch (tag)
	{
	case CC_TAG_TX_QUERY_PARAMS:
		return HandleTxQueryParams(m_pread, size);

	case CC_TAG_TX_QUERY_ADDRESS:
		return HandleTxQueryAddress(m_pread, size);

	case CC_TAG_TX_QUERY_INPUTS:
		return HandleTxQueryInputs(m_pread, size);

	case CC_TAG_TX_QUERY_SERIAL:
		return HandleTxQuerySerial(m_pread, size);

	case CC_TAG_TX_WIRE:
		return HandleTx(smartobj);

	default:
		CCASSERT(0);	// need to handle all tags passed by HandleReadComplete
	}
}

void TransactConnection::HandleTx(SmartBuf smartobj)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTx";

	// !!! check now to see if queue is over full

	// Note: we need to finish use of this connection before calling TxEnqueueValidate, because TxEnqueueValidate will call the callback
	//	which will call write which will close the connection upon completion and free it for reuse
	// Note 2: at the moment, nothing needs to be done to finish use of this connection.

	// set the timer before TxEnqueueValidate so pending ops gets incremented first

	if (SetTimer(TRANSACT_VALIDATION_TIMEOUT))
		return;

	// queue Tx for validation

	static atomic<int64_t> medpriority(1);

	auto priority = medpriority.fetch_add(1, memory_order_acq_rel);
	auto callback_id = expected_callback_id.load();

	auto rc = ProcessTx::TxEnqueueValidate(tx_dbconn, priority, smartobj, m_conn_index, callback_id);
	if (rc)
	{
		CancelTimer();

		return SendServerError(__LINE__);
	}
}

void TransactConnection::HandleValidateDone(unsigned callback_id, int64_t result)
{
	// increment expected_callback_id so either HandleTimeout or HandleValidateDone will run, but not both

	uint32_t expected = callback_id;
	if (!expected_callback_id.compare_exchange_strong(expected, expected + 1))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleValidateDone ignoring late or unexpected callback id " << callback_id;

		return;
	}

	// HandleValidateDone was not passed an AutoCount object since we don't want stop to be delayed while the Tx validation runs
	// But because HandleValidateDone is an async op, we need to acquire an AutoCount now while holding the stop lock

	auto op_pending = AcquireRef();
	if (!op_pending)
	{
		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleValidateDone connection is closing";

		return;
	}

	CancelTimer();

	const char *poutbuf;

	if (result < 0)
		poutbuf = ProcessTx::ResultString(result);
	else
	{
		char *outbuf = (char*)m_writebuf.data();

		#if ULONG_MAX == 0xffffffffffffffff
		sprintf(outbuf, "OK:%lu", result);
		#else
		sprintf(outbuf, "OK:%llu", result);
		#endif

		poutbuf = outbuf;
	}

	if (!poutbuf)
		return SendServerError(__LINE__);

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleValidateDone result " << result << " sending " << poutbuf;

	WriteAsync("TransactConnection::HandleValidateDone", boost::asio::buffer(poutbuf, strlen(poutbuf)),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

bool TransactConnection::SetTimer(unsigned sec)
{
	auto callback_id = expected_callback_id.load();

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::SetTimer callback id " << callback_id;

	auto op_counter = AutoCount();
	return AsyncTimerWait("TransactConnection::SetTimer", sec*1000, boost::bind(&TransactConnection::HandleTimeout, this, callback_id, boost::asio::placeholders::error, op_counter), op_counter);
}

void TransactConnection::HandleTimeout(unsigned callback_id, const boost::system::error_code& e, AutoCount pending_op_counter)
{
	if (e == boost::asio::error::operation_aborted)
		return;

	// increment expected_callback_id so either HandleTimeout or HandleValidateDone will run, but not both

	uint32_t expected = callback_id;
	if (!expected_callback_id.compare_exchange_strong(expected, expected + 1))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTimeout ignoring late timeout callback id " << callback_id << ", e = " << e << " " << e.message();

		return;
	}

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTimeout callback id " << callback_id << ", e = " << e << " " << e.message();

	if (e)
		return SendServerError(__LINE__);
	else
		return SendTimeout();
}

static void StreamNetParams(ostream &os)
{
	os << " \"timestamp\":\"0x" << _time64(NULL) << "\"" JSON_ENDL
	os << ",\"query-work-difficulty\":\"0x" << g_params.query_work_difficulty << "\"" JSON_ENDL
	os << ",\"tx-work-difficulty\":\"0x" << g_params.tx_work_difficulty << "\"" JSON_ENDL
	os << ",\"merkle-tree-oldest-commitment-number\":\"0x0\"" JSON_ENDL
	os << ",\"merkle-tree-next-commitment-number\":\"0x" << g_commitments.GetNextCommitnum() << "\"" JSON_ENDL
}

static void StreamDonationParams(ostream &os)
{
	os << ",\"donation-per-transaction\":\"0x" << g_blockchain.proof_params.donation_per_tx << "\"" JSON_ENDL
	os << ",\"donation-per-byte\":\"0x" << g_blockchain.proof_params.donation_per_byte << "\"" JSON_ENDL
	os << ",\"donation-per-output\":\"0x" << g_blockchain.proof_params.donation_per_output << "\"" JSON_ENDL
	os << ",\"donation-per-input\":\"0x" << g_blockchain.proof_params.donation_per_input << "\"" JSON_ENDL
}

static void StreamValueLimits(ostream &os)
{
	os << ",\"minimum-output-value\":\"0x" << g_blockchain.proof_params.outvalmin << "\"" JSON_ENDL
	os << ",\"maximum-output-value\":\"0x" << g_blockchain.proof_params.outvalmax << "\"" JSON_ENDL
	os << ",\"maximum-input-value\":\"0x" << g_blockchain.proof_params.invalmax << "\"" JSON_ENDL
}

void TransactConnection::HandleTxQueryParams(const uint8_t *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryParams size " << size;

	ostringstream os;
	os << hex;
	os.rdbuf()->pubsetbuf((char*)m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-parameters-query-results\":{" JSON_ENDL
	StreamNetParams(os);
	os << "}}";

	//BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryParams sending " << (char*)m_writebuf.data();

	SendReply(os);
}

void TransactConnection::HandleTxQueryAddress(const uint8_t *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryAddress size " << size;

	bigint_t address;
	uint64_t commitstart;
	static const bool bhex = false;

	uint32_t bufpos = 0;

	copy_from_buf(&address, sizeof(address), bufpos, msg, size, bhex);
	copy_from_buf(&commitstart, sizeof(commitstart), bufpos, msg, size, bhex);

	if (bufpos != size)
	{
		static const string outbuf = "ERROR:malformed binary tx-address-query";

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryAddress error malformed query; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryAddress", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	array<uint64_t, TRANSACT_QUERY_MAX_COMMITS> value_enc, commitnums;
	array<bigint_t, TRANSACT_QUERY_MAX_COMMITS> commitment_iv, commitment;
	bool have_more;

	auto nfound = tx_dbconn->TxOutputsSelect(&address, sizeof(address), commitstart, INT64_MAX, &value_enc[0], (char*)&commitment_iv, sizeof(bigint_t), (char*)&commitment, sizeof(bigint_t), &commitnums[0], TRANSACT_QUERY_MAX_COMMITS, &have_more);
	if (nfound < 0)
		return SendServerError(__LINE__);
	if (!nfound)
	{
		static const string outbuf = "Not Found";

		BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryAddress not found; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryAddress", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	//memset(m_writebuf.data(), 0, m_writebuf.size());	// for testing

	ostringstream os;
	os << hex;
	os.rdbuf()->pubsetbuf((char*)m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-address-query-report\":" JSON_ENDL
	os << "{\"address\":\"0x" << address << "\"" JSON_ENDL
	os << ",\"commitment-number-start\":\"0x" << commitstart << "\"" JSON_ENDL
	os << ",\"more-results-available\":\"0x" << (int)have_more << "\"" JSON_ENDL
	os << ",\"tx-address-query-results\":[" JSON_ENDL
	for (int i = 0; i < nfound; ++i)
	{
		CCASSERT(TX_COMMIT_IV_BITS == 128);
		for (int j = 2; j < commitment_iv[i].numberLimbs(); ++j)	// looks like a compiler bug doesn't like unsigned j?
			commitment_iv[i].data()[j] = 0;
		if (i)
			os << ",";
		os << "{\"encrypted-value\":\"0x" << value_enc[i] << "\"" JSON_ENDL
		os << ",\"commitment-iv\":\"0x" << commitment_iv[i] << "\"" JSON_ENDL
		os << ",\"commitment\":\"0x" << commitment[i] << "\"" JSON_ENDL
		os << ",\"commitment-number\":\"0x" << commitnums[i] << "\"" JSON_ENDL
		os << "}" JSON_ENDL
	}
	os << "]}}";

	BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryAddress found";	// sending " << (char*)m_writebuf.data();

	SendReply(os);
}

void TransactConnection::HandleTxQueryInputs(const uint8_t *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryInputs size " << size;

	bigint_t address, commitment_iv, commitment, hash, nullhash;
	uint64_t param_level, row_end, commitstart, value_enc, commitnum;
	static const bool bhex = false;

	static const unsigned entry_size = sizeof(address) + sizeof(commitstart);
	unsigned nin = size / entry_size;

	//cerr << "size " << size << " entry_size " << entry_size << " nin " << nin << endl;

	if (nin * entry_size != size)
	{
		static const string outbuf = "ERROR:malformed binary tx-input-query";

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryInputs error malformed query; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryInputs", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	// ensure we get a consistent snapshot of the merkle tree

	Finally finally(boost::bind(&DbConnPersistData::EndRead, tx_dbconn));

	auto rc = tx_dbconn->BeginRead();
	if (rc)
		return SendServerError(__LINE__);

	CCASSERT(COMMITMENT_HASH_BYTES <= sizeof(hash));

	rc = tx_dbconn->ParameterSelect(DB_KEY_COMMIT_BLOCK_LEVEL, 0, &param_level, sizeof(param_level));
	if (rc)
		return SendServerError(__LINE__);

	if (param_level)
	{
		rc = tx_dbconn->ParameterSelect(DB_KEY_COMMIT_COMMITNUM_HI, 0, &row_end, sizeof(row_end));
		if (rc)
			return SendServerError(__LINE__);

		rc = tx_dbconn->ParameterSelect(DB_KEY_COMMIT_NULL_INPUT, 0, &nullhash, COMMITMENT_HASH_BYTES);
		if (rc)
			return SendServerError(__LINE__);

		rc = tx_dbconn->CommitTreeSelect(TX_MERKLE_DEPTH, 0, &hash, COMMITMENT_HASH_BYTES);
		if (rc)
			return SendServerError(__LINE__);
	}

	//cerr << "parameter-level " << param_level << endl;
	//cerr << "merkle-root " << hash << endl;

	ostringstream os;
	os << hex;
	os.rdbuf()->pubsetbuf((char*)m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-input-query-report\":{" JSON_ENDL
	StreamNetParams(os);
	StreamDonationParams(os);
	os << ",\"tx-input-query-results\":" JSON_ENDL
	os << "{\"parameter-level\":\"0x" << param_level << "\"" JSON_ENDL
	os << ",\"merkle-root\":\"0x" << hash << "\"" JSON_ENDL
	StreamValueLimits(os);
	os << ",\"inputs\":[" JSON_ENDL

	if (!param_level)
	{
		os << "]}}}";

		BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryInputs Merkle tree is empty"; // sending " << (char*)m_writebuf.data();

		return SendReply(os);
	}

	uint32_t bufpos = 0;

	for (unsigned i = 0; i < nin; ++i)
	{
		copy_from_buf(&address, sizeof(address), bufpos, msg, size, bhex);
		copy_from_buf(&commitstart, sizeof(commitstart), bufpos, msg, size, bhex);

		//cerr << "address " << hex << address << dec << endl;
		//cerr << "commitstart " << hex << commitstart << dec << endl;

		auto rc = tx_dbconn->TxOutputsSelect(&address, sizeof(address), commitstart, INT64_MAX, &value_enc, (char*)&commitment_iv, sizeof(commitment_iv), (char*)&commitment, sizeof(commitment), &commitnum);
		if (rc < 0)
			return SendServerError(__LINE__);
		if (!rc)
		{
			char *outbuf = (char*)m_writebuf.data();

			sprintf(outbuf, "Not Found:%u", i);

			BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryInputs not found"; // sending " << outbuf;

			WriteAsync("TransactConnection::HandleTxQueryInputs", boost::asio::buffer(outbuf, strlen(outbuf)),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

			return;
		}

		CCASSERT(TX_COMMIT_IV_BITS == 128);
		for (unsigned j = 2; j < commitment_iv.numberLimbs(); ++j)
			commitment_iv.data()[j] = 0;

		if (i)
			os << ",";
		os << "{\"address\":\"0x" << address << "\"" JSON_ENDL
		os << ",\"encrypted-value\":\"0x" << value_enc << "\"" JSON_ENDL
		os << ",\"commitment-iv\":\"0x" << commitment_iv << "\"" JSON_ENDL
		os << ",\"commitment\":\"0x" << commitment << "\"" JSON_ENDL
		os << ",\"commitment-number\":\"0x" << commitnum << "\"" JSON_ENDL
		os << ",\"merkle-path\":[" JSON_ENDL
		uint64_t offset = commitnum;
		uint64_t end = row_end;
		for (unsigned height = 0; height < TX_MERKLE_DEPTH; ++height)
		{
			if (height)
				os << ",";

			offset ^= 1;	// fetch the other hash input

			//cerr << "HandleTxQueryInputs commitnum " << commitnum << " height " << height << " offset " << offset << " row_end " << end << endl;

			if (offset > end)
			{
				os << "\"0x" << nullhash << "\"" JSON_ENDL
			}
			else
			{
				auto rc = tx_dbconn->CommitTreeSelect(height, offset, &hash, COMMITMENT_HASH_BYTES);
				if (rc)
					return SendServerError(__LINE__);
				os << "\"0x" << hash << "\"" JSON_ENDL
			}

			offset /= 2;
			end /= 2;
		}
		os << "]}" JSON_ENDL
	}

	CCASSERT(bufpos == size);

	os << "]}}}";

	BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQueryInputs found"; // sending " << (char*)m_writebuf.data();

	SendReply(os);
}

void TransactConnection::HandleTxQuerySerial(const uint8_t *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQuerySerial size " << size;

	bigint_t serialnum;
	static const bool bhex = false;

	uint32_t bufpos = 0;

	copy_from_buf(&serialnum, sizeof(serialnum), bufpos, msg, size, bhex);

	if (bufpos != size)
	{
		static const string outbuf = "ERROR:malformed binary tx-serial-number-query";

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQuerySerial error malformed query; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQuerySerial", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	auto nfound = tx_dbconn->SerialnumCheck(&serialnum, sizeof(serialnum));
	if (nfound < 0)
		return SendServerError(__LINE__);
	if (!nfound)
	{
		static const string outbuf = "Not Found";

		BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQuerySerial not found; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQuerySerial", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}
	else
	{
		static const string outbuf = "Indelible";

		BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::HandleTxQuerySerial found; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQuerySerial", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}
}

void TransactConnection::SendReply(ostringstream& os)
{
	unsigned size = os.tellp();

	if (!os.good() || size >= m_writebuf.size())
		return SendReplyWriteError();

	//BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " TransactConnection::SendReply sending " << (char*)m_writebuf.data();

	WriteAsync("TransactConnection::SendReply", boost::asio::buffer(m_writebuf.data(), size),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

	//cerr << "SendReply done" << endl;
}

void TransactConnection::SendServerError(unsigned line)
{
	static const string outbuf = "ERROR:server error";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " TransactConnection::SendServerError from line " << line << " sending " << outbuf;

	WriteAsync("TransactConnection::SendServerError", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendReplyWriteError()
{
	static const string outbuf = "ERROR:server reply buffer write error";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " TransactConnection::SendReplyWriteError sending " << outbuf;

	WriteAsync("TransactConnection::SendReplyWriteError", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendTimeout()
{
	static const string outbuf = "ERROR:server timeout";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " TransactConnection::SendTimeout sending " << outbuf;

	WriteAsync("TransactConnection::SendTimeout", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactService::Start()
{
	if (!enabled)
		return;

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " TransactService port " << port;

	// unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister
	CCServer::ConnectionFactoryInstantiation<TransactConnection> connfac(TRANSACT_MAX_REQUEST_SIZE, TRANSACT_MAX_REPLY_SIZE, 0, 0, CC_MSG_HEADER_SIZE + TX_POW_SIZE, 0, 1);
	CCThreadFactoryInstantiation<TransactThread> threadfac;

	unsigned maxconns = (unsigned)(max_inconns + max_outconns);
	unsigned nthreads = maxconns * threads_per_conn;	//!!! threads_per_conn can be changed if TransactConnection's do not block

	// unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog
	m_service.Start(boost::asio::ip::tcp::endpoint(address, port),
			nthreads, maxconns, max_inconns, 0, connfac, threadfac);
}

void TransactService::WaitForShutdown()
{
	m_service.WaitForShutdown();
}

void TransactThread::ThreadProc(boost::function<void()> threadproc)
{
	tx_dbconn = new DbConn;

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << "TransactThread::ThreadProc start " << (uintptr_t)this << " dbconn " << (uintptr_t)tx_dbconn;

	threadproc();

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << "TransactThread::ThreadProc end " << (uintptr_t)this << " dbconn " << (uintptr_t)tx_dbconn;

	delete tx_dbconn;
}
