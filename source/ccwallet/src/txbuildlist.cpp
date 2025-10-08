/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * txbuildlist.cpp
*/

#include "ccwallet.h"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "txbuildlist.hpp"
#include "txparams.hpp"
#include "rpc_errors.hpp"
#include "walletdb.hpp"

#include <xtransaction.hpp>

//!#define RTEST_CUZZ_WAIT				(1024-1)

#ifndef RTEST_CUZZ_WAIT
#define RTEST_CUZZ_WAIT					0	// don't test
#endif

#define TRACE_TRANSACTIONS	(g_params.trace_transactions)

TxBuildList g_txbuildlist;

TxBuildEntry::TxBuildEntry()
{
	memset((void*)this, 0, (uintptr_t)&ref_id - (uintptr_t)this);

	xtx = new Xtx();
	ref_count = 1;
}

TxBuildEntry::~TxBuildEntry()
{
	delete xtx;
}

string TxBuildEntry::DebugString() const
{
	ostringstream out;

	out << "TxBuildEntry";
	out << " start_time " << start_time;
	out << " ref_id " << ref_id;
	out << " mode " << mode;
	out << " type " << type << " = " << Transaction::TypeString(type);
	out << " ref_count " << ref_count;
	out << " is_done " << is_done;
	out << " dest " << encoded_dest;
	out << " dest_chain " << dest_chain;
	//out << " destination " << buf2hex(&destination, sizeof(destination));
	out << " amount " << amount;
	out << " txbody " << txbody.size();
	if (xtx)
		out << " " << xtx->DebugString();

	return out.str();
}

void TxBuildEntry::SetTxBody(TxParams& txparams, unsigned retry)
{
	uint32_t bufpos;

	if (xtx->expiration)
		xtx->expire_time = unixtime() + txparams.clock_diff + xtx->expiration + retry * XTX_TIME_DIVISOR;

	if (0) // for testing
	{
		if (xtx->type == CC_TYPE_XCX_NAKED_BUY)
			xtx->expire_time = (unixtime()/60) * 60 + 30;
		cerr << "type " << xtx->type << " expire_time " << xtx->expire_time << " in " << xtx->expire_time - unixtime() << endl;
	}

	try
	{
		txbody.resize(TX_MAX_APPEND);

		auto rc = xtx->ToWire("SetTxBody", txbody.data(), txbody.size(), bufpos);
		if (rc) throw txrpc_wallet_error;
	}
	catch (const exception& e)
	{
		throw RPC_Exception(RPC_WALLET_INTERNAL_ERROR, e.what());
	}

	txbody.resize(bufpos);

	//cerr << "txbody nbytes " << txbody.size() << " data " << buf2hex(txbody.data(), txbody.size()) << endl;
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

int TxBuildList::StartBuild(DbConn *dbconn, TxParams& txparams, const string& ref_id, int mode, const unsigned type, const string& encoded_dest, const uint64_t dest_chain, const bigint_t& destination, const bigint_t& amount, const Xtx *xtx, TxBuildEntry **pentry, Transaction& tx)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "TxBuildList::StartBuild ref_id " << ref_id << " mode " << mode << " encoded_dest " << encoded_dest << " amount " << amount;

	lock_guard<mutex> lock(m_mutex);

	TxBuildEntry* &entry(*pentry);

	entry = FindEntry(ref_id);

	if (entry)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "TxBuildList::StartBuild found " << entry->DebugString();

		if (entry->encoded_dest != encoded_dest || entry->amount != amount)
			throw txrpc_tx_mismatch;

		++entry->ref_count;

		return 1;	// entry already existed
	}

	auto rc = tx.BeginAndReadTxRefId(dbconn, ref_id);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (!rc)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "TxBuildList::StartBuild found " << tx.DebugString();

		if (!Xtx::TypeHasBareMsg(type))
		{
			if (tx.nout < 1 || (dest_chain && tx.output_bills[0].blockchain != dest_chain) || (!Xtx::TypeIsXreq(type) && (tx.output_destinations[0].value != destination || tx.output_bills[0].amount != amount))) // this amount comparison would need to be fixed for "subfee" option
			{
				if (tx.StatusIsNotError() || tx.nout > 0)
					throw txrpc_tx_mismatch;
			}
		}

		return 1;
	}

	entry = new TxBuildEntry();
	CCASSERT(entry);

	entry->start_time = unixtime();
	entry->ref_id = ref_id;
	entry->mode = mode;
	entry->type = type;
	entry->encoded_dest = encoded_dest;
	entry->dest_chain = dest_chain;
	entry->destination = destination;
	entry->amount = amount;

	if (xtx)
	{
		delete entry->xtx;
		entry->xtx = xtx->Clone();

		entry->SetTxBody(txparams);
	}

	m_list.push_back(entry);

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "TxBuildList::StartBuild created new " << entry->DebugString();

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

			if (RTEST_CUZZ_WAIT) usleep(rand() & RTEST_CUZZ_WAIT);

			m_done_condition_variable.wait(lock);
		}
	}

	auto rc = tx.BeginAndReadTxRefId(dbconn, entry->ref_id);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (!rc)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "TxBuildList::WaitForCompletion read " << tx.DebugString();

		if (!Xtx::TypeHasBareMsg(entry->type))
		{
			if (tx.nout < 1 || (entry->dest_chain && tx.output_bills[0].blockchain != entry->dest_chain) || (!Xtx::TypeIsXreq(entry->type) && (tx.output_destinations[0].value != entry->destination || tx.output_bills[0].amount != entry->amount))) // this amount comparison would need to be fixed for "subfee" option
			{
				if (tx.StatusIsNotError() || tx.nout > 0)
					throw txrpc_tx_mismatch;
			}
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
		(void)rc;

		//CCASSERT(rc); // triggered if StartBuild fails
		//if (!rc) BOOST_LOG_TRIVIAL(error) << "TxBuildList::ReleaseEntry FindEntry ref_id " << entry->ref_id << " failed";
	}
}
