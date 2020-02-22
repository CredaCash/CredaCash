/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
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
#include "txbuildlist.hpp"
#include "totals.hpp"
#include "amounts.h"
#include "btc_block.hpp"
#include "lpcserve.hpp"
#include "txparams.hpp"
#include "txquery.hpp"
#include "walletutil.h"
#include "walletdb.hpp"

#include <siphash/siphash.h>

#define TRACE_TX	(g_params.trace_txrpc)

using namespace snarkfront;

#define stdparams	DbConn *dbconn, TxQuery& txquery, ostringstream& rstream
#define stdparamsaq	stdparamsaq
#define CP			const

static RPC_Exception txrpc_invalid_destination_error(RPC_INVALID_PARAMETER, "Invalid destination");
static RPC_Exception txrpc_destination_not_found(RPC_INVALID_ADDRESS_OR_KEY, "Non-wallet destination");

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
		*txquery = NULL;
	}

	if (*dbconn)
	{
		delete *dbconn;
		*dbconn = NULL;
	}

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

		usleep(500*1000);
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

				rc = Total::GetTotalBalance(dbconn, false, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false);
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

void cc_mint_threads(int nthreads, stdparams)
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

void cc_mint(stdparams)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_mint";

	Transaction tx;

	auto rc = tx.CreateTxMint(dbconn, txquery);
	if (rc) throw txrpc_tx_rejected;

	rstream << tx.GetBtcTxid();
}

void cc_unique_id_generate(const string& prefix, unsigned random_bits, unsigned checksum_chars, stdparams)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_unique_id_generate prefix " << prefix << " random_bits " << random_bits << " checksum_chars " << checksum_chars;

	auto ref_id = unique_id_generate(dbconn, prefix, random_bits, checksum_chars);

	rstream << ref_id;
}

void cc_send(bool async, CP string& ref_id_req, CP string& dest, CP bigint_t& amount, CP string& comment, CP string& comment_to, bool subfee, stdparams)
{
	auto ref_id = ref_id_req;

	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_send async " << async << " ref_id " << ref_id << " dest " << dest << " amount " << amount << " comment " << comment << " subfee " << subfee;

	uint64_t dest_chain;
	bigint_t destination;
	auto rc = Secret::DecodeDestination(dest, dest_chain, destination);
	if (rc) throw txrpc_invalid_address;

	Transaction tx;

	rc = tx.CreateTxPay(dbconn, txquery, async, ref_id, dest, dest_chain, destination, amount, comment, comment_to, subfee);

	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_send ref_id " << ref_id << " dest " << dest << " amount " << amount << " result rc " << rc << " txid " << tx.GetBtcTxid();

	if (!rc)
		rstream << tx.GetBtcTxid();
}

void cc_transaction_cancel(CP string& txid, stdparams)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_transaction_cancel txid " << txid;

	bigint_t address, commitment;
	uint64_t dest_chain;

	auto rc = Transaction::DecodeBtcTxid(txid, dest_chain, address, commitment);
	if (rc) throw txrpc_invalid_txid_error;

	Billet bill;

	rc = dbconn->BilletSelectTxid(&address, &commitment, bill);
	if (rc < 0) throw txrpc_txid_not_found;

	Transaction tx;

	rc = tx.BeginAndReadTx(dbconn, bill.create_tx);
	if (rc) throw txrpc_wallet_db_error;

	if (tx.status == TX_STATUS_CONFLICTED)
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction already conflicted");

	if (!tx.nin || (tx.status != TX_STATUS_PENDING && tx.status != TX_STATUS_ABANDONED))
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction cannot be cancelled");

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		Billet& bill = tx.input_bills[i];

		//cout << "input bill " << i << " " << bill.DebugString();

		if (bill.status == BILL_STATUS_SPENT)
			throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction cannot be cancelled");
	}

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		Billet& bill = tx.output_bills[i];

		//cout << "output bill " << i << " " << bill.DebugString();

		if (bill.status != BILL_STATUS_PENDING && bill.status != BILL_STATUS_ABANDONED)
			throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction cannot be cancelled");
	}

	// pick a deterministic pseudorandom input

	#if WALLET_ID_BYTES < 16
	#error WALLET_ID_BYTES < 16
	#endif

	uint64_t hash = siphash_keyed((uint8_t*)&g_params.wallet_id, (uint8_t*)&tx.id, sizeof(tx.id));

	Billet input;
	input.Copy(tx.input_bills[hash % tx.nin]);

	tx.Clear();

	rc = tx.CreateConflictTx(dbconn, txquery, input);

	if (rc) throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction cannot be cancelled");

	rstream << tx.GetBtcTxid();
}

void cc_destination_poll(CP string& destination, unsigned polling_addresses, uint64_t last_receive_max, stdparams)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_destination_poll destination " << destination << " polling_addresses " << polling_addresses << " last_receive_max " << last_receive_max;

	bigint_t dest;
	uint64_t dest_chain;
	auto rc = Secret::DecodeDestination(destination, dest_chain, dest);
	if (rc) throw txrpc_invalid_destination_error;

	Secret secret;
	rc = dbconn->SecretSelectSecret(&dest, TX_INPUT_BYTES, secret);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (rc) throw txrpc_destination_not_found;

	if (secret.dest_chain != dest_chain)
		throw txrpc_destination_not_found;

	//bool watchonly = secret.TypeIsWatchOnlyDestination();
	//bool ismine = (watchonly || secret.type == SECRET_TYPE_SPENDABLE_DESTINATION);

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Checking for new transactions to all addresses associated with this destination...\n" << endl;
	}

	rc = Secret::PollDestination(dbconn, txquery, secret.id, polling_addresses, last_receive_max);
	if (rc < 0) throw txrpc_wallet_error;

	//rstream << "done";
}

void cc_mint_poll(stdparams)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_mint_poll";

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
		memset((void*)&params, 0, sizeof(params));

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

void cc_list_change_destinations(stdparams)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_list_change_destinations";

	Secret destination;

	auto rc = dbconn->SecretSelectId(SELF_DESTINATION_ID, destination);
	if (rc) throw txrpc_wallet_db_error;

	rstream << "[\"" << destination.EncodeDestination() << "\"]";
}

void cc_dump_transactions(uint64_t start, uint64_t count, bool include_billets, stdparams)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_dump_transactions start " << start << " count " << count << " include_billets " << include_billets;

	uint64_t next_id = start;
	if (next_id < TX_ID_MINIMUM)
		next_id = TX_ID_MINIMUM;

	uint64_t scan_count = 0;

	while ((!count || scan_count < count) && next_id >= TX_ID_MINIMUM && next_id < INT64_MAX && !g_shutdown)
	{
		Transaction tx;

		auto rc = tx.BeginAndReadTx(dbconn, next_id, true);
		if (rc < 0) throw txrpc_wallet_db_error;

		if (rc)
			return;

		next_id = tx.id + 1;

		if (tx.type == TX_TYPE_MINT && (tx.status == TX_STATUS_ERROR || tx.status == TX_STATUS_PENDING))
			continue;

		++scan_count;

		rstream << "\nTransaction " << tx.DebugString() << endl;

		if (include_billets)
		{
			for (unsigned i = 0; i < tx.nin; ++i)
				rstream << "---Input Billet " << i << " " << tx.input_bills[i].DebugString() << endl;

			for (unsigned i = 0; i < tx.nout; ++i)
				rstream << "---Output Billet " << i << " " << tx.output_bills[i].DebugString() << endl;
		}
	}
}

void cc_dump_billets(uint64_t start, uint64_t count, bool show_spends, stdparams)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_dump_billets start " << start << " count " << count << " show_spends " << show_spends;

	uint64_t next_id = start;
	if (next_id < 1)
		next_id = 1;

	uint64_t scan_count = 0;

	while ((!count || scan_count < count) && next_id >= 1 && next_id < INT64_MAX && !g_shutdown)
	{
		Billet bill;

		auto rc = dbconn->BilletSelectId(next_id, bill, true);
		if (rc < 0) throw txrpc_wallet_db_error;

		if (rc)
			return;

		next_id = bill.id + 1;

		if (!bill.amount)
			continue;

		if (bill.dest_id == MINT_DESTINATION_ID && bill.status == BILL_STATUS_PENDING)
			continue;

		++scan_count;

		rstream << "\nBillet " << bill.DebugString() << endl;

		uint64_t tx_id = 0;

		while (show_spends && tx_id < INT64_MAX)
		{
			bigint_t hashkey;
			uint64_t tx_commitnum;

			auto rc = dbconn->BilletSpendSelectBillet(bill.id, tx_id, &hashkey, &tx_commitnum);
			if (rc < 0) throw txrpc_wallet_db_error;

			if (rc)
				break;

			rstream << "---Spend Tx id " << tx_id << " hashkey " << hex << hashkey << dec << " tx_commitnum " << tx_commitnum << endl;

			++tx_id;
		}
	}
}

void cc_dump_tx_build(stdparams)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_dump_tx_build";

	g_txbuildlist.Dump(rstream);
}

void cc_billets_poll_unspent(stdparams)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_billets_poll_unspent";

	auto rc = Billet::PollUnspent(dbconn, txquery);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc) throw txrpc_wallet_db_error;
}

void cc_billets_release_allocated(bool reset_balance, stdparams)
{
	BOOST_LOG_TRIVIAL(warning) << "cc_billets_release_allocated reset_balance " << reset_balance;

	auto rc = Billet::ResetAllocated(dbconn, reset_balance);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc) throw txrpc_wallet_db_error;
}
