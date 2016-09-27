/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * processtx.cpp
*/

#include "CCdef.h"
#include "processtx.hpp"
#include "transact.hpp"
#include "blockchain.hpp"
#include "block.hpp"
#include "commitments.hpp"
#include "dbparamkeys.h"
#include "witness.hpp"

#include <transaction.hpp>
#include <transaction.h>

#include <CCobjects.hpp>
#include <ccserver/connection_registry.hpp>

#define TRACE_PROCESS	(g_params.trace_tx_validation)

ProcessTx g_processtx;

void ProcessTx::Init()
{
	if (g_params.tx_validation_threads < 0)
		return;

	m_threads.reserve(g_params.tx_validation_threads);

	for (int i = 0; i < g_params.tx_validation_threads && !g_shutdown; ++i)
	{
		auto t = new thread(ThreadProc);
		m_threads.push_back(t);
	}
}

void ProcessTx::DeInit()
{
	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::DeInit";

	DbConnProcessQ::StopQueuedWork(PROCESS_Q_TYPE_TX);

	for (auto t : m_threads)
	{
		t->join();
		delete t;
	}

	m_threads.clear();

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::DeInit done";
}

const char* ProcessTx::ResultString(int result)
{
	//if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::ResultString result " << result;

	static const char *tx_result_warn_strings[] =
	{
		"INVALID:parameter level too old",
	};

	static const char *tx_result_stop_strings[] =
	{
		"INVALID:parameter level invalid",
		"INVALID:duplicate serial number",
		"INVALID:already spent",
		"INVALID:binary data invalid",
		"INVALID:published commitments not yet supported",
		"INVALID:zero knowledge proof verification failed"
	};

	result = -result;

	if (result < -PROCESS_RESULT_STOP_THRESHOLD)
	{
		result -= -TX_RESULT_STRING_CODE_START;

		if (result < 0 || result >= (int)(sizeof(tx_result_warn_strings) / sizeof(char*)))
			return NULL;

		return tx_result_warn_strings[result];
	}
	else
	{
		result -= -PROCESS_RESULT_STOP_THRESHOLD;

		if (result < 0 || result >= (int)(sizeof(tx_result_stop_strings) / sizeof(char*)))
			return NULL;

		return tx_result_stop_strings[result];
	}
}

int ProcessTx::TxEnqueueValidate(DbConn *dbconn, int64_t priority, SmartBuf smartobj, unsigned conn_index, unsigned callback_id)
{
	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxEnqueueValidate priority " << priority << " smartobj " << (uintptr_t)&smartobj;

	auto obj = (CCObject*)smartobj.data();

	relay_request_wire_params_t req_params;
	memset(&req_params, 0, sizeof(req_params));
	memcpy(&req_params.oid, obj->OidPtr(), sizeof(ccoid_t));

	dbconn->RelayObjsInsert(0, CC_TAG_TX_WIRE, req_params, RELAY_STATUS_DOWNLOADED, 0);	// so we don't download it after sending it to someone else

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::TxEnqueueValidate priority " << priority << " bufp " << (uintptr_t)(smartobj.BasePtr()) << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t)) << " conn_index Conn-" << conn_index << " callback_id " << callback_id;

	auto rc = dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_TX, smartobj, NULL, 0, PROCESS_Q_STATUS_PENDING, priority, conn_index, callback_id);

	return rc;
}

int ProcessTx::TxValidate(DbConn *dbconn, struct TxPay& tx, SmartBuf smartobj)
{
	auto obj = (CCObject*)smartobj.data();

	if (tx_from_wire(tx, (char*)obj->ObjPtr(), obj->ObjSize()))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate error tx_from_wire failed";

		return TX_RESULT_BINARY_DATA_INVALID;
	}

#if !TEST_EXTRA_ON_WIRE

	if (tx.nin != tx.nin_with_path)		// inputs with published commitments are not currently allowed because code to check them isn't implemented
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate tx.nin " << tx.nin << " != tx.nin_with_path" << tx.nin_with_path;

		return TX_RESULT_PUBLISHED_COMMITMENT_NOT_SUPPORTED;
	}

	auto last_indelible_block = g_blockchain.GetLastIndelibleBlock();
	auto block = (Block*)last_indelible_block.data();
	auto wire = block->WireData();

	uint64_t merkle_time;

	// merkle root can be used until 48 hours after it is replaced with a new merkle root
	// so we need to check the timestamp of the next param_level, which is when the merkle root was replaced
	auto rc = dbconn->CommitRootsSelect(tx.param_level + 1, true, merkle_time, &tx.merkle_root, sizeof(tx.merkle_root));
	if (rc < 0)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::TxValidate error retrieving next Merkle root level " << tx.param_level + 1;

		return TX_RESULT_SERVER_ERROR;
	}
	else if (rc)
	{
		merkle_time = wire->timestamp;
	}

	int64_t dt = wire->timestamp - merkle_time;

	BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::TxValidate last indelible level " << wire->level << " timestamp " << wire->timestamp << " param_level " << tx.param_level << " timestamp " << merkle_time << " age " << dt;

	//if (dt > 30)	// for testing
	if (dt > 48*60*60)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate error tx.param_level too old age " << dt;

		return TX_RESULT_PARAM_LEVEL_TOO_OLD;
	}

	rc = dbconn->CommitRootsSelect(tx.param_level, false, merkle_time, &tx.merkle_root, sizeof(tx.merkle_root));
	if (rc < 0)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::TxValidate error retrieving Merkle root level " << tx.param_level;

		return TX_RESULT_SERVER_ERROR;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate error Merkle root level " << tx.param_level << " not found";

		return TX_RESULT_PARAM_LEVEL_INVALID;
	}

	tx.outvalmin = g_blockchain.proof_params.outvalmin;
	tx.outvalmax = g_blockchain.proof_params.outvalmax;
	tx.invalmax = g_blockchain.proof_params.invalmax;

#endif // !TEST_EXTRA_ON_WIRE

	if (CCProof_VerifyProof(tx))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate error CCProof_VerifyProof failed";

		return TX_RESULT_PROOF_VERIFICATION_FAILED;
	}

	bool found_spent = false;
	bool found_not_spent = false;

	if (tx.nin == 0)
		return 2;	// could check pseudo-serial num, but it's easier to just return next_commitnum = 0

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		for (unsigned j = 0; j < i; ++j)
		{
			if (!memcmp(&tx.input[i].S_serialnum, &tx.input[j].S_serialnum, sizeof(tx.input[i].S_serialnum)))
			{
				if (g_witness.IsMalTest() && !(rand() & 1))
					BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate allowing transaction with duplicate serialnums for double-spend testing";
				else
				{
					BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate duplicate serialnums " << i << " and " << j;

					return TX_RESULT_DUPLICATE_SERIALNUM;
				}
			}
		}

		auto rc = dbconn->SerialnumCheck(&tx.input[i].S_serialnum, sizeof(tx.input[i].S_serialnum));
		if (rc < 0)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::TxValidate error checking persistent serialnums";

			return TX_RESULT_SERVER_ERROR;
		}
		else if (rc)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate serialnum " << i << " of " << tx.nin << " already in persistent db";

			found_spent = true;
		}
		else
			found_not_spent = true;

		if (found_spent && found_not_spent)
			return TX_RESULT_ALREADY_SPENT;
	}

	if (found_spent)
		return 1;	// all were spent so tx may have been submitted twice

	return 0;
}

void ProcessTx::ThreadProc()
{
	auto dbconn = new DbConn;
	auto ptx = new TxPay;

	CCASSERT(dbconn);
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::ThreadProc start dbconn " << (uintptr_t)dbconn;

	while (true)
	{
		dbconn->WaitForQueuedWork(PROCESS_Q_TYPE_TX);

		if (g_shutdown)
			break;

		SmartBuf smartobj;
		unsigned conn_index = 0;
		unsigned callback_id = 0;
		int64_t result = TX_RESULT_SERVER_ERROR;

		while (true)	// so we can use break on error
		{
			if (dbconn->ProcessQGetNextValidateObj(PROCESS_Q_TYPE_TX, &smartobj, conn_index, callback_id))
			{
				//BOOST_LOG_TRIVIAL(debug) << "ProcessTx ProcessQGetNextValidateObj failed";

				break;
			}

			SmartBuf retobj;
			auto obj = (CCObject*)smartobj.data();

			BOOST_LOG_TRIVIAL(debug) << "ProcessTx Validating tx " << buf2hex(obj->OidPtr(), sizeof(ccoid_t)) << " conn_index Conn-" << conn_index << " callback_id " << callback_id;

			auto rc = dbconn->ValidObjsGetObj(*obj->OidPtr(), &retobj);
			//retobj.ClearRef();	// for testing
			if (rc < 0)
			{
				BOOST_LOG_TRIVIAL(error) << "ProcessTx ValidObjsGetObj failed";

				break;
			}
			else if (retobj)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessTx ValidObjsGetObj tx already in valid db";

				// the wallet might resubmit tx if it didn't receive the acknowledgement the first time
				// so this is allowed without error, but since the tx might already be in a block a this point,
				// we have to return zero for next_commitnum
				result = 0;
				break;
			}

			// set starting point to search for transaction to clear
			// we have to set this now to avoid a race with the transaction being placed into a persistent block
			auto next_commitnum = g_commitments.GetNextCommitnum();

			rc = TxValidate(dbconn, tx, smartobj);
			if (rc < 0)
			{
				auto result_string = ResultString(rc);

				if (result_string)
					BOOST_LOG_TRIVIAL(info) << "ProcessTx TxValidate failed with result " << rc << " = " << result_string;
				else
					BOOST_LOG_TRIVIAL(info) << "ProcessTx TxValidate failed with result " << rc;

				result = rc;
				break;
			}
			else if (rc == 1)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessTx TxValidate all serialnums found in persistentdb";

				// the wallet might resubmit tx if it didn't receive the acknowledgement the first time
				// so this is allowed without error
				result = 0;
				break;
			}
			else if (rc)
			{
				BOOST_LOG_TRIVIAL(debug) << "ProcessTx TxValidate tx has no serialnums to check";

				next_commitnum = 0;	// force search to start at zero cause tx might already be in an indelible block
			}

			rc = dbconn->ValidObjsInsert(smartobj);
			if (rc < 0)
			{
				BOOST_LOG_TRIVIAL(error) << "ProcessTx ValidObjsInsert failed";

				break;
			}
			else if (rc)
			{
				BOOST_LOG_TRIVIAL(debug) << "ProcessTx ValidObjsInsert constraint violation";

				// the wallet might resubmit tx if it didn't receive the acknowledgement the first time
				// so this is allowed without error, but since the tx might already be in a block a this point,
				// we have to return zero for next_commitnum

				result = 0;
				break;
			}

			result = next_commitnum;

			g_witness.NotifyNewWork(false);

			break;
		}

		if (conn_index)
		{
			auto conn = g_connregistry.GetConn(conn_index);

			conn->HandleValidateDone(callback_id, result);
		}
	}

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::ThreadProc end dbconn " << (uintptr_t)dbconn;

	delete ptx;
	delete dbconn;
}
