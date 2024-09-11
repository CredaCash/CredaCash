/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * txbuildlist.hpp
*/

#pragma once

#include <CCbigint.hpp>

class Transaction;
class Xtx;
class DbConn;

class TxBuildEntry
{
public:
	uint64_t start_time;
	int mode;
	unsigned type;
	uint64_t dest_chain;
	snarkfront::bigint_t destination;
	snarkfront::bigint_t amount;
	Xtx *xtx;
	unsigned ref_count;
	bool is_done;

	string ref_id;	// the constructor may memset to zero all class members above this one
	string encoded_dest;

	vector<char> txbody;

	TxBuildEntry();

	~TxBuildEntry();

	string DebugString() const;

	void SetTxBody(TxParams& txparams, unsigned retry = 0);
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

	int StartBuild(DbConn *dbconn, TxParams& txparams, const string& ref_id, int mode, const unsigned type, const string& encoded_dest, const uint64_t dest_chain, const snarkfront::bigint_t& destination, const snarkfront::bigint_t& amount, const Xtx *xtx, TxBuildEntry **entry, Transaction& tx);

	void WaitForCompletion(DbConn *dbconn, TxBuildEntry *entry, Transaction& tx);

	void SetDone(TxBuildEntry *entry);

	void ReleaseEntry(TxBuildEntry *entry);
};

extern TxBuildList g_txbuildlist;
