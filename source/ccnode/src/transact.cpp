/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * transact.cpp
*/

#include "ccnode.h"
#include "transact.hpp"
#include "processtx.hpp"
#include "processblock.hpp"
#include "blockchain.hpp"
#include "commitments.hpp"
#include "witness.hpp"
#include "exchange_mining.hpp"
#include "dbconn.hpp"
#include "dbparamkeys.h"

#include <CCobjects.hpp>
#include <CCparams.h>
#include <CCmint.h>
#include <transaction.h>
#include <xmatch.hpp>
#include <txquery.h>
#include <jsonutil.h>
#include <BlockChainStatus.hpp>
#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#include <blake2/blake2.h>

#include <boost/date_time/posix_time/posix_time.hpp>

//#undef JSON_ENDL
//#define JSON_ENDL	;
//#define JSON_ENDL	<< "\n";

#define TRANSACT_MAX_REQUEST_SIZE		64000
#define TRANSACT_MAX_REPLY_SIZE			64000

#if TEST_SMALL_BUFS
#define TRANSACT_QUERY_MAX_COMMITS		2	// must be >= 2
#define TRANSACT_QUERY_MAX_XREQS		2
#define TRANSACT_QUERY_MAX_MATCHES		2
#else
#define TRANSACT_QUERY_MAX_COMMITS		20
#define TRANSACT_QUERY_MAX_XREQS		20
#define TRANSACT_QUERY_MAX_MATCHES		8
#endif

#define TRANSACT_TIMEOUT				10	// timeout for entire connection, excluding validation
#define TRANSACT_VALIDATION_TIMEOUT		20

#define TRANSACT_TIMESTAMP_PAST_ALLOWANCE		(40*60)
#define TRANSACT_TIMESTAMP_FUTURE_ALLOWANCE		(5*60)

#define TRACE_TRANSACT	(g_params.trace_tx_server)

thread_local static DbConn *tx_dbconn;

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

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete read " << m_nred << " bytes msg size " << size << " tag " << hex << tag << dec;

	if (size < CC_MSG_HEADER_SIZE + TX_POW_SIZE || size > TRANSACT_MAX_REQUEST_SIZE)
	{
		static const string outbuf = "ERROR:message size field invalid";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete error invalid size " << size << "; sending " << outbuf;

		WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	SmartBuf smartobj;

	switch (tag)
	{
	case CC_TAG_TX_QUERY_PARAMS:
	case CC_TAG_TX_QUERY_ADDRESS:
	case CC_TAG_TX_QUERY_INPUTS:
	case CC_TAG_TX_QUERY_SERIAL:
	case CC_TAG_TX_QUERY_XREQS:
	case CC_TAG_TX_QUERY_XMATCH_OBJID:
	case CC_TAG_TX_QUERY_XMATCH_REQNUM:
	case CC_TAG_TX_QUERY_XMATCH_MATCHNUM:
	case CC_TAG_TX_QUERY_XMINING_INFO:

		if (g_transact_service.relay_only)
		{
			// note: this results in a forcible close because it doesn't read all of the data that was sent

			static const string outbuf = "ERROR:transaction queries disabled";

			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete error disabled message tag " << hex << tag << dec << "; sending " << outbuf;

			WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

			return;
		}

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete CC_TAG_TX_QUERY_*";

		break;

	case CC_TAG_TX:
	case CC_TAG_MINT:
	case CC_TAG_TX_XDOMAIN:
	case CC_TAG_XCX_NAKED_BUY:
	case CC_TAG_XCX_NAKED_SELL:
	case CC_TAG_XCX_SIMPLE_BUY:
	case CC_TAG_XCX_SIMPLE_SELL:
	case CC_TAG_XCX_SIMPLE_TRADE:
	case CC_TAG_XCX_PAYMENT:
	{
		if (g_transact_service.info_only)
		{
			// note: this results in a forcible close because it doesn't read all of the data that was sent

			static const string outbuf = "ERROR:transaction relay disabled";

			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete error disabled message tag " << hex << tag << dec << "; sending " << outbuf;

			WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

			return;
		}

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete tag " << hex << tag << dec;

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

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete error unrecognized message tag " << hex << tag << dec << "; sending " << outbuf;

		WriteAsync("TransactConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	auto timestamp = *(uint64_t*)(m_pread + CC_MSG_HEADER_SIZE);

	if (tag != CC_TAG_TX_QUERY_PARAMS && tx_check_timestamp(timestamp, TRANSACT_TIMESTAMP_PAST_ALLOWANCE, TRANSACT_TIMESTAMP_FUTURE_ALLOWANCE))
	{
		char *outbuf = m_writebuf.data();

		sprintf(outbuf, "ERROR:invalid timestamp:%s", to_string(unixtime()).c_str());

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleReadComplete message tag " << hex << tag << dec << " error invalid timestamp " << timestamp << "; sending " << outbuf;

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

	bool sim_err = RandTest(RTEST_READ_ERRORS);
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

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete read " << m_nred << " bytes msg size " << size << " tag " << hex << tag << dec;

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
	case CC_TAG_TX_QUERY_XREQS:
	case CC_TAG_TX_QUERY_XMATCH_OBJID:
	case CC_TAG_TX_QUERY_XMATCH_REQNUM:
	case CC_TAG_TX_QUERY_XMATCH_MATCHNUM:
	case CC_TAG_TX_QUERY_XMINING_INFO:
	{
		proof_difficulty = g_transact_service.query_work_difficulty;
		const unsigned data_offset = CC_MSG_HEADER_SIZE + TX_POW_SIZE;
		auto rc = blake2b(&objhash, sizeof(objhash), &tag, sizeof(tag), m_pread + data_offset, size - data_offset);
		CCASSERTZ(rc);
		break;
	}

	case CC_TAG_TX:
	case CC_TAG_MINT:
	case CC_TAG_TX_XDOMAIN:
	case CC_TAG_XCX_NAKED_BUY:
	case CC_TAG_XCX_NAKED_SELL:
	case CC_TAG_XCX_SIMPLE_BUY:
	case CC_TAG_XCX_SIMPLE_SELL:
	case CC_TAG_XCX_SIMPLE_TRADE:
	case CC_TAG_XCX_PAYMENT:
	{
		proof_difficulty = g_params.tx_work_difficulty;
		if (tag == CC_TAG_XCX_PAYMENT)
			proof_difficulty = g_params.xcx_pay_work_difficulty;

		auto obj = (CCObject*)smartobj.data();
		if (!obj->IsValid())
		{
			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete tag " << hex << tag << dec << " error object IsValid false";

			return SendObjectNotValid();
		}

		auto param_level = txpay_param_level_from_wire(obj);
		if (param_level == (uint64_t)(-1))
		{
			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete tag " << hex << tag << dec << " invalid object size";

			return SendObjectNotValid();
		}

		if (Implement_CCMint(g_params.blockchain))
		{
			auto block_level = g_blockchain.GetLastIndelibleLevel();

			if (tag == CC_TAG_MINT)
			{
				if (!param_level
					||	(param_level == 1 &&				block_level > CC_MINT_ACCEPT_SPAN + 1)
					||	(param_level  > 1 &&				param_level + CC_MINT_ACCEPT_SPAN + 1 < block_level)
					||										param_level >= CC_MINT_COUNT
					||										param_level > block_level)
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete CC_TAG_MINT INVALID param level " << param_level << " for mint tx at blockchain level " << block_level;

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
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete tag " << hex << tag << dec << " INVALID param level " << param_level << " for non-mint tx at blockchain level " << block_level;

					static const string outbuf = "INVALID:non-mint transaction during mint";

					WriteAsync("TransactConnection::HandleMsgReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
						boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

					return;
				}
			}
		}
		else if (tag == CC_TAG_MINT && !IsTestnet(g_params.blockchain))
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleMsgReadComplete CC_TAG_MINT INVALID on non-testnet";

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

	case CC_TAG_TX_QUERY_XREQS:
		return HandleTxQueryXreqs(m_pread, size);

	case CC_TAG_TX_QUERY_XMATCH_OBJID:
	case CC_TAG_TX_QUERY_XMATCH_REQNUM:
		return HandleTxQueryXmatchreq(tag, m_pread, size);

	case CC_TAG_TX_QUERY_XMATCH_MATCHNUM:
		return HandleTxQueryXmatch(m_pread, size);

	case CC_TAG_TX_QUERY_XMINING_INFO:
		return HandleTxQueryXminingInfo(m_pread, size);

	case CC_TAG_TX:
	case CC_TAG_MINT:
	case CC_TAG_TX_XDOMAIN:
	case CC_TAG_XCX_PAYMENT:

		return HandleTx(PROCESS_Q_PRIORITY_TX_HI, smartobj);

	case CC_TAG_XCX_NAKED_BUY:
	case CC_TAG_XCX_NAKED_SELL:
	case CC_TAG_XCX_SIMPLE_BUY:
	case CC_TAG_XCX_SIMPLE_SELL:
	case CC_TAG_XCX_SIMPLE_TRADE:

		return HandleTx(PROCESS_Q_PRIORITY_X_REQ_HI, smartobj);

	default:
		CCASSERT(0);	// need to handle all tags passed by HandleReadComplete
	}
}

void TransactConnection::HandleTx(Process_Q_Priority priority, SmartBuf smartobj)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTx priority " << priority;

	// !!! TODO: check here to see if queue is over full

	// Note: use of connection needs to be finished before calling TxEnqueueValidate, because TxEnqueueValidate will call HandleValidateDone
	//	which will call Write which will close the connection upon completion and free it for reuse

	if (CancelTimer())	// cancel timer first, to make sure we send a response instead of just stopping on timeout
		return;

	if (!g_transact_service.IsConnectedToNet())
		return SendNotConnectedError();

	auto callback_id = m_use_count.load();

	auto rc = ProcessTx::TxEnqueueValidate(tx_dbconn, false, true, priority, smartobj, m_conn_index, callback_id);
	if (rc < 0)
		return SendServerError(__LINE__);
	else if (rc == 1)
		SendValidateResult(0);	// tx already in valid obj's
	else
		SetValidationTimer(callback_id, TRANSACT_VALIDATION_TIMEOUT);
}

void TransactConnection::HandleValidateDone(uint64_t level, uint32_t callback_id, int64_t result)
{
	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

	// HandleValidateDone was not passed an AutoCount object since we don't want stop to be delayed while the Tx validation runs
	// so acquire an AutoCount to prevent Stop from running to completion while this function is running

	auto autocount = AutoCount(this);
	if (!autocount)
		return;

	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

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
	if (RandTest(RTEST_VALIDATION_FAILURES))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::SendValidateResult simulating validation failure";

		return Stop();
	}

	const char *poutbuf = NULL;

	if (result < 0)
		poutbuf = ProcessTx::ResultString(result);
	else
	{
		char *outbuf = m_writebuf.data();

		sprintf(outbuf, "OK:%s", to_string(result).c_str());

		poutbuf = outbuf;
	}

	if (!poutbuf) cerr << "TransactConnection::SendValidateResult result " << result << " poutbuf null" << endl;

	CCASSERT(poutbuf);

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TransactConnection::SendValidateResult result " << result << " sending " << poutbuf;

	if (SetTimer(TRANSACT_TIMEOUT))
		return;

	WriteAsync("TransactConnection::SendValidateResult", boost::asio::buffer(poutbuf, strlen(poutbuf) + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

bool TransactConnection::SetValidationTimer(uint32_t callback_id, unsigned sec)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::SetValidationTimer callback id " << callback_id << " ops pending " << m_ops_pending.load();

	return TimerWaitAsync("TransactConnection::SetValidationTimer", sec*1000, boost::bind(&TransactConnection::HandleValidationTimeout, this, callback_id, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::HandleValidationTimeout(uint32_t callback_id, const boost::system::error_code& e, AutoCount pending_op_counter)
{
	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

	if (e == boost::asio::error::operation_aborted)
	{
		//if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleValidationTimeout timer cancelled callback id " << callback_id;

		return;
	}

	// increment m_use_count so either HandleValidationTimeout or HandleValidateDone will run, but not both

	uint32_t expected_callback_id = m_use_count.fetch_add(1);

	if (callback_id != expected_callback_id)
	{
		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleValidationTimeout ignoring late or unexpected callback id " << callback_id << " expected " << expected_callback_id;

		return;
	}

	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

	if (CheckOpCount(pending_op_counter))
		return;

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleValidationTimeout callback id " << callback_id << ", e = " << e << " " << e.message();

	if (e)
		return SendServerUnknown(__LINE__);
	else
		return SendTimeout();
}

static void StreamNetParams(ostream& os)
{
	// this function does not output a leading comma, so it has to be called first

	os << dec;
	os << " \"server-timestamp\":" << unixtime() JSON_ENDL
	os << ",\"connected-to-network\":" << g_transact_service.IsConnectedToNet() JSON_ENDL
	os << ",\"blockchain-number\":" << g_params.blockchain JSON_ENDL
	os << ",\"parameters-last-modified-level\":" << g_params.params_last_modified_level JSON_ENDL
}

static void StreamServerParams(ostream& os)
{
	os << dec;
	os << ",\"server-version\":" << g_params.server_version JSON_ENDL
	os << ",\"server-protocol-version\":" << g_params.protocol_version JSON_ENDL
	os << ",\"server-queries-only\":" << g_transact_service.info_only JSON_ENDL
}

static void StreamBlockChainStatus(ostream& os, const BlockChainStatus& blockchain_status)
{
	os << dec;
	os << ",\"blockchain-highest-indelible-level\":" << blockchain_status.last_indelible_level JSON_ENDL
	os << ",\"blockchain-highest-indelible-timestamp\":" << blockchain_status.last_indelible_timestamp JSON_ENDL
	os << ",\"blockchain-last-matching-completed-blocktime\":" << blockchain_status.last_matching_completed_block_time JSON_ENDL
	os << ",\"blockchain-last-matching-start-blocktime\":" << blockchain_status.last_matching_start_block_time JSON_ENDL
}

static void StreamTxParams(ostream& os)
{
	os << dec;
	os << ",\"query-work-difficulty\":" << g_transact_service.query_work_difficulty JSON_ENDL
	os << ",\"tx-work-difficulty\":" << g_params.tx_work_difficulty JSON_ENDL
	os << ",\"xcx-naked-buy-work-difficulty\":" << g_params.xcx_naked_buy_work_difficulty JSON_ENDL
	os << ",\"xcx-pay-work-difficulty\":" << g_params.xcx_pay_work_difficulty JSON_ENDL
	os << ",\"xcx-request-minimum-expiration-time\":" << XREQ_SIMPLE_HOLD_TIME + XREQ_MIN_POSTHOLD_TIME JSON_ENDL
	os << ",\"merkle-tree-oldest-commitment-number\":0" JSON_ENDL
	os << ",\"merkle-tree-next-commitment-number\":" << g_commitments.GetNextCommitnum() JSON_ENDL	// !!! note: small chance this could be out-of-sync with GetLastIndelibleLevel()
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
}

static void StreamValueLimits(ostream& os)
{
	os << dec;
	os << ",\"minimum-output-exponent\":" << g_blockchain.proof_params.outvalmin JSON_ENDL
	os << ",\"maximum-output-exponent\":" << g_blockchain.proof_params.outvalmax JSON_ENDL
	os << ",\"maximum-input-exponent\":" << g_blockchain.proof_params.invalmax JSON_ENDL
}

static void StreamDomainParams(ostream& os)
{
	os << dec;
	os << ",\"default-output-billet-domain-id\":" << g_params.default_domain JSON_ENDL
}

static void StreamDonationParams(ostream& os)
{
	os << dec;
	os << ",\"minimum-donation-per-transaction\":" << g_blockchain.proof_params.minimum_donation_fp JSON_ENDL
	os << ",\"donation-per-transaction\":" << g_blockchain.proof_params.donation_per_tx_fp JSON_ENDL
	os << ",\"donation-per-byte\":" << g_blockchain.proof_params.donation_per_byte_fp JSON_ENDL
	os << ",\"donation-per-output\":" << g_blockchain.proof_params.donation_per_output_fp JSON_ENDL		// !!! split between merkle tree outputs and non-merkle tree outputs?
	os << ",\"donation-per-input\":" << g_blockchain.proof_params.donation_per_input_fp JSON_ENDL		// !!! split between hidden (serialnum) and non-hidden (commitment) inputs?
	os << ",\"donation-per-crosschain-exchange-request\":" << g_blockchain.proof_params.donation_per_xcx_req_fp JSON_ENDL
}

static void StreamParams(ostream& os)
{
	StreamNetParams(os);
	StreamServerParams(os);
	StreamTxParams(os);
	StreamAmountBits(os);		// !!! if these change, need to make sure they stay sync'ed with parameter-level
	StreamDonationParams(os);	// !!! if these change, need to make sure they stay sync'ed with parameter-level
}

void TransactConnection::HandleTxQueryParams(const char *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryParams size " << size;

	BlockChainStatus blockchain_status;
	g_blockchain.GetLastIndelibleValues(blockchain_status);

	ostringstream os;
	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-parameters-query-results\":{" JSON_ENDL
	StreamParams(os);
	StreamBlockChainStatus(os, blockchain_status);
	StreamValueLimits(os);
	StreamDomainParams(os);
	os << "}}";

	//BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryParams sending " << m_writebuf.data();

	SendReply(os);
}

void TransactConnection::HandleTxQueryAddress(const char *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryAddress size " << size;

	uint64_t blockchain = 0, commitstart;
	bigint_t address;
	uint16_t maxret;

	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_from_buf(blockchain, TX_CHAIN_BYTES, bufpos, msg, size, bhex);
	copy_from_buf(address, TX_ADDRESS_BYTES, bufpos, msg, size, bhex);
	copy_from_buf(commitstart, sizeof(commitstart), bufpos, msg, size, bhex);
	copy_from_buf(maxret, sizeof(maxret), bufpos, msg, size, bhex);

	if (!maxret || bufpos != size)
	{
		static const string outbuf = "ERROR:malformed binary tx-address-query";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryAddress error malformed query; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryAddress", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	if (blockchain != g_params.blockchain)
		return SendBlockchainNumberError();

	if (maxret > TRANSACT_QUERY_MAX_COMMITS)
		maxret = TRANSACT_QUERY_MAX_COMMITS;

	bigint_t commitment[maxret];
	char commitiv[maxret][TX_COMMIT_IV_BYTES];
	uint64_t asset_enc[maxret], amount_enc[maxret], commitnum[maxret];
	uint32_t domain[maxret];

	bool have_more;

	auto nfound = tx_dbconn->TxOutputsSelect(&address, TX_ADDRESS_BYTES, commitstart, domain, asset_enc, amount_enc, commitiv[0], sizeof(commitiv[0]), (char*)commitment, sizeof(commitment[0]), commitnum, maxret, &have_more);
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
	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-address-query-report\":" JSON_ENDL
	os << "{\"server-timestamp\":" << unixtime() JSON_ENDL
	os << ",\"address\":\"0x" << hex << address << dec << "\"" JSON_ENDL
	os << ",\"commitment-number-start\":" << commitstart JSON_ENDL
	os << ",\"more-results-available\":" << (int)have_more JSON_ENDL
	os << ",\"tx-address-query-results\":[" JSON_ENDL
	for (int i = 0; i < nfound; ++i)
	{
		#define RETURN_BLOCKLEVEL 0	// this is for testing, but if reenabled, this test itself needs retesting

		#if RETURN_BLOCKLEVEL
		uint64_t level, timestamp;
		bigint_t root;

		auto rc = tx_dbconn->CommitRootsSelectCommitnum(commitnum[i], level, timestamp, &root, TX_MERKLE_BYTES);
		if (rc)
			return SendServerError(__LINE__);
		#endif

		bigint_t iv;

		memcpy((void*)&iv, &commitiv[i], sizeof(commitiv[i]));

		if (i) os << ",";
		os << dec;
		os << "{\"domain\":" << (domain[i] >> 1) JSON_ENDL
		if ((domain[i] >> 1) != g_params.default_domain)
			os << ",\"is-special-domain\":1" JSON_ENDL
		StreamAmountBits(os, false);
		if (domain[i] & 1)
		{
			os << dec;
			os << ",\"encrypted\":0" JSON_ENDL
			os << ",\"asset\":" << asset_enc[i] JSON_ENDL
			os << ",\"amount\":" << amount_enc[i] JSON_ENDL
		}
		else
		{
			os << hex;
			os << ",\"encrypted\":1" JSON_ENDL
			os << ",\"encrypted-asset\":\"0x" << asset_enc[i] << "\"" JSON_ENDL
			os << ",\"encrypted-amount\":\"0x" << amount_enc[i] << "\"" JSON_ENDL
			os << dec;
		}
		os << ",\"blockchain\":" << g_params.blockchain JSON_ENDL
		#if RETURN_BLOCKLEVEL
		os << ",\"block-level\":" << level JSON_ENDL
		os << ",\"block-time\":" << timestamp JSON_ENDL
		#endif
		os << hex;
		os << ",\"commitment-iv\":\"0x" << iv << "\"" JSON_ENDL
		os << ",\"commitment\":\"0x" << commitment[i] << "\"" JSON_ENDL
		os << dec;
		os << ",\"commitment-number\":" << commitnum[i] JSON_ENDL
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
	uint64_t blockchain = 0, param_level, merkle_time, next_commitnum, row_end, commitnum;

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

	rc = tx_dbconn->ParameterSelect(DB_KEY_COMMIT_COMMITNUM_HI, 0, &row_end, sizeof(row_end));
	if (rc)
		return SendServerError(__LINE__);

	rc = tx_dbconn->ParameterSelect(DB_KEY_COMMIT_NULL_INPUT, 0, &nullhash, TX_MERKLE_BYTES);
	if (rc)
		return SendServerError(__LINE__);

	//cerr << "parameter-level " << param_level << endl;
	//cerr << "merkle-root " << hash << endl;

	uint64_t param_time = 0;
	if (merkle_time > TX_TIME_OFFSET)
		param_time = (merkle_time - TX_TIME_OFFSET) / TX_TIME_DIVISOR;

	ostringstream os;
	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-input-query-report\":{" JSON_ENDL
	StreamParams(os);
	os << dec;
	os << ",\"tx-input-query-results\":" JSON_ENDL
	os << "{\"parameter-level\":" << param_level JSON_ENDL
	os << ",\"parameter-time\":" << param_time JSON_ENDL
	os << hex;
	os << ",\"merkle-root\":\"0x" << hash << "\"" JSON_ENDL
	StreamValueLimits(os);		// !!! if these change, need to make sure they stay sync'ed with parameter-level
	StreamDomainParams(os);
	os << dec;
	os << ",\"inputs\":[" JSON_ENDL

	for (unsigned i = 0; i < nin; ++i)
	{
		copy_from_buf(commitnum, sizeof(commitnum), bufpos, msg, size, bhex);

		//cerr << "commitnum " << hex << commitnum << dec << endl;

		if (commitnum > row_end)
		{
			char *outbuf = m_writebuf.data();

			sprintf(outbuf, "Not Found:%u", i);

			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryInputs not found"; // sending " << outbuf;

			WriteAsync("TransactConnection::HandleTxQueryInputs", boost::asio::buffer(outbuf, strlen(outbuf) + 1),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

			return;
		}

		if (i) os << ",";
		os << dec;
		os << "{\"commitment-number\":" << commitnum JSON_ENDL
		os << hex;
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

	uint64_t blockchain = 0;
	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_from_buf(blockchain, TX_CHAIN_BYTES, bufpos, msg, size, bhex);

	if (blockchain != g_params.blockchain)
		return SendBlockchainNumberError();

	ostringstream os;
	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	os << "{\"tx-serial-number-query-results\":[" JSON_ENDL

	for (unsigned i = 0; i < nserials; ++i)
	{
		bigint_t serialnum;

		copy_from_buf(serialnum, TX_SERIALNUM_BYTES, bufpos, msg, size, bhex);

		bigint_t hashkey;
		unsigned hashkey_size = sizeof(hashkey);
		uint64_t tx_commitnum;

		auto rc1 = tx_dbconn->SerialnumSelect(&serialnum, TX_SERIALNUM_BYTES, &hashkey, &hashkey_size, &tx_commitnum);
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

		if (i) os << ",";
		os << "{\"serial-number\":\"0x" << hex << serialnum << dec << "\"" JSON_ENDL
		os << ",\"status\":";
		if (!rc1)
		{
			os << "\"indelible\"" JSON_ENDL
			os << ",\"hashkey\":\"0x" << hex << hashkey << dec << "\"" JSON_ENDL
			if (tx_commitnum)
				os << ",\"transaction-commitment-number\":" << tx_commitnum JSON_ENDL
		}
		else if (rc2)
			os << "\"pending\"" JSON_ENDL
		else
			os << "\"unspent\"" JSON_ENDL
		os << "}";
	}

	os << "]}";

	SendReply(os);
}

void TransactConnection::HandleTxQueryXreqs(const char *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXreqs size " << size;

	uint32_t bufpos = 0;
	const bool bhex = false;

	Xreq xreq;
	unsigned xcx_type = 0;
	uint64_t rate_fp = 0;
	uint16_t maxret, offset;
	uint8_t flags;

	copy_from_buf(xcx_type, 1, bufpos, msg, size, bhex);
	copy_from_buf(xreq.min_amount, sizeof(xreq.min_amount), bufpos, msg, size, bhex);
	copy_from_buf(xreq.max_amount, sizeof(xreq.max_amount), bufpos, msg, size, bhex);
	copy_from_buf(rate_fp, UNIFLOAT_WIRE_BYTES, bufpos, msg, size, bhex);
	copy_from_buf(xreq.base_asset, sizeof(xreq.base_asset), bufpos, msg, size, bhex);
	copy_from_buf(xreq.quote_asset, sizeof(xreq.quote_asset), bufpos, msg, size, bhex);
	copy_from_buf(maxret, sizeof(maxret), bufpos, msg, size, bhex);
	copy_from_buf(offset, sizeof(offset), bufpos, msg, size, bhex);
	copy_from_buf(flags, sizeof(flags), bufpos, msg, size, bhex);

	if (bufpos > size)
	{
		static const string outbuf = "ERROR:malformed exchange-requests-query";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXreqs error malformed query; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryXreqs", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	bool only_pending_matched = !!(flags & TX_QUERY_XREQS_FLAG_ONLY_PENDING_MATCHED);
	bool include_pending_matched = !!(flags & TX_QUERY_XREQS_FLAG_INCLUDE_PENDING_MATCHED);

	if ((only_pending_matched && include_pending_matched) || (flags & ~TX_QUERY_XREQS_FLAG_ONLY_PENDING_MATCHED & ~TX_QUERY_XREQS_FLAG_INCLUDE_PENDING_MATCHED))
	{
		static const string outbuf = "ERROR:invalid exchange-requests-query flags";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXreqs error invalid flags; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryXreqs", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	xreq.foreign_asset = string(msg + bufpos, size - bufpos);

	auto select_buyers = !Xtx::TypeIsBuyer(xcx_type);

	tx_dbconn->MatchingInitTypeRange(xcx_type, true, xreq);

	/*
		Seller wants a higher rate (more Foreign)
			--> when selecting buy reqs, scan descending from high to low, starting at max rate
		Buyer wants a lower rate (less Foreign)
			--> when selecting sell reqs, scan ascending from low to high, starting at min rate

		The search offset may be necessary to scan all requests, but it is inefficient.
		Using the starting rate to scan is more efficient, but it has the limited precision of a UniFloat::WireEncode, and the value therefore has large steps.
		To force the client to use starting rate over offset, the offset will only operate up to the next step up in the starting rate.
		This is implemented by setting xreq.matching_rate_required to this next step up in rate.
		XreqsSelectPendingMatchRate and XreqsSelectOpenRateRequired use this to limit the operation of offset.
	*/

	int scan_direction = Xreq::RateSign(select_buyers);

	auto max_rate_fp = ((uint64_t)1 << UNIFLOAT_BITS) - 1;

	xreq.open_rate_required = UniFloat::WireDecode(rate_fp);

	if (!xreq.open_rate_required.asFloat() && scan_direction < 0)
	{
		xreq.open_rate_required = DBL_MAX;
		rate_fp = max_rate_fp;
	}

	xreq.pending_match_rate = xreq.open_rate_required;

	xreq.matching_rate_required = UniFloat::WireDecode(rate_fp, scan_direction);

	auto rate_step_back = UniFloat::WireDecode(rate_fp, -scan_direction);

	if (0)	// for testing
	{
		uint64_t rate_fp = 0;
		cerr << "encode 0 "		<< UniFloat::WireEncode(0) << endl;
		cerr << "encode 0+1 "		<< UniFloat::WireEncode(UniFloat::WireDecode(rate_fp, 1)) << endl;
		cerr << "encode DBL_MAX "	<< hex << UniFloat::WireEncode(DBL_MAX) << dec << endl;
		cerr << "decode max "		<< UniFloat::WireDecode(UniFloat::WireEncode(DBL_MAX)) << endl;
		cerr << "DBL_MAX "	<< DBL_MAX << endl;
		cerr << "diff "		<< UniFloat::WireDecode(UniFloat::WireEncode(DBL_MAX)).asFloat() - DBL_MAX << endl;

		cerr << "decode 0-1 "		<< UniFloat::WireDecode(rate_fp, -1) << endl;
		cerr << "diff "		<< UniFloat::WireDecode(rate_fp, -1).asFloat() + DBL_MAX << endl;
		cerr << "decode 0+0 "		<< UniFloat::WireDecode(rate_fp, 0) << endl;
		cerr << "decode 0+1 "		<< UniFloat::WireDecode(rate_fp, 1) << endl;

		rate_fp = 1;
		cerr << "decode 1-1 "		<< UniFloat::WireDecode(rate_fp, -1) << endl;
		cerr << "decode 1+0 "		<< UniFloat::WireDecode(rate_fp, 0) << endl;
		cerr << "decode 1+1 "		<< UniFloat::WireDecode(rate_fp, 1) << endl;

		rate_fp = ((uint64_t)1 << UNIFLOAT_BITS) - 1;
		cerr << "decode max-1 "	<< UniFloat::WireDecode(rate_fp, -1) << endl;
		cerr << "decode max+0 "	<< UniFloat::WireDecode(rate_fp, 0) << endl;
		cerr << "decode max+1 "	<< UniFloat::WireDecode(rate_fp, 1) << endl;
		cerr << "DBL_MAX "	<< DBL_MAX << endl;
		cerr << "diff "		<< UniFloat::WireDecode(rate_fp, 1).asFloat() - DBL_MAX << endl;
	}

	xreq.consideration_required = 0;
	xreq.consideration_offered = 0;
	xreq.accept_time_required = 0;
	xreq.accept_time_offered = 0;

	if (Xtx::TypeIsSimple(xcx_type))
		xreq.pledge = XREQ_SIMPLE_PLEDGE;
	else
		xreq.pledge = 0;

	xreq.payment_time = Xreq::DefaultPaymentTime(IsTestnet(g_params.blockchain));
	xreq.confirmations = Xreq::DefaultConfirmations(IsTestnet(g_params.blockchain));

	if (maxret > TRANSACT_QUERY_MAX_XREQS)
		maxret = TRANSACT_QUERY_MAX_XREQS;

	Xreq xreqs[maxret];
	bool have_more;
	int nfound;

	if (only_pending_matched)
		nfound = tx_dbconn->XreqsSelectPendingMatchRate(xreq, xcx_type, maxret, offset, xreqs, &have_more);
	else
		nfound = tx_dbconn->XreqsSelectOpenRateRequired(xreq, xcx_type, maxret, offset, include_pending_matched, xreqs, &have_more);

	if (nfound < 0)
		return SendServerError(__LINE__);

	ostringstream os;
	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	auto t0 = unixtime();

	const char *offered = "offered";
	const char *required = "required";

	os << "{\"exchange-requests-query-report\":" JSON_ENDL
	os << "{\"server-timestamp\":" << t0 JSON_ENDL
	os << ",\"blockchain-number\":" << g_params.blockchain JSON_ENDL
	if (only_pending_matched)
		os << ",\"include-only-pending-matched\":true" JSON_ENDL
	else if (include_pending_matched)
		os << ",\"include-pending-matched\":true" JSON_ENDL
	os << ",\"base-asset\":" << xreq.base_asset JSON_ENDL
	os << ",\"quote-asset\":" << xreq.quote_asset JSON_ENDL
	if (xreq.foreign_asset.length())
		os << ",\"foreign-asset\":\"" << json_escape(xreq.foreign_asset) << "\"" JSON_ENDL
	os << ",\"exchange-request-matching-type\":" << xcx_type JSON_ENDL
	os << ",\"type-minimum\":" << xreq.type JSON_ENDL
	os << ",\"type-maximum\":" << xreq.db_search_max JSON_ENDL
	if (select_buyers)
	{
		os << ",\"type-is-buyer\":true" JSON_ENDL
		os << ",\"maximum-rate\":" << xreq.open_rate_required JSON_ENDL
		os << ",\"maximum-rate-step\":" << xreq.matching_rate_required JSON_ENDL
	}
	else
	{
		os << ",\"type-is-seller\":true" JSON_ENDL
		os << ",\"minimum-rate\":" << xreq.open_rate_required JSON_ENDL
		os << ",\"minimum-rate-step\":" << xreq.matching_rate_required JSON_ENDL
	}
	if (abs(rate_step_back.asFloat()) < DBL_MAX)
		os << ",\"debug-rate-step-back\":" << rate_step_back JSON_ENDL
	os << ",\"minimum-amount\":\"" << xreq.min_amount << "\"" JSON_ENDL
	os << ",\"maximum-amount\":\"" << xreq.max_amount << "\"" JSON_ENDL
	os << ",\"consideration-required\":" << xreq.consideration_required JSON_ENDL
	os << ",\"consideration-offered\":" << xreq.consideration_offered JSON_ENDL
	os << ",\"accept-time-required\":" << xreq.accept_time_required JSON_ENDL
	os << ",\"accept-time-offered\":" << xreq.accept_time_offered JSON_ENDL
	os << ",\"pledge-" << (!select_buyers ? offered : required) << "\":" << xreq.pledge JSON_ENDL
	os << ",\"payment-time-" << (!select_buyers ? offered : required) << "\":" << xreq.payment_time JSON_ENDL
	os << ",\"confirmations-" << (!select_buyers ? offered : required) << "\":" << xreq.confirmations JSON_ENDL
	os << ",\"exchange-requests-query-results\":[" JSON_ENDL

	bool needs_comma = false;

	for (int i = 0; i < nfound; ++i)
	{
		Xreq& xreq = xreqs[i];

		//BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXreqs time " << t0 << " type " << xcx_type << " min_rate " << min_rate << " open_rate_required " << xreq.open_rate_required;

		if (needs_comma) os << ",";
		needs_comma = true;
		os << "{\"exchange-request-type\":" << xreq.type JSON_ENDL
		os << ",\"request-number\":" << xreq.xreqnum JSON_ENDL
		//os << ",\"IsBuyer\":" << xreq.IsBuyer() JSON_ENDL
		os << ",\"object-id\":\"" << buf2hex(&xreq.objid, sizeof(xreq.objid)) << "\"" JSON_ENDL
		os << ",\"expire-time\":" << xreq.expire_time JSON_ENDL
		os << ",\"base-asset\":" << xreq.base_asset JSON_ENDL
		os << ",\"quote-asset\":" << xreq.quote_asset JSON_ENDL
		if (xreq.foreign_asset.length())
			os << ",\"foreign-asset\":\"" << json_escape(xreq.foreign_asset) << "\"" JSON_ENDL
		os << ",\"minimum-amount\":\"" << xreq.min_amount << "\"" JSON_ENDL
		os << ",\"maximum-amount\":\"" << xreq.max_amount << "\"" JSON_ENDL
		os << ",\"net-rate-required\":" << xreq.net_rate_required JSON_ENDL
		os << ",\"wait-discount\":" << xreq.wait_discount JSON_ENDL
		os << ",\"base-costs\":" << xreq.base_costs JSON_ENDL
		os << ",\"quote-costs\":" << xreq.quote_costs JSON_ENDL
		os << ",\"destination\":\"0x" << hex << xreq.destination << dec << "\"" JSON_ENDL
		if (xreq.foreign_address.length())
			os << ",\"foreign-address\":\"" << json_escape(xreq.foreign_address) << "\"" JSON_ENDL

		os << ",\"add-immediately-to-blockchain\":" << xreq.flags.add_immediately_to_blockchain JSON_ENDL
		os << ",\"auto-accept-matches\":" << xreq.flags.auto_accept_matches JSON_ENDL
		os << ",\"no-minimum-after-first-match\":" << xreq.flags.no_minimum_after_first_match JSON_ENDL
		os << ",\"must-liquidate-crossing-minimum\":" << xreq.flags.must_liquidate_crossing_minimum JSON_ENDL
		os << ",\"must-liquidate-below-minimum\":" << xreq.flags.must_liquidate_below_minimum JSON_ENDL

		os << ",\"consideration-required\":" << xreq.consideration_required JSON_ENDL
		os << ",\"consideration-offered\":" << xreq.consideration_offered JSON_ENDL
		os << ",\"pledge-" << (xreq.IsBuyer() ? offered : required) << "\":" << xreq.pledge JSON_ENDL
		os << ",\"hold-time\":" << xreq.hold_time JSON_ENDL
		os << ",\"hold-time-required\":" << xreq.hold_time_required JSON_ENDL
		os << ",\"minimum-wait-time\":" << xreq.min_wait_time JSON_ENDL
		os << ",\"accept-time-required\":" << xreq.accept_time_required JSON_ENDL
		os << ",\"accept-time-offered\":" << xreq.accept_time_offered JSON_ENDL
		os << ",\"payment-time-" << (xreq.IsBuyer() ? offered : required) << "\":" << xreq.payment_time JSON_ENDL
		os << ",\"confirmations-" << (xreq.IsBuyer() ? offered : required) << "\":" << xreq.confirmations JSON_ENDL

		if (xreq.xreqnum)
			os << ",\"blocktime\":" << xreq.blocktime JSON_ENDL
		os << ",\"open-amount\":\"" << xreq.open_amount << "\"" JSON_ENDL
		os << ",\"open-rate-required\":" << xreq.open_rate_required JSON_ENDL

		if (xreq.pending_match_rate.asFloat())
		{
			os << ",\"pending-match-amount\":\"" << xreq.pending_match_amount << "\"" JSON_ENDL
			os << ",\"pending-match-rate\":" << xreq.pending_match_rate JSON_ENDL
			os << ",\"pending-match-hold-time\":" << xreq.pending_match_hold_time JSON_ENDL
		}
		os << "}" JSON_ENDL
	}

	os << "]";
	os << ",\"results-offset\":" << offset JSON_ENDL
	os << ",\"more-results-available\":" << (int)have_more JSON_ENDL
	os << "}}";

	SendReply(os);
}

static void StreamXmatchreq(ostream& os, const Xmatch& match, const Xmatchreq& matchreq)
{
	auto isbuyer = Xtx::TypeIsBuyer(matchreq.type);

	os << "\"" << (isbuyer ? "buy" : "sell") << "-request\":" JSON_ENDL
	os << "{\"number\":" << matchreq.xreqnum JSON_ENDL

	if (match.have_xreqs)
	{
		const char *offered = "offered";
		const char *required = "required";

		os << ",\"object-id\":\"" << buf2hex(&matchreq.objid, sizeof(matchreq.objid)) << "\"" JSON_ENDL
		os << ",\"type\":" << matchreq.type JSON_ENDL
		os << ",\"minimum-amount\":\"" << matchreq.min_amount << "\"" JSON_ENDL
		os << ",\"maximum-amount\":\"" << matchreq.max_amount << "\"" JSON_ENDL
		os << ",\"net-rate-required\":" << matchreq.net_rate_required JSON_ENDL
		os << ",\"wait-discount\":" << matchreq.wait_discount JSON_ENDL
		os << ",\"base-costs\":" << matchreq.base_costs JSON_ENDL
		os << ",\"quote-costs\":" << matchreq.quote_costs JSON_ENDL
		os << ",\"consideration-required\":" << matchreq.consideration_required JSON_ENDL
		os << ",\"consideration-offered\":" << matchreq.consideration_offered JSON_ENDL
		os << ",\"pledge-" << (isbuyer ? offered : required) << "\":" << matchreq.pledge JSON_ENDL
		os << ",\"hold-time\":" << matchreq.hold_time JSON_ENDL
		os << ",\"hold-time-required\":" << matchreq.hold_time_required JSON_ENDL
		os << ",\"minimum-wait-time\":" << matchreq.min_wait_time JSON_ENDL
		os << ",\"accept-time-required\":" << matchreq.accept_time_required JSON_ENDL
		os << ",\"accept-time-offered\":" << matchreq.accept_time_offered JSON_ENDL
		os << ",\"payment-time-" << (isbuyer ? offered : required) << "\":" << matchreq.payment_time JSON_ENDL
		os << ",\"confirmations-" << (isbuyer ? offered : required) << "\":" << matchreq.confirmations JSON_ENDL
		os << ",\"add-immediately-to-blockchain\":" << matchreq.flags.add_immediately_to_blockchain JSON_ENDL
		os << ",\"auto-accept-matches\":" << matchreq.flags.auto_accept_matches JSON_ENDL
		os << ",\"no-minimum-after-first-match\":" << matchreq.flags.no_minimum_after_first_match JSON_ENDL
		os << ",\"must-liquidate-crossing-minimum\":" << matchreq.flags.must_liquidate_crossing_minimum JSON_ENDL
		os << ",\"must-liquidate-below-minimum\":" << matchreq.flags.must_liquidate_below_minimum JSON_ENDL
		if (matchreq.flags.have_matching && !isbuyer)
			os << ",\"foreign-address\":\"" << json_escape(matchreq.foreign_address) << "\"" JSON_ENDL
	}

	os << "}" JSON_ENDL
}

static void StreamXmatch(ostream& os, Xmatch& match)
{
	os << "\"number\":" << match.xmatchnum JSON_ENDL
	os << ",\"type\":" << match.type JSON_ENDL
	os << ",\"status\":" << match.status JSON_ENDL
	os << ",\"base-asset\":" << match.xsell.base_asset JSON_ENDL
	os << ",\"quote-asset\":" << match.xsell.quote_asset JSON_ENDL
	if (match.xsell.foreign_asset.length())
		os << ",\"foreign-asset\":\"" << json_escape(match.xsell.foreign_asset) << "\"" JSON_ENDL
	os << ",\"base-amount\":\"" << match.base_amount << "\"" JSON_ENDL
	os << ",\"rate\":" << match.rate JSON_ENDL
	os << ",\"accept-time\":" << match.accept_time JSON_ENDL
	if (match.xbuy.match_consideration)
		os << ",\"buyer-consideration\":" << match.xbuy.match_consideration JSON_ENDL
	if (match.xsell.match_consideration)
		os << ",\"seller-consideration\":" << match.xsell.match_consideration JSON_ENDL
	if (match.match_pledge)
		os << ",\"match-pledge\":" << match.match_pledge JSON_ENDL
	if (match.next_deadline)
		os << ",\"next-deadline\":" << match.next_deadline JSON_ENDL
	if (match.match_timestamp)
		os << ",\"match-timestamp\":" << match.match_timestamp JSON_ENDL
	if (match.accept_timestamp)
		os << ",\"accept-timestamp\":" << match.accept_timestamp JSON_ENDL
	if (match.final_timestamp)
		os << ",\"final-timestamp\":" << match.final_timestamp JSON_ENDL
	os << ",\"amount-paid\":" << match.amount_paid JSON_ENDL
	os << ",\"mining-amount\":" << match.mining_amount JSON_ENDL
}

void TransactConnection::HandleTxQueryXmatchreq(uint32_t tag, const char *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXmatchreq tag " << hex << tag << dec << " size " << size;

	uint64_t blockchain = 0;
	uint64_t xreqnum = 0;
	uint64_t xmatchnum = 0;
	ccoid_t  objid;
	uint16_t maxret;
	bool header_sent = false;
	bool have_more = false;

	uint32_t bufpos = 0;
	const bool bhex = false;
	ostringstream os;

	BlockChainStatus blockchain_status;
	g_blockchain.GetLastIndelibleValues(blockchain_status);

	copy_from_buf(blockchain, TX_CHAIN_BYTES, bufpos, msg, size, bhex);

	if (tag == CC_TAG_TX_QUERY_XMATCH_OBJID)
	{
		copy_from_buf(objid, sizeof(objid), bufpos, msg, size, bhex);
		copy_from_buf(maxret, sizeof(maxret), bufpos, msg, size, bhex);

		if (!maxret || bufpos != size)
			goto malformed;
	}
	else if (tag == CC_TAG_TX_QUERY_XMATCH_REQNUM)
	{
		copy_from_buf(xreqnum, sizeof(xreqnum), bufpos, msg, size, bhex);
		copy_from_buf(maxret, sizeof(maxret), bufpos, msg, size, bhex);
		copy_from_buf(xmatchnum, sizeof(xmatchnum), bufpos, msg, size, bhex);

		// TODO: parse array of xmatchnum's

		if (!xreqnum || !maxret || bufpos > size || (size - bufpos) % sizeof(xmatchnum))
			goto malformed;
	}
	else
		CCASSERT(0);

	if (blockchain != g_params.blockchain)	// note, malformed msg checked before blockchain #
		return SendBlockchainNumberError();

	if (tag == CC_TAG_TX_QUERY_XMATCH_OBJID)
	{
		auto rc = tx_dbconn->XmatchreqSelectObjIdDescendingId(objid, INT64_MAX, xreqnum);
		if (rc < 0)
			return SendServerError(__LINE__);

		//cerr << "HandleTxQueryXmatchreq found xreqnum " << xreqnum << endl;
	}

	if (maxret > TRANSACT_QUERY_MAX_MATCHES)
		maxret = TRANSACT_QUERY_MAX_MATCHES;

	//memset(m_writebuf.data(), 0, m_writebuf.size());	// for testing

	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	for (unsigned i = 0; i <= maxret && xmatchnum <= INT64_MAX; ++i)
	{
		Xmatch match;
		Xmatchreq *req = NULL;
		Xmatchreq *matching = NULL;

		auto rc = tx_dbconn->XmatchSelectReqnum(xreqnum, xmatchnum, match);
		if (rc < 0)
			return SendServerError(__LINE__);

		if (match.xbuy.xreqnum == xreqnum)
			req = &match.xbuy;
		else
			matching = &match.xbuy;

		if (match.xsell.xreqnum == xreqnum)
			req = &match.xsell;
		else
			matching = &match.xsell;

		if (match.have_xreqs)
		{
			CCASSERT(req);
			CCASSERT(matching);
		}

		if (!header_sent)
		{
			header_sent = true;

			os << "{\"exchange-matchreq-query-report\":" JSON_ENDL
			os << "{\"server-timestamp\":" << unixtime() JSON_ENDL
			os << ",\"blockchain-number\":" << g_params.blockchain JSON_ENDL
			StreamBlockChainStatus(os, blockchain_status);
			if (tag == CC_TAG_TX_QUERY_XMATCH_OBJID)
				os << ",\"request-object-id\":\"" << buf2hex(&objid, sizeof(objid)) << "\"" JSON_ENDL
			if (xreqnum)
				os << ",\"request-number\":" << xreqnum JSON_ENDL
			if (match.have_xreqs)
			{
				os << ",\"disposition\":" << req->disposition JSON_ENDL
				if (req->flags.have_matching)
					os << ",\"open-amount\":\"" << req->open_amount << "\"" JSON_ENDL
			}
			os << ",\"request-match-number-start\":" << xmatchnum JSON_ENDL
			os << ",\"exchange-matchreq-query-results\":[" JSON_ENDL
		}

		if (rc)
			break;

		CCASSERT(match.xmatchnum >= xmatchnum);

		//cerr << "XmatchSelectReqnum xreqnum " << xreqnum << " xmatchnum >= " << xmatchnum << " found xmatchnum " << match.xmatchnum << " buyer xreqnum " << match.xbuy.xreqnum << " seller xreqnum " << match.xsell.xreqnum << endl;

		if (i == maxret)
		{
			have_more = true;
			break;
		}

		if (i) os << ",";
		os << "{\"match\":{" JSON_ENDL

		StreamXmatch(os, match);
		os << ",";
		StreamXmatchreq(os, match, *matching);

		os << "}}" JSON_ENDL

		xmatchnum = match.xmatchnum + 1;
	}

	os << "]";
	os << ",\"more-results-available\":" << (int)have_more JSON_ENDL
	os << "}}";

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXmatchreq found";	// sending " << m_writebuf.data();

	return SendReply(os);

malformed:

	{
		static const string outbuf = "ERROR:malformed binary exchange-matchreq-query";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXmatchreq tag " << hex << tag << dec << " error malformed query; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryXmatchreq", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}
}

void TransactConnection::HandleTxQueryXmatch(const char *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXmatch size " << size;

	uint64_t blockchain = 0;
	uint64_t xmatchnum = 0;

	uint32_t bufpos = 0;
	const bool bhex = false;
	ostringstream os;

	BlockChainStatus blockchain_status;
	g_blockchain.GetLastIndelibleValues(blockchain_status);

	copy_from_buf(blockchain, TX_CHAIN_BYTES, bufpos, msg, size, bhex);

	copy_from_buf(xmatchnum, sizeof(xmatchnum), bufpos, msg, size, bhex);

	if (!xmatchnum || bufpos != size)
	{
		static const string outbuf = "ERROR:malformed binary exchange-match-query";

		if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXmatch error malformed query; sending " << outbuf;

		WriteAsync("TransactConnection::HandleTxQueryXmatch", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	if (blockchain != g_params.blockchain)	// note, malformed msg checked before blockchain #
		return SendBlockchainNumberError();

	//memset(m_writebuf.data(), 0, m_writebuf.size());	// for testing

	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	Xmatch match;

	auto rc = tx_dbconn->XmatchSelect(xmatchnum, match);
	if (rc < 0)
		return SendServerError(__LINE__);

	os << "{\"exchange-match-query-report\":" JSON_ENDL
	os << "{\"server-timestamp\":" << unixtime() JSON_ENDL
	os << ",\"blockchain-number\":" << g_params.blockchain JSON_ENDL
	StreamBlockChainStatus(os, blockchain_status);
	os << ",\"match-number\":" << xmatchnum JSON_ENDL
	os << ",\"exchange-match-query-results\":{" JSON_ENDL

	if (!rc)
	{
		match.have_xreqs = false;	// only send xreqs numbers

		StreamXmatch(os, match);
		os << ",";
		StreamXmatchreq(os, match, match.xbuy);
		os << ",";
		StreamXmatchreq(os, match, match.xsell);
	}

	os << "}}}" JSON_ENDL

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXmatch found";	// sending " << m_writebuf.data();

	return SendReply(os);
}

void TransactConnection::HandleTxQueryXminingInfo(const char *msg, unsigned size)
{
	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXminingInfo size " << size;

	BlockChainStatus blockchain_status;
	g_blockchain.GetLastIndelibleValues(blockchain_status);

	ostringstream os;
	os.rdbuf()->pubsetbuf(m_writebuf.data(), m_writebuf.size());

	os << "{\"exchange-mining-info-query-results\":{" JSON_ENDL
	StreamNetParams(os);
	StreamBlockChainStatus(os, blockchain_status);

	ExchangeMiningParams mining;
	g_exchange_mining.GetMiningParams(mining);
	string amts;

	os << ",\"mining-start-time\":" << g_exchange_mining.mining_start_time JSON_ENDL
	os << ",\"mining-update-time-increment\":" << mining.mining_update_time_increment JSON_ENDL
	amount_to_string(g_exchange_mining.mined_asset, mining.total_mined, amts);
	os << ",\"total-mined\":" << amts JSON_ENDL
	amount_to_string(g_exchange_mining.mined_asset, mining.total_remaining_to_mine, amts);
	os << ",\"total-remaining-to-mine\":" << amts JSON_ENDL
	os << ",\"mining-amount-multiplier\":" << mining.mining_amount_multiplier JSON_ENDL
	os << ",\"currently-mineable-amount-increment\":" << mining.last_nominal_mineable_amount_increase JSON_ENDL
	os << ",\"currently-mineable-amount\":" << mining.currently_mineable_amount JSON_ENDL
	os << ",\"currently-mineable-amount-maximum\":" << mining.max_currently_mineable_amount JSON_ENDL
	os << ",\"mining-fraction-per-match-maximum\":" << mining.mining_max_fraction_per_match JSON_ENDL
	os << ",\"mining-fraction-per-match-minimum\":" << mining.mining_min_fraction_per_match JSON_ENDL
	os << ",\"mining-match-average-amount\":" << mining.mining_stats.avg_amount JSON_ENDL
	os << ",\"mining-match-average-rate\":" << mining.mining_stats.avg_match_rate JSON_ENDL
	os << ",\"mining-request-average-match-rate-required\":" << mining.mining_stats.avg_match_rate_required JSON_ENDL
	os << ",\"mining-request-minimum-expiration-time\":" << XREQ_SIMPLE_HOLD_TIME + XREQ_MIN_POSTHOLD_TIME JSON_ENDL
	os << "}}";

	//BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TransactConnection::HandleTxQueryXminingInfo sending " << m_writebuf.data();

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

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::SendObjectNotValid sending " << outbuf;

	WriteAsync("TransactConnection::SendObjectNotValid", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendBlockchainNumberError()
{
	static const string outbuf = "ERROR:requested blockchain not tracked by this server";

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::SendBlockchainNumberError sending " << outbuf;

	WriteAsync("TransactConnection::SendBlockchainNumberError", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendTooManyObjectsError()
{
	static const string outbuf = "ERROR:too many query objects";

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::SendTooManyObjectsError too many query objects; sending " << outbuf;

	WriteAsync("TransactConnection::SendTooManyObjectsError", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendNotConnectedError()
{
	static const string outbuf = "ERROR:server not connected";

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::SendNotConnectedError sending " << outbuf;

	WriteAsync("TransactConnection::SendNotConnectedError", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendServerError(unsigned line)
{
	static const string outbuf = "ERROR:server error";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TransactConnection::SendServerError from line " << line << " sending " << outbuf;

	WriteAsync("TransactConnection::SendServerError", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendServerUnknown(unsigned line)
{
	static const string outbuf = "UNKNOWN:server error";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TransactConnection::SendServerUnknown from line " << line << " sending " << outbuf;

	WriteAsync("TransactConnection::SendServerUnknown", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendReplyWriteError()
{
	static const string outbuf = "UNKNOWN:server reply buffer write error";

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TransactConnection::SendReplyWriteError sending " << outbuf;

	WriteAsync("TransactConnection::SendReplyWriteError", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactConnection::SendTimeout()
{
	static const string outbuf = "UNKNOWN:server timeout";

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TransactConnection::SendTimeout sending " << outbuf;

	WriteAsync("TransactConnection::SendTimeout", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void TransactService::DumpExtraConfigTop() const
{
	cout << "   info only = " << yesno(info_only) << endl;
	cout << "   relay only = " << yesno(relay_only) << endl;
}

void TransactService::DumpExtraConfigBottom() const
{
	cout << "   max network seconds = " << max_net_sec << endl;
	cout << "   max indelible block age = " << max_block_sec << endl;
	cout << "   query work difficulty = " << query_work_difficulty << endl;
}

bool TransactService::IsConnectedToNet() const
{
	bool connected = false;

	int32_t delta;

	while (true)	// so we can use break on error
	{
		if (max_net_sec)
		{
			// not connected if elapsed time since last block received from relay > max_net_sec

			auto net_ticks = g_processblock.GetLastNetworkTime();

			if (!net_ticks)
				break;

			auto ticks = ccticks();

			delta = ccticks_elapsed(net_ticks, ticks)/CCTICKS_PER_SEC;

			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << "TransactService::IsConnectedToNet net_ticks " << net_ticks << " delta " << delta << " max_net_sec " << max_net_sec;

			if (delta > max_net_sec)
				break;

			// not connected if elapsed time since last block became indelible > max_net_sec

			auto block_ticks = g_blockchain.GetLastIndelibleTicks();

			delta = ccticks_elapsed(block_ticks, ticks)/CCTICKS_PER_SEC;

			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << "TransactService::IsConnectedToNet block_ticks " << block_ticks << " delta " << delta << " max_net_sec " << max_net_sec;

			if (delta > max_net_sec)
				break;
		}

		if (max_block_sec)
		{
			// not connected if timestamp age of last indelible block > max_block_sec

			auto blocktime = g_blockchain.GetLastIndelibleTimestamp();

			delta = unixtime() - blocktime;

			if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << "TransactService::IsConnectedToNet blocktime " << blocktime << " delta " << delta << " max_block_sec " << max_block_sec;

			if (delta > max_block_sec)
				break;
		}

		connected = true;

		break;
	}

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(trace) << "TransactService::IsConnectedToNet connected " << connected;

	return connected;
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

	BOOST_LOG_TRIVIAL(info) << "TransactThread::ThreadProc start " << (uintptr_t)this << " dbconn " << (uintptr_t)tx_dbconn;

	threadproc();

	BOOST_LOG_TRIVIAL(info) << "TransactThread::ThreadProc end " << (uintptr_t)this << " dbconn " << (uintptr_t)tx_dbconn;

	delete tx_dbconn;
}
