/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * billets.cpp
*/

#include "ccwallet.h"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "totals.hpp"
#include "txquery.hpp"
#include "walletdb.hpp"
#include "rpc_errors.hpp"

#include <transaction.h>
#include <transaction.hpp>

#define TRACE_BILLETS	(g_params.trace_billets)

static mutex billet_available_mutex;
static condition_variable billet_available_condition_variable;
static uint64_t billet_available_count;

Billet::Billet()
{
	Clear();
}

void Billet::Clear()
{
	memset((void*)this, 0, sizeof(*this));
}

void Billet::Copy(const Billet& other)
{
	memcpy(this, &other, sizeof(*this));
}

string Billet::DebugString() const
{
	ostringstream out;

	out << "id " << id;
	out << " status " << status;
	out << " flags " << hex << flags << dec;
	out << " create_tx " << create_tx;
	out << " dest_id " << dest_id;
	out << " blockchain " << blockchain;
	out << " pool " << pool;
	out << hex;
	out << " address " << address;
	out << " asset " << asset;
	out << " amount_fp " << amount_fp;
	out << " amount " << dec << amount << hex;
	out << " delaytime " << dec << delaytime << hex;
	out << " commit_iv " << commit_iv;
	out << " commitment " << commitment;
	out << " commitnum " << dec << commitnum << hex;
	out << " serialnum " << serialnum;
	out << " spend_hashkey " << spend_hashkey;
	out << " spend_tx_commitnum " << dec << spend_tx_commitnum;

	return out.str();
}

bool Billet::IsValid() const
{
	return StatusIsValid(status) && create_tx && dest_id;
}

unsigned Billet::FlagsFromDestinationType(unsigned type)
{
	unsigned flags = 0;

	if (type == SECRET_TYPE_SPENDABLE_DESTINATION)
		flags |= BILL_RECV_MASK;

	if (type == SECRET_TYPE_TRACK_DESTINATION)
		flags |= BILL_RECV_MASK_TRACK;

	if (type == SECRET_TYPE_WATCH_DESTINATION)
		flags |= BILL_RECV_MASK_WATCH;

	return flags;
}

bool Billet::HasSerialnum(unsigned status, unsigned flags)
{
	return (flags & BILL_RECV_MASK_TRACK) && status >= BILL_STATUS_SENT;
}

void Billet::SetFromTxOut(const TxPay& tx, const TxOut& txout)
{
	if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(trace) << "Billet::SetFromTxOut";

	Clear();

	flags = txout.addrparams.__flags;
	dest_id = txout.addrparams.__dest_id;
	blockchain = txout.addrparams.dest_chain;
	address = txout.M_address;
	pool = txout.M_pool;
	asset = txout.__asset;
	amount_fp = txout.__amount_fp;
	delaytime = 0;	// eventually, compute this from destination and whether spend and/or trust secrets are known
	commit_iv = tx.M_commitment_iv;
	commitment = txout.M_commitment;

	tx_amount_decode(amount_fp, amount, false, tx.amount_bits, tx.exponent_bits);
}

int Billet::PollUnspent(DbConn *dbconn, TxQuery& txquery)
{
	BOOST_LOG_TRIVIAL(trace) << "Billet::PollUnspent";

	bigint_t amount = 0UL;
	uint64_t next_id = 0;
	Billet bill;
	bigint_t total = 0UL;

	while (true)
	{
		auto rc = dbconn->BilletSelectUnspent(amount, next_id, bill);
		if (rc > 0)
			break;
		if (rc || g_shutdown)
			return -1;

		amount = bill.amount;
		next_id = bill.id + 1;

		CCASSERT(bill.status == BILL_STATUS_CLEARED || bill.status == BILL_STATUS_ALLOCATED);

		rc = Billet::CheckIfBilletsSpent(dbconn, txquery, &bill, 1);
		if (rc < 0)
			return rc;

		if (!rc && !bill.asset)
			total = total + bill.amount;
	}

	BOOST_LOG_TRIVIAL(info) << "Billet::PollUnspent unspent total = " << total;

	if (g_interactive)
	{
		string amount;
		amount_to_string(0, total, amount);
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Total amount of the unspent billets: " << amount << "\n" << endl;
	}

	return 0;
}

int Billet::ResetAllocated(DbConn *dbconn, bool reset_balance)
{
	BOOST_LOG_TRIVIAL(trace) << "Billet::ResetAllocated reset_balance " << reset_balance;

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		return rc;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr <<

R"(Releasing all billets allocated to pending transactions. This should only be used as a last resort and may result in
conflicting (double-spend) transactions.)"

		"\n" << endl;
	}

	rc = dbconn->BilletsResetAllocated(reset_balance);
	if (rc) return rc;

	bigint_t amount = 0UL;
	uint64_t next_id = 0;
	Billet bill;
	unsigned delaytime = 0;
	bigint_t total = 0UL;

	while (reset_balance)
	{
		auto rc = dbconn->BilletSelectUnspent(amount, next_id, bill);
		if (rc > 0)
			break;
		if (rc || g_shutdown)
			return -1;

		amount = bill.amount;
		next_id = bill.id + 1;

		CCASSERT(bill.status == BILL_STATUS_CLEARED || bill.status == BILL_STATUS_ALLOCATED);

		if (!bill.asset)
			total = total + bill.amount;

		Total::AddBalance(dbconn, true, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, 0, bill.asset, delaytime, bill.blockchain, true, bill.amount);
	}

	rc = dbconn->Commit();
	if (rc) return rc;

	dbconn->DoDbFinishTx();

	finally.Clear();

	if (reset_balance)
	{
		BOOST_LOG_TRIVIAL(info) << "Billet::ResetAllocated unspent total = " << total;

		if (g_interactive)
		{
			string amount;
			amount_to_string(0, total, amount);
			lock_guard<FastSpinLock> lock(g_cout_lock);
			cerr << "Total amount of the unspent billets: " << amount << "\n" << endl;
		}
	}

	return 0;
}

/*

Balance/Total Tracking

Types:
	DESTINATION
	DESTINATION 0 = main wallet balance
	ACCOUNT

	BALANCE
	RECEIVED -- only cleared amounts are tracked, watch_only and spendable
		can received watch_only become received spendable?

	PENDING
	CLEARED
	ALLOCATED

	WATCH_ONLY
	SPENDABLE


Sent by this wallet:
	tx pending:
		output bills marked pending
		input bills marked allocated
		allocated total updated
		balances are unchanged
	as each output bill clears:
		output bill marked cleared
		to allow new tx creation, deduct bill amount from allocated total
	when all output bills clear (or some could have become allocated or spent):
		input bills marked spent
		received totals are updated
		balances are updated
		back out changes to allocated balance

Tx sent by this wallet abandon/conflicted/error submitting:
	change input bills back to cleared
	back out changes to allocated balance

Sent by another wallet to this wallet:
	address is polled and a payment is detected
	create a cleared tx for the payment
		output bill marked as cleared
		received totals are updated
		balances are updated

Spend of this wallet's bill by another wallet
	can't create a txid because output bill isn't known
	mark bill spent
	update balances
*/

// FUTURE: In the future, destination type may change (for example, to spendable) after tx is created.  Need to detect this and update balances accordingly.

int Billet::SetStatusCleared(DbConn *dbconn, uint64_t _commitnum)
{
	// must be called from inside a BeginWrite

	if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(trace) << "Billet::SetStatusCleared commitnum " << _commitnum << " " << DebugString();

	CCASSERT(status == BILL_STATUS_PENDING || status == BILL_STATUS_ABANDONED || status == BILL_STATUS_ERROR || status == BILL_STATUS_VOID);

	auto old_status = status;

	if ((flags & BILL_RECV_MASK) == BILL_RECV_MASK)
		status = BILL_STATUS_CLEARED;
	else
		status = BILL_STATUS_SENT;

	commitnum = _commitnum;

	if (HasSerialnum())
	{
		SpendSecrets txsecrets;
		SpendSecretParams params;
		memset((void*)&params, 0, sizeof(params));

		auto rc = Secret::GetParentValue(dbconn, SECRET_TYPE_MONITOR, dest_id, params, &txsecrets[0], sizeof(txsecrets));
		if (rc) return rc;

		compute_serialnum(txsecrets[0].____monitor_secret, commitment, commitnum, serialnum);

		if (status == BILL_STATUS_CLEARED && delaytime == 0)
			Total::AddNoWaitAmounts((flags & BILL_FLAG_TRUSTED) ? amount : 0UL, false, amount, false);

		rc = Total::AddBalances(dbconn, false, (flags & BILL_IS_CHANGE ? 0 : TOTAL_TYPE_RB_RECEIVED) | ((status == BILL_STATUS_SENT) * (flags & BILL_RECV_MASK)), 0, dest_id, asset, delaytime, blockchain, true, amount);
		if (rc) return rc;

		if (old_status == BILL_STATUS_PENDING && (flags & BILL_RECV_MASK) && (flags & BILL_FLAG_TRUSTED))
		{
			auto rc = Total::AddBalances(dbconn, false, TOTAL_TYPE_PENDING_BIT, 0, 0, asset, delaytime, blockchain, false, amount);
			if (rc) return rc;
		}

		#if TEST_LOG_BALANCE
		amtint_t balance;
		Total::GetTotalBalance(dbconn, false, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, 0, 0, -1, 0, -1, false);
		BOOST_LOG_TRIVIAL(info) << "Billet::SetStatusCleared new balance " << balance << " bill amount " << amount << " old status " << old_status << " new status " << status << " flags " << flags;
		//cerr << "  SetStatusCleared new balance " << balance << " bill amount " << amount << " old status " << old_status << " new status " << status << " flags " << flags << endl;
		#endif
	}

	auto rc = dbconn->BilletInsert(*this);

	if (amount && status == BILL_STATUS_CLEARED)
		NotifyNewBillet(true);

	return rc;
}

int Billet::SetStatusSpent(DbConn *dbconn, const bigint_t& hashkey, uint64_t tx_commitnum)
{
	// must be called from inside a BeginWrite

	if (!hashkey) BOOST_LOG_TRIVIAL(info) << "Billet::SetStatusSpent hashkey is zero;" << DebugString() << " hashkey " << hex << hashkey << dec << " tx_commitnum " << tx_commitnum; // !!! this should maybe log at warning level
	else if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(trace) << "Billet::SetStatusSpent " << DebugString() << " hashkey " << hex << hashkey << dec << " tx_commitnum " << tx_commitnum;

	if (status == BILL_STATUS_ALLOCATED)
	{
		auto rc = Total::AddBalances(dbconn, false, TOTAL_TYPE_ALLOCATED_BIT, 0, 0, asset, delaytime, blockchain, false, amount);
		if (rc) return rc;
	}

	status = BILL_STATUS_SPENT;

	auto rc = Total::AddBalances(dbconn, false, 0, 0, 0, asset, delaytime, blockchain, false, amount);
	if (rc) return rc;

	#if TEST_LOG_BALANCE
	amtint_t balance;
	Total::GetTotalBalance(dbconn, false, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, 0, 0, -1, 0, -1, false);
	BOOST_LOG_TRIVIAL(info) << "Billet::SetStatusSpent new balance " << balance << " bill amount " << amount;
	//cerr << "    SetStatusSpent new balance " << balance << " bill amount " << amount << endl;
	#endif

	rc = dbconn->BilletInsert(*this);
	if (rc) return rc;

	// check for any conflicting transactions

	uint64_t tx_id = 0;

	while (true)
	{
		bigint_t check_hashkey;
		uint64_t check_tx_commitnum;

		auto rc = dbconn->BilletSpendSelectBillet(id, tx_id, &check_hashkey, &check_tx_commitnum);
		if (rc < 0) return rc;

		if (rc)
			break;

		if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(trace) << "Billet::CheckConflicts bill id " << id << " returned tx_id " << tx_id << " hashkey " << hex << check_hashkey << dec << " tx_commitnum " << check_tx_commitnum;

		if (check_hashkey != hashkey || (check_tx_commitnum && tx_commitnum && check_tx_commitnum != tx_commitnum))
		{
			rc = Transaction::SetConflicted(dbconn, tx_id);
			if (rc) return rc;
		}

		++tx_id;
	}

	return 0;
}

int Billet::CheckIfBilletsSpent(DbConn *dbconn, TxQuery& txquery, Billet *billets, unsigned nbills, bool or_pending)  // throws RPC_Exception
{
	if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(debug) << "Billet::CheckIfBilletsSpent nbills " << nbills << " or_pending " << or_pending;

	CCASSERT(nbills);
	CCASSERT(nbills <= TX_MAXIN);

	// return 1 if an input was already spent; otherwise 2 if an input spend is pending and or_pending is true
	int result = 0;

	array<uint16_t, TX_MAXIN> statuses;
	array<bigint_t, TX_MAXIN> serialnums;
	array<bigint_t, TX_MAXIN> hashkeys;
	array<uint64_t, TX_MAXIN> tx_commitnums;

	auto blockchain = billets[0].blockchain;

	for (unsigned i = 0; i < nbills; ++i)
	{
		serialnums[i] = billets[i].serialnum;

		if (billets[i].blockchain != blockchain)
		{
			BOOST_LOG_TRIVIAL(error) << "Billet::CheckIfBilletsSpent billet id " << billets[i].id << " blockchain " << billets[i].blockchain << " mismatch billet id " << billets[0].id << " blockchain " << billets[0].blockchain;

			return -1;
		}
	}

	auto rc = txquery.QuerySerialnums(blockchain, &serialnums[0], nbills, &statuses[0], &hashkeys[0], &tx_commitnums[0]);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc) throw txrpc_server_error;

	for (unsigned i = 0; i < nbills; ++i)
	{
		if (statuses[i] == SERIALNUM_STATUS_PENDING && or_pending && !result)
		{
			result = 2;
		}
		else if (statuses[i] == SERIALNUM_STATUS_SPENT)
		{
			rc = dbconn->BeginWrite();
			if (rc)
			{
				dbconn->DoDbFinishTx(-1);

				return -1;
			}

			Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

			rc = dbconn->BilletSelectId(billets[i].id, billets[i]);
			if (rc) throw txrpc_wallet_db_error;

			rc = billets[i].SetStatusSpent(dbconn, hashkeys[i], tx_commitnums[i]);
			if (rc) throw txrpc_wallet_db_error;

			// commit db writes

			rc = dbconn->Commit();
			if (rc)
			{
				BOOST_LOG_TRIVIAL(error) << "Billet::CheckIfBilletsSpent error committing db transaction";

				return -1;
			}

			dbconn->DoDbFinishTx();

			finally.Clear();

			result = 1;
		}
	}

	if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(debug) << "Billet::CheckIfBilletsSpent nbills " << nbills << " or_pending " << or_pending << " result " << result;

	return result;
}

uint64_t Billet::GetBilletAvailableCount()
{
	// call this from inside a DB BeginRead or BeginWrite

	lock_guard<mutex> lock(billet_available_mutex);

	return billet_available_count;
}

void Billet::NotifyNewBillet(bool increment)
{
	// call this from inside a DB BeginWrite

	lock_guard<mutex> lock(billet_available_mutex);

	if (increment)
		++billet_available_count;

	if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(debug) << "Billet::NotifyNewBillet increment " << increment << " billet_available_count " << billet_available_count;

	billet_available_condition_variable.notify_all();
}

int Billet::WaitNewBillet(uint64_t last_count, uint32_t seconds)
{
	// returns 0 on successful wait (no timeout)

	// TODO: future optimization: make this a priority queue so earlier threads get first try with new billets

	if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(debug) << "Billet::WaitNewBillet last_count " << last_count << " seconds " << seconds;

	unique_lock<mutex> lock(billet_available_mutex);

	if (g_shutdown) return -1;

	if (last_count != billet_available_count)
		return 0;

	//cerr << "WaitNewBillet" << endl;

	auto until = chrono::steady_clock::now() + chrono::seconds(seconds);

	auto rc = billet_available_condition_variable.wait_until(lock, until);

	if (g_shutdown) return -1;

	return (rc != cv_status::no_timeout);
}

void Billet::Shutdown()
{
	CCASSERT(g_shutdown);

	NotifyNewBillet(false);
}
