/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * btc_block.hpp
*/

#pragma once

#include <SpinLock.hpp>

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
		m_used(false),
		m_lock(__FILE__, __LINE__)
	{ }

	int Init(DbConn *dbconn);

	static unsigned BlockIncrement();

	uint64_t GetCurrentBlock();
	uint64_t FinishCurrentBlock();
};
