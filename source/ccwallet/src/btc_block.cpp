/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * btc_block.cpp
*/

#include "ccwallet.h"
#include "btc_block.hpp"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "walletdb.hpp"

unsigned BtcBlock::BlockIncrement()
{
	return g_params.cleared_confirmations;
}

int BtcBlock::Init(DbConn *dbconn)
{
	Transaction tx;

	auto rc = dbconn->TransactionSelectLevelDescending(INT64_MAX, INT64_MAX, tx);
	if (rc < 0)
		return -1;

	if (!rc)
		m_current_block = tx.btc_block + g_params.cleared_confirmations - 1;

	BOOST_LOG_TRIVIAL(info) << "BtcBlock::Init current block level " << m_current_block;

	return 0;
}

// assigns a block level to a transaction
// this should be called from inside a BeginWrite to ensure it's in sync with any queries

uint64_t BtcBlock::GetCurrentBlock()
{
	lock_guard<FastSpinLock> lock(m_lock);

	if (m_reported)
	{
		m_current_block += 1;

		m_reported = false;
	}

	m_used = true;

	return m_current_block;
}

// finish any pending block prior to a query
// to be safe, the query should probably not report tx's past the returned block value

uint64_t BtcBlock::FinishCurrentBlock()
{
	lock_guard<FastSpinLock> lock(m_lock);

	if (m_used)
	{
		m_current_block += g_params.cleared_confirmations - 1;

		m_used = false;
	}

	m_reported = true;

	return m_current_block;
}
