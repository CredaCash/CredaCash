/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * processblock.cpp
*/

#include "CCdef.h"
#include "processblock.hpp"
#include "block.hpp"
#include "blockchain.hpp"
#include "witness.hpp"
#include "util.h"

#include <CCobjects.hpp>
#include <transaction.h>
#include <ccserver/connection_registry.hpp>

#define TRACE_PROCESS	(g_params.trace_block_validation)

//#define TEST_CUZZ		1

#ifndef TEST_CUZZ
#define TEST_CUZZ		0	// don't test
#endif

ProcessBlock g_processblock;

static DbConn *dbconn = NULL;

void ProcessBlock::Init()
{
	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::Init";

	dbconn = new DbConn;

	m_thread = new thread(&ProcessBlock::ThreadProc, this);
}

void ProcessBlock::DeInit()
{
	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::DeInit";

	DbConnProcessQ::StopQueuedWork(PROCESS_Q_TYPE_BLOCK);

	if (m_thread)
	{
		m_thread->join();

		delete m_thread;

		m_thread = NULL;
	}

	delete dbconn;

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::DeInit done";
}

int ProcessBlock::BlockValidate(DbConn *dbconn, SmartBuf smartobj, struct TxPay &txbuf)
{
	auto bufp = smartobj.BasePtr();
	auto block = (Block*)smartobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	CCASSERT(auxp);

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::BlockValidate block bufp " << (uintptr_t)bufp << " level " << wire->level << " witness " << (unsigned)wire->witness << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior oid " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

	auto prune_level = g_blockchain.ComputePruneLevel(1, BLOCK_PRUNE_ROUNDS);

	if (wire->level < prune_level)
	{
		BOOST_LOG_TRIVIAL(debug) << "ProcessBlock::BlockValidate skipping validation of block at level " << wire->level << " prune_level " << prune_level;

		return 1;
	}

	if (wire->witness >= MAX_NWITNESSES)
	{
		BOOST_LOG_TRIVIAL(debug) << "ProcessBlock::BlockValidate invalid witness " << (unsigned)wire->witness << " >= " << MAX_NWITNESSES;

		return -1;
	}

	auto priorobj = block->GetPriorBlock();

	if (!priorobj)
	{
		dbconn->ValidObjsGetObj(wire->prior_oid, &priorobj);
		if (!priorobj)
		{
			BOOST_LOG_TRIVIAL(debug) << "ProcessBlock::BlockValidate prior block not yet valid for block oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior oid " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

			return 1;	// hold and retry
		}

		block->ChainToPriorBlock(priorobj);
	}

	auto priorblock = (Block*)priorobj.data();
	auto prior_auxp = priorblock->AuxPtr();

	if (wire->witness >= prior_auxp->blockchain_params.next_nwitnesses)
	{
		BOOST_LOG_TRIVIAL(debug) << "ProcessBlock::BlockValidate unauthorized witness " << (unsigned)wire->witness << " >= " << prior_auxp->blockchain_params.next_nwitnesses;

		return -1;
	}

	if (block->CheckBadSigOrder(-1))
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate error witness order rule failed block oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

		return -1;
	}

	if (block->SignOrVerify(true))
	{
		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate error block signature verification failed block oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

		return -1;
	}

	if (dbconn->TempSerialnumClear((void*)TEMP_SERIALS_PROCESS_BLOCKP))	// before attempting to index, delete whatever is left over from last time
	{
		// if we can't delete them, the serialnum's already in Temp_Serials_db might end up associated with the wrong block,
		// causing a block in the eventually indelible chain to be rejected because the serialnum's appear to be already spent

		BOOST_LOG_TRIVIAL(error) << "ProcessBlock::BlockValidate TempSerialnumClear failed which might cause this node to lose sync with blockchain";
	}

	auto pdata = block->TxData();
	auto pend = block->ObjEndPtr();

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::BlockValidate block level " << wire->level << " bufp " << (uintptr_t)bufp << " objsize " << block->ObjSize() << " pdata " << (uintptr_t)pdata << " pend " << (uintptr_t)pend;

	while (pdata < pend)
	{
		auto txsize = *(uint32_t*)pdata;

		//if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::BlockValidate ptxdata " << (uintptr_t)pdata << " txsize " << txsize << " data " << buf2hex(pdata, 16);

		// !!! need to look up each tx in validobj's

		auto rc = tx_from_wire(txbuf, (char*)pdata, txsize);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate error parsing transaction in block oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			return -1;
		}

		g_blockchain.CheckCreatePseudoSerialnum(txbuf, pdata, txsize);

		pdata += txsize;

		for (unsigned i = 0; i < txbuf.nin; ++i)
		{
			auto rc = g_blockchain.CheckSerialnum(dbconn, priorobj, TEMP_SERIALS_PROCESS_BLOCKP, SmartBuf(), &txbuf.input[i].S_serialnum, sizeof(txbuf.input[i].S_serialnum));
			if (rc)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate CheckSerialnum result " << rc << " in block oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

				return -1;
			}

			auto rc2 = dbconn->TempSerialnumInsert(&txbuf.input[i].S_serialnum, sizeof(txbuf.input[i].S_serialnum), (void*)TEMP_SERIALS_PROCESS_BLOCKP);
			if (rc2)
			{
				BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate TempSerialnumInsert failure " << rc << " in block oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

				return -1;
			}
		}
	}

	if (dbconn->TempSerialnumUpdate((void*)TEMP_SERIALS_PROCESS_BLOCKP, bufp, wire->level))
	{
		// we can't allow this because we could end up with a valid block without its serialnums indexed, which could allow a double-spend

		BOOST_LOG_TRIVIAL(info) << "ProcessBlock::BlockValidate TempSerialnumUpdate failure for block oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));
		BOOST_LOG_TRIVIAL(error) << "ProcessBlock::BlockValidate TempSerialnumClear failed which might cause this node to lose sync with blockchain";

		return -1;
	}

	return 0;
}

void ProcessBlock::ValidObjsBlockInsert(DbConn *dbconn, SmartBuf smartobj, struct TxPay &txbuf, bool enqueue, bool check_indelible)
{
	auto block = (Block*)smartobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::ValidObjsBlockInsert enqueue " << enqueue << " check indelible " << check_indelible << " block level " << wire->level << " witness " << (unsigned)wire->witness << " size " << block->ObjSize() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior oid " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

	if (dbconn->ValidObjsInsert(smartobj))
	{
		// if ValidObjsInsert fails, we might now have serialnum's in the Temp_Serials_db associated with a blockp that could be reused,
		// causing a block in the eventually indelible chain to be rejected because the serialnum's appear to be already spent

		BOOST_LOG_TRIVIAL(error) << "ProcessBlock::ValidObjsBlockInsert ValidObjsInsert failed which might cause this node to lose sync with blockchain";

		return;
	}

	if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

	if (enqueue)
	{
		dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_BLOCK, smartobj, &wire->prior_oid, wire->level, PROCESS_Q_STATUS_VALID, 0, 0, 0);
	}
	else if (IsWitness())
	{
		dbconn->ProcessQUpdateValidObj(PROCESS_Q_TYPE_BLOCK, auxp->oid, PROCESS_Q_STATUS_VALID, 0);
	}
	else
	{
		// leave the block in the ProcessQ to be pruned later by level
		// this helps prevents doubly-downloaded blocks from being validated more than once
	}

	if (dbconn->ProcessQUpdateSubsequentBlockStatus(PROCESS_Q_TYPE_BLOCK, auxp->oid))
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessBlock::ValidObjsBlockInsert ProcessQUpdateSubsequentBlockStatus failed which might cause this node to lose sync with blockchain";
	}

	if (check_indelible && !g_blockchain.DoConfirmations(dbconn, smartobj, txbuf))
	{
		auto prune_level = g_blockchain.ComputePruneLevel(0, BLOCK_PRUNE_ROUNDS + 2);
		auto done_level  = g_blockchain.ComputePruneLevel(0, BLOCK_PRUNE_ROUNDS + 0);	// !!! set to 2 for mal testing; set to zero for production & non-mal tests

		if (!IsWitness())
			done_level = prune_level;

		dbconn->ProcessQPruneLevel(PROCESS_Q_TYPE_BLOCK, prune_level);

		if (done_level > prune_level)
			dbconn->ProcessQDone(PROCESS_Q_TYPE_BLOCK, done_level);

		static uint64_t last_pruned_level = 0;

		if (prune_level > last_pruned_level && prune_level % 3 == 0)	// requires a table scan, so only do it every 4 levels
		{
			last_pruned_level = prune_level;

			dbconn->TempSerialnumPruneLevel(prune_level);
		}
	}

	m_last_block_ticks.store(ccticks());

	g_witness.NotifyNewWork(true);
}

void ProcessBlock::ThreadProc()
{
	static TxPay txbuf;

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::ThreadProc start dbconn " << (uintptr_t)dbconn;

	while (true)
	{
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
		unsigned callback_id;
		int result = -1;

		while (true)	// so we can use break on error
		{
			if (dbconn->ProcessQGetNextValidateObj(PROCESS_Q_TYPE_BLOCK, &smartobj, conn_index, callback_id))
			{
				//BOOST_LOG_TRIVIAL(debug) << "ProcessBlock ProcessQGetNextValidateObj failed";

				break;
			}

			result = BlockValidate(dbconn, smartobj, txbuf);
			if (result)
				break;

			auto block = (Block*)smartobj.data();
			auto wire = block->WireData();
			auto auxp = block->AuxPtr();

			BOOST_LOG_TRIVIAL(info) << "ProcessBlock received valid block level " << wire->level << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " size " << (block->ObjSize() < 1000 ? " " : "") << block->ObjSize() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

			ValidObjsBlockInsert(dbconn, smartobj, txbuf);

			if (1)
			{
				int64_t age = time(NULL) - wire->timestamp;
				lock_guard<FastSpinLock> lock(g_cout_lock);
				cerr << " received block level " << wire->level << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " size " << (block->ObjSize() < 1000 ? " " : "") << block->ObjSize() << " oid " << buf2hex(&auxp->oid, 3, 0) << ".. prior " << buf2hex(&wire->prior_oid, 3, 0) << ".. age " << age << endl;
			}

			break;
		}

		if (conn_index && result < 0)
		{
			auto conn = g_connregistry.GetConn(conn_index);

			conn->HandleValidateDone(callback_id, PROCESS_RESULT_STOP_THRESHOLD);
		}
	}

	if (TRACE_PROCESS) BOOST_LOG_TRIVIAL(trace) << "ProcessBlock::ThreadProc end dbconn " << (uintptr_t)dbconn;
}
