/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * processtx.cpp
*/

#include "ccnode.h"
#include "processtx.hpp"
#include "process-xreq.hpp"
#include "foreign-query.hpp"
#include "foreign-rpc.hpp"
#include "transact.hpp"
#include "blockchain.hpp"
#include "block.hpp"
#include "commitments.hpp"
#include "dbparamkeys.h"
#include "witness.hpp"

#include <CCobjects.hpp>
#include <CCmint.h>
#include <transaction.hpp>
#include <transaction.h>
#include <xtransaction-xreq.hpp>
#include <xtransaction-xpay.hpp>
#include <xmatch.hpp>
#include <ccserver/connection_registry.hpp>

#define TRACE_PROCESS_TX	(g_params.trace_tx_validation)

//!#define RTEST_CUZZ				(1024-1)
//!#define RTEST_CUZZ_WAIT			(1024-1)
//!#define TEST_DELAY_SOME_TXS		1
//#define TEST_IGNORE_TRANSIENT_DUPLICATE_FOREIGN_ADDRESSES		1

#ifndef RTEST_CUZZ
#define RTEST_CUZZ					0	// don't test
#endif

#ifndef RTEST_CUZZ_WAIT
#define RTEST_CUZZ_WAIT					0	// don't test
#endif

#ifndef TEST_DELAY_SOME_TXS
#define TEST_DELAY_SOME_TXS		0	// don't test
#endif

#ifndef TEST_IGNORE_TRANSIENT_DUPLICATE_FOREIGN_ADDRESSES
#define TEST_IGNORE_TRANSIENT_DUPLICATE_FOREIGN_ADDRESSES		0	// don't test
#endif

#define FOREIGN_TX_PAST_ALLOWANCE		(4*3600)		// 4 hours	// TODO config this by blockchain
#define FOREIGN_TX_FUTURE_ALLOWANCE		(2*3600)		// 2 hours	// TODO config this by blockchain

//#define FOREIGN_TX_PAST_ALLOWANCE		123456789 // for testing

ProcessTx g_processtx;

static mutex processtx_mutex;
static condition_variable processtx_condition_variable;
static atomic<int> block_txs_pending;

void ProcessTx::Init()
{
	if (g_params.tx_validation_threads <= 0)
		return;

	m_threads.clear();
	m_threads.reserve(g_params.tx_validation_threads);

	for (int i = 0; i < g_params.tx_validation_threads && !g_shutdown; ++i)
	{
		auto t = new thread(ThreadProc);
		m_threads.push_back(t);
	}
}

void ProcessTx::Stop()
{
	BOOST_LOG_TRIVIAL(trace) << "ProcessTx::Stop";

	CCASSERT(g_shutdown);

	lock_guard<mutex> lock(processtx_mutex);

	processtx_condition_variable.notify_all();

	DbConnProcessQ::StopQueuedWork(PROCESS_Q_TYPE_TX);

	BOOST_LOG_TRIVIAL(trace) << "ProcessTx::Stop done";
}

void ProcessTx::DeInit()
{
	BOOST_LOG_TRIVIAL(trace) << "ProcessTx::DeInit";

	for (auto t : m_threads)
	{
		t->join();
		delete t;
	}

	m_threads.clear();

	BOOST_LOG_TRIVIAL(trace) << "ProcessTx::DeInit done";
}

const char* ProcessTx::ResultString(int result)
{
	//if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::ResultString result " << result;

	static const char *tx_result_warn_strings[] =
	{
		"OK",
		/* TX_RESULT_PARAM_LEVEL_TOO_OLD */			"INVALID:parameter level too old",
		/* TX_RESULT_EXPIRED */						"INVALID:expired",
		/* TX_RESULT_ALREADY_SPENT */				"INVALID:already spent",
		/* TX_RESULT_ADDRESS_IN_USE */				"INVALID:foreign address not unique",
		/* TX_RESULT_ALREADY_PAID */				"INVALID:already paid",
		/* TX_RESULT_FOREIGN_ERROR */				"ERROR:foreign server error"
	};

	static const char *tx_result_stop_strings[] =
	{
		/* TX_RESULT_INTERNAL_ERROR */				"ERROR:internal server error",
		/* TX_RESULT_SERVER_ERROR */				"ERROR:server error",
		/* TX_RESULT_PARAM_LEVEL_INVALID */			"INVALID:parameter level invalid",
		/* TX_RESULT_DUPLICATE_SERIALNUM */			"INVALID:duplicate serial number",
		/* TX_RESULT_BINARY_DATA_INVALID */			"INVALID:binary data invalid",
		/* TX_RESULT_OPTION_NOT_SUPPORTED */		"INVALID:option not yet supported",
		/* TX_RESULT_INSUFFICIENT_DONATION */		"INVALID:insufficient donation",
		/* TX_RESULT_PROOF_VERIFICATION_FAILED */	"INVALID:zero knowledge proof verification failed",
		/* TX_RESULT_FOREIGN_VERIFICATION_FAILED */	"INVALID:foreign payment verification failed",
		/* TX_RESULT_INVALID_VALUE */				"INVALID:invalid value"
	};

	if (result >= 0)
		result = 0;
	else
		result *= -1;

	if (result >= -PROCESS_RESULT_STOP_THRESHOLD)
	{
		result -= -PROCESS_RESULT_STOP_THRESHOLD;

		if (result < 0 || result >= (int)(sizeof(tx_result_stop_strings) / sizeof(char*)))
			return NULL;

		return tx_result_stop_strings[result];
	}
	else
	{
		if (result < 0 || result >= (int)(sizeof(tx_result_warn_strings) / sizeof(char*)))
			return NULL;

		return tx_result_warn_strings[result];
	}
}

void ProcessTx::InitBlockScan()
{
	if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::InitBlockScan block_txs_pending " << block_txs_pending;

	WaitForBlockTxValidation();
}

void ProcessTx::WaitForBlockTxValidation()
{
	if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::WaitForBlockTxValidation block_txs_pending " << block_txs_pending;

	unique_lock<mutex> lock(processtx_mutex);

	while (block_txs_pending > 0 && !g_shutdown)
	{
		if (RTEST_CUZZ_WAIT) usleep(rand() & RTEST_CUZZ_WAIT);

		processtx_condition_variable.wait(lock);
	}

	if (RTEST_CUZZ)
	{
		lock.unlock();
		usleep(rand() & (1024*1024-1));
	}

	if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::WaitForBlockTxValidation done block_txs_pending " << block_txs_pending;
}

int ProcessTx::TxEnqueueValidate(DbConn *dbconn, bool is_block_tx, bool add_to_relay_objs, Process_Q_Priority priority, SmartBuf smartobj, unsigned conn_index, uint32_t callback_id)
{
	// the following operations are atomic, to ensure ValidObj's and ProcessQ stay in sync

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

	unique_lock<mutex> lock(processtx_mutex);

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

	// if tx is in valid obj's, then return 1 without enqueuing

	auto obj = (CCObject*)smartobj.data();
	auto type = obj->ObjType();

	if (TRACE_XPAYS && Xtx::TypeIsXpay(type))  BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxEnqueueValidate priority " << priority << " type " << type << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
	else if (TRACE_PROCESS_TX)                BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxEnqueueValidate priority " << priority << " type " << type << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

	if (Xtx::TypeIsXreq(type))
	{
		Xreq xreq;

		auto rc = dbconn->XreqsSelectObjId(*obj->OidPtr(), false, xreq);
		if (!rc)
		{
			if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxEnqueueValidate already in xreq db oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

			return 1;
		}
	}

	SmartBuf checkobj;

	auto rc = dbconn->ValidObjsGetObj(*obj->OidPtr(), &checkobj);
	if (!rc)
	{
		if (TRACE_XPAYS && Xtx::TypeIsXpay(type))  BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxEnqueueValidate already in valid db oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
		else if (TRACE_PROCESS_TX)                BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxEnqueueValidate already in valid db oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

		return 1;
	}

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

	if (add_to_relay_objs)
	{
		// add tx to relay obj's so we don't download it after sending it to someone else

		relay_request_wire_params_t req_params;
		memset(&req_params, 0, sizeof(req_params));
		memcpy(&req_params.oid, obj->OidPtr(), sizeof(ccoid_t));

		dbconn->RelayObjsInsert(0, CC_TYPE_TXPAY, req_params, RELAY_STATUS_DOWNLOADED, 0);
	}

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

	rc = dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_TX, smartobj, NULL, 0, PROCESS_Q_STATUS_PENDING, priority, is_block_tx, conn_index, callback_id);
	if (TRACE_XPAYS && Xtx::TypeIsXpay(type)) BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxEnqueueValidate ProcessQEnqueueValidate returned " << rc << " for priority " << priority << " type " << type << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
	if (rc < 0)
		return -1;

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

	if (!rc && is_block_tx)
	{
		auto btp = block_txs_pending.fetch_add(1) + 1;

		if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxEnqueueValidate block_txs_pending " << btp;
	}

	if (RTEST_CUZZ)
	{
		lock.unlock();
		usleep(rand() & (1024*1024-1));
	}

	return 0;
}

static void SetMatchFieldsinXpay(const Xmatch& xmatch, Xpay &xpay)
{
	xpay.match_timestamp = xmatch.match_timestamp;
	xpay.foreign_blockchain = xmatch.xsell.quote_asset;
	xpay.foreign_address = xmatch.xsell.foreign_address;					// from Exchange_Matching_Reqs table
	xpay.foreign_confirmations_required = xmatch.xsell.confirmations;		// from Exchange_Match_Reqs table
	xpay.payment_time = xmatch.xsell.payment_time;							// from Exchange_Match_Reqs table
	xpay.match_left_to_pay = xmatch.AmountToPay();

	//cerr << "ProcessTx::SetMatchFieldsinXpay xpay " << hex << (uintptr_t)&xpay << dec << " to " << xpay.DebugString() << endl;
}

bool ProcessTx::ExtractXtxFailed(const TxPay& txbuf, bool for_pseudo_serialnum)
{
	if (!Xtx::TypeIsXtx(txbuf.tag_type))
		return false;

	if (for_pseudo_serialnum && !Xtx::TypeIsXpay(txbuf.tag_type)) // for_pseudo_serialnum, only need to extract an Xpay
		return false;

	return true;
}

// Extracts an Xtx from the append_data in the txbuf
//	if for_pseudo_serialnum is set, then only an Xpay is extracted, and other Xtx types are ignored
//	if dbconn is provided, then Xpay's match is retrieved and corresponding fields in Xpay are set by calling SetMatchFieldsinXpay
shared_ptr<Xtx> ProcessTx::ExtractXtx(DbConn *dbconn, const TxPay& txbuf, bool for_pseudo_serialnum)
{
	if (!ExtractXtxFailed(txbuf, for_pseudo_serialnum))
		return NULL;

	auto xtx = Xtx::New(txbuf.tag_type, IsTestnet(g_params.blockchain));

	xtx->amount_bits = TX_AMOUNT_BITS;
	xtx->exponent_bits = TX_AMOUNT_EXPONENT_BITS;

	try
	{
		auto rc = xtx->FromWire("ExtractXtx", 0, txbuf.append_data.data(), txbuf.append_data_length);
		if (rc)
			throw runtime_error("error parsing binary data");
	}
	catch (const exception& e)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ExtractXtx INVALID binary data for Xtx type " << txbuf.tag_type << ": " << e.what();

		return NULL;
	}

	if (dbconn && Xtx::TypeIsXpay(txbuf.tag_type))
	{
		auto xpay = Xpay::Cast(xtx);
		Xmatch match;

		auto rc = dbconn->XmatchSelect(xpay->xmatchnum, match);
		if (rc) return NULL;

		SetMatchFieldsinXpay(match, *xpay);

		if           (TRACE_XPAYS)  BOOST_LOG_TRIVIAL(info) << "ProcessTx::ExtractXtx set " << xpay->DebugString();
		else if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::ExtractXtx set " << xpay->DebugString();
		//cerr << "ProcessTx::ExtractXtx set xtx " << hex << (uintptr_t)xtx.get() << " xpay " << (uintptr_t)xpay << dec << " to " << xpay->DebugString() << endl;
	}

	return xtx;
}

static int ValidateXpay(DbConn *dbconn, uint64_t prior_blocktime, Xpay& xpay)
{
	if           (TRACE_XPAYS)  BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXpay prior_blocktime " << prior_blocktime << " " << xpay.DebugString();
	else if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::ValidateXpay prior_blocktime " << prior_blocktime << " " << xpay.DebugString();

	if (!xpay.xmatchnum)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXpay INVALID xmatchnum required; " << xpay.DebugString();

		return TX_RESULT_INVALID_VALUE;
	}

	if (xpay.foreign_amount <= 0)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXpay INVALID foreign_amount <= 0; " << xpay.DebugString();

		return TX_RESULT_INVALID_VALUE;
	}

	if (!xpay.foreign_txid.length())
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXpay INVALID foreign_txid required; " << xpay.DebugString();

		return TX_RESULT_INVALID_VALUE;
	}

	Xmatch match;

	auto rc = dbconn->XmatchSelect(xpay.xmatchnum, match);
	if (rc < 0)
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessTx::ValidateXpay ERROR retrieving matchnum " << xpay.xmatchnum;

		return TX_RESULT_INTERNAL_ERROR;
	}
	if (rc)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXpay INVALID matchnum " << xpay.xmatchnum << " not found";

		return TX_RESULT_INVALID_VALUE;
	}

	if (match.status != XMATCH_STATUS_ACCEPTED && match.status != XMATCH_STATUS_PART_PAID_OPEN && match.status != XMATCH_STATUS_PAID)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXpay INVALID matchnum " << match.xmatchnum << " status " << match.status;

		return TX_RESULT_INVALID_VALUE;
	}

	if (!match.have_xreqs)
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessTx::ValidateXpay ERROR xreqs purged from database for matchnum " << match.xmatchnum << " status " << match.status;

		return TX_RESULT_SERVER_ERROR;
	}

	SetMatchFieldsinXpay(match, xpay);

	if           (TRACE_XPAYS)  BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXpay set " << xpay.DebugString();
	else if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::ValidateXpay set " << xpay.DebugString();

	if (!xpay.match_timestamp)
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessTx::ValidateXpay ERROR match_timestamp missing from matchnum " << match.xmatchnum;

		return TX_RESULT_SERVER_ERROR;
	}

	if (!xpay.payment_time)
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessTx::ValidateXpay ERROR payment_time missing from matchnum " << match.xmatchnum;

		return TX_RESULT_SERVER_ERROR;
	}

	if (xpay.match_timestamp + xpay.payment_time < prior_blocktime)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXpay INVALID match payment time expired matchnum " << xpay.xmatchnum << " timestamp " << xpay.match_timestamp << " payment_time " << xpay.payment_time << " prior_blocktime " << prior_blocktime;

		return TX_RESULT_EXPIRED;	// expiration checked last
	}

	if (xpay.foreign_blockchain < 1 || xpay.foreign_blockchain > XREQ_BLOCKCHAIN_MAX)
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessTx::ValidateXpay ERROR invalid foreign_blockchain; " << xpay.DebugString();

		return TX_RESULT_SERVER_ERROR;
	}

	if (!xpay.foreign_address.length())
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessTx::ValidateXpay ERROR foreign_address missing from matchnum " << match.xmatchnum << " status " << match.status;

		return TX_RESULT_SERVER_ERROR;
	}

	if (xpay.foreign_confirmations_required <= 0)
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessTx::ValidateXpay ERROR invalid foreign_confirmations_required; " << xpay.DebugString();

		return TX_RESULT_SERVER_ERROR;
	}

	if (!xpay.foreign_block_id.length())
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXpay INVALID foreign_block_id required; " << xpay.DebugString();

		return TX_RESULT_INVALID_VALUE;
	}

	// check this last

	if (match.status == XMATCH_STATUS_PAID)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXpay matchnum " << match.xmatchnum << " already paid";

		return TX_RESULT_ALREADY_PAID;
	}

	return 0;
}

bool ProcessTx::CheckTransientDuplicateForeignAddresses(uint64_t foreign_blockchain)
{
	// The foreign addresseses in sell xreq's must be unique so that one buyer cannot claim another buyer's foreign payment that was made to the same address
	// Duplicate foreign addresses are checked:
	//		- via a PseudoSerialnum in non-persistent blocks
	//		- via the Exchange_Matching_Reqs table in persistent blocks
	//
	// This function only determines if dupliciate foreign addresses should be checked in transient Xreq's
	// so that the transact server can alert a wallet that the duplicate is invalid.

	if (TEST_IGNORE_TRANSIENT_DUPLICATE_FOREIGN_ADDRESSES)
		return false;

	if (!g_transact_service.enabled)
		return false;	// checking not needed by transact service

	return Xmatchreq::BlockchainRequiresUniqueForeignAddress(foreign_blockchain);
}

static int ValidateForeign(DbConn *dbconn, const uint64_t prior_blocktime, bool in_block, const Xpay& xpay)
{
	ForeignQueryResult result;

	if (xpay.foreign_blockchain < 1 || xpay.foreign_blockchain > XREQ_BLOCKCHAIN_MAX)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateForeign INVALID foreign_blockchain; " << xpay.DebugString();

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	// get bitcoin transaction amount, blocktime, and confirmations

	auto rc = ForeignQuery::QueryPayment(xpay.foreign_blockchain, xpay.foreign_block_id, xpay.foreign_address, xpay.foreign_txid, result);
	if (rc > 0)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateForeign INVALID payment not found on foreign blockchain; " << xpay.DebugString();

		return TX_RESULT_INVALID_VALUE;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(debug) << "ProcessTx::ValidateForeign QueryPayment ERROR; in_block " << in_block <<"; " << xpay.DebugString();

		if (g_foreignrpc_client.IgnoreError(xpay.foreign_blockchain, in_block))
			return 0;

		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateForeign QueryPayment ERROR; in_block " << in_block <<"; " << xpay.DebugString();

		return TX_RESULT_FOREIGN_ERROR;
	}

	// This node may have a lagging view of the foreign blockchain. Therefore, if an xpay is inside a block,
	// it is considered valid if it has half the required confirmations

	auto required = xpay.foreign_confirmations_required / (in_block ? 2 : 1);

	if (result.confirmations < required)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateForeign INVALID confirmed confirmations " << result.confirmations << " < required confirmations " << required;

		return TX_RESULT_FOREIGN_VERIFICATION_FAILED;
	}

	//BOOST_LOG_TRIVIAL(warning) << "ProcessTx::ValidateForeign " << xpay.foreign_amount_fp << "=validate_encoded " << xpay.foreign_amount << "=validate_claimed " << result.amount << "=validate_confirmed";

	if (result.amount < xpay.foreign_amount)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateForeign INVALID confirmed amount " << result.amount << " < claimed amount " << xpay.foreign_amount;

		return TX_RESULT_FOREIGN_VERIFICATION_FAILED;
	}

	if (result.amount > xpay.foreign_amount && xpay.foreign_amount < xpay.match_left_to_pay)
	{
		auto encoded = UniFloat::WireEncode(result.amount, -1);	// assume wallet rounded down
		auto decoded = UniFloat::WireDecode(encoded);

		//BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateForeign confirmed amount " << result.amount << " wire encoded " << decoded << " claimed amount " << xpay.foreign_amount << " match_left_to_pay " << xpay.match_left_to_pay;

		if (decoded > xpay.foreign_amount)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateForeign INVALID claimed amount " << xpay.foreign_amount << " < confirmed amount " << decoded << " with " << xpay.match_left_to_pay << " remaining to pay";

			return TX_RESULT_FOREIGN_VERIFICATION_FAILED;
		}
	}

	// desired order is xpay.match_timestamp < result.blocktime < prior_blocktime
	int64_t past = xpay.match_timestamp - result.blocktime;
	int64_t future = result.blocktime - prior_blocktime;

	if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::ValidateForeign payment age past " << past << " future " << future;

	if (past > FOREIGN_TX_PAST_ALLOWANCE)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateForeign INVALID foreign payment past age " << past << " exceeds limit " << FOREIGN_TX_PAST_ALLOWANCE;

		return TX_RESULT_FOREIGN_VERIFICATION_FAILED;
	}

	if (future > FOREIGN_TX_FUTURE_ALLOWANCE)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateForeign INVALID foreign payment future age " << future << " exceeds limit " << FOREIGN_TX_FUTURE_ALLOWANCE;

		return TX_RESULT_FOREIGN_VERIFICATION_FAILED;
	}

	// TODO: check result.memo

	CCASSERTZ(result.memo.length());

	return 0;
}

static bool CheckXreqAmount(const bigint_t& amount, const char* label)
{
	if (!amount)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq " << label << amount << " = 0";

		return true;
	}

	if (BIGWORD(amount, 2) || BIGWORD(amount, 3))
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq " << label << amount << " > 128 bits";

		return true;
	}

	return false;
}

static int ValidateXreq(DbConn *dbconn, const uint64_t prior_blocktime, bool in_block, TxPay& tx, Xreq& xreq, bigint_t& donation, unsigned tx_size, unsigned total_nout)
{
	if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::ValidateXreq " << xreq.DebugString();

	auto foreign_blockchain = xreq.quote_asset;

	if (xreq.foreign_address.length() && tx.nin >= TX_MAXIN)	// need a free input for a pseudo serialnum
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq nin " << tx.nin << " >= " << TX_MAXIN;

		return TX_RESULT_INVALID_VALUE;
	}

	if (!xreq.destination)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq destination = 0";

		return TX_RESULT_INVALID_VALUE;
	}

	if (CheckXreqAmount(xreq.min_amount, "min_amount "))
		return TX_RESULT_INVALID_VALUE;

	if (CheckXreqAmount(xreq.max_amount, "max_amount "))
		return TX_RESULT_INVALID_VALUE;

	if (xreq.min_amount > xreq.max_amount)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq min_amount " << xreq.min_amount << " > max_amount " << xreq.max_amount;

		return TX_RESULT_INVALID_VALUE;
	}

	if (xreq.net_rate_required <= 0)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq net_rate_required " << xreq.net_rate_required << " <= 0";

		return TX_RESULT_INVALID_VALUE;
	}

	if (xreq.wait_discount < 0 || xreq.wait_discount > 1)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq wait_discount " << xreq.wait_discount << " not in range 0 to 1";

		return TX_RESULT_INVALID_VALUE;
	}

	if (xreq.quote_costs < 0)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq quote_costs " << xreq.quote_costs << " < 0";

		return TX_RESULT_INVALID_VALUE;
	}

	if (xreq.base_costs < 0)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq base_costs " << xreq.base_costs << " < 0";

		return TX_RESULT_INVALID_VALUE;
	}

	if (xreq.base_asset)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq base_asset " << xreq.base_asset << " != 0";

		return TX_RESULT_INVALID_VALUE;
	}

	if (foreign_blockchain < XREQ_BLOCKCHAIN_BTC || foreign_blockchain > XREQ_BLOCKCHAIN_BCH)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq quote_asset " << foreign_blockchain << " < " << XREQ_BLOCKCHAIN_BTC << " or > " << XREQ_BLOCKCHAIN_BCH;

		return TX_RESULT_INVALID_VALUE;
	}

	if (xreq.type < CC_TYPE_XCX_NAKED_BUY && xreq.type > CC_TYPE_XCX_SIMPLE_SELL && xreq.type != CC_TYPE_XCX_MINING_TRADE)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq type " << xreq.type << " < " << CC_TYPE_XCX_NAKED_BUY << " or > " << CC_TYPE_XCX_SIMPLE_SELL << " and != " << CC_TYPE_XCX_MINING_TRADE;

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}
	else
	{
		if (xreq.consideration_required)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq consideration_required " << xreq.consideration_required << " != 0";

			return TX_RESULT_INVALID_VALUE;
		}

		if (xreq.consideration_offered)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq consideration_offered " << xreq.consideration_offered << " != 0";

			return TX_RESULT_INVALID_VALUE;
		}

		if (xreq.type == CC_TYPE_XCX_SIMPLE_BUY || xreq.type == CC_TYPE_XCX_SIMPLE_SELL || xreq.type == CC_TYPE_XCX_MINING_TRADE)
		{
			if (xreq.pledge != XREQ_SIMPLE_PLEDGE)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq pledge " << xreq.pledge << " != " << XREQ_SIMPLE_PLEDGE;

				return TX_RESULT_INVALID_VALUE;
			}
		}
		else if (xreq.pledge)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID naked Xreq pledge " << xreq.pledge << " != 0";

			return TX_RESULT_INVALID_VALUE;
		}

		// !!!!! AUDIT THESE

		if (xreq.hold_time != XREQ_SIMPLE_HOLD_TIME)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq hold_time " << xreq.hold_time << " != " << XREQ_SIMPLE_HOLD_TIME;

			return TX_RESULT_INVALID_VALUE;
		}

		if (xreq.hold_time_required != XREQ_SIMPLE_HOLD_TIME)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq hold_time_required " << xreq.hold_time_required << " != " << XREQ_SIMPLE_HOLD_TIME;

			return TX_RESULT_INVALID_VALUE;
		}

		if (xreq.min_wait_time != XREQ_SIMPLE_WAIT_TIME)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq min_wait_time " << xreq.min_wait_time << " != " << XREQ_SIMPLE_WAIT_TIME;

			return TX_RESULT_INVALID_VALUE;
		}

		if (xreq.accept_time_required)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq accept_time_required " << xreq.accept_time_required << " != 0";

			return TX_RESULT_INVALID_VALUE;
		}

		if (xreq.accept_time_offered)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq accept_time_offered " << xreq.accept_time_offered << " != 0";

			return TX_RESULT_INVALID_VALUE;
		}

		if (xreq.payment_time != Xreq::DefaultPaymentTime(IsTestnet(g_params.blockchain)))
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq payment_time " << xreq.payment_time << " != " << Xreq::DefaultPaymentTime(IsTestnet(g_params.blockchain));

			return TX_RESULT_INVALID_VALUE;
		}

		if (xreq.confirmations != Xreq::DefaultConfirmations(IsTestnet(g_params.blockchain)))
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq confirmations " << xreq.confirmations << " != " << Xreq::DefaultConfirmations(IsTestnet(g_params.blockchain));

			return TX_RESULT_INVALID_VALUE;
		}

		if (xreq.flags.add_immediately_to_blockchain)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq add_immediately_to_blockchain " << xreq.flags.add_immediately_to_blockchain << " != 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (!xreq.flags.auto_accept_matches)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq auto_accept_matches " << xreq.flags.auto_accept_matches << " != 1";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (xreq.flags.no_minimum_after_first_match)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq no_minimum_after_first_match " << xreq.flags.no_minimum_after_first_match << " != 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (xreq.flags.must_liquidate_crossing_minimum)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq must_liquidate_crossing_minimum " << xreq.flags.must_liquidate_crossing_minimum << " != 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (xreq.flags.must_liquidate_below_minimum)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq must_liquidate_below_minimum " << xreq.flags.must_liquidate_below_minimum << " != 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}
	}

	if (xreq.foreign_asset.length())
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq foreign_asset " << xreq.foreign_asset << " != \"\"";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	while (xreq.foreign_address.length())	// break to exit
	{
		int rc = 1;
		if (!in_block && ProcessTx::CheckTransientDuplicateForeignAddresses(foreign_blockchain))
			rc = dbconn->XreqsSelectUniqueForeignAddress(foreign_blockchain, xreq.foreign_address);
		if (rc > 0)
			rc = dbconn->XmatchingreqUniqueForeignAddressSelect(prior_blocktime, foreign_blockchain, xreq.foreign_address);
		if (rc < 0)
			return TX_RESULT_INTERNAL_ERROR;
		if (!rc)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID non-unique foreign_address " << xreq.foreign_address << " blockchain " << foreign_blockchain;

			return TX_RESULT_ADDRESS_IN_USE;
		}

		rc = dbconn->XcxBlockedForeignAddressSelect(foreign_blockchain, xreq.foreign_address);
		if (rc < 0)
			return TX_RESULT_INTERNAL_ERROR;
		if (!rc)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID blocked foreign_address " << xreq.foreign_address << " blockchain " << foreign_blockchain;

			return TX_RESULT_INVALID_VALUE;
		}

		rc = ForeignQuery::QueryAddress(foreign_blockchain, xreq.foreign_address);
		if (rc > 0)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID foreign_address " << xreq.foreign_address << " blockchain " << foreign_blockchain;

			return TX_RESULT_INVALID_VALUE;
		}
		else if (rc)
		{
			BOOST_LOG_TRIVIAL(debug) << "ProcessTx::ValidateForeign QueryAddress ERROR; in_block " << in_block <<" foreign_blockchain " << foreign_blockchain << " foreign_address " << xreq.foreign_address;

			if (g_foreignrpc_client.IgnoreError(foreign_blockchain, in_block))
				break;

			BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq QueryAddress ERROR; in_block " << in_block <<" foreign_blockchain " << foreign_blockchain << " foreign_address " << xreq.foreign_address;

			return TX_RESULT_FOREIGN_ERROR;
		}

		break;
	}

	bigint_t min_donation =
			  (bigint_t)(tx_size) *			g_blockchain.proof_params.donation_per_byte
			+ (bigint_t)(total_nout) *		g_blockchain.proof_params.donation_per_output
			+ (bigint_t)(tx.nin) *			g_blockchain.proof_params.donation_per_input;

	if (Xtx::TypeHasBareMsg(xreq.type))
		min_donation = 0UL;
	else
		min_donation = min_donation + g_blockchain.proof_params.donation_per_xcx_req;

	BOOST_LOG_TRIVIAL(trace) << "ProcessTx::ValidateXreq Xreq donation " << tx.donation_fp << " decoded " << donation << " min_donation " << min_donation << " tx bytes " << tx_size << " nout " << total_nout << " nin " << tx.nin;
	//cerr << "ProcessTx::ValidateXreq donation " << tx.donation_fp << " decoded " << donation << " min_donation " << min_donation << " tx bytes " << tx_size << " nout " << total_nout << " nin " << tx.nin << endl;

	if (donation < min_donation && tx.donation_fp < TX_DONATION_MASK)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq donation " << donation << " < minimum donation " << min_donation;

		return TX_RESULT_INSUFFICIENT_DONATION;
	}

	//cerr << "ProcessTx::ValidateXreq Xreq expire_time " << xreq.expire_time << " prior_blocktime " << prior_blocktime << " hold_time " << xreq.hold_time << " XREQ_MIN_POSTHOLD_TIME " << XREQ_MIN_POSTHOLD_TIME << " XREQ_MAX_EXPIRE_TIME " << XREQ_MAX_EXPIRE_TIME << endl;

	if (xreq.expire_time > prior_blocktime + XREQ_MAX_EXPIRE_TIME)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq expire_time " << xreq.expire_time << " prior_blocktime " << prior_blocktime << " XREQ_MAX_EXPIRE_TIME " << XREQ_MAX_EXPIRE_TIME;

		return TX_RESULT_INVALID_VALUE;
	}

	if (xreq.expire_time <= prior_blocktime + xreq.hold_time + XREQ_MIN_POSTHOLD_TIME)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID Xreq expire_time " << xreq.expire_time << " prior_blocktime " << prior_blocktime << " hold_time " << xreq.hold_time << " XREQ_MIN_POSTHOLD_TIME " << XREQ_MIN_POSTHOLD_TIME;

		return TX_RESULT_EXPIRED;	// expiration checked last
	}

	if (xreq.IsSeller())
		tx.amount_carry_out = xreq.max_amount;
	else
		tx.amount_carry_out = 0UL;

	if (xreq.IsBuyer() && xreq.pledge == 100)
		tx.amount_carry_out = tx.amount_carry_out + xreq.max_amount;
	else if (xreq.IsBuyer() && xreq.pledge)
		tx.amount_carry_out = tx.amount_carry_out + xreq.max_amount * bigint_t(xreq.pledge) / bigint_t(100UL);	// pledge amounts always rounded down

	if (xreq.type == CC_TYPE_XCX_NAKED_BUY && Xtx::CheckPow(tx.append_data.data(), tx.append_data_length, g_params.xcx_naked_buy_work_difficulty))
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::ValidateXreq INVALID proof of work";

		return TX_RESULT_INVALID_VALUE;
	}

	return 0;
}

int ProcessTx::TxValidate(DbConn *dbconn, TxPay& tx, SmartBuf smartobj, uint64_t prior_blocktime, bool in_block)
{
	if (TEST_DELAY_SOME_TXS && !IsWitness() && RandTest(16)) usleep(500*1000);

	if (g_witness.test_mal)
		return 0;			// for testing, allow bad transactions

	if (!prior_blocktime)
		prior_blocktime = g_blockchain.GetLastIndelibleTimestamp();	// use blockchain time in case local clock is off

	auto obj = (CCObject*)smartobj.data();

	if (tx_from_wire(tx, (char*)obj->ObjPtr(), obj->ObjSize()))
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID tx_from_wire failed";

		return TX_RESULT_BINARY_DATA_INVALID;
	}

	unsigned tx_size = sizeof(CCObject::Header) + obj->BodySize();

#if !TEST_EXTRA_ON_WIRE

	//tx_dump_stream(cerr, tx);

	tx.source_chain = g_params.blockchain;

	if (tx.tag_type == CC_TYPE_MINT && Implement_CCMint(tx.source_chain))
		tx.zkkeyid = CC_MINT_ZKKEY_ID;

	switch (tx.tag_type)
	{
		case CC_TYPE_MINT:
		case CC_TYPE_TXPAY:
		case CC_TYPE_XCX_NAKED_BUY:
		case CC_TYPE_XCX_NAKED_SELL:
		case CC_TYPE_XCX_SIMPLE_BUY:
		case CC_TYPE_XCX_SIMPLE_SELL:
		case CC_TYPE_XCX_PAYMENT:
		case CC_TYPE_XCX_MINING_TRADE:
			break;

		default:
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID tag_type " << tx.tag_type;

			return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.tx_type != tx.tag_type)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID tx type " << tx.tx_type;

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.source_chain != g_params.blockchain)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID source-chain != " << g_params.blockchain;

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.revision)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID revision != 0";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.expiration)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID expiration != 0";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.reserved)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID reserved != 0";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.tag_type == CC_TYPE_MINT && tx.nin != 1)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID tx.nin " << tx.nin << " != 1 in mint tx";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.tag_type != CC_TYPE_MINT && tx.nin != tx.nin_with_path)		// inputs with published commitments are not currently allowed because code to check them isn't implemented
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID tx.nin " << tx.nin << " != tx.nin_with_path " << tx.nin_with_path;

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	unsigned total_nout = 0;

	if (!tx.nout)
		tx.allow_restricted_addresses = false;

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		TxOut& txout = tx.outputs[i];

		txout.addrparams.dest_chain = g_params.blockchain;
		if (!txout.M_domain)
			txout.M_domain = g_params.default_domain;

		total_nout += (txout.repeat_count + 1);

		if (txout.no_address)
			tx.allow_restricted_addresses = false;

		if (txout.addrparams.dest_chain != g_params.blockchain)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID destination-chain != " << g_params.blockchain;

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.M_domain != g_params.default_domain)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID domain != " << g_params.default_domain;

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.no_address)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID no-address true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.acceptance_required)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID acceptance-required true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.repeat_count)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID repeat-count > 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.no_asset)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID no-asset true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.no_amount)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID no-amount true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (tx.tag_type == CC_TYPE_MINT)
		{
			if (txout.asset_mask)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID mint tx asset-mask != 0";

				return TX_RESULT_OPTION_NOT_SUPPORTED;
			}

			if (txout.amount_mask)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID mint tx amount-mask != 0";

				return TX_RESULT_OPTION_NOT_SUPPORTED;
			}

			if (txout.M_asset_enc)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID mint tx encrypted-asset != 0";

				return TX_RESULT_OPTION_NOT_SUPPORTED;
			}
		}
		else
		{
			if (txout.asset_mask != TX_ASSET_WIRE_MASK)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID asset-mask != all one's";

				return TX_RESULT_OPTION_NOT_SUPPORTED;
			}

			if (txout.amount_mask != TX_AMOUNT_MASK)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID amount-mask != all one's";

				return TX_RESULT_OPTION_NOT_SUPPORTED;
			}
		}
	}

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		TxIn& txin = tx.inputs[i];

		if (!txin.M_domain && tx.tag_type != CC_TYPE_MINT)
			txin.M_domain = g_params.default_domain;

		if (txin.enforce_master_secret)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID enforce-master-secret true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txin.enforce_spend_secrets)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID enforce-spend-secrets true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (!txin.enforce_trust_secrets)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID enforce-trust-secrets false";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txin.enforce_freeze)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID enforce-freeze true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txin.enforce_unfreeze)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID enforce-unfreeze true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txin.delaytime)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID delaytime > 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (tx.tag_type != CC_TYPE_MINT && txin.no_serialnum)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID no-serial-number true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (tx.tag_type == CC_TYPE_MINT && !txin.no_serialnum)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID mint tx no-serial-number false";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txin.S_spendspec_hashed)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID hashed-spendspec != 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (tx.tag_type == CC_TYPE_MINT && txin.pathnum)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID pathnum != 0 in mint tx";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (tx.tag_type != CC_TYPE_MINT && !txin.pathnum)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID pathnum = 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		/*if (txin.nsigkeys)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID nsigkeys > 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}*/
	}

	bigint_t donation;
	tx_amount_decode(tx.donation_fp, donation, true, TX_DONATION_BITS, TX_AMOUNT_EXPONENT_BITS);

	auto xtx = ProcessTx::ExtractXtx(NULL, tx);
	if (!xtx && ProcessTx::ExtractXtxFailed(tx))
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID ExtractXtx failed type " << tx.tag_type;

		return TX_RESULT_BINARY_DATA_INVALID;
	}

	bool already_paid = false;

	if (Xtx::TypeIsXpay(tx.tag_type))
	{
		auto rc = ValidateXpay(dbconn, prior_blocktime, *Xpay::Cast(xtx));

		if (rc == TX_RESULT_ALREADY_PAID)
			already_paid = true;	// defer this return code until after pseudo serialnum check
		else if (rc)
			return rc;
	}
	else if (Xtx::TypeIsXreq(tx.tag_type))
	{
		auto rc = ValidateXreq(dbconn, prior_blocktime, in_block, tx, *Xreq::Cast(xtx), donation, tx_size, total_nout);
		if (rc)	return rc;
	}
	else if (tx.tag_type == CC_TYPE_MINT)
	{
		bigint_t mint_donation;
		mint_donation = TX_CC_MINT_DONATION;

		if (Implement_CCMint(g_params.blockchain) && !tx.param_level)
			mint_donation = TX_CC_MINT_AMOUNT;

		if (donation != mint_donation)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID donation " << donation << " != mint donation " << mint_donation;

			return TX_RESULT_INSUFFICIENT_DONATION;
		}
	}
	else if (tx.tag_type == CC_TYPE_TXPAY)
	{
		bigint_t min_donation =
												g_blockchain.proof_params.donation_per_tx
				+ (bigint_t)(tx_size) *			g_blockchain.proof_params.donation_per_byte
				+ (bigint_t)(total_nout) *		g_blockchain.proof_params.donation_per_output
				+ (bigint_t)(tx.nin) *			g_blockchain.proof_params.donation_per_input;

		if (min_donation < g_blockchain.proof_params.minimum_donation)
			min_donation = g_blockchain.proof_params.minimum_donation;

		BOOST_LOG_TRIVIAL(trace) << "ProcessTx::TxValidate donation " << tx.donation_fp << " decoded " << donation << " min_donation " << min_donation << " tx bytes " << tx_size << " nout " << total_nout << " nin " << tx.nin;
		//cerr << "ProcessTx::TxValidate donation " << tx.donation_fp << " decoded " << donation << " min_donation " << min_donation << " tx bytes " << tx_size << " nout " << total_nout << " nin " << tx.nin << endl;

		if (donation < min_donation && tx.donation_fp < TX_DONATION_MASK)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID donation " << donation << " < minimum donation " << min_donation;

			return TX_RESULT_INSUFFICIENT_DONATION;
		}

		if (donation > min_donation * bigint_t(4UL))
			tx.allow_restricted_addresses = false;
	}
	else
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID unexpected type " << tx.tag_type;

		return TX_RESULT_SERVER_ERROR;
	}

	uint64_t merkle_time, next_commitnum;

	// Merkle root can be used until "max_param_age" seconds after it is replaced with a new Merkle root
	// so we need to check the timestamp of the next param_level, which is when the Merkle root was replaced
	auto next_level = tx.param_level + 1;
	auto rc = dbconn->CommitRootsSelectLevel(next_level, true, merkle_time, next_commitnum, &tx.tx_merkle_root, TX_MERKLE_BYTES);
	if (rc < 0)
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessTx::TxValidate ERROR retrieving next Merkle root level " << next_level;

		return TX_RESULT_INTERNAL_ERROR;
	}
	if (rc)
	{
		merkle_time = prior_blocktime;
	}

	int64_t dt = prior_blocktime - merkle_time;

	BOOST_LOG_TRIVIAL(trace) << "ProcessTx::TxValidate prior_blocktime " << prior_blocktime << " tx param_level " << tx.param_level << " timestamp " << merkle_time << " age " << dt;

	if (dt > g_params.max_param_age && !Xtx::TypeHasBareMsg(tx.tag_type))
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID tx.param_level too old age " << dt << " tx type " << tx.tag_type << " param_level " << tx.param_level;

		return TX_RESULT_PARAM_LEVEL_TOO_OLD;
	}

	rc = dbconn->CommitRootsSelectLevel(tx.param_level, false, merkle_time, next_commitnum, &tx.tx_merkle_root, TX_MERKLE_BYTES);
	if (rc < 0)
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessTx::TxValidate ERROR retrieving Merkle root level " << tx.param_level;

		return TX_RESULT_INTERNAL_ERROR;
	}
	if (rc)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID Merkle root level " << tx.param_level << " not found";

		return TX_RESULT_PARAM_LEVEL_INVALID;
	}

	if (merkle_time > TX_TIME_OFFSET)
		tx.param_time = (merkle_time - TX_TIME_OFFSET) / TX_TIME_DIVISOR;
	else
		tx.param_time = 0;

	tx.outvalmin = g_blockchain.proof_params.outvalmin;		// !!! should come from param_level
	tx.outvalmax = g_blockchain.proof_params.outvalmax;

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		TxIn& txin = tx.inputs[i];

		if (tx.tag_type == CC_TYPE_MINT)
		{
			txin.invalmax = TX_CC_MINT_EXPONENT;
		}
		else
		{
			txin.merkle_root = tx.tx_merkle_root;
			txin.invalmax = g_blockchain.proof_params.invalmax;
		}
	}

#endif // !TEST_EXTRA_ON_WIRE

	if (!Xtx::TypeHasBareMsg(tx.tag_type))
	{
		tx_set_commit_iv(tx);

		//tx_dump_stream(cerr, tx);

		if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::TxValidate param_level " << tx.param_level << " setting M_commitment_iv " << tx.M_commitment_iv;

		if (CCProof_VerifyProof(tx))
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID CCProof_VerifyProof failed";

			return TX_RESULT_PROOF_VERIFICATION_FAILED;
		}
	}

	if (tx.tag_type == CC_TYPE_MINT)
		return 0;

	BlockChain::CheckCreatePseudoSerialnum(tx, xtx, obj->ObjPtr());

	bool found_one_spent = false;
	bool found_one_not_spent = false;
	uint64_t tx_commitnum = 0;

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		for (unsigned j = 0; j < i; ++j)
		{
			if (!memcmp(&tx.inputs[i].S_serialnum, &tx.inputs[j].S_serialnum, TX_SERIALNUM_BYTES))
			{
				if (g_witness.test_mal && RandTest(2))
					BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate allowing transaction with duplicate serialnums for double-spend testing";
				else
				{
					BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate INVALID duplicate serialnums " << i << " and " << j;

					return TX_RESULT_DUPLICATE_SERIALNUM;
				}
			}
		}

		bigint_t hashkey;
		unsigned hashkey_size = sizeof(hashkey);
		uint64_t commitnum;

		auto rc = dbconn->SerialnumSelect(&tx.inputs[i].S_serialnum, TX_SERIALNUM_BYTES, &hashkey, &hashkey_size, &commitnum);
		if (rc < 0)
		{
			BOOST_LOG_TRIVIAL(error) << "ProcessTx::TxValidate ERROR checking persistent serialnums";

			return TX_RESULT_INTERNAL_ERROR;
		}
		if (rc)
		{
			found_one_not_spent = true;
		}
		else
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate serialnum " << i << " of " << tx.nin << " already in persistent db";

			if (hashkey_size && hashkey != tx.inputs[i].S_hashkey)
				return TX_RESULT_ALREADY_SPENT;		// found a different tx with same serialnum

			if (!found_one_spent)
				tx_commitnum = commitnum;
			else if (commitnum && commitnum != tx_commitnum)
				return TX_RESULT_ALREADY_SPENT;		// found a different tx with same serialnum

			found_one_spent = true;
		}

		if (found_one_not_spent && found_one_spent)
			return TX_RESULT_ALREADY_SPENT;				// found one serialnum in tx not spent and one spent, so the latter was spent in a different conflicting tx
	}

	if (found_one_spent)
		return 1;			// don't flag this tx as invalid since all of the serialnums in tx have been spent using the same hashkeys and tx_commitnum's, which likely means this tx has been re-submitted due to a dropped network connection

	if (already_paid)
		return TX_RESULT_ALREADY_PAID;	// a new xpay was submitted for an already paid match, so warn the wallet

	if (Xtx::TypeIsXpay(tx.tag_type))
	{
		auto xpay = Xpay::Cast(xtx);

		auto rc = ValidateForeign(dbconn, prior_blocktime, in_block, *xpay);
		if (rc)
			return rc;
	}

	return 0;
}

void ProcessTx::ThreadProc()
{
	auto dbconn = new DbConn;
	auto ptx = new TxPay;

	CCASSERT(dbconn);
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	BOOST_LOG_TRIVIAL(info) << "ProcessTx::ThreadProc start dbconn " << (uintptr_t)dbconn;

	while (true)
	{
		if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

		dbconn->WaitForQueuedWork(PROCESS_Q_TYPE_TX);

		if (g_shutdown)
			break;

		SmartBuf smartobj;
		CCObject* obj = NULL;
		int64_t seqnum = 0;
		unsigned block_tx_count = 0;
		unsigned conn_index = 0;
		uint32_t callback_id = 0;
		bool validated = false;
		bool inserted = false;
		bool block_done = false;
		uint64_t next_commitnum = 0;
		int64_t result = TX_RESULT_INTERNAL_ERROR;

		while (true)	// so we can use break on error
		{
			if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

			if (dbconn->ProcessQGetNextValidateObj(PROCESS_Q_TYPE_TX, &smartobj, conn_index, callback_id))
			{
				//BOOST_LOG_TRIVIAL(debug) << "ProcessTx ProcessQGetNextValidateObj failed";

				break;
			}

			obj = (CCObject*)smartobj.data();
			BOOST_LOG_TRIVIAL(debug) << "ProcessTx Validating oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE) << " conn_index Conn " << conn_index << " callback_id " << callback_id;

			// set starting point to search for transaction to clear
			// we have to set this now to avoid a race with the transaction being placed into a persistent block
			next_commitnum = g_commitments.GetNextCommitnum();

			if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

			auto rc = TxValidate(dbconn, tx, smartobj);
			if (rc < 0)
			{
				auto result_string = ResultString(rc);

				if (result_string)
					BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate obj tag " << hex << obj->ObjTag() << dec << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE) << " failed with result " << rc << " = " << result_string;
				else
					BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate obj tag " << hex << obj->ObjTag() << dec << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE) << " failed with result " << rc;

				result = rc;
				break;
			}
			else if (rc == 1)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessTx::TxValidate all serialnums found in PersistentDB";

				// the wallet might resubmit tx if it didn't receive the acknowledgement the first time
				// so this is allowed without error
				result = 0;
				break;
			}
			else if (rc)
			{
				BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxValidate tx has no serialnums to check";

				next_commitnum = 0;	// force search to start at zero cause tx might already be in an indelible block
			}

			validated = true;

			break;
		}

		if (smartobj)
		{
			//BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxValidate validated " << validated << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

			if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

			// the following operations are atomic, to ensure ValidObj's and ProcessQ stay in sync

			lock_guard<mutex> lock(processtx_mutex);

			if (validated)
			{
				auto rc = dbconn->ValidObjsInsert(smartobj, &seqnum);
				if (rc < 0)
					BOOST_LOG_TRIVIAL(error) << "ProcessTx ValidObjsInsert failed obj tag " << hex << obj->ObjTag() << dec << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
				else if (rc)
				{
					BOOST_LOG_TRIVIAL(debug) << "ProcessTx ValidObjsInsert constraint violation obj tag " << hex << obj->ObjTag() << dec << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

					// the wallet might resubmit tx if it didn't receive the acknowledgement the first time
					// so this is allowed without error, but since the tx might already be in a block at this point,
					// we have to return zero for next_commitnum

					result = 0;
				}
				else
				{
					inserted = true;

					result = next_commitnum;
				}
			}

			if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

			// refetch block_tx_count, conn_index and callback_id, in case they have changed
			auto rc = dbconn->ProcessQSelectAndDelete(PROCESS_Q_TYPE_TX, *obj->OidPtr(), block_tx_count, conn_index, callback_id);
			CCASSERTZ(rc);	// not allowed to fail

			// decrement block_txs_pending
			if (block_tx_count)
			{
				auto btp = block_txs_pending.fetch_sub(block_tx_count) - (int)block_tx_count;

				if (btp < 0)
				{
					// !!!!! why does block_txs_pending sometimes go negative?

					BOOST_LOG_TRIVIAL(warning) << "ProcessTx block_tx_count " << block_tx_count << " block_txs_pending " << btp;

					block_txs_pending.fetch_sub(btp);
				}
				else if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(trace) << "ProcessTx block_tx_count " << block_tx_count << " block_txs_pending " << btp;

				if (btp <= 0)
					block_done = true;
			}

			BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxValidate result " << result << " obj tag " << hex << obj->ObjTag() << dec << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE) << " block_txs_pending " << block_txs_pending;
		}

		if (inserted)
		{
			if (Xtx::TypeIsXreq(obj->ObjType()))
			{
				g_process_xreqs.AddPendingRequest(dbconn, tx, seqnum, obj->OidPtr());
				g_witness.NotifyNewWork(XREQSEQ);
			}
			else
				g_witness.NotifyNewWork(TXSEQ);
		}

		if (conn_index && !g_shutdown)
		{
			if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(debug) << "ProcessTx calling HandleValidateDone Conn " << conn_index << " callback_id " << callback_id << " result " << result;

			auto conn = g_connregistry.GetConn(conn_index);

			conn->HandleValidateDone(0, callback_id, result);
		}

		if (block_done)
		{
			if (TRACE_PROCESS_TX) BOOST_LOG_TRIVIAL(debug) << "ProcessTx block_txs_pending done";

			processtx_condition_variable.notify_all();
		}
	}

	BOOST_LOG_TRIVIAL(info) << "ProcessTx::ThreadProc end dbconn " << (uintptr_t)dbconn;

	delete ptx;
	delete dbconn;
}
