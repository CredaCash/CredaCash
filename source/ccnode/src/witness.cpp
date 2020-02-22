/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * witness.cpp
*/

#include "ccnode.h"
#include "witness.hpp"
#include "block.hpp"
#include "blockchain.hpp"
#include "processtx.hpp"
#include "processblock.hpp"
#include "dbconn.hpp"

#include <CCobjects.hpp>
#include <CCcrypto.hpp>
#include <CCmint.h>
#include <transaction.h>
#include <siphash/siphash.h>
#include <blake2/blake2.h>
#include <ed25519/ed25519.h>

#define TRACE_WITNESS	(g_params.trace_witness)

#define WITNESS_TIME_SPACING			(block_time_ms*(CCTICKS_PER_SEC/1000))
#define MIN_BLOCK_WORK_TIME				(block_min_work_ms*(CCTICKS_PER_SEC/1000))
#define WITNESS_NO_WORK_TIME_SPACING	(block_max_time*CCTICKS_PER_SEC)

#define WITNESS_RANDOM_TEST_TIME		(test_block_random_ms*(CCTICKS_PER_SEC/1000))

//#define TEST_RANDOM_WITNESS_ORDER		1
//#define TEST_BUILD_ON_RANDOM			1
//#define TEST_DELAY_LAST_INDELIBLE		1

//#define TEST_PAUSE					1

//#define TEST_MAL_IGNORE_SIG_ORDER		1	// when used, a few bad blocks will be relayed before this node has a fatal blockchain error

//!#define TEST_CUZZ					1
//#define TEST_PROCESS_Q				1

//#define TEST_UNRECOGNIZED_TAG			1

//!#define TEST_WITNESS_LOSS				(GENESIS_NWITNESSES)

#define TEST_MIN_MAL_RATIO				(GENESIS_NWITNESSES * GENESIS_MAXMAL)

#define WITNESS_TEST_LOSS_REQ_WITNESS	((GENESIS_NWITNESSES - GENESIS_MAXMAL) / 2 + GENESIS_MAXMAL + 1)
//#define WITNESS_TEST_LOSS_REQ_WITNESS	(GENESIS_NWITNESSES - 2)
#define WITNESS_TEST_LOSS_NWITNESSES_HI	GENESIS_NWITNESSES
//#define WITNESS_TEST_LOSS_NWITNESSES_HI	(WITNESS_TEST_LOSS_REQ_WITNESS + 1)

#ifndef WITNESS_RANDOM_TEST_TIME
#define WITNESS_RANDOM_TEST_TIME		0	// don't test
#endif

#ifndef TEST_RANDOM_WITNESS_ORDER
#define TEST_RANDOM_WITNESS_ORDER		0	// don't test
#endif

#ifndef TEST_BUILD_ON_RANDOM
#define TEST_BUILD_ON_RANDOM			0	// don't test
#endif

#ifndef TEST_DELAY_LAST_INDELIBLE
#define TEST_DELAY_LAST_INDELIBLE		0	// don't test
#endif

#ifndef TEST_PAUSE
#define TEST_PAUSE						0	// don't test
#endif

#ifndef TEST_MAL_IGNORE_SIG_ORDER
#define TEST_MAL_IGNORE_SIG_ORDER		0	// don't test
#endif

#ifndef TEST_CUZZ
#define TEST_CUZZ						0	// don't test
#endif

#ifndef TEST_PROCESS_Q
#define TEST_PROCESS_Q					0	// don't test
#endif

#ifndef TEST_UNRECOGNIZED_TAG
#define TEST_UNRECOGNIZED_TAG			0	// don't test
#endif

#ifndef TEST_WITNESS_LOSS
#define TEST_WITNESS_LOSS				0	// don't test
#endif

Witness g_witness;
atomic<unsigned> max_mint_level(0);

void IncrementOid(ccoid_t& oid)
{
	for (auto i = oid.rbegin(); i != oid.rend(); ++i)
	{
		if (++(*i))
			return;
	}
}

static unsigned FindBestMintTx(DbConn *dbconn, SmartBuf priorobj, uint64_t priorlevel, SmartBuf *retobj)
{
	auto targetlevel = priorlevel - CC_MINT_ACCEPT_SPAN;
	if (targetlevel <= CC_MINT_ACCEPT_SPAN)
		targetlevel = 1;
	if (!priorlevel)
		targetlevel = 0;

	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestMintTx priorlevel " << priorlevel << " targetlevel " << targetlevel;

	if (priorlevel && priorlevel < CC_MINT_ACCEPT_SPAN)
		return 0;

	ccoid_t oid;
	SmartBuf smartobj;
	TxPay txbuf;
	bool passed_zero = false;

	auto block = (Block*)priorobj.data();
	auto rc = blake2b(&oid, sizeof(oid), NULL, 0, block->TxData(), block->TxDataSize());
	CCASSERTZ(rc);

	while (true)
	{
		while (true)
		{
			while (true)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestMintTx select object oid " << buf2hex(&oid, sizeof(oid));

				auto rc = dbconn->ValidObjsGetObj(oid, &smartobj, true);
				if (!rc)
					break;

				if (passed_zero)
					return 0;

				memset(&oid, 0, sizeof(oid));
				passed_zero = true;
			}

			// object must be a mint tx with the right param_level

			auto obj = (CCObject*)smartobj.data();

			memcpy(&oid, obj->OidPtr(), sizeof(oid));

			if (obj->ObjType() != CC_TYPE_MINT)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestMintTx found non-mint object oid " << buf2hex(&oid, sizeof(oid));

				break;
			}

			auto param_level = txpay_param_level_from_wire(obj);

			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestMintTx found mint param_level " << param_level << " oid " << buf2hex(&oid, sizeof(oid));

			if (param_level != targetlevel)
				break;

			auto rc = ProcessTx::TxValidate(dbconn, txbuf, smartobj);
			if (rc)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestMintTx TxValidate failed";

				break;
			}

			*retobj = smartobj;

			BOOST_LOG_TRIVIAL(info) << "Witness::FindBestMintTx returning mint param_level " << param_level << " oid " << buf2hex(&oid, sizeof(oid));

			return 1;
		}

		IncrementOid(oid);
	}
}

Witness::Witness()
 :	m_pthread(NULL),
	m_score_genstamp(0),
	m_waiting_on_block(false),
	m_waiting_on_tx(false),
	witness_index(-1),
	block_time_ms(10000),
	block_min_work_ms(1000),
	block_max_time(20),
	test_block_random_ms(0),
	test_mal(0)
{
	memset(&m_highest_witnessed_level, 0, sizeof(m_highest_witnessed_level));
}

void Witness::Init()
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::Init witness " << witness_index << " test mal " << test_mal;

	if (IsWitness())
	{
		// create static buffer to use when building blocks
		m_blockbuf = SmartBuf(CC_BLOCK_MAX_SIZE);
		CCASSERT(m_blockbuf);

		m_dbconn = new DbConn;
		CCASSERT(m_dbconn);

		if (TEST_WITNESS_LOSS && !TEST_SIM_ALL_WITNESSES) BOOST_LOG_TRIVIAL(info) << "Witness::Init simulating witness loss with max witness " << WITNESS_TEST_LOSS_REQ_WITNESS << " at levels mod " << TEST_WITNESS_LOSS;

		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::Init launching ThreadProc";

		m_pthread = new thread(&Witness::ThreadProc, this);
		CCASSERT(m_pthread);
	}
}

void Witness::DeInit()
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::DeInit";

	ShutdownWork();

	if (m_pthread)
	{
		m_pthread->join();

		delete m_pthread;

		m_pthread = NULL;
	}

	if (m_dbconn)
	{
		delete m_dbconn;

		m_dbconn = NULL;
	}
}

void Witness::ThreadProc()
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::ThreadProc start m_dbconn " << (uintptr_t)m_dbconn;

	SmartBuf last_indelible_block;

	while (!last_indelible_block)	// wait for last_indelible_block to be set
	{
		ccsleep(1);

		if (g_shutdown)
			return;

		last_indelible_block = g_blockchain.GetLastIndelibleBlock();
	}

	for (unsigned i = 0; i < MAX_NWITNESSES; ++i)
	{
		m_last_indelible_blocks[i] = last_indelible_block;
		m_highest_witnessed_level[i] = g_blockchain.GetLastIndelibleLevel();	// setting to last_indelible_level helps prevent protocol violations; for this to work, make sure blockchain is sync'ed past the witness's last created block before enabling the witness
	}

	int malcount = 0;
	int nonmalcount = 0;
	int failcount = 0;

	while (IsWitness())
	{
		if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

		if (g_blockchain.HasFatalError())
		{
			BOOST_LOG_TRIVIAL(fatal) << "Witness::ThreadProc exiting due to fatal error in blockchain";

			break;
		}

		if (g_shutdown)
			break;

		auto last_indelible_block = g_blockchain.GetLastIndelibleBlock();
		auto block = (Block*)last_indelible_block.data();
		auto wire = block->WireData();
		auto auxp = block->AuxPtr();

		int sim_nwitnesses = auxp->blockchain_params.nwitnesses;

		// the following code block probably doesn't work anymore...
		if (TEST_SIM_ALL_WITNESSES)
		{
			if (TEST_WITNESS_LOSS && sim_nwitnesses > WITNESS_TEST_LOSS_REQ_WITNESS && (wire->level.GetValue() & TEST_WITNESS_LOSS) == 0)
			{
				usleep(200*1000);	// wait for pending blocks to become indelible

				if (wire->witness < WITNESS_TEST_LOSS_REQ_WITNESS)	// make the test hard by disabling the witness immediately after it generates a indelible block
				{
					sim_nwitnesses = WITNESS_TEST_LOSS_REQ_WITNESS;

					BOOST_LOG_TRIVIAL(info) << "Witness::ThreadProc BuildNewBlock simulating witness loss; sim_nwitnesses now = " << sim_nwitnesses;
				}
			}
			else if (TEST_WITNESS_LOSS && sim_nwitnesses < WITNESS_TEST_LOSS_NWITNESSES_HI && (wire->level.GetValue() & TEST_WITNESS_LOSS) > 0)
			{
				sim_nwitnesses = WITNESS_TEST_LOSS_NWITNESSES_HI;

				BOOST_LOG_TRIVIAL(info) << "Witness::ThreadProc BuildNewBlock simulating witness loss fixed; sim_nwitnesses now = " << sim_nwitnesses;
			}

			while (true)
			{
				if (TEST_RANDOM_WITNESS_ORDER)
					witness_index = rand() % sim_nwitnesses;
				else
					witness_index = (witness_index + 1) % sim_nwitnesses;

				if (!test_mal || !GENESIS_MAXMAL || IsMalTest() || nonmalcount * TEST_MIN_MAL_RATIO < malcount || failcount > sim_nwitnesses)
					break;
			}

			BOOST_LOG_TRIVIAL(info) << "Witness::ThreadProc BuildNewBlock simulating witness " << witness_index << " maltest " << IsMalTest();
		}

		//if (witness_index >= 1 && witness_index <= 1)
		//	continue;	// for testing

		auto failed = AttemptNewBlock();

		if (failed)
			failcount++;
		else
			failcount = 0;

		if (IsMalTest())
			malcount++;
		else
			nonmalcount++;

		if (failcount > 200)
		{
			static uint32_t last_msg_time = 0;	// not thread safe

			if (!last_msg_time || ccticks_elapsed(last_msg_time, ccticks()) >= 20 * CCTICKS_PER_SEC)
			{
				last_msg_time = ccticks();

				const char *msg = "Witness appears stuck";

				BOOST_LOG_TRIVIAL(error) << msg;

				failcount = 0;

				//g_blockchain.SetFatalError(msg);	// for testing
			}
		}

		//usleep(200*1000);	// for testing
		//ccsleep(1);		// for testing

		if (TEST_PAUSE && !(g_blockchain.GetLastIndelibleLevel() & TEST_PAUSE))
		{
			{
				lock_guard<FastSpinLock> lock(g_cout_lock);
				cerr << "Press return to continue..." << endl;
			}

			string in;
			getline(cin, in);
		}
	}

	//witness_index = -1;	// no longer acting as a witness

	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::ThreadProc end m_dbconn " << (uintptr_t)m_dbconn;
}

uint32_t Witness::NextTurnTicks() const
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::NextTurnTicks";

	// stub

	return 0;
}

bool Witness::IsMalTest() const
{
	if (!IsWitness() || !test_mal)
		return false;

	if (TEST_SIM_ALL_WITNESSES && witness_index > 0)
		return false;

	return true;
}

bool Witness::SimLoss(uint64_t level)
{
	auto hash = siphash((const uint8_t *)&level, sizeof(level));

	return !(hash % (TEST_WITNESS_LOSS + 1));
}

uint64_t Witness::FindBestOwnScore(SmartBuf last_indelible_block)
{
	uint64_t bestscore = 0;
	SmartBuf smartobj;

	if (m_test_ignore_order)
		m_dbconn->ProcessQClearValidObjs(PROCESS_Q_TYPE_BLOCK);

	for (unsigned offset = 0; ; ++offset)
	{
		m_dbconn->ProcessQGetNextValidObj(PROCESS_Q_TYPE_BLOCK, offset, &smartobj);

		if (!smartobj)
			break;

		auto block = (Block*)smartobj.data();
		auto wire = block->WireData();
		auto auxp = block->AuxPtr();

		if (wire->witness != witness_index)
			continue;

		auto score = block->CalcSkipScore(-1, last_indelible_block, m_score_genstamp, m_test_ignore_order);
		if (score > bestscore)
			bestscore = score;

		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestOwnScore witness " << (unsigned)wire->witness << " maltest " << m_test_ignore_order << " score " << hex << score << " bestscore " << bestscore << dec << " level " << wire->level.GetValue() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

		if (m_test_ignore_order)
			m_dbconn->ProcessQUpdateValidObj(PROCESS_Q_TYPE_BLOCK, auxp->oid, PROCESS_Q_STATUS_VALID, score);	// for maltest, store score in sqlite; later we'll use this to ensure maltest doesn't generate many blocks with the same score
	}

	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestOwnScore witness " << witness_index << " maltest " << m_test_ignore_order << " returning " << hex << bestscore << dec;

	return bestscore;
}

SmartBuf Witness::FindBestBuildingBlock(SmartBuf last_indelible_block, uint64_t m_highest_witnessed_level, uint64_t bestscore)
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " maltest " << m_test_ignore_order << " highest witnessed level " << m_highest_witnessed_level << " bestscore " << hex << bestscore << dec;

	SmartBuf smartobj, bestobj;

	if (m_test_ignore_order || TEST_BUILD_ON_RANDOM)
		m_dbconn->ProcessQRandomizeValidObjs(PROCESS_Q_TYPE_BLOCK);

	for (unsigned offset = 0; ; ++offset)
	{
		m_dbconn->ProcessQGetNextValidObj(PROCESS_Q_TYPE_BLOCK, offset, &smartobj);

		if (!smartobj)
			break;

		auto block = (Block*)smartobj.data();
		auto wire = block->WireData();
		auto auxp = block->AuxPtr();
		auto level = wire->level.GetValue();

		if (0) //TEST_WITNESS_LOSS && witness_index > WITNESS_TEST_LOSS_REQ_WITNESS && SimLoss(level))
		{
			BOOST_LOG_TRIVIAL(info) << "Witness::FindBestBuildingBlock simulating witness " << witness_index << " loss; will not build on level " << level;

			continue;
		}

		if (m_highest_witnessed_level > level && !m_test_ignore_order)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " highest witnessed level " << m_highest_witnessed_level << " > building block level " << level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			continue;
		}

		if (block->CheckBadSigOrder(witness_index))
		{
			if (m_test_ignore_order)
			{
				BOOST_LOG_TRIVIAL(info) << "Witness::FindBestBuildingBlock witness " << witness_index << " ignoring bad sig order for building block level " << level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));
			}
			else
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " bad sig order building block level " << level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

				continue;
			}
		}

		auto score = block->CalcSkipScore(witness_index, last_indelible_block, m_score_genstamp, m_test_ignore_order);

		if (bestscore >= score && !m_test_ignore_order)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " bestscore " << hex << bestscore << " >= score " << score << dec << " for building block level " << level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			continue;
		}

		if (m_test_ignore_order)
		{
			auto count = m_dbconn->ProcessQCountValidObjs(PROCESS_Q_TYPE_BLOCK, score);

			if (count >= auxp->blockchain_params.maxmal + 1)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " maltest true count " << count << " for score " << hex << score << dec;

				continue;
			}
		}

		bestobj = smartobj;
		bestscore = score;

		if (m_test_ignore_order)
			break;		// take the first block

		if (score && TEST_BUILD_ON_RANDOM)
			break;		// take the first legal block
	}

	if (bestobj && TRACE_WITNESS)
	{
		auto block = (Block*)bestobj.data();
		auto wire = block->WireData();
		auto auxp = block->AuxPtr();
		auto level = wire->level.GetValue();

		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " returning best score " << hex << bestscore << dec << " best block level " << level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));
	}
	else if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " found no building block";

	return bestobj;
}

bool Witness::AttemptNewBlock()
{
	SmartBuf priorobj, bestblock;
	uint64_t priorlevel = 0;
	bool is_ccmint = false;

	uint32_t min_time = 0;
	uint32_t max_time = 0;

	while (true)
	{
		if (g_shutdown)
			return true;

		uint64_t last_indelible_level = 0;

		while (!bestblock || HaveNewBlockWork())	// use break to exit
		{
			ResetNewBlockWork();

			SmartBuf last_indelible_block;

			if (TEST_DELAY_LAST_INDELIBLE)
				last_indelible_block = m_last_indelible_blocks[TEST_SIM_ALL_WITNESSES * witness_index];

			if (!last_indelible_block)
				last_indelible_block = g_blockchain.GetLastIndelibleBlock();

			auto block = (Block*)last_indelible_block.data();
			auto wire = block->WireData();
			auto auxp = block->AuxPtr();

			last_indelible_level = wire->level.GetValue();

			if (TEST_WITNESS_LOSS && witness_index > WITNESS_TEST_LOSS_REQ_WITNESS && SimLoss(last_indelible_level))
			{
				BOOST_LOG_TRIVIAL(info) << "Witness::AttemptNewBlock simulating witness " << witness_index << " loss; req witness " << WITNESS_TEST_LOSS_REQ_WITNESS << " last_indelible_level " << last_indelible_level;

				break;
			}

			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::AttemptNewBlock looking for best prior block witness " << witness_index << " maltest " << m_test_ignore_order << " last indelible block witness " << (unsigned)wire->witness << " level " << wire->level.GetValue() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			static bool last_maltest = false;	// not thread safe

			if (last_indelible_block != m_last_last_indelible_block || m_test_ignore_order != last_maltest)
			{
				m_last_last_indelible_block = last_indelible_block;
				last_maltest = m_test_ignore_order;

				if (++m_score_genstamp == 0)
					++m_score_genstamp;
			}

			auto bestscore = FindBestOwnScore(last_indelible_block);

			bestblock = FindBestBuildingBlock(last_indelible_block, m_highest_witnessed_level[TEST_SIM_ALL_WITNESSES * witness_index], bestscore);

			break;
		}

		if (!bestblock)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(debug) << "Witness::AttemptNewBlock no building block found for witness " << witness_index << " maltest " << m_test_ignore_order;

			if (TEST_SIM_ALL_WITNESSES)
				return true;

			if (TEST_DELAY_LAST_INDELIBLE)
				m_last_indelible_blocks[TEST_SIM_ALL_WITNESSES * witness_index] = g_blockchain.GetLastIndelibleBlock();

			WaitForWork(true, false, 0);

			continue;	// retry with new blocks
		}

		//cerr << "priorobj " << priorobj.BasePtr() << " bestblock " << bestblock.BasePtr() << " priorobj != bestblock " << (priorobj != bestblock) << endl;

		if (priorobj != bestblock)
		{
			priorobj = bestblock;

			auto block = (Block*)priorobj.data();
			auto wire = block->WireData();
			auto auxp = block->AuxPtr();
			auto skip = Block::ComputeSkip(wire->witness, witness_index, auxp->blockchain_params.next_nwitnesses);

			priorlevel = wire->level.GetValue();

			is_ccmint = (Implement_CCMint(g_params.blockchain) && priorlevel < CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN);

			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::AttemptNewBlock best prior block witness " << witness_index << " is_ccmint " << is_ccmint << " maltest " << m_test_ignore_order << " best block witness " << (unsigned)wire->witness << " skip " << skip << " level " << priorlevel << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			StartNewBlock();

			auto now = ccticks();
			min_time = now;
			max_time = now;

			if (ccticks_elapsed(now, auxp->announce_ticks) <= 0)
				min_time = auxp->announce_ticks;

			if (is_ccmint)
			{
				min_time += (CC_MINT_BLOCK_SEC + CC_MINT_BLOCK_SEC/2) * CCTICKS_PER_SEC;
				min_time /= CC_MINT_BLOCK_SEC * CCTICKS_PER_SEC;
				min_time *= CC_MINT_BLOCK_SEC * CCTICKS_PER_SEC;

				max_time = now - 1 + (1 << 30);
			}
			else
			{
				if (WITNESS_RANDOM_TEST_TIME <= 0)
					min_time += (skip + 1) * WITNESS_TIME_SPACING;
				else
					min_time += rand() % (WITNESS_RANDOM_TEST_TIME * 2);	// double the input value so it is an average time

				if (ccticks_elapsed(m_block_start_time, min_time) <= MIN_BLOCK_WORK_TIME && WITNESS_RANDOM_TEST_TIME < 0)
					min_time = m_block_start_time + MIN_BLOCK_WORK_TIME;

				auto delibletxs = g_blockchain.ChainHasDelibleTxs(priorobj, last_indelible_level);
				if (!delibletxs && WITNESS_RANDOM_TEST_TIME <= 0)
					max_time = g_blockchain.GetLastIndelibleTicks() + WITNESS_NO_WORK_TIME_SPACING + skip * WITNESS_TIME_SPACING;
			}

			if (ccticks_elapsed(min_time, max_time) <= 0)
				max_time = min_time;

			//lock_guard<FastSpinLock> lock(g_cout_lock);
			//cerr << "GetLastIndelibleTicks " << g_blockchain.GetLastIndelibleTicks() << " announce " << auxp->announce_ticks << " now " << now << " diff " << ccticks_elapsed(auxp->announce_ticks, now) << " skip " << skip << " spacing " << WITNESS_TIME_SPACING << " min_time " << min_time << " diff " << ccticks_elapsed(now, min_time) << " max_time " << max_time << " diff " << ccticks_elapsed(min_time, max_time) << endl;
		}

		CCASSERT(priorobj);
		CCASSERT(min_time);
		CCASSERT(max_time);

		while (is_ccmint && priorlevel && priorlevel < CC_MINT_COUNT && priorlevel >= max_mint_level.load())
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(debug) << "Witness::AttemptNewBlock BuildNewBlock witness " << WitnessIndex() << " waiting for max_mint_level; priorlevel " << priorlevel << " max_mint_level " << max_mint_level.load();

			ccsleep(1);	// don't build a new block until there's a valid mint tx at the prior level

			if (WitnessIndex() || g_shutdown)
				return true;
		}

		auto rc = BuildNewBlock(min_time, max_time, priorobj, priorlevel);

		if (rc == BUILD_NEWBLOCK_STATUS_ERROR)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(debug) << "Witness::AttemptNewBlock BuildNewBlock could not create a new block; maltest " << m_test_ignore_order;

			return true;
		}

		if (rc == BUILD_NEWBLOCK_STATUS_FULL)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(debug) << "Witness::AttemptNewBlock BuildNewBlock is full";

			while (is_ccmint && ccticks_elapsed(ccticks(), min_time) > CCTICKS_PER_SEC/2 && !g_shutdown)
				ccsleep(1);

			break;
		}

		if (!WaitForWork(true, true, (m_newblock_bufpos ? min_time : max_time)))
			break;

		if (HaveNewBlockWork())
			continue;	// check to restart with a different prior block

		auto now = ccticks();

		//lock_guard<FastSpinLock> lock(g_cout_lock);
		//cerr << "GetLastIndelibleTicks " << g_blockchain.GetLastIndelibleTicks() << " now " << now << " min_time " << min_time << " diff " << ccticks_elapsed(now, min_time) << " max_time " << max_time << " diff " << ccticks_elapsed(min_time, max_time) << endl;

		if (ccticks_elapsed(now, (m_newblock_bufpos ? min_time : max_time)) <= 0)
			break;
	}

	if (g_shutdown)
		return true;

	if (is_ccmint && (!priorlevel || priorlevel >= CC_MINT_ACCEPT_SPAN) && !m_newblock_bufpos)
		return true;	// don't build a block without a mint tx

	auto smartobj = FinishNewBlock(priorobj);

	if (!smartobj)
	{
		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(debug) << "Witness::AttemptNewBlock FinishNewBlock could not create a new block by witness " << witness_index << " maltest " << m_test_ignore_order;

		return true;
	}

	auto block = (Block*)smartobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	auxp->total_donations = m_total_donations;

	if (m_test_ignore_order)
		m_test_ignore_order = block->CheckBadSigOrder(-1);

	if (!m_test_is_double_spend && !m_test_ignore_order)
		m_highest_witnessed_level[TEST_SIM_ALL_WITNESSES * witness_index] = wire->level.GetValue();

	block->ConsoleAnnounce("  created", wire, auxp, (m_test_is_double_spend ? " double-spend" : ""), (m_test_ignore_order ? " bad-order" : ""));

	relay_request_wire_params_t req_params;
	memset(&req_params, 0, sizeof(req_params));
	memcpy(&req_params.oid, &auxp->oid, sizeof(ccoid_t));

	m_dbconn->RelayObjsInsert(0, CC_TYPE_BLOCK, req_params, RELAY_STATUS_DOWNLOADED, 0);	// so we don't download it after sending it to someone else

	if (TEST_PROCESS_Q)
	{
		// test blocks going through the Process_Q
		m_dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_BLOCK, smartobj, &wire->prior_oid, wire->level.GetValue(), PROCESS_Q_STATUS_PENDING, -1000, false, 0, 0);
		// the new block needs to be placed into the blockchain before this witness attempts to create another block
		// since we're not normally using this code path, we'll do this the easy way by just sleeping a little and hoping Process_Q is finished by then
		usleep(200*1000);
	}
	else
	{
		bool isvalid = true;

		if (m_test_is_double_spend || m_test_ignore_order)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(debug) << "Witness::AttemptNewBlock witness " << witness_index << " block with double-spend " << m_test_is_double_spend << " bad sig order " << m_test_ignore_order << " won't be considered when building additional blocks, level " << wire->level.GetValue() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			isvalid = false;
		}

		g_processblock.ValidObjsBlockInsert(m_dbconn, smartobj, m_txbuf, isvalid, isvalid);
	}

	return false;
}

void Witness::StartNewBlock()
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::StartNewBlock";

	m_block_start_time = ccticks();
	m_newblock_bufpos = 0;
	m_newblock_next_tx_seqnum = 1;
	m_total_donations = 0UL;

	m_test_ignore_order = false;
	m_test_try_persistent_double_spend = false;
	m_test_try_inter_double_spend = false;
	m_test_try_intra_double_spend = false;
	m_test_is_double_spend = false;

	if (IsMalTest() && (GENESIS_MAXMAL > 0 || TEST_MAL_IGNORE_SIG_ORDER))
	{
		if (RandTest(8))
			m_test_ignore_order = true;				// add param for this?
	}

	if (IsMalTest())
	{
		if (RandTest(8))
			m_test_try_persistent_double_spend = true;
		else if (RandTest(8))
			m_test_try_inter_double_spend = true;
		else if (RandTest(4))
			m_test_try_intra_double_spend = true;
	}

	m_dbconn->TempSerialnumClear((void*)TEMP_SERIALS_WITNESS_BLOCKP);
}

Witness::BuildNewBlockStatus Witness::BuildNewBlock(uint32_t& min_time, uint32_t max_time, SmartBuf priorobj, uint64_t priorlevel)
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock min_time " << min_time << " max_time " << max_time << " priorlevel " << priorlevel;

	BuildNewBlockStatus build_status = BUILD_NEWBLOCK_STATUS_OK;

	auto output = m_blockbuf.data();
	CCASSERT(output);

	const uint32_t bufsize = TEST_SMALL_BUFS ? 2*1024 : (CC_BLOCK_MAX_SIZE - sizeof(CCObject::Header) - sizeof(BlockWireHeader));
	CCASSERT(m_blockbuf.size() >= bufsize);

	const int TXARRAYSIZE = TEST_SMALL_BUFS ? 5 : 100;
	static array<SmartBuf, TXARRAYSIZE> txarray;	// not thread safe

	bool test_no_delete_persistent_txs = IsMalTest();

	bool is_ccmint = (Implement_CCMint(g_params.blockchain) && priorlevel < CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN);

	while (!g_shutdown)
	{
		SetNewTxWork(false);

		unsigned ntx;

		if (is_ccmint)
			ntx = FindBestMintTx(m_dbconn, priorobj, priorlevel, &txarray[0]);
		else
			ntx = m_dbconn->ValidObjsFindNew(m_newblock_next_tx_seqnum, TXARRAYSIZE, false, (uint8_t*)&txarray, TXARRAYSIZE);

		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " fetched " << ntx << " potential tx's";

		if (ntx >= TXARRAYSIZE)
			SetNewTxWork(true);		// still more there

		for (unsigned i = 0; i < ntx; ++i)
		{
			auto smartobj = txarray[i];
			txarray[i].ClearRef();

			auto bufp = smartobj.BasePtr();
			auto obj = (CCObject*)smartobj.data();
			auto type = obj->ObjType();

			if (type != CC_TYPE_TXPAY && type != CC_TYPE_MINT)
			{
				BOOST_LOG_TRIVIAL(error) << "Witness::BuildNewBlock obj type " << type << " bufp " << (uintptr_t)bufp;

				continue;
			}

			if (Implement_CCMint(g_params.blockchain) && type == CC_TYPE_MINT && !is_ccmint)
				continue;

			auto txwire = obj->ObjPtr();
			auto txsize = obj->ObjSize();
			auto txbody = obj->BodyPtr();
			auto bodysize = obj->BodySize();
			uint32_t newsize = bodysize + 2 * sizeof(uint32_t);				// need space for tag and size

			if (m_newblock_bufpos + newsize > bufsize)
			{
				build_status = BUILD_NEWBLOCK_STATUS_FULL;

				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " skipping tx bufp " << (uintptr_t)bufp << " because it doesn't fit ";

				continue;	// keep looking for a smaller tx
			}

			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " checking tx bufp " << (uintptr_t)bufp << " size " << txsize;

			auto rc = tx_from_wire(m_txbuf, (char*)txwire, txsize);
			if (rc)
				continue;

			g_blockchain.CheckCreatePseudoSerialnum(m_txbuf, txwire, txsize);

			bool badserial = 0;

			for (unsigned i = 0; i < m_txbuf.nin; ++i)
			{
				auto rc = g_blockchain.CheckSerialnum(m_dbconn, priorobj, TEMP_SERIALS_WITNESS_BLOCKP, (test_no_delete_persistent_txs ? SmartBuf() : smartobj), &m_txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES);
				if (rc)
				{
					badserial = rc;

					if (rc == 4 && m_test_try_persistent_double_spend)
					{
						m_test_is_double_spend = true;
						m_test_try_persistent_double_spend = false;
					}

					if (rc == 3 && m_test_try_inter_double_spend)
					{
						m_test_is_double_spend = true;
						m_test_try_inter_double_spend = false;
					}

					if (rc == 2 && m_test_try_intra_double_spend)
					{
						m_test_is_double_spend = true;
						m_test_try_intra_double_spend = false;
					}

					break;
				}
			}

			if (badserial && !m_test_is_double_spend)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " skipping tx bufp " << (uintptr_t)bufp << " with bad serialnum status " << badserial;

				continue;
			}

			int badinsert = 0;

			for (unsigned i = 0; i < m_txbuf.nin; ++i)
			{
				// if the witness accepts tx's with duplicate serialnums (which it does for maltest),
				// we can end up with extra serialnums in the tempdb that were put there before the duplicate was detected and the tx rejected
				// that can result in the later rejection of a valid block from another witness that contains the same serialnum so it appears to be a double-spend
				// but the non-mal witnesses won't have this problem, and they will accept the valid block

				auto rc = m_dbconn->TempSerialnumInsert(&m_txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES, (void*)TEMP_SERIALS_WITNESS_BLOCKP);
				if (rc)
				{
					badinsert = rc;

					if (m_test_try_intra_double_spend)
					{
						m_test_is_double_spend = true;
						m_test_try_intra_double_spend = false;
					}

					break;
				}
			}

			if (badinsert && !m_test_is_double_spend)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " skipping tx bufp " << (uintptr_t)bufp << " due to TempSerialnumInsert failure " << badinsert;

				continue;
			}

			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " adding tx bufp " << (uintptr_t)bufp << " size " << newsize;

			//cerr << "ntx " << ntx << " adding txarray[" << i << "] bufp " << (uintptr_t)bufp << " obj " << (uintptr_t)obj << " body " << (uintptr_t)txbody << " size = " << newsize << endl;
			//cerr << "ntx " << ntx << " adding txarray[" << i << "] bufp " << (uintptr_t)bufp << " size " << newsize << endl;

			if (!m_newblock_bufpos)
			{
				auto now = ccticks();
				if (ccticks_elapsed(now, min_time) <= MIN_BLOCK_WORK_TIME && WITNESS_RANDOM_TEST_TIME <= 0)
				{
					min_time = now + MIN_BLOCK_WORK_TIME;

					if (ccticks_elapsed(min_time, max_time) <= 0)
						max_time = min_time;
				}

				if (TEST_UNRECOGNIZED_TAG && !witness_index)
				{
					uint32_t size = 2*sizeof(uint32_t);
					copy_to_buf(size, sizeof(size), m_newblock_bufpos, output, bufsize);
					copy_to_buf(size, sizeof(size), m_newblock_bufpos, output, bufsize);

					lock_guard<FastSpinLock> lock(g_cout_lock);
					cerr << "testing unrecognized tag" << endl;
				}
			}

			uint32_t newtag = 0;

			if (m_txbuf.tag_type == CC_TYPE_MINT)
				newtag = CC_TAG_MINT_BLOCK;
			else if (m_txbuf.tag_type == CC_TYPE_TXPAY)
			{
				newtag = CC_TAG_TX_BLOCK;

				bigint_t donation;
				tx_amount_decode(m_txbuf.donation_fp, donation, true, TX_DONATION_BITS, TX_AMOUNT_EXPONENT_BITS);

				m_total_donations = m_total_donations + donation;
			}
			else
			{
				CCASSERT(newtag);
				CCASSERT(0);
			}

			copy_to_buf(newsize, sizeof(newsize), m_newblock_bufpos, output, bufsize);
			copy_to_buf(newtag, sizeof(newtag), m_newblock_bufpos, output, bufsize);
			copy_to_bufp(txbody, bodysize, m_newblock_bufpos, output, bufsize);
		}

		if (is_ccmint && (m_newblock_bufpos || (priorlevel && priorlevel < CC_MINT_ACCEPT_SPAN)))
		{
			build_status = BUILD_NEWBLOCK_STATUS_FULL;
			break;
		}

		if (is_ccmint && ntx)
			continue;

		if (HaveNewBlockWork())
			break;

		if (!HaveNewTxWork())
			break;

		if (ccticks_elapsed(ccticks(), (m_newblock_bufpos ? min_time : max_time)) <= 0)
			break;
	}

	if (m_newblock_bufpos > bufsize)
	{
		BOOST_LOG_TRIVIAL(error) << "Witness::BuildNewBlock witness " << witness_index << " buffer overflow m_newblock_bufpos " << m_newblock_bufpos << " bufsize " << bufsize;

		return BUILD_NEWBLOCK_STATUS_ERROR;
	}

	return build_status;
}

SmartBuf Witness::FinishNewBlock(SmartBuf priorobj)
{
	SmartBuf smartobj;

	auto priorblock = (Block*)priorobj.data();
	CCASSERT(priorblock);

	auto priorwire = priorblock->WireData();
	auto priorauxp = priorblock->AuxPtr();

	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FinishNewBlock witness " << witness_index << " prior block level " << priorwire->level.GetValue() << " oid " << buf2hex(&priorauxp->oid, sizeof(ccoid_t));

	CCASSERT(witness_index >= 0);

	if (witness_index >= priorauxp->blockchain_params.next_nwitnesses)
	{
		BOOST_LOG_TRIVIAL(info) << "Witness::FinishNewBlock skipping witness " << witness_index << " because it exceeds block prior block next_nwitnesses " << priorauxp->blockchain_params.next_nwitnesses;

		return SmartBuf();
	}

	// copy block to SmartBuf

	auto objsize = m_newblock_bufpos + sizeof(CCObject::Header) + sizeof(BlockWireHeader);

	smartobj = SmartBuf(objsize + sizeof(CCObject::Preamble));
	if (!smartobj)
	{
		BOOST_LOG_TRIVIAL(error) << "Witness::FinishNewBlock witness " << witness_index << " smartobj failed";

		return SmartBuf();
	}

	auto block = (Block*)smartobj.data();
	CCASSERT(block);

	block->SetTag(CC_TAG_BLOCK);
	block->SetSize(objsize);

	auto wire = block->WireData();

	auto level = priorwire->level.GetValue() + 1;
	auto timestamp = time(NULL);
	int64_t delta = timestamp - priorwire->timestamp.GetValue();
	if (delta < 0)
		timestamp = priorwire->timestamp.GetValue();
	if (IsMalTest() && level % 100 < 10)
		timestamp += 200;

	memcpy(&wire->prior_oid, &priorauxp->oid, sizeof(ccoid_t));
	wire->timestamp.SetValue(timestamp);
	wire->level.SetValue(level);
	wire->witness = witness_index;

	if (m_newblock_bufpos)
	{
		//if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FinishNewBlock adding m_txbuf's at txdata " << (uintptr_t)block->TxData() << " size " << m_newblock_bufpos << " data " << buf2hex(output, 16);
		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FinishNewBlock adding tx's size " << m_newblock_bufpos << " to block level " << wire->level.GetValue() << " bufp " << (uintptr_t)smartobj.BasePtr() << " prior bufp " << (uintptr_t)priorobj.BasePtr() << " prior oid " << buf2hex(&priorauxp->oid, sizeof(ccoid_t));

		memcpy(block->TxData(), m_blockbuf.data(), m_newblock_bufpos);
	}

	auto auxp = block->SetupAuxBuf(smartobj, true);
	if (!auxp)
	{
		BOOST_LOG_TRIVIAL(error) << "Witness::FinishNewBlock witness " << witness_index << " SetupAuxBuf failed";

		return SmartBuf();
	}

	block->ChainToPriorBlock(priorobj);

#if ROTATE_BLOCK_SIGNING_KEYS
	int keynum = 0;
	if (TEST_SIM_ALL_WITNESSES)
		keynum = witness_index;

	CCRandom(&auxp->witness_params.next_signing_private_key[keynum], sizeof(auxp->witness_params.next_signing_private_key[keynum]));
	ed25519_publickey(&auxp->witness_params.next_signing_private_key[keynum][0], &auxp->blockchain_params.signing_keys[witness_index][0]);
	memcpy(&wire->witness_next_signing_public_key, &auxp->blockchain_params.signing_keys[witness_index], sizeof(wire->witness_next_signing_public_key));
#endif

	block_hash_t block_hash;
	block->CalcHash(block_hash);
	auxp->SetHash(block_hash);

	block->SignOrVerify(false);

	if (block->SignOrVerify(true))
	{
		BOOST_LOG_TRIVIAL(error) << "Witness::FinishNewBlock witness " << witness_index << " verify own signature failed";

		const char *msg = "Verify own signature failed --> this server will stop acting as a witness.";

		BOOST_LOG_TRIVIAL(error) << "Witness::FinishNewBlock " << msg;

		{
			lock_guard<FastSpinLock> lock(g_cout_lock);
			cerr << "ERROR: " << msg << endl;
		}

		witness_index = -1;	// stop acting as a witness, so we don't get this message again

		return SmartBuf();
	}

	ccoid_t oid;
	block->CalcOid(block_hash, oid);
	auxp->SetOid(oid);

	if (m_dbconn->TempSerialnumUpdate((void*)TEMP_SERIALS_WITNESS_BLOCKP, smartobj.BasePtr(), wire->level.GetValue()))
	{
		BOOST_LOG_TRIVIAL(error) << "Witness::FinishNewBlock witness " << witness_index << " TempSerialnumUpdate failed";

		return SmartBuf();
	}

	BOOST_LOG_TRIVIAL(info) << "Witness::FinishNewBlock built new block level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " size " << block->ObjSize() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior oid " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

	if (m_test_is_double_spend)
		BOOST_LOG_TRIVIAL(info) << "Witness::FinishNewBlock built double-spend test block level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " size " << block->ObjSize() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior oid " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

	return smartobj;
}

bool Witness::IsSoleWitness() const
{
	if (witness_index)
		return false;

	if (TEST_SIM_ALL_WITNESSES)
		return true;

	auto last_indelible_block = g_blockchain.GetLastIndelibleBlock();
	auto block = (Block*)last_indelible_block.data();
	auto auxp = block->AuxPtr();
	auto nwitnesses = auxp->blockchain_params.nwitnesses;

	return (nwitnesses == 1);
}

void Witness::NotifyNewWork(bool is_block)
{
	if (!IsWitness())
		return;

	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::NotifyNewWork is_block " << is_block << " waiting_on_block " << m_waiting_on_block << " waiting_on_tx " << m_waiting_on_tx;

	lock_guard<mutex> lock(m_work_mutex);

	if (is_block)
	{
		m_have_new_block = true;

		if (!m_waiting_on_block)
			return;
	}
	else
	{
		m_have_new_tx = true;

		if (!m_waiting_on_tx)
			return;
	}

	m_work_condition_variable.notify_one();
}

void Witness::ShutdownWork()
{
	lock_guard<mutex> lock(m_work_mutex);

	m_work_condition_variable.notify_all();
}

int Witness::WaitForWork(bool bwait4block, bool bwait4tx, uint32_t target_time)
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork for block " << bwait4block << " for tx " << bwait4tx << " target_time " << target_time;

	unique_lock<mutex> lock(m_work_mutex, defer_lock);

	bool needs_lock = true;

	while (!g_shutdown)
	{
		if (bwait4block && HaveNewBlockWork())
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork HaveNewBlockWork true";

			return 1;
		}

		if (bwait4tx && HaveNewTxWork())
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork HaveNewTxWork true GetNextTxSeqnum() " << DbConnValidObjs::GetNextTxSeqnum() << " m_newblock_next_tx_seqnum " << m_newblock_next_tx_seqnum;

			return 1;
		}

		if (needs_lock)
		{
			lock.lock();

			needs_lock = false;

			continue;		// recheck with lock
		}

		int elapse = -1;

		if (target_time)
		{
			auto now = ccticks();
			elapse = ccticks_elapsed(now, target_time);
			if (elapse <= 1)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork target time " << target_time << " reached at " << now;

				return 0;
			}
		}

		m_waiting_on_block = bwait4block;
		m_waiting_on_tx = bwait4tx;

		#if CCTICKS_PER_SEC != 1000
		#error fix wait: units != milliseconds
		#endif

		if (target_time)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork waiting for " << elapse << " milliseconds";

			m_work_condition_variable.wait_for(lock, chrono::milliseconds(elapse));
		}
		else
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork waiting";

			m_work_condition_variable.wait(lock);
		}

		m_waiting_on_block = false;
		m_waiting_on_tx = false;

		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork rechecking conditions at " << ccticks();
	}

	return 0;
}