/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * txrpc.cpp
*/

#include "ccwallet.h"
#include "txrpc.h"
#include "txrpc_btc.h"
#include "rpc_errors.hpp"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "totals.hpp"
#include "amounts.h"
#include "btc_block.hpp"
#include "lpcserve.hpp"
#include "txparams.hpp"
#include "txquery.hpp"
#include "walletdb.hpp"

#include <jsonutil.h>

#define TRACE_TX	(g_params.trace_txrpc)

using namespace snarkfront;

static RPC_Exception txrpc_invalid_destination_error(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet destination");

static FastSpinLock mint_thread_lock;
static volatile unsigned mint_thread_lo = 1;
static volatile unsigned mint_thread_hi = 1;
static volatile unsigned mint_thread_adjust = 0;
static atomic<unsigned> mint_thread_count(0);

static void cc_mint_thread_proc_cleanup(unsigned threadnum, bool interactive, DbConn **dbconn, TxQuery **txquery)
{
	BOOST_LOG_TRIVIAL(info) << "cc_mint_thread_proc " << threadnum << " cleanup";

	if (*txquery)
	{
		(*txquery)->Stop();

		(*txquery)->WaitForStopped();

		(*txquery)->FreeConnection();
	}

	if (*dbconn)
		delete *dbconn;

	if (interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "cc.mint thread " << threadnum << " ended" << endl;
	}

	{
		lock_guard<FastSpinLock> lock(mint_thread_lock);

		if (threadnum == mint_thread_lo)
			++mint_thread_lo;
		else if (threadnum == mint_thread_hi && mint_thread_hi > mint_thread_lo)
			--mint_thread_hi;
		else if (threadnum > mint_thread_lo)
		{
			if (++mint_thread_adjust == mint_thread_hi - mint_thread_lo)
			{
				mint_thread_adjust = 0;
				mint_thread_lo = mint_thread_hi;
			}
		}
	}

	--mint_thread_count;
}

void cc_mint_threads_shutdown()
{
	while (true)
	{
		auto n = mint_thread_count.load();

		BOOST_LOG_TRIVIAL(info) << "cc_mint_threads_shutdown " << n << " threads running";

		if (!n)
			return;

		sleep(1);
	}
}

static void cc_mint_thread_proc(unsigned threadnum, bool interactive)
{
	BOOST_LOG_TRIVIAL(info) << "cc_mint_thread_proc " << threadnum << " start " << " interactive " << interactive;

	if (interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "cc.mint thread " << threadnum << " started" << endl;
	}

	DbConn *dbconn = NULL;
	TxQuery *txquery = NULL;

	Finally finally(boost::bind(&cc_mint_thread_proc_cleanup, threadnum, interactive, &dbconn, &txquery));

	txquery = g_lpc_service.GetConnection(false);
	if (!txquery)
	{
		BOOST_LOG_TRIVIAL(error) << "cc_mint_thread_proc " << threadnum << " txquery GetConnection failed";

		if (interactive)
		{
			lock_guard<FastSpinLock> lock(g_cout_lock);
			cerr << "cc.mint thread " << threadnum << " GetConnection failed" << endl;
		}

		return;
	}

	dbconn = new DbConn;
	CCASSERT(dbconn);

	set_nice(100);

	while (threadnum >= mint_thread_lo && !g_shutdown)
	{
		auto t0 = ccticks();

		//usleep(20*1000);	// for testing--slow it down a little

		//if (((t0/CCTICKS_PER_SEC) & 31) == 0)	// for testing--burst transactions
		//	ccsleep(35);

		try
		{
			Transaction tx;

			auto rc = tx.CreateTxMint(dbconn, *txquery);

			auto elapsed = ccticks_elapsed(t0, ccticks());

			BOOST_LOG_TRIVIAL(info) << "cc_mint_thread_proc " << threadnum << " CreateTxMint returned " << rc << " elapsed time " << elapsed;

			if (rc < 0)
			{
				if (interactive && !g_shutdown)
				{
					lock_guard<FastSpinLock> lock(g_cout_lock);
					cerr << "cc.mint thread " << threadnum << " mint transaction not allowed" << endl;
				}

				return;
			}

			if (interactive)
			{
				amtint_t amounti = 0UL;
				string balance;

				rc = Total::GetTotalBalance(dbconn, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false);
				amount_to_string(0, amounti, balance);

				lock_guard<FastSpinLock> lock(g_cout_lock);
				cerr << "cc.mint thread " << threadnum << ", tx submitted, elapsed time " << (elapsed+500)/1000 << " seconds";
				if (!rc)
					cerr << ", wallet balance " << balance;
				cerr << endl;
			}
		}
		catch (const RPC_Exception &e)
		{
			auto elapsed = ccticks_elapsed(t0, ccticks());

			BOOST_LOG_TRIVIAL(info) << "cc_mint_thread_proc " << threadnum << " CreateTxMint elapsed time " << elapsed << " exception " << e.what();

			if (interactive && !g_shutdown)
			{
				lock_guard<FastSpinLock> lock(g_cout_lock);
					cerr << "cc.mint thread " << threadnum << ", elapsed time " << (elapsed+500)/1000 << " seconds, ";
				if (e.what()[0])
					cerr << e.what() << endl;
				else
					cerr << "exception" << endl;
			}

			ccsleep(10);
		}
	}

	BOOST_LOG_TRIVIAL(info) << "cc_mint_thread_proc " << threadnum << " ending";
}

void cc_mint_threads(int nthreads, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_mint_threads nthreads " << nthreads << " mint_thread_lo " << mint_thread_lo << " mint_thread_hi " << mint_thread_hi;

	unsigned first;
	int nstart;

	{
		lock_guard<FastSpinLock> lock(mint_thread_lock);

		first = mint_thread_hi;

		nstart = nthreads - (int)(mint_thread_hi - mint_thread_lo);

		if (nstart < 0)
		{
			BOOST_LOG_TRIVIAL(info) << "cc_mint_threads stopping " << -nstart << " threads";

			mint_thread_lo -= nstart;

			return;
		}

		BOOST_LOG_TRIVIAL(info) << "cc_mint_threads starting " << nstart << " threads";

		mint_thread_hi += nstart;
	}

	for (int i = 0; i < nstart; ++i)
	{
		if (i)
			usleep(400*1000);

		if (g_shutdown)
			return;

		++mint_thread_count;

		thread t(&cc_mint_thread_proc, first++, g_interactive);
		t.detach();
	}

	ccsleep(2);	// wait for threads to show console messages

	//rstream << "ok";
}

void cc_mint(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_mint";

	Transaction tx;

	auto rc = tx.CreateTxMint(dbconn, txquery);
	if (rc) throw txrpc_tx_rejected;

	rstream << tx.GetBtcTxid();
}

void cc_poll_destination(string destination, uint64_t last_receive_max, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_poll_destination destination " << destination << " last_receive_max " << last_receive_max;

	if (last_receive_max == 0)
		last_receive_max = 48 * 3600;

	bigint_t dest;
	uint64_t dest_chain;
	auto rc = Secret::DecodeDestination(destination, dest_chain, dest);
	if (rc) throw txrpc_invalid_destination_error;

	Secret secret;
	rc = dbconn->SecretSelectSecret(&dest, TX_INPUT_BYTES, secret);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (rc) throw txrpc_invalid_destination_error;

	if (secret.dest_chain != dest_chain)
		throw txrpc_invalid_destination_error;

	//bool watchonly = secret.TypeIsWatchOnlyDestination();
	//bool ismine = (watchonly || secret.type == SECRET_TYPE_SPENDABLE_DESTINATION);

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Checking for new transactions to all addresses associated with this destination...\n" << endl;
	}

	rc = Secret::PollDestination(dbconn, txquery, secret.id, last_receive_max);
	if (rc < 0) throw txrpc_wallet_error;

	//rstream << "done";
}

void cc_poll_mint(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_poll_mint";

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Creating all possible mint addresses associated with this wallet...\n" << endl;
	}

	unsigned count = 0;

	while (!g_shutdown)
	{
		Secret address;
		SpendSecretParams params;
		memset(&params, 0, sizeof(params));

		auto rc = address.CreateNewSecret(dbconn, SECRET_TYPE_SELF_ADDRESS, MINT_DESTINATION_ID, txparams.blockchain, params);
		if (rc) break;

		rc = address.UpdateSavePollingTimes(dbconn);
		if (rc) break;

		++count;

		//if (!(count & 4095))
		//	cerr << "count " << count << endl;
	}

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Created " << count << " addresses. These addresses will be polled in the background for successful mint transactions...\n" << endl;
	}

	//rstream << "done";
}
