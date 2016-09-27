/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * processblock.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <transaction.hpp>

#include <thread>

class ProcessBlock
{
	thread *m_thread;

	atomic<uint32_t> m_last_block_ticks;

	void ThreadProc();
	void CheckpointThreadProc();

public:

	ProcessBlock()
	:	m_thread(NULL),
		m_last_block_ticks(0)
	{ }

	void Init();
	void DeInit();

	int BlockValidate(DbConn *dbconn, SmartBuf smartobj, struct TxPay &txbuf);
	void ValidObjsBlockInsert(DbConn *dbconn, SmartBuf smartobj, struct TxPay &txbuf, bool enqueue = false, bool check_indelible = true);

	uint32_t GetLastBlockTicks() const
	{
		return m_last_block_ticks.load();
	}
};

extern ProcessBlock g_processblock;
