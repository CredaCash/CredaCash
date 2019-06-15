/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * transact.cpp
*/

#include "ccnode.h"
#include "transact.hpp"
#include "processtx.hpp"
#include "blockchain.hpp"
#include "commitments.hpp"
#include "witness.hpp"
#include "dbconn.hpp"
#include "dbparamkeys.h"

#include <CCobjects.hpp>
#include <CCparams.h>
#include <CCmint.h>
#include <transaction.h>
#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#include <blake2/blake2.h>

#include <boost/date_time/posix_time/posix_time.hpp>

//#define JSON_ENDL	;
#define JSON_ENDL	<< "\n";

#define TRANSACT_MAX_REQUEST_SIZE		64000
#define TRANSACT_MAX_REPLY_SIZE			64000

#define TRANSACT_QUERY_MAX_COMMITS		20

#define TRANSACT_TIMEOUT				10	// timeout for entire connection, excluding validation
#define TRANSACT_VALIDATION_TIMEOUT		20

#define TRACE_TRANSACT	(g_params.trace_tx_server)

thread_local DbConn *tx_dbconn;

void TransactConnection::StartConnection()
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::StartConnection";

	m_conn_state = CONN_CONNECTED;

	// On timeout, the connection will simply Stop. To prevent this, the timer can be reset with a different handler; see for example HandleTx()

	if (SetTimer(TRANSACT_TIMEOUT))
		return;

	StartRead();
}

void TransactConnection::HandleReadComplete()
{
	if (m_nred < CC_MSG_HEADER_SIZE + TX_POW_SIZE)
	{
		static const string outbuf = "ERROR:unexpected short read";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete error short read " << m_nred << "; sending " << outbuf;

		WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	unsigned size = *(uint32_t*)m_pread;
	unsigned tag = *(uint32_t*)(m_pread + 4);

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete read " << m_nred << " bytes msg size " << size << " tag " << tag;

	if (size < CC_MSG_HEADER_SIZE + TX_POW_SIZE || size > TRANSACT_MAX_REQUEST_SIZE)
	{
		static const string outbuf = "ERROR:message size field invalid";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete error invalid size " << size << "; sending " << outbuf;

		WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	unsigned clock_allowance;
	SmartBuf smartobj;

	switch (tag)
	{
	case CC_TAG_TX_QUERY_PARAMS:
		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete CC_TAG_TX_QUERY_PARAMS";

		clock_allowance = 0;
		break;

	case CC_TAG_TX_QUERY_ADDRESS:
	case CC_TAG_TX_QUERY_INPUTS:
	case CC_TAG_TX_QUERY_SERIAL:
		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete CC_TAG_TX_QUERY_ADDRESS/INPUTS/SERIAL";

		clock_allowance = 5*60;
		break;

	case CC_TAG_TX_WIRE:
	case CC_TAG_MINT_WIRE:
	{
		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete " << (tag == CC_TAG_TX_WIRE ? "CC_TAG_TX_WIRE" : "CC_TAG_MINT_WIRE");

		clock_allowance = 5*60;

		CCASSERT(CC_MSG_HEADER_SIZE == sizeof(CCObject::Header));

		smartobj = SmartBuf(size + sizeof(CCObject::Preamble));
		if (!smartobj)
		{
			BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete error smartobj failed";

			return Stop();
		}

		auto obj = (CCObject*)smartobj.data();

		memcpy(obj->ObjPtr(), m_pread, m_nred);

		m_pread = (char*)obj->ObjPtr();

		break;
	}

	default:
		// note: this results in a forcible close because it doesn't read all of the data that was sent

		static const string outbuf = "ERROR:unrecognized message type";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete error unrecognized message tag " << tag << "; sending " << outbuf;

		WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	if (clock_allowance && tx_check_timestamp(*(uint64_t*)(m_pread + CC_MSG_HEADER_SIZE), clock_allowance))
	{
		char *outbuf = m_writebuf.data();

		sprintf(outbuf, "ERROR:invalid timestamp:%s", to_string(time(NULL)).c_str());

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete error invalid timestamp; sending " << outbuf;

		WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf, strlen(outbuf) + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	m_maxread = size;

	if (m_nred < m_maxread)
	{
		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete queueing read size " << m_maxread - m_nred;

		ReadAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_exactly(m_maxread - m_nred),
				boost::bind(&TransactConnection::HandleMsgReadComplete, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, smartobj, AutoCount(this)));
	}
	else
	{
		HandleMsgReadComplete(boost::system::error_code(), 0, smartobj, AutoCount(this));	// don't need to increment op count, but too much effort to chain the AutoCount from the function calling HandleReadComplete
	}
}

void TransactConnection::HandleMsgReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	bool sim_err = RandTest(TEST_RANDOM_READ_ERRORS);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete simulating read error";

	if (e || sim_err)
	{
		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete error " << e << " " << e.message() << "; read " << bytes_transferred << " total " << m_nred;

		return Stop();
	}

	m_nred += bytes_transferred;

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete read " << bytes_transferred << " total " << m_nred;

	auto size = *(uint32_t*)m_pread;
	auto tag = *(uint32_t*)(m_pread + 4);

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete read " << m_nred << " bytes msg size " << size << " tag " << tag;

	if (size != m_nred)
	{
		static const string outbuf = "ERROR:message size field does not match bytes received";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete error size " << size << " mismatch " << m_nred << "; sending " << outbuf;

		WriteAsync("TransactConnection::HandleMsgReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
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
		auto rc = blake2b(&objhash, sizeof(objhash), &tag, sizeof(tag), m_pread + data_offset, size - data_offset);
		CCASSERTZ(rc);
		break;
	}

	case CC_TAG_TX_WIRE:
	case CC_TAG_MINT_WIRE:
	{
		proof_difficulty = g_params.tx_work_difficulty;
		auto obj = (CCObject*)smartobj.data();
		if (!obj->IsValid())
		{
			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete CC_TAG_TX_WIRE error object IsValid false";

			return SendObjectNotValid();
		}

		auto param_level = txpay_param_level_from_wire(obj);
		if (param_level == (uint64_t)(-1))
		{
			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete CC_TAG_TX_WIRE invalid object size";

			return SendObjectNotValid();
		}

		if (Implement_CCMint(g_params.blockchain))
		{
			auto block_level = g_blockchain.GetLastIndelibleLevel();

			if (tag == CC_TAG_MINT_WIRE)
			{
				if (!param_level
					||	(param_level == 1 &&				block_level > CC_MINT_ACCEPT_SPAN + 1)
					||	(param_level  > 1 &&				param_level + CC_MINT_ACCEPT_SPAN + 1 < block_level)
					||										param_level >= CC_MINT_COUNT
					||										param_level > block_level)
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete CC_TAG_MINT_WIRE INVALID param level " << param_level << " for mint tx at blockchain level " << block_level;

					static const string outbuf = "INVALID:mint transaction not allowed, invalid or too old";

					WriteAsync("TransactConnection::HandleMsgReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
						boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

					return;
				}
			}
			else
			{
				if (param_level < CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete CC_TAG_TX_WIRE INVALID param level " << param_level << " for non-mint tx at blockchain level " << block_level;

					static const string outbuf = "INVALID:non-mint transaction during mint";

					WriteAsync("TransactConnection::HandleMsgReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
						boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

					return;
				}
			}
		}
		else if (tag == CC_TAG_MINT_WIRE && !IsTestnet(g_params.blockchain))
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete CC_TAG_MINT_WIRE INVALID on non-testnet";

			static const string outbuf = "INVALID:mint transaction not allowed";

			WriteAsync("TransactConnection::HandleMsgReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
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

	if (proof_difficulty && tx_set_work_internal(m_pread, pobjhash, 0, TX_POW_NPROOFS, 1, proof_difficulty))
	{
		char *outbuf = m_writebuf.data();

		sprintf(outbuf, "ERROR:proof of work failed:%s", to_string(proof_difficulty).c_str());

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete error proof of work failed; sending " << outbuf;

		WriteAsync("TransactConnection::HandleMsgReadComplete", boost::asio::buffer(outbuf, strlen(outbuf) + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	if (SetTimer(TRANSACT_TIMEOUT))		// allow some more time
		return;

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
		return HandleTxQuerySerials(m_pread, size);

	case CC_TAG_MINT_WIRE:
	case CC_TAG_TX_WIRE:
		return HandleTx(smartobj);

	default:
		CCASSERT(0);	// need to handle all tags passed by HandleReadComplete
	}
}

void TransactConnection::HandleTx(SmartBuf smartobj)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTx";

	// !!! TODO: check here to see if queue is over full

	// Note: use of connection needs to be finished before calling TxEnqueueValidate, because TxEnqueueValidate will call HandleValidateDone
	//	which will call Write which will close the connection upon completion and free it for reuse

	if (CancelTimer())	// cancel timer first, to make sure we send a response instead of just stopping on timeout
		return;

	static atomic<int64_t> med_tx_priority(1);
	auto priority = med_tx_priority.fetch_add(1);
	auto callback_id = m_use_count.load();

	auto rc = ProcessTx::TxEnqueueValidate(tx_dbconn, priority, smartobj, m_conn_index, callback_id);
	if (rc)	// TODO: if rc == 1, the tx is already in the validation queue; instead of returning server error, wait for it?
	{
		return SendServerError(__LINE__);
	}

	SetValidationTimer(callback_id, TRANSACT_VALIDATION_TIMEOUT);
}

void TransactConnection::HandleValidateDone(uint32_t callback_id, int64_t result)
{
	if (RandTest(TEST_DELAY_CONN_RELEASE)) sleep(1);

	// HandleValidateDone was not passed an AutoCount object since we don't want stop to be delayed while the Tx validation runs
	// so acquire an AutoCount to prevent Stop from running to completion while this function is running

	auto autocount = AutoCount(this);
	if (!autocount)
		return;

	if (RandTest(TEST_DELAY_CONN_RELEASE)) sleep(1);

	// increment m_use_count so either HandleValidationTimeout or HandleValidateDone will run, but not both

	uint32_t expected_callback_id = m_use_count.fetch_add(1);

	if (callback_id != expected_callback_id)
	{
		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleValidateDone ignoring late or unexpected callback id " << callback_id << " expected " << expected_callback_id;

		return;
	}

	return SendValidateResult(result);
}

void TransactConnection::SendValidateResult(int64_t result)
{
	if (RandTest(TEST_RANDOM_VALIDATION_FAILURES))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::SendValidateResult simulating validation failure";

		return Stop();
	}

	const char *poutbuf;

	if (result < 0)
		poutbuf = ProcessTx::ResultString(result);
	else
	{
		char *outbuf = m_writebuf.data();

		sprintf(outbuf, "OK:%s", to_string(result).c_str());

		poutbuf = outbuf;
	}

	if (!poutbuf)
		return SendServerError(__LINE__);

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TransactConnection::SendValidateResult result " << result << " sending " << (poutbuf ? poutbuf : "(null)");

	if (SetTimer(TRANSACT_TIMEOUT))
		return;

	WriteAsync("TransactConnection::SendValidateResult", boost::asio::buffer(poutbuf, strlen(poutbuf) + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

bool TransactConnection::SetValidationTimer(uint32_t callback_id, unsigned sec)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::SetValidationTimer callback id " << callback_id << " ops pending " << m_ops_pending.load();

	return AsyncTimerWait("TransactConnection::SetValidationTimer", sec*1000, boost::bind(&TransactConnection::HandleValidationTimeout, this, callback_id, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::HandleValidationTimeout(uint32_t callback_id, const boost::system::error_code& e, AutoCount pending_op_counter)
{
	if (RandTest(TEST_DELAY_CONN_RELEASE)) sleep(1);

	// increment m_use_count so either HandleValidationTimeout or HandleValidateDone will run, but not both

	uint32_t expected_callback_id = m_use_count.fetch_add(1);

	if (callback_id != expected_callback_id)
	{
		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleValidationTimeout ignoring late or unexpected callback id " << callback_id << " expected " << expected_callback_id;

		return;
	}

	if (RandTest(TEST_DELAY_CONN_RELEASE)) sleep(1);

	if (CheckOpCount(pending_op_counter))
		return;

	if (e == boost::asio::error::operation_aborted)
	{
		//if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleValidationTimeout timer canceled callback id " << callback_id;

		return;
	}

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleValidationTimeout callback id " << callback_id << ", e = " << e << " " << e.message();

	if (e)
		return SendServerError(__LINE__);
	else
		return SendTimeout();
}

static void StreamNetParams(ostream& os)
{
	os << " \"timestamp\":\"0x" << time(NULL) << "\"" JSON_ENDL
	os << ",\"server-version\":\"0x" << g_params.server_version << "\"" JSON_ENDL
	os << ",\"protocol-version\":\"0x" << g_params.protocol_version << "\"" JSON_ENDL
	os << ",\"effective-level\":\"0x" << g_params.effective_level << "\"" JSON_ENDL
	os << ",\"query-work-difficulty\":\"0x" << g_params.query_work_difficulty << "\"" JSON_ENDL
	os << ",\"tx-work-difficulty\":\"0x" << g_params.tx_work_difficulty << "\"" JSON_ENDL
	os << ",\"blockchain-number\":\"0x" << g_params.blockchain << "\"" JSON_ENDL
	os << ",\"blockchain-highest-indelible-level\":\"0x" << g_blockchain.GetLastIndelibleLevel() << "\"" JSON_ENDL
	os << ",\"merkle-tree-oldest-commitment-number\":\"0x0\"" JSON_ENDL
	os << ",\"merkle-tree-next-commitment-number\":\"0x" << g_commitments.GetNextCommitnum() << "\"" JSON_ENDL	// !!! note: small chance this could be out-of-sync with GetLastIndelibleLevel()
}

static void StreamPoolParams(ostream& os)
{
	os << ",\"default-output-pool\":\"0x" << g_params.default_pool << "\"" JSON_ENDL
}

static void StreamAmountBits(ostream& os, bool include_donation_bits = true)
{
	os << dec;
	if (TEST_EXTRA_ON_WIRE)
		os << ",\"asset-bits\":" << TX_ASSET_BITS JSON_ENDL
	else
		os << ",\"asset-bits\":" << TX_ASSET_WIRE_BITS JSON_ENDL
	os << ",\"amount-bits\":" << TX_AMOUNT_BITS JSON_ENDL
	if (include_donation_bits)
		os << ",\"donation-bits\":" << TX_DONATION_BITS JSON_ENDL
	os << ",\"exponent-bits\":" << TX_AMOUNT_EXPONENT_BITS JSON_ENDL
	os << hex;
}

static void StreamValueLimits(ostream& os)
{
	os << ",\"minimum-output-exponent\":\"0x" << g_blockchain.proof_params.outvalmin << "\"" JSON_ENDL
	os << ",\"maximum-output-exponent\":\"0x" << g_blockchain.proof_params.outvalmax << "\"" JSON_ENDL
	os << ",\"maximum-input-exponent\":\"0x" << g_blockchain.proof_params.invalmax << "\"" JSON_ENDL
}

static void StreamDonationParams(ostream& os)
{
	os << ",\"minimum-donation-per-transaction\":\"0x" << g_blockchain.proof_params.minimum_donation_fp << "\"" JSON_ENDL
	os << ",\"donation-per-transaction\":\"0x" << g_blockchain.proof_params.donation_per_tx_fp << "\"" JSON_ENDL
	os << ",\"donation-per-byte\":\"0x" << g_blockchain.proof_params.donation_per_byte_fp << "\"" JSON_ENDL
	os << ",\"donation-per-output\":\"0x" << g_blockchain.proof_params.donation_per_output_fp << "\"" JSON_ENDL	// !!! split between merkle tree outputs and non-merkle tree outputs?
	os << ",\"donation-per-input\":\"0x" << g_blockchain.proof_params.donation_per_input_fp << "\"" JSON_ENDL		// !!! split between hidden (serialnum) and non-hidden (commitment) inputs?
}

void TransactConnection::HandleTxQueryParams(const char *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryParams size " << size;

	ostringstream os;
	os << hex;
	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-parameters-query-results\":{" JSON_ENDL
	StreamNetParams(os);
	StreamAmountBits(os);
	StreamValueLimits(os);
	StreamPoolParams(os);
	StreamDonationParams(os);
	os << "}}";

	//BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryParams sending " << m_writebuf.data();

	SendReply(os);
}

void TransactConnection::HandleTxQueryAddress(const char *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryAddress size " << size;

	uint64_t blockchain, commitstart;
	bigint_t address;
	uint16_t maxret;

	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_from_buf(blockchain, TX_CHAIN_BYTES, bufpos, msg, size, bhex);
	copy_from_buf(address, TX_ADDRESS_BYTES, bufpos, msg, size, bhex);
	copy_from_buf(commitstart, sizeof(commitstart), bufpos, msg, size, bhex);
	copy_from_buf(maxret, sizeof(maxret), bufpos, msg, size, bhex);

	if (bufpos != size)
	{
		static const string outbuf = "ERROR:malformed binary tx-address-query";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryAddress error malformed query; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryAddress", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	if (blockchain != g_params.blockchain)
		return SendBlockchainNumberError();

	array<uint32_t, TRANSACT_QUERY_MAX_COMMITS> pool;
	array<uint64_t, TRANSACT_QUERY_MAX_COMMITS> asset_enc, amount_enc, commitnum;
	array<char[TX_COMMIT_IV_BYTES], TRANSACT_QUERY_MAX_COMMITS> commitiv;
	array<bigint_t, TRANSACT_QUERY_MAX_COMMITS> commitment;
	bool have_more;

	if (maxret > TRANSACT_QUERY_MAX_COMMITS)
		maxret = TRANSACT_QUERY_MAX_COMMITS;

	auto nfound = tx_dbconn->TxOutputsSelect(&address, TX_ADDRESS_BYTES, commitstart, &pool[0], &asset_enc[0], &amount_enc[0], (char*)&commitiv, sizeof(commitiv[0]), (char*)&commitment, sizeof(commitment[0]), &commitnum[0], maxret, &have_more);
	if (nfound < 0)
		return SendServerError(__LINE__);
	if (!nfound)
	{
		static const string outbuf = "Not Found";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryAddress not found; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryAddress", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	//memset(m_writebuf.data(), 0, m_writebuf.size());	// for testing

	ostringstream os;
	os << hex;
	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-address-query-report\":" JSON_ENDL
	os << "{\"address\":\"0x" << address << "\"" JSON_ENDL
	os << ",\"commitment-number-start\":\"0x" << commitstart << "\"" JSON_ENDL
	os << dec;
	os << ",\"more-results-available\":" << (int)have_more JSON_ENDL
	os << ",\"tx-address-query-results\":[" JSON_ENDL
	for (int i = 0; i < nfound; ++i)
	{
		#define RETURN_BLOCKLEVEL 0

		#if RETURN_BLOCKLEVEL
		uint64_t level, timestamp;
		bigint_t root;

		auto rc = tx_dbconn->CommitRootsSelectCommitnum(commitnum[i], level, timestamp, &root, TX_MERKLE_BYTES);
		if (rc)
			return SendServerError(__LINE__);
		#endif

		bigint_t iv;

		memcpy(&iv, &commitiv[i], sizeof(commitiv[i]));

		if (i) os << ",";
		os << dec;
		os << "{\"pool\":" << (pool[i] >> 1) JSON_ENDL
		StreamAmountBits(os, false);
		os << hex;
		if (pool[i] & 1)
		{
			os << ",\"encrypted\":0" JSON_ENDL
			os << ",\"asset\":\"0x" << asset_enc[i] << "\"" JSON_ENDL
			os << ",\"amount\":\"0x" << amount_enc[i] << "\"" JSON_ENDL
		}
		else
		{
			os << ",\"encrypted\":1" JSON_ENDL
			os << ",\"encrypted-asset\":\"0x" << asset_enc[i] << "\"" JSON_ENDL
			os << ",\"encrypted-amount\":\"0x" << amount_enc[i] << "\"" JSON_ENDL
		}
		os << ",\"blockchain\":\"0x" << g_params.blockchain << "\"" JSON_ENDL
		#if RETURN_BLOCKLEVEL
		os << ",\"block-level\":\"0x" << level << "\"" JSON_ENDL
		os << ",\"block-time\":\"0x" << timestamp << "\"" JSON_ENDL
		#endif
		os << ",\"commitment-iv\":\"0x" << iv << "\"" JSON_ENDL
		os << ",\"commitment\":\"0x" << commitment[i] << "\"" JSON_ENDL
		os << ",\"commitment-number\":\"0x" << commitnum[i] << "\"" JSON_ENDL
		os << "}" JSON_ENDL
	}
	os << "]}}";

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryAddress found";	// sending " << m_writebuf.data();

	SendReply(os);
}

void TransactConnection::HandleTxQueryInputs(const char *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryInputs size " << size;

	bigint_t hash, nullhash;
	uint64_t blockchain, param_level, merkle_time, next_commitnum, row_end, commitnum;

	const unsigned entry_size = sizeof(commitnum);
	unsigned nin = (size - TX_CHAIN_BYTES) / entry_size;

	//cerr << "size " << size << " entry_size " << entry_size << " nin " << nin << endl;

	if (size < TX_CHAIN_BYTES || TX_CHAIN_BYTES + nin * entry_size != size)
	{
		static const string outbuf = "ERROR:malformed binary tx-input-query";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryInputs error malformed query; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryInputs", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	if (nin > TX_MAXINPATH)
		return SendTooManyObjectsError();

	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_from_buf(blockchain, TX_CHAIN_BYTES, bufpos, msg, size, bhex);

	if (blockchain && blockchain != g_params.blockchain)
		return SendBlockchainNumberError();

	// ensure we get a consistent snapshot of the Merkle tree

	Finally finally(boost::bind(&DbConnPersistData::EndRead, tx_dbconn));

	auto rc = tx_dbconn->BeginRead();
	if (rc)
		return SendServerError(__LINE__);

	rc = tx_dbconn->ParameterSelect(DB_KEY_COMMIT_BLOCKLEVEL, 0, &param_level, sizeof(param_level), TX_BLOCKLEVEL_BYTES < sizeof(param_level));
	if (rc)
		return SendServerError(__LINE__);

	rc = tx_dbconn->CommitRootsSelectLevel(param_level, false, merkle_time, next_commitnum, &hash, TX_MERKLE_BYTES);
	if (rc)
		return SendServerError(__LINE__);

	if (param_level)
	{
		rc = tx_dbconn->ParameterSelect(DB_KEY_COMMIT_COMMITNUM_HI, 0, &row_end, sizeof(row_end));
		if (rc)
			return SendServerError(__LINE__);

		rc = tx_dbconn->ParameterSelect(DB_KEY_COMMIT_NULL_INPUT, 0, &nullhash, TX_MERKLE_BYTES);
		if (rc)
			return SendServerError(__LINE__);
	}

	//cerr << "parameter-level " << param_level << endl;
	//cerr << "merkle-root " << hash << endl;

	uint64_t param_time = 0;
	if (merkle_time > TX_TIME_OFFSET)
		param_time = (merkle_time - TX_TIME_OFFSET) / TX_TIME_DIVISOR;

	ostringstream os;
	os << hex;
	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-input-query-report\":{" JSON_ENDL
	StreamNetParams(os);
	StreamAmountBits(os);		// !!! if these change, need to make sure they stay sync'ed with parameter-level
	StreamDonationParams(os);	// !!! if these change, need to make sure they stay sync'ed with parameter-level
	os << ",\"tx-input-query-results\":" JSON_ENDL
	os << "{\"parameter-level\":\"0x" << param_level << "\"" JSON_ENDL
	os << ",\"parameter-time\":\"0x" << param_time << "\"" JSON_ENDL
	os << ",\"merkle-root\":\"0x" << hash << "\"" JSON_ENDL
	StreamValueLimits(os);		// !!! if these change, need to make sure they stay sync'ed with parameter-level
	StreamPoolParams(os);
	os << ",\"inputs\":[" JSON_ENDL

	if (!param_level && !nin)
	{
		os << "]}}}";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryInputs Merkle tree is empty"; // sending " << m_writebuf.data();

		return SendReply(os);
	}

	for (unsigned i = 0; i < nin; ++i)
	{
		copy_from_buf(commitnum, sizeof(commitnum), bufpos, msg, size, bhex);

		//cerr << "commitnum " << hex << commitnum << dec << endl;

		if (!param_level || commitnum > row_end)
		{
			char *outbuf = m_writebuf.data();

			sprintf(outbuf, "Not Found:%u", i);

			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryInputs not found"; // sending " << outbuf;

			WriteAsync("TransactConnection::HandleTxQueryInputs", boost::asio::buffer(outbuf, strlen(outbuf) + 1),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

			return;
		}

		if (i) os << ",";
		os << "{\"commitment-number\":\"0x" << commitnum << "\"" JSON_ENDL
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
				auto rc = tx_dbconn->CommitTreeSelect(height, offset, &hash, TX_MERKLE_BYTES);
				if (rc)
					return SendServerError(__LINE__);

				if (height == 0)
					tx_commit_tree_hash_leaf(hash, offset, hash);

				os << "\"0x" << hash << "\"" JSON_ENDL
			}

			offset /= 2;
			end /= 2;
		}
		os << "]}" JSON_ENDL
	}

	CCASSERT(bufpos == size);

	os << "]}}}";

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryInputs found"; // sending " << m_writebuf.data();

	SendReply(os);
}

void TransactConnection::HandleTxQuerySerials(const char *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQuerySerials size " << size;

	const unsigned entry_size = TX_SERIALNUM_BYTES;
	unsigned nserials = (size - TX_CHAIN_BYTES) / entry_size;

	//cerr << "size " << size << " entry_size " << entry_size << " nserials " << nserials << endl;

	if (size < TX_CHAIN_BYTES || nserials < 1 || TX_CHAIN_BYTES + nserials * entry_size != size)
	{
		static const string outbuf = "ERROR:malformed binary tx-serial-number-query";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQuerySerials error malformed query; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQuerySerials", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	if (nserials > TX_MAXIN)
		return SendTooManyObjectsError();

	uint64_t blockchain;
	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_from_buf(blockchain, TX_CHAIN_BYTES, bufpos, msg, size, bhex);

	if (blockchain != g_params.blockchain)
		return SendBlockchainNumberError();

	ostringstream os;
	os << hex;
	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-serial-number-query-results\":[" JSON_ENDL

	for (unsigned i = 0; i < nserials; ++i)
	{
		bigint_t serialnum;

		copy_from_buf(serialnum, TX_SERIALNUM_BYTES, bufpos, msg, size, bhex);

		bigint_t hashkey;
		unsigned hashkey_size = sizeof(hashkey);

		auto rc1 = tx_dbconn->SerialnumSelect(&serialnum, TX_SERIALNUM_BYTES, &hashkey, &hashkey_size);
		if (rc1 < 0)
			return SendServerError(__LINE__);

		int rc2 = 0;
		if (rc1)
		{
			rc2 = tx_dbconn->TempSerialnumSelect(&serialnum, TX_SERIALNUM_BYTES, NULL, NULL, 0);
			if (rc2 < 0)
				return SendServerError(__LINE__);
		}

		// @@! TODO: Check the "mempool" to prevent the wallet from making a double-spend attempt after a tx submit appears to fail but actually succeeds?

		if (i)
			os << ",";
		os << "{\"serial-number\":\"0x" << serialnum << "\"" JSON_ENDL
		os << ",\"status\":";
		if (!rc1)
		{
			os << "\"indelible\"" JSON_ENDL
			if (hashkey_size)
				os << ",\"hashkey\":\"0x" << hashkey << "\"}" JSON_ENDL
			else
				os << "}";
		}
		else if (rc2)
			os << "\"pending\"}" JSON_ENDL
		else
			os << "\"unspent\"}" JSON_ENDL
	}

	os << "]}";

	SendReply(os);
}

void TransactConnection::SendReply(ostringstream& os)
{
	os.put(0);

	unsigned size = os.tellp();

	if (!os.good() || size >= m_writebuf.size())
		return SendReplyWriteError();

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::SendReply sending " << size << " bytes: " << m_writebuf.data();

	// !!! at some point, add a config param for connection keep-alive after SendReply

	if (SetTimer(TRANSACT_TIMEOUT))
		return;

	WriteAsync("TransactConnection::SendReply", boost::asio::buffer(m_writebuf.data(), size),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

	//cerr << "SendReply done" << endl;
}

void TransactConnection::SendObjectNotValid()
{
	static const string outbuf = "ERROR:binary object not valid";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TransactConnection::SendObjectNotValid sending " << outbuf;

	WriteAsync("TransactConnection::SendObjectNotValid", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendBlockchainNumberError()
{
	static const string outbuf = "ERROR:requested blockchain not tracked by this server";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TransactConnection::SendBlockchainNumberError sending " << outbuf;

	WriteAsync("TransactConnection::SendBlockchainNumberError", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendTooManyObjectsError()
{
	static const string outbuf = "ERROR:too many query objects";

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::SendTooManyObjectsError too many query objects; sending " << outbuf;

	WriteAsync("TransactConnection::SendTooManyObjectsError", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

	return;
}

void TransactConnection::SendServerError(unsigned line)
{
	static const string outbuf = "ERROR:server error";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TransactConnection::SendServerError from line " << line << " sending " << outbuf;

	WriteAsync("TransactConnection::SendServerError", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendReplyWriteError()
{
	static const string outbuf = "ERROR:server reply buffer write error";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TransactConnection::SendReplyWriteError sending " << outbuf;

	WriteAsync("TransactConnection::SendReplyWriteError", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendTimeout()
{
	static const string outbuf = "ERROR:server timeout";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TransactConnection::SendTimeout sending " << outbuf;

	WriteAsync("TransactConnection::SendTimeout", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactService::Start()
{
	if (!enabled)
		return;

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " TransactService port " << port;

	// unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister
	CCServer::ConnectionFactoryInstantiation<TransactConnection> connfac(TRANSACT_MAX_REQUEST_SIZE + 2, TRANSACT_MAX_REPLY_SIZE, 0, 0, CC_MSG_HEADER_SIZE + TX_POW_SIZE, 0, 1);
	CCThreadFactoryInstantiation<TransactThread> threadfac;

	unsigned maxconns = (unsigned)(max_inconns + max_outconns);
	unsigned nthreads = maxconns * threads_per_conn;	//!!! threads_per_conn can be changed if TransactConnection's do not block

	// unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog
	m_service.Start(boost::asio::ip::tcp::endpoint(address, port),
			nthreads, maxconns, max_inconns, 0, connfac, threadfac);
}

void TransactService::StartShutdown()
{
	m_service.StartShutdown();
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
