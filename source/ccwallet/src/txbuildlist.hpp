/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * txbuildlist.hpp
*/

#pragma once

#include <CCbigint.hpp>

class Transaction;
class DbConn;

class TxBuildEntry
{
public:
	uint64_t start_time;
	string ref_id;
	string encoded_dest;
	uint64_t dest_chain;
	snarkfront::bigint_t destination;
	snarkfront::bigint_t amount;

	unsigned ref_count;
	bool is_done;

	TxBuildEntry()
	 :	ref_count(1),
		is_done(false)
	{ }

	string DebugString() const;
};

class TxBuildList
{
	mutex m_mutex;
	condition_variable m_done_condition_variable;
	list<TxBuildEntry*> m_list;

	TxBuildEntry* FindEntry(const string& ref_id, bool remove = false);
	void ReleaseEntryWithLock(TxBuildEntry *entry);

public:

	~TxBuildList();

	void Shutdown();

	void Dump(ostream& out);

	int StartBuild(DbConn *dbconn, const string& ref_id, const string& encoded_dest, const uint64_t dest_chain, const snarkfront::bigint_t& destination, const snarkfront::bigint_t& amount, TxBuildEntry **entry, Transaction& tx);

	void WaitForCompletion(DbConn *dbconn, TxBuildEntry *entry, Transaction& tx);

	void SetDone(TxBuildEntry *entry);

	void ReleaseEntry(TxBuildEntry *entry);
};

extern TxBuildList g_txbuildlist;
