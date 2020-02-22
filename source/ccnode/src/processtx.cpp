/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * processtx.cpp
*/

#include "ccnode.h"
#include "processtx.hpp"
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
#include <ccserver/connection_registry.hpp>

#define TRACE_PROCESS	(g_params.trace_tx_validation)

//!#define TEST_CUZZ		1

#ifndef TEST_CUZZ
#define TEST_CUZZ		0	// don't test
#endif

ProcessTx g_processtx;

static mutex processtx_mutex;
static condition_variable processtx_condition_variable;
static atomic<int> block_txs_pending;

void ProcessTx::Init()
{
	if (g_params.tx_validation_threads <= 0)
		return;

	m_threads.reserve(g_params.tx_validation_threads);

	for (int i = 0; i < g_params.tx_validation_threads && !g_shutdown; ++i)
	{
		auto t = new thread(ThreadProc);
		m_threads.push_back(t);
	}
}

void ProcessTx::Stop()
{
	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::Stop";

	CCASSERT(g_shutdown);

	lock_guard<mutex> lock(processtx_mutex);

	processtx_condition_variable.notify_all();

	DbConnProcessQ::StopQueuedWork(PROCESS_Q_TYPE_TX);

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::Stop done";
}

void ProcessTx::DeInit()
{
	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::DeInit";

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
		"OK",
		"INVALID:parameter level too old",
		"INVALID:already spent"
	};

	static const char *tx_result_stop_strings[] =
	{
		/* TX_RESULT_SERVER_ERROR */				"ERROR:server error",
		/* TX_RESULT_PARAM_LEVEL_INVALID */			"INVALID:parameter level invalid",
		/* TX_RESULT_DUPLICATE_SERIALNUM */			"INVALID:duplicate serial number",
		/* TX_RESULT_BINARY_DATA_INVALID */			"INVALID:binary data invalid",
		/* TX_RESULT_OPTION_NOT_SUPPORTED */		"INVALID:option not yet supported",
		/* TX_RESULT_INSUFFICIENT_DONATION */		"INVALID:insufficient donation",
		/* TX_RESULT_PROOF_VERIFICATION_FAILED */	"INVALID:zero knowledge proof verification failed"
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
	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::InitBlockScan block_txs_pending " << block_txs_pending;

	WaitForBlockTxValidation();
}

void ProcessTx::WaitForBlockTxValidation()
{
	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::WaitForBlockTxValidation block_txs_pending " << block_txs_pending;

	unique_lock<mutex> lock(processtx_mutex);

	while (block_txs_pending && !g_shutdown)
	{
		processtx_condition_variable.wait(lock);
	}

	if (TEST_CUZZ)
	{
		lock.unlock();
		usleep(rand() & (1024*1024-1));
	}

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::WaitForBlockTxValidation done block_txs_pending " << block_txs_pending;
}

int ProcessTx::TxEnqueueValidate(DbConn *dbconn, bool is_block_tx, bool add_to_relay_objs, int64_t priority, SmartBuf smartobj, unsigned conn_index, uint32_t callback_id)
{
	// the following operations are atomic, to ensure ValidObj's and ProcessQ stay in sync

	if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

	unique_lock<mutex> lock(processtx_mutex);

	if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

	// if tx is in valid obj's, then return 1 without enqueuing

	auto obj = (CCObject*)smartobj.data();
	SmartBuf retobj;

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxEnqueueValidate priority " << priority << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	auto rc = dbconn->ValidObjsGetObj(*obj->OidPtr(), &retobj);
	if (!rc)
	{
		if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxEnqueueValidate already in valid db oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

		return 1;
	}

	if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

	if (add_to_relay_objs)
	{
		// add tx to relay obj's so we don't download it after sending it to someone else

		relay_request_wire_params_t req_params;
		memset(&req_params, 0, sizeof(req_params));
		memcpy(&req_params.oid, obj->OidPtr(), sizeof(ccoid_t));

		dbconn->RelayObjsInsert(0, CC_TYPE_TXPAY, req_params, RELAY_STATUS_DOWNLOADED, 0);
	}

	if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

	rc = dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_TX, smartobj, NULL, 0, PROCESS_Q_STATUS_PENDING, priority, is_block_tx, conn_index, callback_id);
	if (rc < 0)
		return -1;

	if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

	if (!rc && is_block_tx)
	{
		auto btp = ++block_txs_pending;

		if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(debug) << "ProcessTx::TxEnqueueValidate block_txs_pending " << btp;
	}

	if (TEST_CUZZ)
	{
		lock.unlock();
		usleep(rand() & (1024*1024-1));
	}

	return 0;
}

int ProcessTx::TxValidate(DbConn *dbconn, TxPay& tx, SmartBuf smartobj)
{
	if (g_witness.test_mal)
		return 0;			// for testing, allow bad transactions

	auto obj = (CCObject*)smartobj.data();

	if (tx_from_wire(tx, (char*)obj->ObjPtr(), obj->ObjSize()))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID tx_from_wire failed";

		return TX_RESULT_BINARY_DATA_INVALID;
	}

#if !TEST_EXTRA_ON_WIRE

	//tx_dump_stream(cerr, tx);

	tx.source_chain = g_params.blockchain;

	if (tx.tag_type == CC_TYPE_MINT && Implement_CCMint(tx.source_chain))
		tx.zkkeyid = CC_MINT_ZKKEY_ID;

	if (tx.tx_type)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID tx type != 0";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.source_chain != g_params.blockchain)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID source-chain != " << g_params.blockchain;

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.revision)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID revision != 0";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.expiration)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID expiration != 0";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.refhash)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID reference-hash != 0";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.reserved)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID reserved != 0";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.tag_type == CC_TYPE_MINT && tx.nin != 1)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID tx.nin " << tx.nin << " != 1 in mint tx";

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	if (tx.tag_type != CC_TYPE_MINT && tx.nin != tx.nin_with_path)		// inputs with published commitments are not currently allowed because code to check them isn't implemented
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID tx.nin " << tx.nin << " != tx.nin_with_path " << tx.nin_with_path;

		return TX_RESULT_OPTION_NOT_SUPPORTED;
	}

	unsigned total_nout = 0;

	if (!tx.nout)
		tx.allow_restricted_addresses = false;

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		TxOut& txout = tx.outputs[i];

		txout.addrparams.dest_chain = g_params.blockchain;
		txout.M_pool = g_params.default_pool;

		total_nout += (txout.repeat_count + 1);

		if (txout.no_address)
			tx.allow_restricted_addresses = false;

		if (txout.addrparams.dest_chain != g_params.blockchain)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID destination-chain != " << g_params.blockchain;

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.M_pool != g_params.default_pool)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID pool != " << g_params.default_pool;

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.no_address)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID no-address true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.acceptance_required)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID acceptance-required true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.repeat_count)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID repeat-count > 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.no_asset)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID no-asset true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txout.no_amount)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID no-amount true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (tx.tag_type == CC_TYPE_MINT)
		{
			if (txout.asset_mask)
			{
				BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID mint tx asset-mask != 0";

				return TX_RESULT_OPTION_NOT_SUPPORTED;
			}

			if (txout.amount_mask)
			{
				BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID mint tx amount-mask != 0";

				return TX_RESULT_OPTION_NOT_SUPPORTED;
			}

			if (txout.M_asset_enc)
			{
				BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID mint tx encrypted-asset != 0";

				return TX_RESULT_OPTION_NOT_SUPPORTED;
			}
		}
		else
		{
			if (txout.asset_mask != TX_ASSET_WIRE_MASK)
			{
				BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID asset-mask != all one's";

				return TX_RESULT_OPTION_NOT_SUPPORTED;
			}

			if (txout.amount_mask != TX_AMOUNT_MASK)
			{
				BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID amount-mask != all one's";

				return TX_RESULT_OPTION_NOT_SUPPORTED;
			}
		}
	}

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		TxIn& txin = tx.inputs[i];

		if (tx.tag_type != CC_TYPE_MINT)
			txin.M_pool = g_params.default_pool;

		if (txin.enforce_master_secret)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID enforce-master-secret true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txin.enforce_spend_secrets)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID enforce-spend-secrets true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (!txin.enforce_trust_secrets)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID enforce-trust-secrets false";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txin.enforce_freeze)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID enforce-freeze true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txin.enforce_unfreeze)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID enforce-unfreeze true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txin.delaytime)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID delaytime > 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (tx.tag_type != CC_TYPE_MINT && txin.no_serialnum)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID no-serial-number true";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (tx.tag_type == CC_TYPE_MINT && !txin.no_serialnum)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID mint tx no-serial-number false";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (txin.S_spendspec_hashed)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID hashed-spendspec != 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (tx.tag_type == CC_TYPE_MINT && txin.pathnum)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID pathnum != 0 in mint tx";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		if (tx.tag_type != CC_TYPE_MINT && !txin.pathnum)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID pathnum = 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}

		/*if (txin.nsigkeys)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID nsigkeys > 0";

			return TX_RESULT_OPTION_NOT_SUPPORTED;
		}*/
	}

	bigint_t donation;
	tx_amount_decode(tx.donation_fp, donation, true, TX_DONATION_BITS, TX_AMOUNT_EXPONENT_BITS);

	if (tx.tag_type == CC_TYPE_MINT)
	{
		bigint_t mint_donation;
		mint_donation = TX_CC_MINT_DONATION;

		if (Implement_CCMint(g_params.blockchain) && !tx.param_level)
			mint_donation = TX_CC_MINT_AMOUNT;

		if (donation != mint_donation)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID donation " << donation << " != mint donation " << mint_donation;

			return TX_RESULT_INSUFFICIENT_DONATION;
		}
	}
	else
	{
		unsigned tx_size = sizeof(CCObject::Header) + obj->BodySize();

		bigint_t min_donation =
												g_blockchain.proof_params.donation_per_tx
				+ (bigint_t)(tx_size) *			g_blockchain.proof_params.donation_per_byte
				+ (bigint_t)(total_nout) *		g_blockchain.proof_params.donation_per_output
				+ (bigint_t)(tx.nin) *			g_blockchain.proof_params.donation_per_input;

		if (min_donation < g_blockchain.proof_params.minimum_donation)
			min_donation = g_blockchain.proof_params.minimum_donation;

		BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::TxValidate donation " << hex << tx.donation_fp << dec << " decoded " << donation << " min_donation " << min_donation << " tx bytes " << tx_size << " nout " << total_nout << " nin " << tx.nin;

		if (donation < min_donation && tx.donation_fp < TX_DONATION_MASK)
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID donation " << donation << " < minimum donation " << min_donation;

			return TX_RESULT_INSUFFICIENT_DONATION;
		}

		if (donation > min_donation * bigint_t(4UL))
			tx.allow_restricted_addresses = false;
	}

	auto last_indelible_block = g_blockchain.GetLastIndelibleBlock();
	auto block = (Block*)last_indelible_block.data();
	auto wire = block->WireData();

	uint64_t merkle_time, next_commitnum;

	// Merkle root can be used until "max_param_age" seconds after it is replaced with a new Merkle root
	// so we need to check the timestamp of the next param_level, which is when the Merkle root was replaced
	auto rc = dbconn->CommitRootsSelectLevel(tx.param_level + 1, true, merkle_time, next_commitnum, &tx.tx_merkle_root, TX_MERKLE_BYTES);
	if (rc < 0)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::TxValidate error retrieving next Merkle root level " << tx.param_level + 1;

		return TX_RESULT_SERVER_ERROR;
	}
	else if (rc)
	{
		merkle_time = wire->timestamp.GetValue();
	}

	int64_t dt = wire->timestamp.GetValue() - merkle_time;

	BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::TxValidate last indelible level " << wire->level.GetValue() << " timestamp " << wire->timestamp.GetValue() << " param_level " << tx.param_level << " timestamp " << merkle_time << " age " << dt;

	if (dt > g_params.max_param_age)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID tx.param_level too old age " << dt;

		return TX_RESULT_PARAM_LEVEL_TOO_OLD;
	}

	rc = dbconn->CommitRootsSelectLevel(tx.param_level, false, merkle_time, next_commitnum, &tx.tx_merkle_root, TX_MERKLE_BYTES);
	if (rc < 0)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::TxValidate error retrieving Merkle root level " << tx.param_level;

		return TX_RESULT_SERVER_ERROR;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID Merkle root level " << tx.param_level << " not found";

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

	tx_set_commit_iv(tx);

	//tx_dump_stream(cerr, tx);

	if (CCProof_VerifyProof(tx))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID CCProof_VerifyProof failed";

		return TX_RESULT_PROOF_VERIFICATION_FAILED;
	}

	if (tx.tag_type == CC_TYPE_MINT)
		return 0;

	if (tx.nin == 0)
		return 2;	// could check pseudo-serial num, but it's easier to just return next_commitnum = 0

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
					BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate allowing transaction with duplicate serialnums for double-spend testing";
				else
				{
					BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate INVALID duplicate serialnums " << i << " and " << j;

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
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::TxValidate error checking persistent serialnums";

			return TX_RESULT_SERVER_ERROR;
		}
		else if (rc)
		{
			found_one_not_spent = true;
		}
		else
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::TxValidate serialnum " << i << " of " << tx.nin << " already in persistent db";

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
		if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

		dbconn->WaitForQueuedWork(PROCESS_Q_TYPE_TX);

		if (g_shutdown)
			break;

		SmartBuf smartobj;
		CCObject* obj = NULL;
		unsigned block_tx_count = 0;
		unsigned conn_index = 0;
		uint32_t callback_id = 0;
		bool validated = false;
		bool inserted = false;
		bool block_done = false;
		uint64_t next_commitnum = 0;
		int64_t result = TX_RESULT_SERVER_ERROR;

		while (true)	// so we can use break on error
		{
			if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

			if (dbconn->ProcessQGetNextValidateObj(PROCESS_Q_TYPE_TX, &smartobj, conn_index, callback_id))
			{
				//BOOST_LOG_TRIVIAL(debug) << "ProcessTx ProcessQGetNextValidateObj failed";

				break;
			}

			obj = (CCObject*)smartobj.data();
			BOOST_LOG_TRIVIAL(debug) << "ProcessTx Validating oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t)) << " conn_index Conn " << conn_index << " callback_id " << callback_id;

			// set starting point to search for transaction to clear
			// we have to set this now to avoid a race with the transaction being placed into a persistent block
			next_commitnum = g_commitments.GetNextCommitnum();

			if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

			auto rc = TxValidate(dbconn, tx, smartobj);
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

			validated = true;

			break;
		}

		if (smartobj)
		{
			BOOST_LOG_TRIVIAL(debug) << "ProcessTx TxValidate validated " << validated << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

			if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

			// the following operations are atomic, to ensure ValidObj's and ProcessQ stay in sync

			lock_guard<mutex> lock(processtx_mutex);

			if (validated)
			{
				auto rc = dbconn->ValidObjsInsert(smartobj);
				if (rc < 0)
					BOOST_LOG_TRIVIAL(error) << "ProcessTx ValidObjsInsert failed";
				else if (rc)
				{
					BOOST_LOG_TRIVIAL(debug) << "ProcessTx ValidObjsInsert constraint violation";

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

			if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

			// refetch block_tx_count, conn_index and callback_id, in case they have changed
			auto rc = dbconn->ProcessQSelectAndDelete(PROCESS_Q_TYPE_TX, *obj->OidPtr(), block_tx_count, conn_index, callback_id);
			CCASSERTZ(rc);	// not allowed to fail

			// decrement block_txs_pending
			if (block_tx_count)
			{
				if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx block_tx_count " << block_tx_count << " block_txs_pending " << block_txs_pending << " net " << block_txs_pending - (int)block_tx_count;

				CCASSERT(block_txs_pending >= (int)block_tx_count);

				block_txs_pending -= block_tx_count;

				if (!block_txs_pending)
					block_done = true;
			}
		}

		if (conn_index && !g_shutdown)
		{
			if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(debug) << "ProcessTx calling HandleValidateDone Conn " << conn_index << " callback_id " << callback_id << " result " << result;

			auto conn = g_connregistry.GetConn(conn_index);

			conn->HandleValidateDone(callback_id, result);
		}

		if (block_done)
		{
			if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(debug) << "ProcessTx block_txs_pending done";

			processtx_condition_variable.notify_all();
		}

		if (inserted)
			g_witness.NotifyNewWork(false);
	}

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessTx::ThreadProc end dbconn " << (uintptr_t)dbconn;

	delete ptx;
	delete dbconn;
}
