/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * commitments.cpp
*/

#include "CCdef.h"
#include "commitments.hpp"
#include "blockchain.hpp"
#include "block.hpp"
#include "dbparamkeys.h"

using namespace snarkfront;

#define TRACE_COMMITMENTS	(g_params.trace_commitments)

Commitments g_commitments;

void Commitments::Init(DbConn *dbconn)
{
	if (TRACE_COMMITMENTS) BOOST_LOG_TRIVIAL(trace) << "Commitments::Init";

	uint64_t row_end;

	auto rc = dbconn->ParameterSelect(DB_KEY_COMMIT_COMMITNUM_HI, 0, &row_end, sizeof(row_end));
	if (rc < 0)
	{
		const char *msg = "FATAL ERROR Commitments::Init error retrieving next commitment number";

		return g_blockchain.SetFatalError(msg);
	}

	if (rc == 0)
	{
		m_next_tree_update_commitnum = row_end + 1;
		m_next_commitnum.store(m_next_tree_update_commitnum);
	}
}

void Commitments::DeInit()
{
	if (TRACE_COMMITMENTS) BOOST_LOG_TRIVIAL(trace) << "Commitments::DeInit";

}

uint64_t Commitments::GetNextCommitnum(bool increment)
{
	if (increment)
		return m_next_commitnum.fetch_add(1);
	else
		return m_next_commitnum.load();
}

bool Commitments::AddCommitment(DbConn *dbconn, uint64_t commitnum, const bigint_t& commitment)
{
	//cerr << "AddCommitment commitnum " << commitnum << endl;

	bigint_t hash;

	tx_commit_tree_hash_leaf(commitment, commitnum, hash);

	auto rc = dbconn->CommitTreeInsert(0, commitnum, &hash, COMMITMENT_HASH_BYTES);
	if (rc)
		return true;

	return false;
}

bool Commitments::UpdateCommitTree(DbConn *dbconn, SmartBuf newobj, uint64_t timestamp)
{
	auto block = (Block*)newobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	bool treechanged = (m_next_tree_update_commitnum != m_next_commitnum.load());

	if (!wire->level || treechanged)
	{
		// stash block level in db
		auto rc = dbconn->ParameterInsert(DB_KEY_COMMIT_BLOCK_LEVEL, 0, &wire->level, sizeof(wire->level));
		if (rc)
			return true;
	}

	bigint_t hash1, hash2, hash, nullhash;

	if (treechanged)
	{
		// update tree if it has changed

		uint64_t row_start = m_next_tree_update_commitnum & -2;
		m_next_tree_update_commitnum = m_next_commitnum.load();
		uint64_t row_end = m_next_tree_update_commitnum - 1;

		memcpy(&nullhash, &auxp->block_hash, COMMITMENT_HASH_BYTES);
		nullhash = nullhash * bigint_t(1UL);	// mod prime

		// stash row_end in db
		auto rc = dbconn->ParameterInsert(DB_KEY_COMMIT_COMMITNUM_HI, 0, &row_end, sizeof(row_end));
		if (rc)
			return true;

		// stash the "null" input in db
		rc = dbconn->ParameterInsert(DB_KEY_COMMIT_NULL_INPUT, 0, &nullhash, COMMITMENT_HASH_BYTES);
		if (rc)
			return true;

		for (unsigned height = 0; height < TX_MERKLE_DEPTH; ++height)
		{
			//cerr << "UpdateCommitTree height " << height << " row_end " << row_end << endl;

			for (uint64_t offset = row_start; offset <= row_end; offset += 2)
			{
				auto rc = dbconn->CommitTreeSelect(height, offset, &hash1, COMMITMENT_HASH_BYTES);
				if (rc)
					return true;

				if (offset >= row_end)
				{
					memcpy(&hash2, &nullhash, COMMITMENT_HASH_BYTES);
				}
				else
				{
					auto rc = dbconn->CommitTreeSelect(height, offset + 1, &hash2, COMMITMENT_HASH_BYTES);
					if (rc)
						return true;
				}

				tx_commit_tree_hash_node(hash1, hash2, hash);

				rc = dbconn->CommitTreeInsert(height + 1, offset/2, &hash, COMMITMENT_HASH_BYTES);
				if (rc)
					return true;
			}

			row_start = (row_start / 2) & -2;
			row_end /= 2;
		}
	}

	if (!wire->level || treechanged)
	{
		auto rc = dbconn->CommitRootsInsert(wire->level, (wire->level ? timestamp : 0), &hash, COMMITMENT_HASH_BYTES);
		if (rc)
			return true;
	}

	return false;
}