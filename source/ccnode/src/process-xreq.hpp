/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * process-xreq.hpp
*/

#pragma once

#include <CCobjdefs.h>
#include <CCbigint.hpp>

//#define DEBUG_SYNC	1

#ifndef DEBUG_SYNC
#define DEBUG_SYNC		0
#endif

#if DEBUG_SYNC
#define LOG_SYNC_DEBUG	info
#define LOG_SYNC_TRACE	info
#else
#define LOG_SYNC_DEBUG	debug
#define LOG_SYNC_TRACE	trace
#endif


class DbConn;
class Xreq;
class Xmatch;
class UniFloat;
struct TxPay;

class ProcessXreqs
{
	friend class BlockChain;

	static int ExpireXreq(DbConn *dbconn, Xreq& xreq, TxPay& txbuf);

	static int ExpireXreqs(DbConn *dbconn, const uint64_t block_time, TxPay& txbuf);
	static int PruneXreqs(DbConn *dbconn, const uint64_t new_xreqnum, TxPay& txbuf);

	bool FindMutualMatches(DbConn *dbconn, const uint64_t passnum, uint64_t &next_match_index, const uint64_t block_time, const bool for_witness);
	void UpdateMutualMatch(DbConn *dbconn, Xreq& xreq, const Xreq& other, const snarkfront::bigint_t& match_amount, const UniFloat& match_rate, uint64_t passnum, uint64_t block_time, uint64_t hold, bool for_witness);

	int MakeMatchesPersistent(DbConn *dbconn, const uint64_t block_time, TxPay& txbuf);

	uint64_t m_last_matched_block_time;
	atomic<uint64_t> m_matching_block_time;
	atomic<uint64_t> m_matching_max_xreqnum;
	atomic<int> m_matching_state;

	const int MATCHING_STATE_IDLE = 0;
	const int MATCHING_STATE_START = 1;

	uint64_t m_last_matching_epoch;

	mutex m_matching_mutex;
	condition_variable m_matching_condition_variable;
	thread *m_thread;

	void MatchingThread();
	void WaitForCondition(int condition);
	void SetCondition(int condition);

	void CheckUpdateWitnessWorkTime(const uint64_t block_time);

public:

	ProcessXreqs()
	 : m_last_matched_block_time(0),
	   m_matching_block_time(0),
	   m_matching_max_xreqnum(0),
	   m_matching_state(-1),
	   m_thread(NULL)
	{ }

	int AddPendingRequest(DbConn *dbconn, TxPay& tx, const int64_t seqnum, const ccoid_t *objid);
	int AddRequest(DbConn *dbconn, Xreq& xreq);

	int Init(DbConn *dbconn, const uint64_t block_level, const uint64_t block_time);
	void DeInit();

	int SynchronizeMatching(DbConn *dbconn, const uint64_t block_level, const uint64_t block_time, const uint64_t new_xreqnum, TxPay& txbuf);

	void MatchReqs(DbConn *dbconn, const uint64_t block_time, const uint64_t max_xreqnum, const bool for_witness = false);
};

extern ProcessXreqs g_process_xreqs;
