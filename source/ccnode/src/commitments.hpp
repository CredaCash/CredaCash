/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * commitments.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <CCparams.h>
#include <transaction.h>

class Commitments
{
	atomic<uint64_t> m_next_commitnum;
	uint64_t m_next_tree_update_commitnum;

public:

	Commitments()
	 :	m_next_commitnum(0),
		m_next_tree_update_commitnum(0)
	{ }

	void Init(DbConn *dbconn);
	void DeInit();

	uint64_t GetNextCommitnum(bool increment = false);
	bool AddCommitment(DbConn *dbconn, uint64_t commitnum, const snarkfront::bigint_t& commitment);
	bool UpdateCommitTree(DbConn *dbconn, SmartBuf newobj, uint64_t timestamp);
};

extern Commitments g_commitments;
