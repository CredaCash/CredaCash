/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * processblock.cpp
*/

#include "ccnode.h"
#include "processblock.hpp"
#include "processtx.hpp"
#include "block.hpp"
#include "blockchain.hpp"
#include "witness.hpp"

#include <CCobjects.hpp>
#include <transaction.h>
#include <xtransaction-xreq.hpp>
#include <xtransaction-xpay.hpp>
#include <ccserver/connection_registry.hpp>

#define XCX_PAY_RETRIES				200
#define XCX_PAY_WITNESS_RETRIES		4

#define TRACE_PROCESS_BLOCK	(g_params.trace_block_validation)

//!#define RTEST_CUZZ			(1024-1)
//!#define RTEST_SKIP_TX_WAIT	4
//!#define TEST_FUTURE_BLOCKS		1

#ifndef RTEST_CUZZ
#define RTEST_CUZZ			0	// don't test
#endif

#ifndef RTEST_SKIP_TX_WAIT
#define RTEST_SKIP_TX_WAIT	0	// don't test
#endif

#ifndef TEST_FUTURE_BLOCKS
#define TEST_FUTURE_BLOCKS	0	// don't test
#endif

ProcessBlock g_processblock;

static DbConn *dbconn = NULL;	// not thread safe

void ProcessBlock::Init()
{
	BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::Init";

	dbconn = new DbConn;

	m_thread = new thread(&ProcessBlock::ThreadProc, this);
}

void ProcessBlock::Stop()
{
	BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::Stop";

	DbConnProcessQ::StopQueuedWork(PROCESS_Q_TYPE_BLOCK);

	BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::Stop done";
}

void ProcessBlock::DeInit()
{
	BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::DeInit";

	if (m_thread)
	{
		m_thread->join();

		delete m_thread;

		m_thread = NULL;
	}

	delete dbconn;

	BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::DeInit done";
}

int ProcessBlock::ExtractTx(const char *wire, const uint32_t txsize, SmartBuf& smartobj)
{
	if (txsize < CC_MSG_HEADER_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::ExtractTx size " << txsize << " < " << CC_MSG_HEADER_SIZE;

		return -1;
	}

	smartobj = SmartBuf(txsize + sizeof(CCObject::Preamble));
	if (!smartobj)
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessBlock::ExtractTx SmartBuf allocation failed size " << txsize + sizeof(CCObject::Preamble);

		return -1;
	}

	auto obj = (CCObject*)smartobj.data();

	memcpy(obj->ObjPtr(), wire, txsize);

	if (!obj->IsValid() || obj->ObjSize() != txsize)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::ExtractTx invalid tx object";

		return -1;
	}

	obj->SetObjId();

	return 0;
}

int ProcessBlock::CompareBinaryTxs(SmartBuf smartobj1, SmartBuf smartobj2)
{
	auto obj1 = (CCObject*)smartobj1.data();
	auto obj2 = (CCObject*)smartobj2.data();

	if (obj1->WireTag() != obj2->WireTag())
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::CompareBinaryTxs WireTag's differ";

		return -1;
	}

	if (obj1->BodySize() != obj2->BodySize())
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::CompareBinaryTxs sizes differ";

		return -1;
	}

	if (memcmp(obj1->BodyPtr(), obj2->BodyPtr(), obj1->BodySize()))
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::CompareBinaryTxs binary objects differ";

		return -1;
	}

	return 0;
}

int ProcessBlock::BlockValidate(DbConn *dbconn, SmartBuf smartobj, TxPay& txbuf)
{
	auto bufp = smartobj.BasePtr();
	auto block = (Block*)smartobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	CCASSERT(auxp);

	if (TRACE_PROCESS_BLOCK) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::BlockValidate block bufp " << (uintptr_t)bufp << " level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " prior oid " << buf2hex(&wire->prior_oid, CC_OID_TRACE_SIZE);

	auto prune_level = g_blockchain.ComputePruneLevel(1, BLOCK_PRUNE_ROUNDS);

	if (wire->level.GetValue() < prune_level)
	{
		BOOST_LOG_TRIVIAL(debug) << "ProcessBlock::BlockValidate skipping validation of block at level " << wire->level.GetValue() << " prune_level " << prune_level;

		return 1;
	}

	if (wire->witness >= MAX_NWITNESSES)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate invalid witness " << (unsigned)wire->witness << " >= " << MAX_NWITNESSES;

		return -1;
	}

	auto priorobj = block->GetPriorBlock();

	if (!priorobj)
	{
		auto rc = dbconn->ValidObjsGetObj(wire->prior_oid, &priorobj);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(debug) << "ProcessBlock::BlockValidate prior block not yet valid for block oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " prior oid " << buf2hex(&wire->prior_oid, CC_OID_TRACE_SIZE);

			return 2;	// hold and retry (block will be requeued by ProcessQUpdateSubsequentBlockStatus)
		}

		block->ChainToPriorBlock(priorobj);
	}

	auto priorblock = (Block*)priorobj.data();
	auto prior_auxp = priorblock->AuxPtr();

	if (wire->witness >= prior_auxp->blockchain_params.next_nwitnesses)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate unauthorized witness " << (unsigned)wire->witness << " >= " << prior_auxp->blockchain_params.next_nwitnesses;

		return -1;
	}

	if (g_witness.test_mal)
		return 0;			// for testing, allow bad blocks

	if (block->CheckBadSigOrder(-1))
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate error witness order rule failed";

		return -1;
	}

	if (block->SignOrVerify(true))
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate error block signature verification failed";

		return -1;
	}

	auto prior_blocktime = priorblock->WireData()->timestamp.GetValue();

	auto block_time = wire->timestamp.GetValue();
	int64_t future = block_time - unixtime();
	if (TEST_FUTURE_BLOCKS && !IsWitness()) future += 60;

	if (future > g_params.block_future_tolerance)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate error block timestamp " << block_time << " is " << future << " seconds in future";

		// delete queue entry so validation can be retried later
		auto obj = (CCObject*)smartobj.data();
		unsigned block_tx_count, conn_index;
		uint32_t callback_id;
		dbconn->ProcessQSelectAndDelete(PROCESS_Q_TYPE_BLOCK, *(obj->OidPtr()), block_tx_count, conn_index, callback_id);

		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Warning: Block timestamp " << block_time << " is " << future << " seconds in future; for proper operation, please check that this computer's clock is accurate." << endl;

		return -1;
	}

	if (dbconn->TempSerialnumClear((void*)TEMP_SERIALS_PROCESS_BLOCKP))	// before attempting to index, delete whatever is left over from last time
	{
		// if we can't delete them, the serialnums already in Temp_Serials_db might end up associated with the wrong block,
		// causing a block in the eventually indelible chain to be rejected because the serialnums appear to be already spent

		BOOST_LOG_TRIVIAL(error) << "ProcessBlock::BlockValidate TempSerialnumClear failed which might cause this node to lose sync with blockchain";
	}

	auto pdata = block->TxData();
	auto pend = block->ObjEndPtr();
	uint32_t txsize = (1 << 30);

	if (TRACE_PROCESS_BLOCK) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::BlockValidate block level " << wire->level.GetValue() << " bufp " << (uintptr_t)bufp << " objsize " << block->ObjSize() << " pdata " << (uintptr_t)pdata << " pend " << (uintptr_t)pend;

	g_processtx.InitBlockScan();

	auto p = pdata;
	for (; p < pend; p += txsize)
	{
		if (g_shutdown)
			return 1;

		txsize = *(uint32_t*)p;

		//if (TRACE_PROCESS_BLOCK) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::BlockValidate ptxdata " << (uintptr_t)p << " txsize " << txsize << " data " << buf2hex(p, 16);

		auto rc = tx_from_wire(txbuf, (char*)p, txsize);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate error parsing transaction";

			auto save = txbuf.tag_type;
			txbuf.tag_type = -1;
			if (txbuf.tag_type == save)
			{
				lock_guard<mutex> lock(g_cerr_lock);
				check_cerr_newline();
				cerr << "WARNING: unrecognized block contents; please check if a newer software version is available" << endl;
			}

			return -1;
		}

		if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

		auto xtx = ProcessTx::ExtractXtx(dbconn, txbuf);
		if (!xtx && ProcessTx::ExtractXtxFailed(txbuf, true)) return -1;

		if (Xtx::TypeIsXreq(txbuf.tag_type))	// TODO: test this
		{
			auto xreq = Xreq::Cast(xtx);

			if (xreq->expire_time > prior_blocktime + XREQ_MAX_EXPIRE_TIME)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate xreq excess expire_time " << xreq->expire_time << " prior_blocktime " << prior_blocktime << " XREQ_MAX_EXPIRE_TIME " << XREQ_MAX_EXPIRE_TIME;

				return -1;
			}

			if (xreq->expire_time <= prior_blocktime + xreq->hold_time + XREQ_MIN_POSTHOLD_TIME)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate expired xreq expire_time " << xreq->expire_time << " prior_blocktime " << prior_blocktime << " hold_time " << xreq->hold_time << " XREQ_MIN_POSTHOLD_TIME " << XREQ_MIN_POSTHOLD_TIME;

				return -1;
			}

			if (xreq->foreign_address.length())
			{
				auto rc = dbconn->XmatchingreqUniqueForeignAddressSelect(prior_blocktime, xreq->quote_asset, xreq->foreign_address);
				if (rc <= 0)
				{
					BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate duplicate foreign_address " << xreq->foreign_address;

					return -1;
				}
			}
		}

		if (Xtx::TypeIsXpay(txbuf.tag_type))	// TODO: test this
		{
			auto xpay = Xpay::Cast(xtx);

			if (!xpay->match_timestamp)
			{
				BOOST_LOG_TRIVIAL(error) << "ProcessBlock::BlockValidate missing xpay.match_timestamp; " << xpay->DebugString();

				return -1;
			}

			if (!xpay->payment_time)
			{
				BOOST_LOG_TRIVIAL(error) << "ProcessBlock::BlockValidate missing xpay.payment_time; " << xpay->DebugString();

				return -1;
			}

			if (xpay->match_timestamp + xpay->payment_time < prior_blocktime)	// TODO: test this
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate expired xpay match_timestamp " << xpay->match_timestamp << " payment_time " << xpay->payment_time << " prior_blocktime " << prior_blocktime;

				return -1;
			}
		}

		BlockChain::CheckCreatePseudoSerialnum(txbuf, xtx, p);

		for (unsigned i = 0; i < txbuf.nin; ++i)
		{
			if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

			auto rc = g_blockchain.CheckSerialnum(dbconn, priorobj, TEMP_SERIALS_PROCESS_BLOCKP, SmartBuf(), &txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES);

			if (g_shutdown)
				return 1;

			if (rc)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate CheckSerialnum result " << rc << " in block oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE);

				return 1;	// don't disconnect peer for sending a block that duplicates an existing serialnum
							// !!! TODO: disconnect if duplicate serialnum was in same block or indelible block with respect to block being checked
			}

			if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

			auto rc2 = dbconn->TempSerialnumInsert(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES, (void*)TEMP_SERIALS_PROCESS_BLOCKP);
			if (rc2)
			{
				BOOST_LOG_TRIVIAL(error) << "ProcessBlock::BlockValidate TempSerialnumInsert failure " << rc;

				return -1;
			}
		}

		SmartBuf smartobj;
		rc = ExtractTx((char*)p, txsize, smartobj);
		if (rc) return -1;
		CCASSERT(smartobj);

		// The below function invocation validates tx's assuming the prior block is the last indelible block.
		// That may have an earlier timestamp than the actual block in which the tx appears.
		// As a result, an xreq could fail validation here because
		// (xreq.expire_time > last indelible block time + XREQ_MAX_EXPIRE_TIME)
		// but succeed when validated below against the actual block in which the xreq appears.

		g_processtx.TxEnqueueValidate(dbconn, true, false, PROCESS_Q_PRIORITY_BLOCK_TX, smartobj, 0, 0);
	}

	if (p != pend)
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate end of block has " << (uintptr_t)pend - (uintptr_t)p << " extra bytes";

		return -1;
	}

	if (!RandTest(RTEST_SKIP_TX_WAIT))
		g_processtx.WaitForBlockTxValidation();
	else
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate skipping WaitForBlockTxValidation";

	for (p = pdata; p < pend; p += txsize)
	{
		if (g_shutdown)
			return 1;

		txsize = *(uint32_t*)p;

		SmartBuf smartobj, validobj;
		auto rc = ExtractTx((char*)p, txsize, smartobj);
		if (rc) return -1;
		CCASSERT(smartobj);

		auto obj = (CCObject*)smartobj.data();

		if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

		rc = dbconn->ValidObjsGetObj(*obj->OidPtr(), &validobj);
		if (!rc)
		{
			if (TRACE_PROCESS_BLOCK) BOOST_LOG_TRIVIAL(debug) << "ProcessBlock::BlockValidate tx found in valid db oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

			auto rc = ProcessBlock::CompareBinaryTxs(smartobj, validobj);
			if (rc) return -1;
		}
		else
		{
			if (TRACE_PROCESS_BLOCK) BOOST_LOG_TRIVIAL(debug) << "ProcessBlock::BlockValidate tx not found in valid db oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

			for (unsigned retry = 0; ; ++retry)
			{
				auto rc = ProcessTx::TxValidate(dbconn, txbuf, smartobj, prior_blocktime, true);	// use prior_blocktime, so that tx's in this block expire after the block is processed
				if (!rc)
					break;

				if (!Xtx::TypeIsXpay(txbuf.tag_type) || retry > (IsWitness() ? XCX_PAY_WITNESS_RETRIES : XCX_PAY_RETRIES))
				{
					BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate tx type " << txbuf.tag_type << " invalid; oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

					return -1;
				}

				if (g_shutdown)
					return 1;

				sleep(1);

				BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate retrying validation of tx type " << txbuf.tag_type;
			}
		}
	}

	return 0;
}

void ProcessBlock::ValidObjsBlockInsert(DbConn *dbconn, SmartBuf smartobj, TxPay& txbuf, bool enqueue, bool check_indelible)
{
	auto block = (Block*)smartobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	if (TRACE_PROCESS_BLOCK) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::ValidObjsBlockInsert enqueue " << enqueue << " check indelible " << check_indelible << " block level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " size " << block->ObjSize() << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " prior oid " << buf2hex(&wire->prior_oid, CC_OID_TRACE_SIZE);

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

	if (dbconn->ValidObjsInsert(smartobj))
	{
		// if ValidObjsInsert fails, we might now have serialnums in the Temp_Serials_db associated with a blockp that could be reused,
		// causing a block in the eventually indelible chain to be rejected because the serialnums appear to be already spent

		BOOST_LOG_TRIVIAL(error) << "ProcessBlock::ValidObjsBlockInsert ValidObjsInsert failed which might cause this node to lose sync with blockchain";

		return;
	}

	if (auxp->from_tx_net)
	{
		m_last_network_ticks = (auxp->announce_ticks ? auxp->announce_ticks : 1);
	}

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

	if (enqueue)
	{
		dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_BLOCK, smartobj, &wire->prior_oid, wire->level.GetValue(), PROCESS_Q_STATUS_VALID, PROCESS_Q_PRIORITY_BLOCK_HI, false, 0, 0);
	}
	else
	{
		// leave the block in the ProcessQ to be pruned later by level
		// this helps prevents doubly-downloaded blocks from being validated more than once
		dbconn->ProcessQUpdateObj(PROCESS_Q_TYPE_BLOCK, auxp->oid, PROCESS_Q_STATUS_VALID, 0);
	}

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

	if (dbconn->ProcessQUpdateSubsequentBlockStatus(PROCESS_Q_TYPE_BLOCK, auxp->oid))
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessBlock::ValidObjsBlockInsert ProcessQUpdateSubsequentBlockStatus failed which might cause this node to lose sync with blockchain";
	}

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

	if (check_indelible && !g_blockchain.DoConfirmations(dbconn, smartobj, txbuf))
	{
		auto prune_level = g_blockchain.ComputePruneLevel(0, BLOCK_PRUNE_ROUNDS + 2);
		auto done_level  = g_blockchain.ComputePruneLevel(0, BLOCK_PRUNE_ROUNDS + 0);	// !!! set to +2 for mal testing; set to +0 for production & non-mal tests

		if (!IsWitness())
			done_level = prune_level;

		dbconn->ProcessQPruneLevel(PROCESS_Q_TYPE_BLOCK, prune_level);

		if (done_level > prune_level)
			dbconn->ProcessQDone(PROCESS_Q_TYPE_BLOCK, done_level);

		static uint64_t last_pruned_level = 0;	// not thread safe

		if (prune_level > last_pruned_level && prune_level % 3 == 0)	// requires a table scan, so only do it every 4 levels
		{
			last_pruned_level = prune_level;

			dbconn->TempSerialnumPruneLevel(prune_level);
		}
	}

	m_last_block_ticks = ccticks();

	g_witness.NotifyNewWork(BLOCKSEQ);
}

void ProcessBlock::ThreadProc()
{
	static TxPay txbuf;	// not thread safe

	BOOST_LOG_TRIVIAL(info) << "ProcessBlock::ThreadProc start dbconn " << (uintptr_t)dbconn;

	while (true)
	{
		if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

		dbconn->WaitForQueuedWork(PROCESS_Q_TYPE_BLOCK);

		if (g_blockchain.HasFatalError())
		{
			BOOST_LOG_TRIVIAL(fatal) << "ProcessBlock::ThreadProc exiting due to fatal error in blockchain";

			break;
		}

		if (g_shutdown)
			break;

		SmartBuf smartobj;
		unsigned conn_index;
		uint32_t callback_id;
		uint64_t level = 0;
		int result = -1;

		while (true)	// so we can use break on error
		{
			if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

			if (dbconn->ProcessQGetNextValidateObj(PROCESS_Q_TYPE_BLOCK, &smartobj, conn_index, callback_id))
			{
				//BOOST_LOG_TRIVIAL(debug) << "ProcessBlock ProcessQGetNextValidateObj failed";

				break;
			}

			if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

			auto bufp = smartobj.BasePtr();
			auto block = (Block*)smartobj.data();
			auto wire = block->WireData();
			auto auxp = block->AuxPtr();

			level = wire->level.GetValue();

			result = BlockValidate(dbconn, smartobj, txbuf);
			if (result)
			{
				if (result < 0)
				{
					BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate failed block level " << level << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE);

					{
						lock_guard<mutex> lock(g_cerr_lock);
						check_cerr_newline();
						cerr << "Block validate failed block level " << level << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << endl;
					}

					g_blockchain.DebugStop("Block validate failed");

					//start_shutdown(); // for debugging

					dbconn->ProcessQUpdateObj(PROCESS_Q_TYPE_BLOCK, auxp->oid, PROCESS_Q_STATUS_INVALID, 0);
				}

				break;
			}

			if (dbconn->TempSerialnumUpdate((void*)TEMP_SERIALS_PROCESS_BLOCKP, bufp, level))
			{
				// we can't allow a block with this error because we could end up with a valid block without its serialnums indexed, which could allow a double-spend

				BOOST_LOG_TRIVIAL(error) << "ProcessBlock TempSerialnumUpdate failure for block oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE);

				result = 1;

				break;
			}

			CCASSERTZ(result);

			BOOST_LOG_TRIVIAL(info) << "ProcessBlock received valid block level " << level << " timestamp " << wire->timestamp.GetValue() << " age " << unixtime() - wire->timestamp.GetValue() << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " size " << (block->ObjSize() < 1000 ? " " : "") << block->ObjSize() << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " prior " << buf2hex(&wire->prior_oid, CC_OID_TRACE_SIZE);

			block->ConsoleAnnounce(" received", wire, auxp);

			ValidObjsBlockInsert(dbconn, smartobj, txbuf);

			break;
		}

		if (conn_index && !g_shutdown)
		{
			if (TRACE_PROCESS_BLOCK) BOOST_LOG_TRIVIAL(debug) << "ProcessBlock calling HandleValidateDone level " << level << " Conn " << conn_index << " callback_id " << callback_id << " result " << result;

			auto conn = g_connregistry.GetConn(conn_index);

			conn->HandleValidateDone(level, callback_id, result);
		}
	}

	BOOST_LOG_TRIVIAL(info) << "ProcessBlock::ThreadProc end dbconn " << (uintptr_t)dbconn;
}
