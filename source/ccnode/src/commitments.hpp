/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * commitments.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <CCproof.h>
#include <transaction.h>

#define ADDRESS_BYTES			((TX_FIELD_BITS + 7) / 8)
#define COMMITMENT_BYTES		((TX_FIELD_BITS + 7) / 8)
#define COMMITMENT_HASH_BYTES	((TX_MERKLE_PATH_BITS + 7) / 8)
#if TX_MERKLE_ROOT_BITS != TX_MERKLE_PATH_BITS
#error TX_MERKLE_ROOT_BITS != TX_MERKLE_PATH_BITS
#endif

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
