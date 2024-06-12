/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * txrpc.cpp
*/

#include "ccwallet.h"
#include "jsonrpc.h"
#include "txrpc.h"
#include "txrpc_btc.h"
#include "rpc_errors.hpp"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "txbuildlist.hpp"
#include "totals.hpp"
#include "btc_block.hpp"
#include "exchange.hpp"
#include "lpcserve.hpp"
#include "txparams.hpp"
#include "polling.hpp"
#include "txquery.hpp"
#include "walletutil.h"
#include "walletdb.hpp"

#include <transaction.h>
#include <amounts.h>
#include <encode.h>
#include <xtransaction-xreq.hpp>
#include <xtransaction-xpay.hpp>
#include <jsonutil.h>

#include <SpinLock.hpp>

#include <siphash/siphash.h>
#include <jsoncpp/json/json.h>

#define TRACE_TX	(g_params.trace_txrpc)

using namespace snarkfront;

static RPC_Exception txrpc_invalid_type_error(RPC_INVALID_PARAMETER, "Invalid type");
static RPC_Exception txrpc_invalid_destination_error(RPC_INVALID_PARAMETER, "Invalid destination");
static RPC_Exception txrpc_destination_not_found(RPC_INVALID_ADDRESS_OR_KEY, "Non-wallet destination");

static FastSpinLock mint_thread_lock(__FILE__, __LINE__);
static volatile unsigned mint_thread_lo = 1;
static volatile unsigned mint_thread_hi = 1;
static volatile unsigned mint_thread_adjust = 0;
static atomic<unsigned> mint_thread_count(0);

void estimate_donation(const TxParams& txparams, unsigned nout, unsigned nin, string& str)
{
	bigint_t donation;
	auto nbytes = txparams.ComputeTxSize(nout, nin);
	txparams.ComputeDonation(0, nbytes, nout, nin, donation);
	auto amount_fp = tx_amount_encode(donation, true, txparams.donation_bits, txparams.exponent_bits);
	tx_amount_decode(amount_fp, donation, true, txparams.donation_bits, txparams.exponent_bits);
	amount_to_string(0, donation, str);

	if (TRACE_TX) BOOST_LOG_TRIVIAL(debug) << "estimate_donation nout " << nout << " nin " << nin << " est nbytes " << nbytes << " donation " << donation << " = " << str;
}

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
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
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
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
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
			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
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
					lock_guard<mutex> lock(g_cerr_lock);
					check_cerr_newline();
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

				lock_guard<mutex> lock(g_cerr_lock);
				check_cerr_newline();
				cerr << "cc.mint thread " << threadnum << ", tx submitted, elapsed time " << (elapsed+500)/1000 << " seconds";
				if (!rc)
					cerr << ", wallet balance " << balance;
				cerr << endl;
			}
		}
		catch (const RPC_Exception& e)
		{
			auto elapsed = ccticks_elapsed(t0, ccticks());

			BOOST_LOG_TRIVIAL(info) << "cc_mint_thread_proc " << threadnum << " CreateTxMint elapsed time " << elapsed << " exception " << e.what();

			if (interactive && !g_shutdown)
			{
				lock_guard<mutex> lock(g_cerr_lock);
				check_cerr_newline();
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

void cc_mint_threads(int nthreads, RPC_STDPARAMS)
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

		thread t(&cc_mint_thread_proc, first++, IsInteractive());
		t.detach();
	}

	ccsleep(2);	// wait for threads to show console messages

	//rstream << "ok";
}

void cc_mint(RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_mint";

	Transaction tx;

	auto rc = tx.CreateTxMint(dbconn, txquery);
	if (rc) throw txrpc_tx_rejected;

	rstream << tx.GetBtcTxid();
}

void cc_unique_id_generate(CP string& prefix, unsigned random_bits, unsigned checksum_chars, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_unique_id_generate prefix " << prefix << " random_bits " << random_bits << " checksum_chars " << checksum_chars;

	auto ref_id = unique_id_generate(dbconn, prefix, random_bits, checksum_chars);

	rstream << ref_id;
}

void cc_donation_estimate(unsigned type, unsigned nin, unsigned nout, RPC_STDPARAMSAQ)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_donation_estimate type " << type << " nin " << nin << " nout " << nout;

	if (!type) type = CC_TYPE_TXPAY;

	switch (type)
	{
	case CC_TYPE_TXPAY:
		if (!nin)	nin = 2;
		if (!nout)	nout = 2;
		break;
	default:
		throw txrpc_invalid_type_error;
	}

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	string donation;

	estimate_donation(txparams, nout, nin, donation);

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "The amount shown is the donation for a baseline " << Transaction::TypeString(type) << " transaction with ";
		cerr << nin << " input" << (nin == 1 ? "" : "s") << " and " << nout << " output" << (nout == 1 ? "." : "s.") << endl;
		cerr << "The donation for an actual transaction will depend on the billets available in the wallet.\n" << endl;
	}

	rstream << donation;
	add_quotes = false;
}

void cc_send(bool async, CP string& ref_id_req, CP string& dest, CP bigint_t& amount, CP string& comment, CP string& comment_to, bool subfee, RPC_STDPARAMS)
{
	auto ref_id = ref_id_req;

	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_send async " << async << " ref_id " << ref_id << " dest " << dest << " amount " << amount << " comment " << comment << " subfee " << subfee;

	uint64_t dest_chain;
	bigint_t destination;
	auto rc = Secret::DecodeDestination(dest, dest_chain, destination);
	if (rc) throw txrpc_invalid_address;

	Transaction tx;

	rc = tx.CreateTxPay(dbconn, txquery, async, ref_id, CC_TYPE_TXPAY, dest, dest_chain, destination, amount, comment, comment_to, subfee);

	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_send ref_id " << ref_id << " dest " << dest << " amount " << amount << " result rc " << rc << " txid " << tx.GetBtcTxid();

	if (!rc)
		rstream << tx.GetBtcTxid();
}

void cc_transaction_cancel(CP string& txid, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_transaction_cancel txid " << txid;

	bigint_t address, commitment;
	uint64_t id, dest_chain;

	auto rc = Transaction::DecodeBtcTxid(txid, id, dest_chain, address, commitment);
	if (rc > 0) throw txrpc_txid_not_found;
	if (rc) throw txrpc_invalid_txid_error;

	if (!id)
	{
		Billet bill;

		rc = dbconn->BilletSelectTxid(&address, &commitment, bill);
		if (rc > 0) throw txrpc_txid_not_found;
		if (rc) throw txrpc_wallet_db_error;

		id = bill.create_tx;
	}

	Transaction tx;

	rc = tx.BeginAndReadTx(dbconn, id);
	if (rc > 0) throw txrpc_txid_not_found;
	if (rc) throw txrpc_wallet_db_error;

	if (tx.status == TX_STATUS_CONFLICTED)
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction already conflicted");

	if (!tx.nin || !tx.TxCouldClear())
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction cannot be cancelled");

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		Billet& bill = tx.input_bills[i];

		//cout << "input " << i << " " << bill.DebugString();

		if (bill.status == BILL_STATUS_SPENT)
			throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction cannot be cancelled");
	}

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		Billet& bill = tx.output_bills[i];

		//cout << "output " << i << " " << bill.DebugString();

		if (bill.status != BILL_STATUS_PENDING && bill.status != BILL_STATUS_ABANDONED)
			throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction cannot be cancelled");
	}

	// pick a deterministic pseudorandom input

	#if WALLET_ID_BYTES < 16
	#error WALLET_ID_BYTES < 16
	#endif

	CCASSERT(sizeof(g_params.wallet_id) >= 16);
	auto hash = siphash(&tx.id, sizeof(tx.id), &g_params.wallet_id, 16);

	Billet input;
	input.Copy(tx.input_bills[hash % tx.nin]);

	tx.Clear();

	rc = tx.CreateConflictTx(dbconn, txquery, input);

	if (rc) throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction cannot be cancelled");

	rstream << tx.GetBtcTxid();
}

void cc_list_change_destinations(RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_list_change_destinations";

	Secret destination;

	auto rc = dbconn->SecretSelectId(SELF_DESTINATION_ID, destination);
	if (rc) throw txrpc_wallet_db_error;

	rstream << "[\"" << destination.EncodeDestination() << "\"]";
}

void cc_destination_poll(CP string& destination, unsigned polling_addresses, uint64_t last_receive_max, RPC_STDPARAMS)
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

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Checking for new transactions to all addresses associated with this destination...\n" << endl;
	}

	rc = Secret::PollDestination(dbconn, txquery, secret.id, polling_addresses, last_receive_max);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc < 0) throw txrpc_wallet_error;

	//rstream << "done";
}

void cc_mint_poll(RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_mint_poll";

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Creating all possible mint addresses associated with this wallet...\n" << endl;
	}

	unsigned count = 0;

	while (true)
	{
		if (g_shutdown)
			throw txrpc_shutdown_error;

		Secret address;
		SpendSecretParams params;

		auto rc = address.CreateNewSecret(dbconn, SECRET_TYPE_SELF_ADDRESS, MINT_DESTINATION_ID, txparams.blockchain, params);
		if (rc) break;

		rc = address.UpdateSavePollTime(dbconn);
		if (rc) break;

		++count;

		//if (!(count & 4095))
		//	cerr << "count " << count << endl;
	}

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Created " << count << " addresses. These addresses will be polled in the background for successful mint transactions...\n" << endl;
	}

	//rstream << "done";
}

void cc_billets_poll_unspent(RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_billets_poll_unspent"; //@retest

	auto rc = Billet::PollUnspent(dbconn, txquery);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc) throw txrpc_wallet_db_error;
}

void cc_billets_release_allocated(bool reset_balance, RPC_STDPARAMS)
{
	BOOST_LOG_TRIVIAL(warning) << "cc_billets_release_allocated reset_balance " << reset_balance;

	auto rc = Billet::ResetAllocated(dbconn, reset_balance);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc) throw txrpc_wallet_db_error;
}

void cc_billets_list_unspent(unsigned statuses, CP snarkfront::bigint_t& min_amount, RPC_STDPARAMS)
{
	static const char* statstr[] =
	{
		"all",
		"pending",
		"allocated",
		"cleared",
		"cleared and allocated",
		"cleared and pending"
	};

	static const uint16_t statmask[] =
	{
		(1 << BILL_STATUS_PENDING) | (1 << BILL_STATUS_PREALLOCATED) | (1 << BILL_STATUS_ALLOCATED) | (1 << BILL_STATUS_CLEARED),
		(1 << BILL_STATUS_PENDING) | (0 << BILL_STATUS_PREALLOCATED) | (0 << BILL_STATUS_ALLOCATED) | (0 << BILL_STATUS_CLEARED),
		(0 << BILL_STATUS_PENDING) | (1 << BILL_STATUS_PREALLOCATED) | (1 << BILL_STATUS_ALLOCATED) | (0 << BILL_STATUS_CLEARED),
		(0 << BILL_STATUS_PENDING) | (0 << BILL_STATUS_PREALLOCATED) | (0 << BILL_STATUS_ALLOCATED) | (1 << BILL_STATUS_CLEARED),
		(0 << BILL_STATUS_PENDING) | (1 << BILL_STATUS_PREALLOCATED) | (1 << BILL_STATUS_ALLOCATED) | (1 << BILL_STATUS_CLEARED),
		(1 << BILL_STATUS_PENDING) | (0 << BILL_STATUS_PREALLOCATED) | (0 << BILL_STATUS_ALLOCATED) | (1 << BILL_STATUS_CLEARED),
	};

	CCASSERT(statuses < sizeof(statstr) / sizeof(char*));

	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_billets_list_unspent statuses " << statuses << " " << statstr[statuses] << " min_amount " << min_amount;

	auto btc_block = g_btc_block.FinishCurrentBlock();

	bigint_t last_amount = 0UL;
	uint64_t last_id = INT64_MAX;
	bigint_t total = 0UL;
	bool needs_comma = false;
	Billet bill;

	rstream << "[";

	while (true)
	{
		auto rc = dbconn->BilletSelectUnspent(last_amount, last_id, bill);

		if (rc > 0)
			break;
		if (rc)
			throw txrpc_wallet_db_error;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		last_amount = bill.amount;
		last_id = bill.id;

		CCASSERT(bill.BillIsUnspent());
		CCASSERT(bill.amount);

		if ((bill.flags & BILL_RECV_MASK) != BILL_RECV_MASK)
			continue;	// bill is sent, not received

		if (!((1 << bill.status) & statmask[statuses]))
			continue;

		if (bill.amount < min_amount)
			continue;

		Transaction tx;
		rc = tx.BeginAndReadTx(dbconn, bill.create_tx);
		if (rc) throw txrpc_wallet_db_error;

		if (tx.btc_block > btc_block)
			continue;

		Secret destination;
		rc = dbconn->SecretSelectId(bill.dest_id, destination);
		if (rc) throw txrpc_wallet_db_error;

		unsigned confirmations = 0;
		if (bill.status == BILL_STATUS_CLEARED || bill.status == BILL_STATUS_ALLOCATED)
			confirmations = btc_block - tx.btc_block + 1;

		if (!bill.asset)
			total = total + bill.amount;

		string amount;
		amount_to_string(bill.asset, bill.amount, amount);

		rstream << (needs_comma ? ",{" : "{");
		needs_comma = true;

		rstream <<
		 "\"txid\":\"" << tx.GetBtcTxid() << "\""
		",\"vout\":0"
		",\"address\":\"" << destination.EncodeDestination() << "\""
		",\"account\":\"\""
		//",\"scriptPubKey\":\"" << string(64,'0') << "\""
		",\"amount\":" << amount <<
		",\"confirmations\":" << confirmations <<
		",\"cc.status\":" << bill.status <<
		",\"cc.status-label\":\"" << bill.StatusString() << "\""
		",\"cc.tx-type\":" << tx.type <<
		",\"cc.tx-type-label\":\"" << tx.TypeString() << "\""
		",\"spendable\":true"
		",\"solvable\":true"
		",\"safe\":" << truefalse(tx.type != CC_TYPE_MINT || confirmations);

		bool ischange = (bill.flags & BILL_IS_CHANGE);
		if (ischange)
			rstream << ",\"cc.is_change\":true}";
		else
			rstream << "}";
	}

	rstream << "]";

	BOOST_LOG_TRIVIAL(info) << "cc_billets_list_unspent statuses " << statuses << " " << statstr[statuses] << " min_amount " << min_amount << " total " << total;

	if (IsInteractive())
	{
		string amount;
		amount_to_string(0, total, amount);
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Total " << statstr[statuses] << " unspent billets amount: " << amount << "\n" << endl;
	}
}

void cc_dump_secrets(unsigned type, uint64_t parent, uint64_t start, uint64_t count, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_dump_secrets type " << type << " parent " << parent << " start " << start << " count " << count;

	uint64_t next_id = start;
	if (next_id < 1)
		next_id = 1;

	if (!count)
		count = 100;

	uint64_t scan_count = 0;

	while (scan_count < count && next_id <= INT64_MAX)
	{
		Secret secret;

		auto rc = dbconn->SecretSelectId(next_id, secret, true);
		if (rc < 0) throw txrpc_wallet_db_error;

		if (rc)
			return;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		next_id = secret.id + 1;

		if (type && type != secret.type)
			continue;

		if (parent && parent != secret.parent_id)
			continue;

		++scan_count;

		rstream << "\n" << secret.DebugString() << endl;
	}
}

void cc_dump_transactions(uint64_t start, uint64_t count, bool show_billets, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_dump_transactions start " << start << " count " << count << " show_billets " << show_billets;

	uint64_t next_id = start;
	if (next_id < TX_ID_MINIMUM)
		next_id = TX_ID_MINIMUM;

	if (!count)
		count = 100;

	uint64_t scan_count = 0;

	while (scan_count < count && next_id <= INT64_MAX)
	{
		Transaction tx;

		auto rc = tx.BeginAndReadTx(dbconn, next_id, true);
		if (rc < 0) throw txrpc_wallet_db_error;

		if (rc)
			return;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		next_id = tx.id + 1;

		if (tx.type == CC_TYPE_MINT && (tx.status == TX_STATUS_ERROR || tx.status == TX_STATUS_PENDING))
			continue;

		++scan_count;

		rstream << "\n" << tx.DebugString() << endl;

		if (show_billets)
		{
			for (unsigned i = 0; i < tx.nin; ++i)
				rstream << "---Input " << i << " " << tx.input_bills[i].DebugString() << endl;

			for (unsigned i = 0; i < tx.nout; ++i)
				rstream << "---Output " << i << " " << tx.output_bills[i].DebugString() << endl;
		}
	}
}

void cc_dump_billets(uint64_t start, uint64_t count, bool show_spends, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_dump_billets start " << start << " count " << count << " show_spends " << show_spends;

	uint64_t next_id = start;
	if (next_id < 1)
		next_id = 1;

	if (!count)
		count = 100;

	uint64_t scan_count = 0;

	while (scan_count < count && next_id <= INT64_MAX)
	{
		Billet bill;

		auto rc = dbconn->BilletSelectId(next_id, bill, true);
		if (rc < 0) throw txrpc_wallet_db_error;

		if (rc)
			return;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		next_id = bill.id + 1;

		if (!bill.amount)
			continue;

		if (bill.dest_id == MINT_DESTINATION_ID && bill.status == BILL_STATUS_PENDING)
			continue;

		++scan_count;

		rstream << "\n" << bill.DebugString() << endl;

		uint64_t tx_id = 0;

		while (show_spends && tx_id <= INT64_MAX)
		{
			bigint_t hashkey;
			uint64_t tx_commitnum;

			auto rc = dbconn->BilletSpendSelectBillet(bill.id, tx_id, &hashkey, &tx_commitnum);
			if (rc < 0) throw txrpc_wallet_db_error;

			if (rc)
				break;

			if (g_shutdown)
				throw txrpc_shutdown_error;

			rstream << "---Spend Tx id " << tx_id << " hashkey " << hex << hashkey << dec << " tx_commitnum " << tx_commitnum << endl;

			++tx_id;
		}
	}
}

void cc_dump_tx_build(RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_dump_tx_build";

	g_txbuildlist.Dump(rstream);
}

void cc_dump_exchange_requests(uint64_t start, uint64_t count, bool show_transactions, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_dump_exchange_requests start " << start << " count " << count << " show_transactions " << show_transactions;

	uint64_t next_id = start;
	if (next_id < 1)
		next_id = 1;

	if (!count)
		count = 100;

	uint64_t scan_count = 0;

	while (scan_count < count && next_id <= INT64_MAX)
	{
		Xmatchreq xreq;
		Transaction tx;

		auto rc = dbconn->ExchangeRequestSelectId(next_id, xreq, &tx, true);
		if (rc < 0) throw txrpc_wallet_db_error;

		if (rc)
			return;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		next_id = xreq.id + 1;

		if (!xreq.tx_id)
			continue;

		++scan_count;

		rstream << "\n" << xreq.DebugString() << endl;

		if (show_transactions)
			rstream << "---" << tx.DebugString() << endl;

		rstream << endl;
	}
}

void cc_dump_exchange_matches(uint64_t start, uint64_t count, bool show_requests, bool show_transactions, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_dump_exchange_matches start " << start << " count " << count << " show_requests " << show_requests << " show_transactions " << show_transactions;

	uint64_t next_id = start;
	if (next_id < 1)
		next_id = 1;

	if (!count)
		count = 100;

	uint64_t scan_count = 0;

	while (scan_count < count && next_id <= INT64_MAX)
	{
		Xmatch xmatch;
		Transaction txbuy, txsell;

		auto rc = dbconn->ExchangeMatchSelectNum(next_id, xmatch, true, &txbuy, &txsell, true);
		if (rc < 0) throw txrpc_wallet_db_error;

		if (rc)
			return;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		next_id = xmatch.xmatchnum + 1;

		++scan_count;

		rstream << "\nExchange Match " << xmatch.DebugString(false) << endl;

		if (show_requests)
		{
			rstream << "---Buy " << xmatch.xbuy.DebugString() << endl;
			if (show_transactions)
				rstream << "------" << txbuy.DebugString() << endl;

			rstream << "---Sell " << xmatch.xsell.DebugString() << endl;
			if (show_transactions)
				rstream << "------" << txsell.DebugString() << endl;
		}

		rstream << endl;
	}
}

static unsigned maxrun(const string& s)
{
	unsigned maxrun = 1;
	unsigned run = 1;
	char last = 0;

	for (unsigned i = 0; i < s.length(); ++i)
	{
		auto c = s[i];

		if (c != last)
			run = 1;
		else if (++run > maxrun)
			maxrun = run;

		last = c;
	}

	//cerr << "string " << s << " maxrun " << maxrun << endl;

	return maxrun;
}

class XreqQueryJsonWriter : public Json::StyledStreamWriter
{
	string json;

	void writeValue(const Json::Value& value) override
	{
		//cerr << "XreqQueryJsonWriter writeValue" << endl;

		if (value.type() != Json::stringValue)
			StyledStreamWriter::writeValue(value);
		else
		{
			static const char ccxfloat_prefix[] = CCXFLOAT_STRING_PREFIX;
			static const int  ccxfloat_plen = sizeof(ccxfloat_prefix) - 1;

			auto s = value.asString();

			if (s.length() > ccxfloat_plen && !memcmp(s.data(), ccxfloat_prefix, ccxfloat_plen))
				pushValue(s.data() + ccxfloat_plen);
			else
				StyledStreamWriter::writeValue(value);
		}
	}

public:
	XreqQueryJsonWriter()
	 :	StyledStreamWriter("")
	{ }
};

void cc_exchange_query_mining_info(RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_exchange_query_mining_info";

	QueryXreqsMiningInfoResults results;

	auto rc = txquery.QueryXminingInfo(results);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc) throw txrpc_server_error;

	const char *key = "exchange-mining-info-query-results";
	if (!results.json.isMember(key))
	{
		BOOST_LOG_TRIVIAL(info) << "cc_exchange_query_mining_info error missing key " << key;

		throw txrpc_server_error;
	}

	Json::Value& root = results.json[key];

	bigint_t total_mined;
	rc = Exchange::GetTotalMined(dbconn, total_mined);
	if (rc < 0) throw txrpc_server_error;

	string amts;
	amount_to_string(MINED_ASSET, total_mined, amts);
	root["wallet-total-mined"] = CCXFLOAT_STRING_PREFIX + amts;

	root["wallet-polling-interval"] = g_params.exchange_poll_time;

	root["wallet-exchange-request-minimum-amount"] = CCXFLOAT_STRING_PREFIX + UniFloat(XCX_REQ_MIN).asFullString();

	//rstream << results.json;

	XreqQueryJsonWriter writer;

	writer.write(rstream, results.json);
}

void cc_exchange_query_requests(CP unsigned xcx_type, CP bigint_t& min_amount, CP bigint_t& max_amount, CP double& min_rate, CP double& base_costs, CP double& quote_costs, CP uint64_t base_asset, CP uint64_t quote_asset, CP string& foreign_asset, CP unsigned maxret, CP unsigned offset, CP unsigned flags, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_exchange_query_requests xcx_type " << xcx_type << " min_amount " << min_amount << " max_amount " << max_amount << " min_rate " << min_rate << " base_costs " << base_costs << " quote_costs " << quote_costs << " base_asset " << base_asset << " quote_asset " << quote_asset << " foreign_asset " << foreign_asset << " maxret " << maxret << " offset " << offset << " flags " << flags;

	QueryXreqsResults results;
	const char *key;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;

	auto rc = txquery.QueryXreqs(xcx_type, min_amount, max_amount, min_rate, base_costs, quote_costs, base_asset, quote_asset, foreign_asset, maxret, offset, flags, results);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc) throw txrpc_server_error;

	key = "exchange-requests-query-results";
	Json::Value& root = *results.json.begin();
	Json::Value& array = root[key];

	if (!array.isArray())
	{
		BOOST_LOG_TRIVIAL(error) << "cc_exchange_query_requests expected an json array";

		throw txrpc_wallet_error;
	}

	key = "quote-asset";
	if (!root.isMember(key))
	{
		BOOST_LOG_TRIVIAL(info) << "cc_exchange_query_requests error missing key " << key;

		throw txrpc_server_error;
	}
	rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(info) << "cc_exchange_query_requests error parsing key " << key << " value " << root[key].asString();

		throw txrpc_server_error;
	}

	key = "foreign-asset";
	if (root.isMember(key) && root[key].asString().length())
	{
		BOOST_LOG_TRIVIAL(info) << "cc_exchange_query_requests unexpected key " << key << " value " << root[key].asString();

		throw txrpc_server_error;
	}

	root[key] = Xreq::ForeignAssetString(BIG64(bigval), root[key].asString());

	for (unsigned i = 0; i < array.size(); ++i)
	{
		Json::Value& root = array[i];
		ccoid_t objid;
		Transaction tx;

		key = "object-id";
		if (!root.isMember(key))
			goto missing_key;
		rc = parse_objid(fn, key, root[key].asString(), objid, output, outsize);
		if (rc) goto parse_error;
		//cerr << i << " " << root[key] << " " << buf2hex(&objid, sizeof(objid)) << endl;

		rc = dbconn->TransactionSelectObjIdDescendingId(objid, INT64_MAX, tx);
		if (rc < 0) throw txrpc_wallet_error;

		if (!rc)
		{
			key = "self-origin";
			root[key] = true;

			key = "wallet-txid";
			root[key] = tx.GetBtcTxid();
		}

		key = "quote-asset";
		if (!root.isMember(key))
			goto missing_key;
		rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;

		key = "foreign-asset";
		if (root.isMember(key) && root[key].asString().length())
			goto unexpected_key;

		root[key] = Xreq::ForeignAssetString(BIG64(bigval), root[key].asString());

		continue;

	missing_key:

		BOOST_LOG_TRIVIAL(info) << "cc_exchange_query_requests error missing key " << key;

		throw txrpc_server_error;

	unexpected_key:

		BOOST_LOG_TRIVIAL(info) << "cc_exchange_query_requests unexpected key " << key << " value " << root[key].asString();

		throw txrpc_server_error;

	parse_error:

		BOOST_LOG_TRIVIAL(info) << "cc_exchange_query_requests error parsing key " << key << " value " << root[key].asString();

		throw txrpc_server_error;
	}

	//rstream << results.json;

	XreqQueryJsonWriter writer;

	writer.write(rstream, results.json);
}

/*

Note:

!!!!! AUDIT THESE

Naked UI supports the following 13 fields (# = cross-chain only):
	Type, ExpireTime, BaseAsset(=0)/QuoteAsset, #ForeignAsset,
	MinAmount, MaxAmount, NetRateRequired, WaitDiscount, #BaseCosts(=0)/#QuoteCosts,
	MinWaitTime, #PaymentTime, #Confirmations, #ForeignAddress, Destination

Non-naked adds these 6 fields (naked value in parenthesis):
	ConsiderationRequired(=0), ConsiderationOffered(=0), Pledge(=0),
	AcceptTimeRequired(=0), AcceptTimeOffered(=0),
	PubSigningKey

These 7 fields are not supported in this UI:
	AddImmediatelyToBlockchain, AutoAcceptMatches(=true),
	NoMinimumAfterFirstMatch, MustLiquidateCrossingMinimum, MustLiquidateBelowMinimum,
	HoldTime(=0), HoldTimeRequired(=0)
*/

void cc_crosschain_request_create(bool async, CP string& ref_id_req, CP unsigned xcx_type, CP bigint_t& min_amount, CP bigint_t& max_amount, CP double& rate, CP double& costs, CP uint64_t quote_asset, CP string& foreign_asset, CP string& foreign_address, unsigned expiration, CP double& wait_discount, RPC_STDPARAMS)
{
	auto ref_id = ref_id_req;

	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_crosschain_request_create async " << async << " ref_id " << ref_id << " xcx_type " << xcx_type << " min_amount " << min_amount << " max_amount " << max_amount << " rate " << rate << " costs " << costs << " foreign_asset " << foreign_asset << " foreign_address " << foreign_address << " expiration " << expiration << " wait_discount " << wait_discount;

	// TODO? create destination with something other than SECRET_TYPE_SPENDABLE_DESTINATION and MAIN_PRE_DESTINATION_ID?

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	if (!expiration)
		expiration = (IsTestnet(txparams.blockchain) ? 5*60 : 10*60);

	Secret dest, address;
	SpendSecretParams params;

	dest.CreateNewDestination(dbconn, txquery, -1);

	rc = address.CreateNewSecret(dbconn, SECRET_TYPE_EXCHANGE_ADDRESS, dest.id, txparams.blockchain, params);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc) throw txrpc_wallet_error;

	CCASSERT(address.id);

	Xreq xreq(xcx_type, expiration, min_amount, max_amount, rate, costs, quote_asset, foreign_asset, foreign_address, IsTestnet(txparams.blockchain));

	xreq.hold_time = XREQ_SIMPLE_HOLD_TIME;
	xreq.hold_time_required = XREQ_SIMPLE_HOLD_TIME;
	xreq.min_wait_time = XREQ_SIMPLE_WAIT_TIME;
	xreq.wait_discount = wait_discount;
	xreq.payment_time = xreq.DefaultPaymentTime();
	xreq.confirmations = xreq.DefaultConfirmations();
	xreq.destination = dest.value;
	xreq.address_id = address.id;

	if (xreq.IsSimple())
		xreq.pledge = XREQ_SIMPLE_PLEDGE;

	if (xreq.IsSeller() || xreq.pledge == 100)
		xreq.amount_carry_out = max_amount;
	else if (xreq.pledge)
		xreq.amount_carry_out = max_amount * bigint_t(xreq.pledge) / bigint_t(100UL);	// pledge amounts always rounded down

	Transaction tx;

	rc = tx.CreateTxPay(dbconn, txquery, async, ref_id, xreq.type, "", 0, 0UL, 0UL, "", "", false, &xreq);

	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_crosschain_request_create ref_id " << ref_id << " xcx_type " << xcx_type << " min_amount " << min_amount << " max_amount " << max_amount << " rate " << rate << " foreign_asset " << foreign_asset << " result rc " << rc << " txid " << tx.GetBtcTxid();

	// TODO: how does the tx status get updated?

	if (!rc)
		rstream << tx.GetBtcTxid();
}

void cc_exchange_requests_pending_totals(CP uint64_t base_asset, CP uint64_t quote_asset, CP string& foreign_asset, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_exchange_requests_pending_totals base_asset " << base_asset << " quote_asset " << quote_asset << " foreign_asset " << foreign_asset;

	double req_pending[5];
	double match_pending[5];
	amtint_t balance;

	{
		auto rc = dbconn->BeginRead();
		if (rc)
		{
			dbconn->DoDbFinishTx(-1);

			throw txrpc_wallet_db_error;
		}

		Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

		Total::GetTotalBalance(dbconn, true, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, base_asset, 0, -1, 0, -1, false);

		rc = dbconn->ExchangeRequestsSumPending(base_asset, quote_asset, foreign_asset, req_pending);
		if (rc < 0) throw txrpc_wallet_db_error;

		rc = dbconn->ExchangeMatchesSumPending(base_asset, quote_asset, foreign_asset, match_pending);
		if (rc < 0) throw txrpc_wallet_db_error;
	}

	string amts;
	amount_to_string(base_asset, balance, amts);

	rstream << "{\"wallet-balance\":" << amts;

	if (!base_asset)
		rstream << ",\"base-asset\":\"CredaCash\"";
	else
		rstream << ",\"base-asset\":" << base_asset;

	if (quote_asset == XREQ_BLOCKCHAIN_BTC)
		rstream << ",\"quote-asset\":\"" XREQ_SYMBOL_BTC "\"";
	else if (quote_asset == XREQ_BLOCKCHAIN_BCH)
		rstream << ",\"quote-asset\":\"" XREQ_SYMBOL_BCH "\"";
	else if (foreign_asset.length())
		rstream << ",\"quote-asset\":\"" << json_escape(foreign_asset) << "\"";

	for (unsigned i = 0; i < 2; ++i)
	{
		if (!i)
			rstream << ",\"buy-request-pending-totals\":";
		else
			rstream << "},\"sell-request-pending-totals\":";

		amount_to_string(req_pending[i] + match_pending[i], amts);				// CredaCash total_pending[0] = buyer may get; total_pending[1] = tied up in sell reqs
		rstream << "{\"base-amount\":" << amts;
		amount_to_string(req_pending[i+2] + match_pending[i+2], amts);			// Foreign   total_pending[2] = buyer may pay; total_pending[3] = seller may get
		rstream << ",\"quote-amount\":" << amts;

		if (!i)
		{
			amount_to_string(req_pending[4] + match_pending[4], amts);			// CredaCash total_pending[4] = tied up in buy reqs
			rstream << ",\"pledge-amount\":" << amts;
		}
	}
	rstream << "}}";
}

static string make_memo(uint64_t matchnum, uint64_t seed)
{
	string memo;

	for (unsigned t = 2; t < 7; ++t)
	{
		for (unsigned i = 0; i < 8; ++i)
		{
			auto dchar1 = cc_stringify_byte(base8sym, ((matchnum ^ seed) + i) % 8);	// min val = decimal 50
			auto num8 = matchnum + 82*dchar1;										// min val > 8^4 >= 5 digits base8
			auto check = matchnum % (8*8);
			auto dchar2 = cc_stringify_byte(base8sym, (dchar1 ^ (check / 8)) % 8);
			auto dchar3 = cc_stringify_byte(base8sym, (dchar2 ^ (check % 8)) % 8);

			memo.clear();
			memo.push_back(dchar1);
			cc_stringify(base8sym, 0UL, false, -1, num8, memo);
			memo.push_back(dchar2);
			memo.push_back(dchar3);

			if (maxrun(memo) <= t)
				return memo;
		}
	}

	return memo;
}

static void stream_match(const Xmatch& xmatch, bool is_buyer, bool is_seller, int64_t now, int64_t clock_diff, ostringstream& rstream)
{
	// note: some coins that support memo: Cosmos (ATOM), EOS, Stacks (STX), Stellar (XLM), XRP, BNB, Hashgraph (HBAR)
	// note: length of memo might be limited on some chains

	auto memo = make_memo(xmatch.xmatchnum, xmatch.next_deadline);

	if (xmatch.xsell.quote_asset <= XREQ_BLOCKCHAIN_BCH)
		memo.clear();	// bitcoin does not support memos

	rstream << "{\"match-info\":";
	rstream << "{\"match-number\":" << xmatch.xmatchnum;
	rstream << ",\"buy-request-number\":" << xmatch.xbuy.xreqnum;
	rstream << ",\"sell-request-number\":" << xmatch.xsell.xreqnum;
	rstream << ",\"type\":" << xmatch.type;
	rstream << ",\"type-label\":\"" << Transaction::TypeString(xmatch.type) << "\"";
	rstream << ",\"status\":" << xmatch.status;
	rstream << ",\"status-label\":\"" << xmatch.StatusString() << "\"";
	rstream << ",\"base-asset\":" << xmatch.xsell.base_asset;
	rstream << ",\"base-amount\":" << Xtx::asFullFloat(xmatch.xsell.base_asset, xmatch.base_amount);
	rstream << ",\"foreign-amount\":" << xmatch.QuoteAmount();
	rstream << ",\"rate\":" << xmatch.rate;
	rstream << ",\"pledge\":" << xmatch.match_pledge;
	rstream << ",\"match-timestamp\":" << xmatch.match_timestamp;
	rstream << ",\"match-localtime\":" << xmatch.match_timestamp - clock_diff;
	rstream << ",\"mining-amount\":" << xmatch.mining_amount;
	if (xmatch.accept_time)
		rstream << ",\"accept-time\":" << xmatch.accept_time;
	if (xmatch.accept_timestamp)
	{
		rstream << ",\"accept-timestamp\":" << xmatch.accept_timestamp;
		rstream << ",\"accept-localtime\":" << xmatch.accept_timestamp - clock_diff;
	}
	if (xmatch.final_timestamp)
	{
		rstream << ",\"final-timestamp\":" << xmatch.final_timestamp;
		rstream << ",\"final-localtime\":" << xmatch.final_timestamp - clock_diff;
	}
	rstream << ",\"wallet-is-buyer\":" << truefalse(is_buyer);
	rstream << ",\"wallet-is-seller\":" << truefalse(is_seller);

	rstream << "},\"payment-info\":";
	rstream << "{\"foreign-blockchain\":" << xmatch.xsell.quote_asset;
	rstream << ",\"payment-address\":\"" << json_escape(xmatch.xsell.foreign_address) << "\"";
	rstream << ",\"payment-time\":" << xmatch.xsell.payment_time;
	rstream << ",\"payment-confirmations-required\":" << xmatch.xsell.confirmations;
	if (xmatch.xsell.quote_asset == XREQ_BLOCKCHAIN_BTC)
		rstream << ",\"payment-asset\":\"" XREQ_SYMBOL_BTC "\"";
	else if (xmatch.xsell.quote_asset == XREQ_BLOCKCHAIN_BCH)
		rstream << ",\"payment-asset\":\"" XREQ_SYMBOL_BCH "\"";
	else if (xmatch.xsell.foreign_asset.length())
		rstream << ",\"payment-asset\":\"" << json_escape(xmatch.xsell.foreign_asset) << "\"";
	rstream << ",\"payment-amount\":" << xmatch.AmountToPayString();
	rstream << ",\"amount-paid\":" << xmatch.amount_paid;
	if (memo.length())
		rstream << ",\"payment-memo\":\"" << memo << "\"";
	if (is_buyer)
		rstream << ",\"wallet-marked-as-paid\":" << truefalse(xmatch.wallet_paid);
	if (xmatch.wallet_payment_foreign_txid.length())
		rstream << ",\"foreign-payment-txid\":\"" << json_escape(xmatch.wallet_payment_foreign_txid) << "\"";
	if (is_buyer && xmatch.next_deadline)
	{
		rstream << ",\"to-do\":";
		if (!xmatch.wallet_paid)
			rstream << "\"Make payment on foreign blockchain, then mark as paid in this wallet using cc.crosschain_match_mark_paid\"";
		else
			rstream << "\"After payment has 'payment-confirmations-required' on foreign blockchain, submit payment advice here using cc.crosschain_payment_claim\"";
	}
	if (xmatch.next_deadline)
	{
		rstream << ",\"deadline\":" << xmatch.next_deadline;
		rstream << ",\"deadline-localtime\":" << xmatch.next_deadline - clock_diff;
		rstream << ",\"deadline-minutes\":" << ((int64_t)xmatch.next_deadline - now)/60;
	}

	rstream << "}}";
}

void cc_exchange_request_info(const string& key, const string& strval, uint64_t intval, RPC_STDPARAMS)
{
	throw txrpc_not_implemented_error;
}

void cc_exchange_match_info(uint64_t matchnum, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_exchange_match_info matchnum " << matchnum;

	Xmatch xmatch;
	Transaction txbuy, txsell;

	auto rc = (matchnum ? dbconn->ExchangeMatchSelectNum(matchnum, xmatch, true, &txbuy, &txsell) : 1);
	if (rc < 0) throw txrpc_wallet_db_error;

	if (rc)
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "matchnum not found");

	TxParams txparams;

	rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	int64_t now = unixtime() + txparams.clock_diff;

	stream_match(xmatch, txbuy.id, txsell.id, now, txparams.clock_diff, rstream);
}

void cc_crosschain_match_action_list(double minutes, bool override_reminder_times, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_crosschain_match_action_list minutes " << minutes << " over_ride_reminder_times " << override_reminder_times;

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	uint64_t last_time = 0;
	uint64_t last_id = 1;

	int64_t now = unixtime() + txparams.clock_diff;
	int64_t deadline = (minutes ? now + ceil(60 * minutes) : INT64_MAX);

	rstream << "[";
	bool needs_comma = false;

	while (true)
	{
		Xmatch xmatch;
		Transaction txbuy, txsell;

		auto rc = dbconn->ExchangeMatchSelectDeadline(last_time, last_id, xmatch, true, &txbuy, &txsell);
		if (rc < 0) throw txrpc_wallet_db_error;

		if (rc)
			break;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		last_time = xmatch.next_deadline;
		last_id = xmatch.xmatchnum;

		if (TRACE_TX) BOOST_LOG_TRIVIAL(debug) << "cc_crosschain_match_action_list found xmatchnum " << xmatch.xmatchnum << " with txbuy.id " << txbuy.id << " type " << txbuy.type << " xmatch.next_deadline " << xmatch.next_deadline << " deadline " << deadline << " xmatch.wallet_reminder_time " << xmatch.wallet_reminder_time << " now " << now;

		//cerr << "next_deadline " << xmatch.next_deadline << " deadline " << deadline << " wallet_reminder_time " << xmatch.wallet_reminder_time << " now " << now << endl;

		if (xmatch.next_deadline && xmatch.next_deadline > (uint64_t)deadline)
		{
			//if (TRACE_TX) BOOST_LOG_TRIVIAL(trace) << "cc_crosschain_match_action_list xmatchnum " << xmatch.xmatchnum << " xmatch.next_deadline " << xmatch.next_deadline << " > deadline " << deadline;

			break;
		}

		if (!override_reminder_times && xmatch.wallet_reminder_time > (uint64_t)now)
		{
			//if (TRACE_TX) BOOST_LOG_TRIVIAL(trace) << "cc_crosschain_match_action_list xmatchnum " << xmatch.xmatchnum << " xmatch.wallet_reminder_time " << xmatch.wallet_reminder_time << " > now " << now;

			continue;
		}

		if (!txbuy.id || !Xtx::TypeIsCrosschain(txbuy.type))
		{
			//if (TRACE_TX) BOOST_LOG_TRIVIAL(trace) << "cc_crosschain_match_action_list xmatchnum " << xmatch.xmatchnum << " !txbuy.id " << txbuy.id << " || !!Xtx::TypeIsCrosschain(txbuy.type) " << txbuy.type;

			continue;
		}

		if (TRACE_TX) BOOST_LOG_TRIVIAL(debug) << "cc_crosschain_match_action_list returning xmatchnum " << xmatch.xmatchnum;

		// TODO: double check match status

		if (needs_comma)
			rstream << ",";
		needs_comma = true;

		stream_match(xmatch, txbuy.id, txsell.id, now, txparams.clock_diff, rstream);
	}

	rstream << "]";
}

void cc_crosschain_match_mark_paid(uint64_t matchnum, CP string& foreign_txid, double reminder_minutes, double minimum_advance_minutes, RPC_STDPARAMS)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_crosschain_match_mark_paid matchnum " << matchnum << " foreign_txid " << foreign_txid << " reminder_minutes " << reminder_minutes << " minimum_advance_minutes " << minimum_advance_minutes;

	TxParams txparams;
	int64_t reminder = 0;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	{
		auto rc = dbconn->BeginWrite();
		if (rc)
		{
			dbconn->DoDbFinishTx(-1);

			throw txrpc_wallet_db_error;;
		}

		Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

		Xmatch xmatch;

		rc = dbconn->ExchangeMatchSelectNum(matchnum, xmatch);
		if (rc < 0) throw txrpc_wallet_db_error;
		if (rc) throw txrpc_match_not_found;

		if (foreign_txid.length())
		{
			xmatch.wallet_paid = true;
			xmatch.wallet_payment_foreign_txid = foreign_txid;
		}

		int64_t now = unixtime() + txparams.clock_diff;
		reminder = (reminder_minutes < 0 ? -1 : now + ceil(60 * reminder_minutes));
		if (xmatch.next_deadline && minimum_advance_minutes > 0)
		{
			int64_t max_reminder = (int64_t)xmatch.next_deadline - ceil(60 * minimum_advance_minutes);
			if (reminder < 0 || reminder > max_reminder)
				reminder = max_reminder;
		}

		xmatch.wallet_reminder_time = reminder;

		rc = dbconn->ExchangeMatchInsert(xmatch);
		if (rc) throw txrpc_wallet_db_error;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		rc = dbconn->Commit();
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::PollXmatchreq error committing db transaction";

			throw txrpc_wallet_db_error;
		}

		dbconn->DoDbFinishTx();

		finally.Clear();
	}

	rstream << "{\"reminder\":" << reminder;
	rstream << "}";
}

void cc_crosschain_payment_claim(bool async, CP string& ref_id_req, uint64_t matchnum, double amount, CP string& foreign_block_id, string& foreign_txid, double reminder_minutes, double minimum_advance_minutes, RPC_STDPARAMS)
{
	auto ref_id = ref_id_req;

	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_crosschain_payment_claim async " << async << " ref_id_req " << ref_id_req << " matchnum " << matchnum << " amount " << amount << " foreign_block_id " << foreign_block_id << " foreign_txid " << foreign_txid << " reminder_minutes " << reminder_minutes << " minimum_advance_minutes " << minimum_advance_minutes;

	Xmatch xmatch;

	auto rc = dbconn->ExchangeMatchSelectNum(matchnum, xmatch);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (rc) throw txrpc_match_not_found;

	if (Xmatch::StatusIsClosed(xmatch.status))
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Match is closed");

	if (Xmatch::StatusIsPending(xmatch.status))
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Match is still pending");

	if (!amount)
		amount = xmatch.AmountToPay(true).asFloat();

	if (!foreign_txid.length())
	{
		foreign_txid = xmatch.wallet_payment_foreign_txid;

		if (!foreign_txid.length())
			throw RPC_Exception(RPC_INVALID_PARAMETER, "foreign payment identifier must be included");
	}

	// TODO: add a preliminary inquiry of the tx server to make sure pay advice is good before creating network msg with large POW

	Xpay xpay(matchnum, amount, foreign_block_id, foreign_txid);

	Transaction tx;

	rc = tx.CreateTxPay(dbconn, txquery, async, ref_id, xpay.type, "", 0, 0UL, 0UL, "", "", false, &xpay);

	// TODO: how does the tx status get updated?
	// TODO: this payment advice could be simultaneous with a conflicting payment advice, so at some point, need to detect status

	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_crosschain_payment_claim ref_id " << ref_id << " matchnum " << matchnum << " amount " << amount << " result rc " << rc << " txid " << tx.GetBtcTxid();

	if (!rc)
	{
		rstream << tx.GetBtcTxid();

		auto lastblocktime = Polling::EstimatedBlocktime(unixtime());
		const unsigned delay = 60;

		if (xmatch.wallet_polltime <= lastblocktime + delay + 2)
			return;

		auto rc = dbconn->BeginWrite();
		if (rc)
		{
			dbconn->DoDbFinishTx(-1);

			return;
		}

		Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

		rc = dbconn->ExchangeMatchSelectNum(matchnum, xmatch);
		if (rc) return;

		ExchangeMatch::UpdatePollTime(xmatch, lastblocktime, delay);

		rc = dbconn->ExchangeMatchInsert(xmatch);
		if (rc) return;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		rc = dbconn->Commit();
		if (rc) return;

		dbconn->DoDbFinishTx();

		finally.Clear();
	}
}
