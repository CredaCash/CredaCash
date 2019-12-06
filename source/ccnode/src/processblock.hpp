/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * processblock.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <transaction.hpp>

#include <thread>

struct TxPay;

class ProcessBlock
{
	thread *m_thread;

	atomic<uint32_t> m_last_network_ticks;
	atomic<uint32_t> m_last_block_ticks;

	void ThreadProc();
	void CheckpointThreadProc();

public:

	ProcessBlock()
	:	m_thread(NULL),
		m_last_network_ticks(0),
		m_last_block_ticks(0)
	{ }

	void Init();
	void Stop();
	void DeInit();

	int ExtractTx(const char *wire, const uint32_t txsize, SmartBuf& smartobj);
	int CompareBinaryTxs(SmartBuf smartobj1, SmartBuf smartobj2);

	int BlockValidate(DbConn *dbconn, SmartBuf smartobj, TxPay& txbuf);
	void ValidObjsBlockInsert(DbConn *dbconn, SmartBuf smartobj, TxPay& txbuf, bool enqueue = false, bool check_indelible = true);

	uint32_t GetLastNetworkTime() const
	{
		return m_last_network_ticks.load();
	}

	uint32_t GetLastBlockTicks() const
	{
		return m_last_block_ticks.load();
	}
};

extern ProcessBlock g_processblock;
