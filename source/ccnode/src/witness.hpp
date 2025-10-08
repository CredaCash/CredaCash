/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * witness.hpp
*/

#pragma once

#include "block.hpp"
#include "dbconn.hpp"
#include "seqnum.hpp"

#include <transaction.hpp>
#include <SmartBuf.hpp>
#include <SpinLock.hpp>

class Witness
{
	thread *m_pthread;
	DbConn *m_dbconn;
	SmartBuf m_blockbuf;
	TxPay m_txbuf;

	uint64_t m_first_allowed_tx_level;

	uint32_t m_block_start_time;
	uint32_t m_newblock_bufpos;
	int64_t m_newblock_next_seqnum[NSEQOBJ];

	bool m_test_ignore_order;
	bool m_test_try_persistent_double_spend;
	bool m_test_try_inter_double_spend;
	bool m_test_try_intra_double_spend;
	bool m_test_is_double_spend;

	array<uint64_t, MAX_NWITNESSES> m_highest_witnessed_level;
	array<SmartBuf, MAX_NWITNESSES> m_last_indelible_blocks;
	SmartBuf m_last_last_indelible_block;
	uint16_t m_score_genstamp = 0;

	void ThreadProc();

	uint32_t NextTurnTicks() const;

	bool AttemptNewBlock();
	uint64_t FindBestOwnScore(SmartBuf last_indelible_block);
	SmartBuf FindBestBuildingBlock(SmartBuf last_indelible_block, uint64_t m_highest_witnessed_level, uint64_t bestscore);

	enum BuildNewBlockStatus
	{
		BUILD_NEWBLOCK_STATUS_ERROR,
		BUILD_NEWBLOCK_STATUS_OK,
		BUILD_NEWBLOCK_STATUS_FULL
	};

	void StartNewBlock();
	BuildNewBlockStatus BuildNewBlock(uint32_t& min_time, uint32_t max_time, SmartBuf priorobj, uint64_t priorlevel, unsigned nconfsigs, uint64_t last_indelible_level);
	SmartBuf FinishNewBlock(SmartBuf priorobj);

	mutex m_work_mutex;
	condition_variable m_work_condition_variable;

	atomic_bool m_have_new_work[NSEQOBJ];
	atomic_bool m_waiting_on_block;
	atomic_bool m_waiting_on_tx;

	atomic<uint64_t> m_exchange_work_time;
	atomic<uint64_t> m_pending_exchange_work_time;
	FastSpinLock m_pending_exchange_work_lock;

	void ResetWork(unsigned type, bool value = false)
	{
		CCASSERT(type < NSEQOBJ);

		m_have_new_work[type] = value;
	}

	bool HaveWork(unsigned type) const
	{
		CCASSERT(type < NSEQOBJ);

		return m_have_new_work[type];
	}

	int WaitForWork(bool bwait4block, bool bwait4tx, uint32_t exchange_time, uint32_t max_time);

	void ShutdownWork();

public:
	int witness_index;
	int block_time_ms;
	int block_min_work_ms;
	int block_max_time;
	int test_block_random_ms;
	bool test_mal;

	Witness();

	void Init();
	void DeInit();

	inline bool IsWitness() const
	{
		return witness_index >= 0;
	}

	inline int WitnessIndex() const
	{
		return witness_index;
	}

	bool IsSoleWitness() const;

	bool IsMalTest() const;
	static bool SimLoss(uint64_t level);

	void NotifyNewWork(unsigned type);

	void ResetExchangeWorkTime(bool freeze = false);
	void UpdateExchangeWorkTime(uint64_t timestamp, bool bnotify = false);
};

extern Witness g_witness;

inline bool IsWitness()
{
	return g_witness.IsWitness();
}