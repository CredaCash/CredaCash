/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * btc_block.hpp
*/

#pragma once

class DbConn;

class BtcBlock
{
	uint64_t m_current_block;
	bool m_reported;
	bool m_used;
	FastSpinLock m_lock;

public:
	BtcBlock()
	 :	m_current_block(0),
		m_reported(true),
		m_used(false)
	{ }

	int Init(DbConn *dbconn);

	static unsigned BlockIncrement();

	uint64_t GetCurrentBlock();
	uint64_t FinishCurrentBlock();
};
