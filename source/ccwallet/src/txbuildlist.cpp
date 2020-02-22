/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * txbuildlist.cpp
*/

#include "ccwallet.h"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "txbuildlist.hpp"
#include "rpc_errors.hpp"
#include "walletdb.hpp"

#define TRACE_TRANSACTIONS	(g_params.trace_transactions)

TxBuildList g_txbuildlist;

string TxBuildEntry::DebugString() const
{
	ostringstream out;

	out << "start_time " << start_time;
	out << " ref_id " << ref_id;
	out << " ref_count " << ref_count;
	out << " is_done " << is_done;
	out << " dest " << encoded_dest;
	out << " dest_chain " << dest_chain;
	//out << " destination " << hex << destination << dec;
	out << " amount " << amount;

	return out.str();
}

void TxBuildList::Dump(ostream& out)
{
	lock_guard<mutex> lock(m_mutex);

	for (auto i = m_list.begin(); i != m_list.end(); ++i)
		out << (*i)->DebugString() << endl;
}

TxBuildEntry* TxBuildList::FindEntry(const string& ref_id, bool remove)
{
	for (auto i = m_list.begin(); i != m_list.end(); ++i)
	{
		if ((*i)->ref_id == ref_id)
		{
			auto entry = *i;

			if (remove)
			{
				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "TxBuildList::FindEntry deleting entry ref_id " << ref_id;

				m_list.erase(i);
				delete entry;
			}

			return entry;
		}
	}

	return NULL;
}

int TxBuildList::StartBuild(DbConn *dbconn, const string& ref_id, const string& encoded_dest, const uint64_t dest_chain, const bigint_t& destination, const bigint_t& amount, TxBuildEntry **pentry, Transaction& tx)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "TxBuildList::StartBuild ref_id " << ref_id << " encoded_dest " << encoded_dest << " amount " << amount;

	lock_guard<mutex> lock(m_mutex);

	TxBuildEntry* &entry(*pentry);

	entry = FindEntry(ref_id);

	if (entry)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "TxBuildList::StartBuild found entry " << entry->DebugString();

		if (entry->encoded_dest != encoded_dest || entry->amount != amount)
			throw txrpc_tx_mismatch;

		++entry->ref_count;

		return 1;	// entry already existed
	}

	auto rc = tx.BeginAndReadTxRefId(dbconn, ref_id);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (!rc)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "TxBuildList::StartBuild found tx " << tx.DebugString();

		if (tx.nout < 1 || tx.output_bills[0].blockchain != dest_chain || tx.output_destinations[0].value != destination || tx.output_bills[0].amount != amount) // this amount comparison would need to be fixed for "subfee" option
		{
			if (tx.StatusIsNotError() || tx.nout > 0)
				throw txrpc_tx_mismatch;
		}

		return 1;
	}

	entry = new TxBuildEntry();
	CCASSERT(entry);

	entry->start_time = time(NULL);
	entry->ref_id = ref_id;
	entry->encoded_dest = encoded_dest;
	entry->dest_chain = dest_chain;
	entry->destination = destination;
	entry->amount = amount;

	m_list.push_back(entry);

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "TxBuildList::StartBuild created new entry " << entry->DebugString();

	return 0;
}

void TxBuildList::WaitForCompletion(DbConn *dbconn, TxBuildEntry *entry, Transaction& tx)
{
	CCASSERT(entry);

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "TxBuildList::WaitForCompletion start_time " << entry->start_time << " ref_id " << entry->ref_id << " ref_count " << entry->ref_count;

	CCASSERT(entry->ref_count);

	{
		unique_lock<mutex> lock(m_mutex);

		while (!entry->is_done)
		{
			if (g_shutdown)
				throw txrpc_shutdown_error;

			m_done_condition_variable.wait(lock);
		}
	}

	auto rc = tx.BeginAndReadTxRefId(dbconn, entry->ref_id);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (!rc)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "TxBuildList::WaitForCompletion read tx " << tx.DebugString();

		if (tx.nout < 1 || tx.output_bills[0].blockchain != entry->dest_chain || tx.output_destinations[0].value != entry->destination || tx.output_bills[0].amount != entry->amount) // this amount comparison would need to be fixed for "subfee" option
		{
			if (tx.StatusIsNotError() || tx.nout > 0)
				throw txrpc_tx_mismatch;
		}
	}
}

TxBuildList::~TxBuildList()
{
	if (!m_list.empty())
		cerr << "Warning: TxBuildList is not empty" << endl;
	Dump(cerr);
}

void TxBuildList::Shutdown()
{
	CCASSERT(g_shutdown);

	lock_guard<mutex> lock(m_mutex);

	m_done_condition_variable.notify_all();
}

void TxBuildList::SetDone(TxBuildEntry *entry)
{
	CCASSERT(entry);

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "TxBuildList::SetDone start_time " << entry->start_time << " ref_id " << entry->ref_id << " ref_count " << entry->ref_count;

	lock_guard<mutex> lock(m_mutex);

	CCASSERT(entry->ref_count);
	CCASSERTZ(entry->is_done);

	entry->is_done = true;

	if (entry->ref_count > 1)
		m_done_condition_variable.notify_all();
}

void TxBuildList::ReleaseEntry(TxBuildEntry *entry)
{
	lock_guard<mutex> lock(m_mutex);

	ReleaseEntryWithLock(entry);
}

void TxBuildList::ReleaseEntryWithLock(TxBuildEntry *entry)
{
	CCASSERT(entry);

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "TxBuildList::ReleaseEntry start_time " << entry->start_time << " ref_id " << entry->ref_id << " ref_count " << entry->ref_count;

	CCASSERT(entry->ref_count);

	if (!--entry->ref_count)
	{
		auto rc = FindEntry(entry->ref_id, true);
		CCASSERT(rc);
	}
}
