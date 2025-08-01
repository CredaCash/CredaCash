/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * exchange.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <CCparams.h>
#include <transaction.h>

class Exchange
{
	atomic<uint64_t> m_next_xreqnum;
	atomic<uint64_t> m_next_xmatchnum;
	atomic_flag m_saved;

public:

	Exchange()
	 :	m_next_xreqnum(0),
		m_next_xmatchnum(0)
	{
		m_saved.test_and_set();
	}

	void Init(DbConn *dbconn);
	void DeInit();

	void Restore(DbConn *dbconn);

	uint64_t GetNextXreqnum(bool increment = false);
	uint64_t GetNextXmatchnum(bool increment = false);

	int SaveNextNums(DbConn *dbconn, uint64_t level, uint64_t timestamp);
};

extern Exchange g_exchange;
