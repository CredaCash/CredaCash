/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * transactions.cpp
*/

/*

Life-cycle of a Transaction:

Sent from this wallet:
1. Allocate balance
2. Construct tx with TX_STATUS_PENDING
Output bills BILL_STATUS_PENDING + BILL_FLAG_TRUSTED (except for mint tx's) and BILL_FLAG_RECV and BILL_FLAG_TRACK if applicable
	Output amounts paid to this wallet are added to received pending and balance pending.
Input bills BILL_STATUS_ALLOCATED
3. Submit to network
	Handle submit fail
	Handle wallet stopping before submit
4. Start polling of output addresses
5. When payment is detected, update output bill that matches payment txid
Output bill BILL_STATUS_CLEARED
6. When all output bills are BILL_STATUS_CLEARED:
Update input bills to BILL_STATUS_SPENT
Update tx to TX_STATUS_CLEARED

Sent from another wallet to this wallet:
1. Poll anticipated output addresses
2. When payment is detected, create tx and output bill:
Output bill BILL_STATUS_CLEARED
Tx to TX_STATUS_CLEARED

When another wallet spends a bill in this wallet:
1. Under polling, mark bill as BILL_STATUS_SPENT (can't create tx without output bill for txid)

*/

#include "ccwallet.h"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "txbuildlist.hpp"
#include "totals.hpp"
#include "btc_block.hpp"
#include "rpc_errors.hpp"
#include "txparams.hpp"
#include "txquery.hpp"
#include "lpcserve.hpp"
#include "walletutil.h"
#include "walletdb.hpp"

#include <CCobjdefs.h>
#include <CCmint.h>
#include <transaction.h>
#include <transaction.hpp>
#include <jsonutil.h>

#include <siphash/siphash.h>

//!#define TEST_NO_ROUND_UP				1
//!#define TEST_FEWER_RETRIES			1
//!#define RTEST_TX_ERRORS				16	// when this is enabled, it's helpful to set polling_addresses = 50 and TEST_RANDOM_POLLING = 5 to 20
//!#define TEST_FAIL_ALL_TXS			1	// all tx's fail (except mints), so balance should never change--useful for testing that the donation is handled correctly on error
//!#define RTEST_RELEASE_ERROR_TXID		2	// discard txid after an error
//!#define RTEST_ALLOW_DOUBLE_SPENDS	2
//!#define RTEST_CUZZ					16
//#define TEST_BIG_DIVISION				1

#ifndef TEST_NO_ROUND_UP
#define TEST_NO_ROUND_UP			0	// don't test
#endif

#ifndef TEST_FEWER_RETRIES
#define TEST_FEWER_RETRIES			0	// don't test
#endif

#ifndef RTEST_TX_ERRORS
#define RTEST_TX_ERRORS				0	// don't test
#endif

#ifndef TEST_FAIL_ALL_TXS
#define TEST_FAIL_ALL_TXS			0	// don't test
#endif

#ifndef RTEST_RELEASE_ERROR_TXID
#define RTEST_RELEASE_ERROR_TXID		0	// don't test
#endif

#ifndef RTEST_ALLOW_DOUBLE_SPENDS
#define RTEST_ALLOW_DOUBLE_SPENDS	0	// don't test
#endif

#ifndef RTEST_CUZZ
#define RTEST_CUZZ					0	// don't test
#endif

#ifndef TEST_BIG_DIVISION
#define TEST_BIG_DIVISION			0	// don't test
#endif

#define WALLET_TX_MINOUT	2
//#define WALLET_TX_MAXOUT	2

#define WALLET_TX_MININ		1
#define WALLET_TX_MAXIN		4

static const string cc_txid_prefix = "CCTX_";
static const string cc_tx_internal_prefix = "CCTX_Internal_";

#define TRACE_TRANSACTIONS	(g_params.trace_transactions)

#define SUBTX_AMOUNT	output_bills[0].amount

static mutex billet_allocate_mutex;
static atomic<unsigned> tx_thread_count(0);

Transaction::Transaction()
{
	Clear();
}

void Transaction::Clear()
{
	memset((void*)this, 0, (uintptr_t)&body - (uintptr_t)this);

	body.clear();
	ref_id.clear();

	we_sent[0] = -1;
	we_sent[1] = -1;
	inputs_involve_watchonly = -1;

	for (unsigned i = 0; i < TX_MAXOUT; ++i)
	{
		output_bills[i].Clear();
		output_destinations[i].Clear();
		output_accounts[i].Clear();
	}

	for (unsigned i = 0; i < TX_MAXIN; ++i)
	{
		input_bills[i].Clear();
		input_destinations[i].Clear();
		input_accounts[i].Clear();
	}
}

void Transaction::Copy(const Transaction& other)
{
	memcpy((void*)this, &other, (uintptr_t)(&this->body) - (uintptr_t)this);

	body = other.body;
	ref_id = other.ref_id;

	for (unsigned i = 0; i < TX_MAXOUT; ++i)
	{
		output_bills[i].Copy(other.output_bills[i]);
		output_destinations[i].Copy(other.output_destinations[i]);
		output_accounts[i].Copy(other.output_accounts[i]);
	}

	for (unsigned i = 0; i < TX_MAXIN; ++i)
	{
		input_bills[i].Copy(other.input_bills[i]);
		input_destinations[i].Copy(other.input_destinations[i]);
		input_accounts[i].Copy(other.input_accounts[i]);
	}
}

string Transaction::DebugString() const
{
	ostringstream out;

	out << "id " << id;
	out << " type " << type;
	out << " status " << status;
	out << " build_type " << build_type;
	out << " build_state " << build_state;
	out << " parent_id " << parent_id;
	out << " param_level " << param_level;
	out << " create_time " << create_time;
	out << " btc_block " << btc_block;
	out << " donation " << donation;
	out << " nout " << nout;
	out << " nin " << nin;
	if (body.length())
		out << " body " << body;
	out << " txid " << GetBtcTxid();
	if (ref_id.length())
	out << " ref_id " << ref_id;

	return out.str();
}

bool Transaction::IsValid() const
{
	return TypeIsValid(type) && StatusIsValid(status);
}

string Transaction::EncodeInternalTxid() const
{
	string outs = cc_tx_internal_prefix;

	bigint_t maxval;
	subBigInt(bigint_t(0UL), bigint_t(1UL), maxval, false);

	bigint_t wallet_id = g_params.wallet_id;

	bigint_mask(wallet_id, 48);
	bigint_mask(maxval, 48);

	cc_encode(base57, 57, maxval, false, 0, wallet_id, outs);

	//cerr << "g_params.wallet_id " << hex << g_params.wallet_id << " wallet_id " << wallet_id << " maxval " << maxval << " outs " << outs << dec << endl;

	// encode internal id

	cc_encode(base57, 57, 0UL, false, -1, id, outs);

	//cerr << "id " << id << " outs " << outs << endl;

	// add checksum

	uint64_t hash = siphash((const uint8_t *)outs.data(), outs.length());
	//cerr << "hash " << hex << hash << dec << endl;
	cc_encode(base57, 57, 0UL, false, 2, hash, outs);

	//DecodeBtcTxid(outs, dest_chain, m1, maxval);

	return outs;
}

string Transaction::EncodeBtcTxid(uint64_t dest_chain, const bigint_t& address, const bigint_t& commitment)
{
	bigint_t m1;
	subBigInt(bigint_t(0UL), bigint_t(1UL), m1, false);

	string outs = cc_txid_prefix;

	// encode dest_chain

	cc_encode(base57, 57, 0UL, false, -1, dest_chain, outs);

	// encode address

	bigint_t maxval = m1;
	bigint_mask(maxval, TX_ADDRESS_BITS);

	cc_encode(base57, 57, maxval, false, 0, address, outs);

	// encode commitment

	auto masked = commitment;
	maxval = m1;

	bigint_mask(masked, TXID_COMMITMENT_BYTES * 8);
	bigint_mask(maxval, TXID_COMMITMENT_BYTES * 8);

	cc_encode(base57, 57, maxval, false, 0, masked, outs);

	// add checksum

	uint64_t hash = siphash((const uint8_t *)outs.data(), outs.length());
	//cerr << "hash " << hex << hash << dec << endl;
	cc_encode(base57, 57, 0UL, false, 4, hash, outs);

	//cerr << hex << " dest_chain " << dest_chain << " address " << address << " commitment " << commitment << dec << endl;
	//DecodeBtcTxid(outs, dest_chain, m1, maxval);

	return outs;
}

int Transaction::DecodeBtcTxid(const string& txid, uint64_t& dest_chain, bigint_t& address, bigint_t& commitment)
{
	dest_chain = -1;
	address = 0UL;
	commitment = 0UL;

	string fn;
	char output[128];
	uint32_t outsize = sizeof(output);

	auto inlen = txid.length();
	int chainlen = inlen - (cc_txid_prefix.length() + 22 + 22 + 4);
	if (chainlen < 1)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeBtcTxid invalid length";

		return -1;
	}

	if (txid.compare(0, cc_txid_prefix.length(), cc_txid_prefix))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeBtcTxid prefix != \"" << cc_txid_prefix << "\"";

		return -1;
	}

	string instring = txid.substr(cc_txid_prefix.length());

	//cerr << "encoded address " << instring << endl;

	bigint_t bigval;
	auto rc = cc_decode(fn, base57int, 57, false, chainlen, instring, bigval, output, outsize);
	if (rc || bigval > bigint_t((uint64_t)(-1)))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeBtcTxid blockchain decode failed: " << output;

		return -1;
	}

	dest_chain = BIG64(bigval);

	rc = cc_decode(fn, base57int, 57, false, 22, instring, address, output, outsize);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeBtcTxid address decode failed: " << output;

		return -1;
	}

	auto masked = address;
	bigint_mask(masked, TX_ADDRESS_BITS);
	if (address - masked)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeBtcTxid address overflow " << address;

		return -1;
	}

	//cerr << "encoded commitment " << instring << endl;

	rc = cc_decode(fn, base57int, 57, false, 22, instring, commitment, output, outsize);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeBtcTxid commitment decode failed: " << output;

		return -1;
	}

	masked = commitment;
	bigint_mask(masked, TXID_COMMITMENT_BYTES * 8);
	if (commitment - masked)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeBtcTxid commitment overflow " << commitment;

		return -1;
	}

	//cerr << "encoded checksum " << instring << endl;

	string outs;
	uint64_t hash = siphash((const uint8_t *)txid.data(), inlen - instring.length());
	cc_encode(base57, 57, 0UL, false, 4, hash, outs);
	//cerr << "hash " << hex << hash << dec << " " << outs << " " << instring << endl;
	if (outs != instring)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeBtcTxid checksum mismatch";

		return -1;
	}

	//cerr << hex << " dest_chain " << dest_chain << " address " << address << " commitment " << commitment << dec << endl;

	return 0;
}

string Transaction::GetBtcTxid() const
{
	if (!parent_id && nout && !(output_bills[0].flags & (BILL_FLAG_NO_TXID | BILL_IS_CHANGE)))
		return EncodeBtcTxid(output_bills[0].blockchain, output_bills[0].address, output_bills[0].commitment);
	else
		return EncodeInternalTxid();
}

void Transaction::FinishCreateTx(TxPay& ts)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FinishCreateTx " << DebugString();

	string fn;
	char output[128];
	uint32_t outsize = sizeof(output);

	auto rc = txpay_create_finish(fn, ts, output, outsize);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::FinishCreateTx txpay_create_finish failed: " << output;

		//tx_dump_stream(cout, ts);

		if (output[0])
			throw RPC_Exception(RPCErrorCode(-32001), output);
		else
			throw txrpc_wallet_error;
	}

	if (build_type == TX_BUILD_CANCEL_TX)
		status = TX_STATUS_ABANDONED;
	else
		status = TX_STATUS_PENDING;

	type = (ts.tag_type == CC_TYPE_MINT ? TX_TYPE_MINT : TX_TYPE_SEND);
	param_level = ts.param_level;

	tx_amount_decode(ts.donation_fp, donation, true, ts.donation_bits, ts.exponent_bits);

	nout = ts.nout;

	for (unsigned i = 0; i < nout; ++i)
		output_bills[i].SetFromTxOut(ts, ts.outputs[i]);
}

int Transaction::SaveOutgoingTx(DbConn *dbconn)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::SaveOutgoingTx " << DebugString();

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	CCASSERTZ(id);

	rc = dbconn->TransactionInsert(*this);

	auto tx_id = id;
	id = 0;				// leave this zero until the tx is committed

	if (rc) return -1;

	// input billets were all marked when allocated, but some could now be deallocated or marked spent

	uint64_t blockchain = 0;
	bigint_t balance_allocated = 0UL;

	for (unsigned i = 0; i < nin; ++i)
	{
		Billet& bill = input_bills[i];
		Billet check;

		CCASSERT(bill.id);

		rc = dbconn->BilletSelectId(bill.id, check);
		if (rc) return -1;

		if (check.status == BILL_STATUS_SPENT)
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::SaveOutgoingTx billet already spent " << check.DebugString();

			//cerr << "Transaction::SaveOutgoingTx billet already spent" << endl;

			return 1;
		}

		if (build_type != TX_BUILD_CANCEL_TX && check.status == BILL_STATUS_CLEARED)
		{
			if (!blockchain)
				blockchain = check.blockchain;
			else
				CCASSERT(check.blockchain == blockchain);

			CCASSERTZ(check.asset);

			check.status = BILL_STATUS_ALLOCATED;

			rc = dbconn->BilletInsert(check);
			if (rc) return -1;

			balance_allocated = balance_allocated + check.amount;
		}

		CCASSERT(build_type == TX_BUILD_CANCEL_TX || check.status == BILL_STATUS_ALLOCATED);

		rc = dbconn->BilletSpendInsert(tx_id, bill.id, &bill.spend_hashkey);
		if (rc) return rc;	// if billets were deallocated while tx was being built, the same billet could have been used twice
	}

	if (balance_allocated)
	{
		uint64_t asset = 0;
		unsigned delaytime = 0;

		// TBD: this will get more complicated if billets have various delaytimes or blockchains

		rc = Total::AddBalances(dbconn, false, TOTAL_TYPE_ALLOCATED_BIT, 0, 0, asset, delaytime, blockchain, false, balance_allocated);
		if (rc) return -1;

		#if TEST_LOG_BALANCE
		amtint_t balance;
		Total::GetTotalBalance(dbconn, false, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, 0, 0, -1, 0, -1, false);
		BOOST_LOG_TRIVIAL(info) << "Transaction::SaveOutgoingTx new balance " << balance << " balance_allocated " << balance_allocated;
		//cerr << "Transaction::SaveOutgoingTx new balance " << balance << " balance_allocated " << balance_allocated;
		#endif
	}

	for (unsigned i = 0; i < nout; ++i)
	{
		Billet& bill = output_bills[i];

		if (!bill.create_tx)
			bill.create_tx = tx_id;
		else
			CCASSERT(bill.create_tx == tx_id);

		if (build_type == TX_BUILD_CANCEL_TX)
			bill.status = BILL_STATUS_ABANDONED;
		else
			bill.status = BILL_STATUS_PENDING;

		CCASSERTZ(bill.id);

		rc =  dbconn->BilletInsert(bill);
		if (rc) return -1;
	}

	// commit db writes

	if (RandTest(RTEST_CUZZ)) sleep(1);

	rc = dbconn->Commit();
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::SaveOutgoingTx error committing db transaction";

		return -1;
	}

	dbconn->DoDbFinishTx();

	finally.Clear();

	id = tx_id;

	return 0;
}

int Transaction::UpdatePolling(DbConn *dbconn, uint64_t next_commitnum)
{
	// Updating polling times for output addresses
	// Also sets query_commitnum and expected_commitnum

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::UpdatePolling next_commitnum " << next_commitnum;

	uint64_t now = time(NULL);

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	Secret secret;

	for (unsigned i = 0; i < nout; ++i)
	{
		Billet& bill = output_bills[i];
		Secret secret;

		rc = dbconn->SecretSelectSecret(&bill.address, TX_ADDRESS_BYTES, secret);
		if (rc) continue;

		secret.next_check = now + 1 + (rand() & 1);

		// don't do this so tx's sent by a copy of this wallet is detected:
		//if (secret.type == SECRET_TYPE_SEND_ADDRESS && secret.query_commitnum == 0)
		//	secret.query_commitnum = next_commitnum;		// start query at this commitnum (skip tx's from other wallets)

		if (next_commitnum > secret.expected_commitnum)
			secret.expected_commitnum = next_commitnum;		// continue fast polling until tx is seen with this commitnum

		rc = dbconn->SecretInsert(secret);
		if (rc) continue;
	}

	// commit db writes

	rc = dbconn->Commit();
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::UpdatePolling error committing db transaction";

		return -1;
	}

	dbconn->DoDbFinishTx();

	finally.Clear();

	return 0;
}

int Transaction::BeginAndReadTx(DbConn *dbconn, uint64_t id, bool or_greater)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::BeginAndReadTx id " << id << " or_greater = " << or_greater;

	//dbconn->BeginRead();	// for testing

	auto rc = dbconn->BeginRead();
	if (rc)
	{
		Clear();

		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	return ReadTx(dbconn, id, or_greater);
}

int Transaction::BeginAndReadTxRefId(DbConn *dbconn, const string& ref_id)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::BeginAndReadTxRefId ref_id " << ref_id;

	//dbconn->BeginRead();	// for testing

	auto rc = dbconn->BeginRead();
	if (rc)
	{
		Clear();

		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	return ReadTxRefId(dbconn, ref_id);
}

int Transaction::BeginAndReadTxIdDescending(DbConn *dbconn, uint64_t id)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::BeginAndReadTxIdDescending id " << id;

	auto rc = dbconn->BeginRead();
	if (rc)
	{
		Clear();

		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	return ReadTxIdDescending(dbconn, id);
}

int Transaction::BeginAndReadTxLevel(DbConn *dbconn, uint64_t level, uint64_t id)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::BeginAndReadTxLevel level " << level << " id " << id;

	auto rc = dbconn->BeginRead();
	if (rc)
	{
		Clear();

		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	return ReadTxLevel(dbconn, level, id);
}

int Transaction::ReadTx(DbConn *dbconn, uint64_t id, bool or_greater)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTx id " << id << " or_greater = " << or_greater;

	Clear();

	auto rc = dbconn->TransactionSelectId(id, *this, or_greater);
	if (rc > 0 && or_greater)
	{
		BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTx transaction id " << id << " returned " << rc;

		return 1;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTx error reading transaction id " << id;

		return -1;
	}

	return ReadTxBillets(dbconn);
}

int Transaction::ReadTxRefId(DbConn *dbconn, const string& ref_id)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTxRefId ref_id " << ref_id;

	Clear();

	auto rc = dbconn->TransactionSelectRefId(ref_id.c_str(), *this);
	if (rc > 0)
	{
		BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTxRefId transaction id " << id << " returned " << rc;

		return 1;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxRefId error reading transaction id " << id;

		return -1;
	}

	return ReadTxBillets(dbconn);
}

int Transaction::ReadTxIdDescending(DbConn *dbconn, uint64_t id)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTxIdDescending id " << id;

	Clear();

	auto rc = dbconn->TransactionSelectIdDescending(id, *this);
	if (rc > 0)
	{
		BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTxIdDescending transaction id " << id << " returned " << rc;

		return 1;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxIdDescending error reading transaction id " << id;

		return -1;
	}

	return ReadTxBillets(dbconn);
}

int Transaction::ReadTxLevel(DbConn *dbconn, uint64_t level, uint64_t id)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTxLevel level " << level << " id " << id;

	Clear();

	auto rc = dbconn->TransactionSelectLevel(level, id, *this);
	if (rc > 0)
	{
		BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTxLevel transaction level " << level << " id " << id << " returned " << rc;

		return 1;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxLevel error reading transaction level " << level << " id " << id;

		return -1;
	}

	return ReadTxBillets(dbconn);
}

int Transaction::ReadTxBillets(DbConn *dbconn)
{
	auto rc = dbconn->BilletSelectCreateTx(id, nout, &output_bills[0], TX_MAXOUT);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxBillets error reading output billets for transaction id " << id;

		return -1;
	}

	rc = dbconn->BilletSelectSpendTx(id, nin, &input_bills[0], TX_MAXIN);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxBillets error reading input billets for transaction id " << id;

		return -1;
	}

	// FUTURE: only read destinations and accounts if needed

	for (unsigned i = 0; i < nout; ++i)
	{
		// FUTURE: destination might be zero if tx was created in another wallet and only address is known by this wallet?

		Billet& bill = output_bills[i];

		rc = dbconn->SecretSelectId(bill.dest_id, output_destinations[i]);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxBillets error reading destination id " << bill.dest_id << " for output billet " << i << " id " << bill.id << " of transaction id " << id;

			return -1;
		}

		if (output_destinations[i].account_id)
		{
			if (i && output_destinations[i].account_id == output_destinations[i-1].account_id)
				output_accounts[i] = output_accounts[i-1]; // FUTURE: test this
			else
			{
				rc = dbconn->AccountSelectId(output_destinations[i].account_id, output_accounts[i]);
				if (rc)
				{
					BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxBillets error reading account id " << output_destinations[i].account_id << " for destination id " << output_destinations[i].id << " for output billet " << i << " id " << bill.id << " of transaction id " << id;

					return -1;
				}
			}
		}
	}

	for (unsigned i = 0; i < nin; ++i)
	{
		// !!! TODO: destination might be zero?

		Billet& bill = input_bills[i];

		rc = dbconn->SecretSelectId(bill.dest_id, input_destinations[i]);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxBillets error reading destination id " << bill.dest_id << " for input billet " << i << " id " << bill.id << " of transaction id " << id;

			return -1;
		}

		if (input_destinations[i].account_id)
		{
			if (i && input_destinations[i].account_id == input_destinations[i-1].account_id)
				input_accounts[i] = input_accounts[i-1]; // FUTURE: test this
			else
			{
				rc = dbconn->AccountSelectId(input_destinations[i].account_id, input_accounts[i]);
				if (rc)
				{
					BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxBillets error reading account id " << input_destinations[i].account_id << " for destination id " << input_destinations[i].id << " for input billet " << i << " id " << bill.id << " of transaction id " << id;

					return -1;
				}
			}
		}
	}

	return 0;
}

bool Transaction::WeSent(bool incwatch)
{
	if (we_sent[incwatch] < 0)
		CalcWeSent(incwatch);

	return we_sent[incwatch];
}

bool Transaction::InputsInvolveWatchOnly()
{
	if (inputs_involve_watchonly < 0)
		CalcWeSent(0);

	return inputs_involve_watchonly;
}

void Transaction::CalcWeSent(bool incwatch)
{
	we_sent[incwatch] = false;
	inputs_involve_watchonly = false;

	for (unsigned i = 0; i < nin; ++i)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CalcWeSent input_destinations " << i << " " << input_destinations[i].DebugString();

		we_sent[incwatch] |= (input_destinations[i].DestinationFromThisWallet(incwatch));

		inputs_involve_watchonly |= input_destinations[i].TypeIsWatchOnlyDestination();
	}

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CalcWeSent nin " << nin << " incwatch " << incwatch << " we_sent " << we_sent[incwatch] << " inputs_involve_watchonly " << inputs_involve_watchonly;
}

#if TEST_BIG_DIVISION

static void TestBigDiv()
{
	bigint_t a, b, c, d, e;

	//a = b / c;	// divide by zero
	//return;

	for (unsigned i = 1; i < 256; ++i)
	{
		a.randomize();
		bigint_mask(a, 256 - i);

		while (true)
		{
			b.randomize();
			bigint_mask(b, i);
			if (b)
				break;
		}

		for (unsigned j = 0; j <= i; ++j)
		{
			c.randomize();
			bigint_mask(c, i - j);
			//cerr << "TestBigDiv " << i << " " << j << " " << hex << b << " " << c << dec << " " << (c < b) << endl;
			if (c < b)
				break;
			CCASSERT(j < i);
		}

		//d = a * b + c;
		mulBigInt(a, b, d, false);
		addBigInt(d, c, d, false);

		e = d / b;

		//cerr << "TestBigDiv " << i << " " << hex << a << " " << b << " " << c << " " << d << " " << e << dec << endl;

		CCASSERT(e == a);
	}
}

#endif

void Transaction::SetAdjustedAmounts(bool incwatch)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts incwatch " << incwatch << " id " << id;

#if TEST_BIG_DIVISION
	for (unsigned i = 0; i < 8000; ++i)
		TestBigDiv();
#endif

	// the bitcoin-emulation API reports each txout (except change) as a separate transaction with its own txid
	// the donation is assigned pro rata to the non-change txout's + the excess txin amount
	//	Note: if a transaction comes in from outside, the change outputs might not be marked
	// the total transaction amount for the txout is generally the same as the txout amount, except:
	//	if the txin's exceed the txout's, then the excess txin's are assigned pro rata to all the non-zero txout's

	// TODO: needs to be tested to work correctedly as more capabilities are added to wallet

	bigint_t total_sent = 0UL;
	bigint_t sum_sent = 0UL;
	bigint_t sum_out = 0UL;
	bigint_t sum_in = 0UL;

	for (unsigned i = 0; i < nout; ++i)
	{
		Billet& bill = output_bills[i];

		if (bill.asset == 0 && !bill.BillIsChange())
		{
			total_sent = total_sent + bill.amount;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts output " << i << " amount " << bill.amount << " total_sent " << total_sent;
		}

		if (bill.asset == 0 && !output_destinations[i].DestinationFromThisWallet(false))
		{
			sum_sent = sum_sent + bill.amount;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts output " << i << " amount " << bill.amount << " sum_sent " << sum_sent;
		}

		if (bill.asset == 0)
		{
			sum_out = sum_out + bill.amount;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts output " << i << " amount " << bill.amount << " sum_out " << sum_out;
		}
	}

	for (unsigned i = 0; i < nin; ++i)
	{
		Billet& bill = input_bills[i];

		if (bill.asset == 0)
		{
			sum_in = sum_in + bill.amount;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts input " << i << " amount " << bill.amount << " sum_in " << sum_in;
		}
	}

	auto excess_in = sum_in;
	if (excess_in > sum_out + donation)
		excess_in = excess_in - sum_out - donation;
	else
		excess_in = 0UL;

	sum_sent = sum_sent + excess_in;

	bigint_t net_donation = 0UL;
	if (WeSent(incwatch))
		net_donation = donation + excess_in;

	bigint_t total_donation = 0UL;
	bigint_t total_out = 0UL;

	for (unsigned i = 0; i < nout; ++i)
	{
		Billet& bill = output_bills[i];

		if (bill.asset == 0 && !bill.BillIsChange() && total_sent)
		{
			adj_donations[i] = (net_donation * bill.amount + total_sent/2) / total_sent;

			total_donation = total_donation + adj_donations[i];

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts output " << i << " adj_donations " << adj_donations[i] << " total_donation " << total_donation;
		}

		if (bill.asset == 0 && bill.amount && sum_sent)
		{
			adj_amounts[i] = bill.amount + (excess_in * bill.amount + sum_sent/2) / sum_sent;

			total_out = total_out + adj_amounts[i];

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts output " << i << " adj_amount " << adj_amounts[i] << " total_out " << total_out;
		}
	}

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts sum_out " << sum_out << " sum_in " << sum_in << " donation " << donation << " excess_in " << excess_in << " sum_sent " << sum_sent << " total_sent " << total_sent << " total_out " << total_out << " total_donation " << total_donation;

	const bigint_t one = 1UL;

	for (unsigned iter = 0; iter < 2000 && total_donation != net_donation; ++iter)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts net_donation " << net_donation << " total_donation " << total_donation;

		for (unsigned i = 0; i < nout && total_donation < net_donation; ++i)
		{
			Billet& bill = output_bills[i];

			if (bill.asset == 0 && !bill.BillIsChange())
			{
				adj_donations[i] = adj_donations[i] + one;
				total_donation = total_donation + one;
			}
		}

		for (unsigned i = nout; i > 0 && total_donation > net_donation; --i)
		{
			if (adj_donations[i-1])
			{
				adj_donations[i-1] = adj_donations[i-1] - one;
				total_donation = total_donation - one;
			}
		}
	}

	for (unsigned iter = 0; iter < 2000 && total_out != sum_sent; ++iter)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts total_out " << total_out << " sum_sent " << sum_sent;

		for (unsigned i = 0; i < nout && total_out < sum_sent; ++i)
		{
			Billet& bill = output_bills[i];

			if (bill.asset == 0 && bill.amount)
			{
				adj_amounts[i] = adj_amounts[i] + one;
				total_out = total_out + one;
			}
		}

		for (unsigned i = nout; i > 0 && total_out > sum_sent; --i)
		{
			if (adj_amounts[i-1])
			{
				adj_amounts[i-1] = adj_amounts[i] - one;
				total_out = total_out - one;
			}
		}
	}
}

int Transaction::CreateTxMint(DbConn *dbconn, TxQuery& txquery) // throws RPC_Exception
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CreateTxMint";

	// returns -1 if mint tx's are not allowed

	Clear();

	TxParams txparams;
	QueryInputResults inputs;

	auto rc = txquery.QueryInputs(NULL, 0, txparams, inputs);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc) throw txrpc_server_error;
	if (txparams.NotConnected()) throw txrpc_server_error;

	if (Implement_CCMint(txparams.blockchain))
	{
		if (inputs.param_level >= CC_MINT_COUNT)
			return -1;
	}
	else if (!IsTestnet(txparams.blockchain))
		return -1;

	Secret address;
	SpendSecretParams params;
	memset((void*)&params, 0, sizeof(params));

	rc = address.CreateNewSecret(dbconn, SECRET_TYPE_SELF_ADDRESS, MINT_DESTINATION_ID, txparams.blockchain, params);
	if (rc) throw txrpc_wallet_error;

	//tx_dump_spend_secret_params_stream(cerr, params);

	TxPay ts;

	tx_init(ts);
	ts.tag_type = CC_TYPE_MINT;

	//ts.no_proof = rand() & 3;	// for testing, randomly create a bad mint

	ts.source_chain = txparams.blockchain;
	ts.param_level = inputs.param_level;
	ts.param_time = inputs.param_time;
	ts.amount_bits = txparams.amount_bits;
	ts.donation_bits = txparams.donation_bits;
	ts.exponent_bits = txparams.exponent_bits;
	ts.outvalmin = txparams.outvalmin;
	ts.outvalmax = txparams.outvalmax;
	ts.allow_restricted_addresses = true;
	ts.tx_merkle_root = inputs.merkle_root;

	ts.nout = 1;
	TxOut& txout = ts.outputs[0];
	memcpy(&txout.addrparams, &params.addrparams, sizeof(txout.addrparams));

	txout.addrparams.__flags |= BILL_RECV_MASK;

	//tx_dump_address_params_stream(cerr, txout.addrparams);

	txout.M_pool = txparams.default_output_pool;

	donation = TX_CC_MINT_DONATION;
	ts.donation_fp = tx_amount_encode(donation, true, txparams.donation_bits, txparams.exponent_bits);

	uint64_t asset = 0;
	bigint_t amount;
	amount = TX_CC_MINT_AMOUNT;
	amount = amount - donation;
	txout.__amount_fp = tx_amount_encode(amount, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);

	FinishCreateTx(ts);

	CCASSERT(txout.M_address == address.value);

	// Save ts in db before submitting
	// (If tx submitted first, Poll thread could detect and save billets before this thread, and that would have to be sorted out...)

	rc = SaveOutgoingTx(dbconn);
	if (rc) throw txrpc_wallet_db_error;

	//BeginAndReadTx(dbconn, id);	// testing
	//BeginAndReadTx(dbconn, id);	// testing

	if (RandTest(RTEST_TX_ERRORS) && 0)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxMint simulating error after save, before submit";

		throw txrpc_simulated_error;
	}

	// submit ts to network

	uint64_t next_commitnum;

	//rc = txquery.SubmitTx(ts, next_commitnum);	// testing

	rc = txquery.SubmitTx(ts, next_commitnum);
	if (rc)
	{
		SetMintStatus(dbconn, TX_STATUS_ERROR);

		if (rc < 0)
			throw txrpc_server_error;
		else if (Implement_CCMint(txparams.blockchain) && !ts.param_level)
			throw RPC_Exception(RPC_VERIFY_REJECTED, "Mint not yet started");
		else
			throw txrpc_tx_rejected;
	}

	rc = UpdatePolling(dbconn, next_commitnum);
	(void)rc;

	if (g_interactive)
	{
		string amts;
		amount_to_string(asset, SUBTX_AMOUNT, amts);
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Mint transaction submitted to network.  If this transaction is accepted by the blockchain," << endl;
		cerr << amts << " units of currency will be credited to the newly-generated unique address " << hex << ts.outputs[0].M_address << dec << endl;
		for (unsigned i = 0; i < ts.nout; ++i)
		{
			amount_to_string(asset, output_bills[i].amount, amts);
			cerr << "Output " << i + 1 << "\n"
			"   billet commitment " << hex << ts.outputs[i].M_commitment << "\n"
			"   published asset   " << ts.outputs[i].M_asset_enc << "\n"
			"   published amount  " << amts << "\n"
			"   address           " << ts.outputs[i].M_address << dec << endl;
		}
		amount_to_string(asset, donation, amts);
		cerr << "Donation amount " << amts << endl;
		cerr << endl;
	}

	return 0;
}

void Transaction::SetMintStatus(DbConn *dbconn, unsigned _status)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetMintStatus status " << _status;

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		throw txrpc_wallet_db_error;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	rc = dbconn->TransactionSelectId(id, *this);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(warning) << "Transaction::SetMintStatus error reading tx id " << id;

		throw txrpc_wallet_db_error;
	}

	status = _status;

	rc = dbconn->TransactionInsert(*this);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(warning) << "Transaction::SetMintStatus error saving tx id " << id;

		throw txrpc_wallet_db_error;
	}

	rc = dbconn->Commit();
	if (rc) throw txrpc_wallet_db_error;

	dbconn->DoDbFinishTx();

	finally.Clear();
}

void Transaction::SetPendingBalances(DbConn *dbconn, uint64_t blockchain, const bigint_t& balance_allocated, const bigint_t& balance_pending)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::SetPendingBalances balance_allocated " << balance_allocated << " balance_pending " << balance_pending;

	uint64_t asset = 0;
	unsigned delaytime = 0;	// TBD: this will get more complicated if billets have various delaytimes

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		throw txrpc_wallet_db_error;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	Total::AddBalances(dbconn, true, TOTAL_TYPE_ALLOCATED_BIT, 0, 0, asset, delaytime, blockchain, true, balance_allocated);
	Total::AddBalances(dbconn, true, TOTAL_TYPE_PENDING_BIT, 0, 0, asset, delaytime, blockchain, true, balance_pending);

	#if TEST_LOG_BALANCE
	amtint_t balance;
	Total::GetTotalBalance(dbconn, false, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, 0, 0, -1, 0, -1, false);
	BOOST_LOG_TRIVIAL(info) << "Transaction::SetPendingBalances new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending;
	//cerr << "SetPendingBalances new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending << endl;
	#endif

	if (RandTest(RTEST_CUZZ)) sleep(1);

	rc = dbconn->Commit();
	if (rc) throw txrpc_wallet_db_error;

	dbconn->DoDbFinishTx();

	finally.Clear();
}

int Transaction::LocateBillet(DbConn *dbconn, uint64_t blockchain, const bigint_t& amount, const bigint_t& min_amount, Billet& bill, uint64_t& billet_count, const bigint_t& total_required)
{
	// returns:
	//	0 if billet located and allocated
	//	1 if no billet could be allocated and amount was added to required (resulting required >= pending)
	//	throws txrpc_insufficient_funds if no billet could be allocated and not enough pending

	//if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::LocateBillet amount " << amount << " min_amount " << min_amount << " total_required " << total_required;

	uint64_t asset = 0;
	unsigned max_delaytime = 0;

	if (RandTest(RTEST_CUZZ)) sleep(1);

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		throw txrpc_wallet_db_error;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	billet_count = Billet::GetBilletAvailableCount();

	while (true)	// break on success
	{
		auto rc = dbconn->BilletSelectAmount(blockchain, asset, amount, max_delaytime, bill);
		if (rc < 0) throw txrpc_wallet_db_error;
		if (!rc) break;

		rc = dbconn->BilletSelectAmountMax(blockchain, asset, max_delaytime, bill);
		if (rc < 0) throw txrpc_wallet_db_error;
		if (!rc && bill.amount >= min_amount)
			break;

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::LocateBillet no billet found amount " << amount << " min_amount " << min_amount << " total_required " << total_required;

		bill.Clear();

		/* FUTURE: Ideally, the wallet would immediately count the billet change as pending, and then have a way to figure
		out when that change is immediately needed and then run a change aka split intermediate tx.  But since the
		wallet isn't currently capable of running split tx's, instead the wallet allocates total_required only if
		it does not exceed available, and then the amount allocated is released after the wait before retrying.  This
		ensures the allocated amount is tracked correctly, and that the required amount is not allocated more than once
		for a single tx */

		rc = Total::AddNoWaitAmounts(0UL, true, total_required, true);
		if (rc)
		{
			if (g_interactive)
			{
				string amt;
				amount_to_string(0, total_required, amt);

				lock_guard<FastSpinLock> lock(g_cout_lock);
				cerr <<

R"(The wallet appears to have sufficient balance, but there are not enough billets available to construct the
transaction. It may be possible to manually solve this problem by sending transactions to yourself to split the wallet
balance into more output billets (total required = )"

				<< amt << ").\n" << endl;
			}

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::LocateBillet need more billets to complete tx";

			throw txrpc_insufficient_funds;
		}

		return 1;
	}

	CCASSERT(bill.status == BILL_STATUS_CLEARED);

	bill.status = BILL_STATUS_ALLOCATED;

	rc = dbconn->BilletInsert(bill);
	if (rc) throw txrpc_wallet_db_error;

	if (RandTest(RTEST_CUZZ)) sleep(1);

	rc = dbconn->Commit();
	if (rc) throw txrpc_wallet_db_error;

	dbconn->DoDbFinishTx();

	finally.Clear();

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::LocateBillet amount " << amount << " min_amount " << min_amount << " found billet amount " << bill.amount;

	return 0;
}

void Transaction::ReleaseTxBillet(DbConn *dbconn, Billet& bill)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::ReleaseTxBillet";

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		throw txrpc_wallet_db_error;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	rc = dbconn->BilletSelectId(bill.id, bill);
	if (rc) throw txrpc_wallet_db_error;

	if (bill.status != BILL_STATUS_ALLOCATED)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::ReleaseTxBillet skipping release of billet id " << bill.id << " status " << bill.status;

		return;
	}

	bill.status = BILL_STATUS_CLEARED;

	rc = dbconn->BilletInsert(bill);
	if (rc) throw txrpc_wallet_db_error;

	if (RandTest(RTEST_CUZZ)) sleep(1);

	rc = dbconn->Commit();
	if (rc) throw txrpc_wallet_db_error;

	dbconn->DoDbFinishTx();

	finally.Clear();
}

int Transaction::WaitNewBillet(const bigint_t& total_required, const uint64_t billet_count, const uint32_t timeout, unique_lock<mutex>& lock, bool test_fail) // throws RPC_Exception)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::WaitNewBillet";

	if (lock) lock.unlock();

	if (test_fail && RandTest(RTEST_TX_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::WaitNewBillet simulating error";

		Total::AddNoWaitAmounts(0UL, true, total_required, false);

		throw txrpc_simulated_error;
	}

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Waiting for pending billets to become available...\n" << endl;
	}

	// TODO: adjust the wait time to not exceed the transaction timeout?

	auto rc = Billet::WaitNewBillet(billet_count, g_params.billet_wait_time);
	if (g_shutdown) throw txrpc_shutdown_error;

	Total::AddNoWaitAmounts(0UL, true, total_required, false);

	CheckTimeout(timeout);

	if (rc)
	{
		if (g_interactive)
		{
			lock_guard<FastSpinLock> lock(g_cout_lock);
			cerr <<

R"(The wallet appears to have sufficient balance, but it timed out waiting for expected billets to clear and become
spendable. This transaction might succeed if it's resubmitted.  Note: the timeout period can be adjusted by using the
tx-new-billet-wait-sec command line option when starting the wallet.)"

			"\n" << endl;
		}

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::WaitNewBillet billet wait failed; throwing txrpc_insufficient_funds";

		throw txrpc_insufficient_funds;
	}

	//throw txrpc_simulated_error;	// for testing

	return 1;	// a billet cleared, so retry
}

void Transaction::CheckTimeout(const uint32_t timeout)
{
	int32_t dt = timeout - time(NULL);

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CheckTimeout timeout " << timeout << " dt " << dt;

	if (dt < 0)
	{
		if (g_interactive)
		{
			lock_guard<FastSpinLock> lock(g_cout_lock);
			cerr <<

R"(The wallet reached its time limit while attempting to build this transaction.  The transaction might succeed if it's
resubmitted. Note: the time limit can be adjusted by using the tx-create-timeout command line option when starting the
wallet.)"

			"\n" << endl;
		}

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CheckTimeout timed out; throwing txrpc_tx_timeout";

		throw txrpc_tx_timeout;
	}
}

int Transaction::ComputeChange(TxParams& txparams, const bigint_t& input_total)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::ComputeChange build_type " << build_type << " nin " << nin << " input_total " << input_total << " send amount " << SUBTX_AMOUNT;

	if (input_total < SUBTX_AMOUNT && build_type != TX_BUILD_CONSOLIDATE && build_type != TX_BUILD_CANCEL_TX)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::ComputeChange build_type " << build_type << " nin " << nin << " input_total " << input_total << " < send amount " << SUBTX_AMOUNT;

		return 1;
	}

	auto remainder = input_total;

	if (build_type != TX_BUILD_CONSOLIDATE && build_type != TX_BUILD_CANCEL_TX)
		remainder = remainder - SUBTX_AMOUNT;

	// set donation and change

	nout = WALLET_TX_MINOUT - 1;

	while (true)
	{
		if (++nout > TX_MAXOUT)
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::ComputeChange build_type " << build_type << " nin " << nin << " input_total " << input_total << " send amount " << SUBTX_AMOUNT << " remainder " << remainder << " unable to complete tx nout " << nout;

			throw txrpc_wallet_error;
		}

		txparams.ComputeDonation(nout, nin, donation);
		auto amount_fp = tx_amount_encode(donation, true, txparams.donation_bits, txparams.exponent_bits);
		tx_amount_decode(amount_fp, donation, true, txparams.donation_bits, txparams.exponent_bits);

		if (donation > remainder)
		{
			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(info) << "Transaction::ComputeChange build_type " << build_type << " nin " << nin << " nout " << nout << " donation " << donation << " > remainder " << remainder;

			return 1;
		}

		bigint_t change = 0UL;

		for (unsigned i = (build_type != TX_BUILD_CONSOLIDATE && build_type != TX_BUILD_CANCEL_TX); i < nout; ++i)
		{
			Billet& bill = output_bills[i];

			// TODO: ignore outvalmin and outvalmax when asset > 0
			amount_fp = tx_amount_encode(remainder - change - donation, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);

			tx_amount_decode(amount_fp, bill.amount, false, txparams.amount_bits, txparams.exponent_bits);

			change = change + bill.amount;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ComputeChange build_type " << build_type << " nout " << nout << " change billet " << i << " amount " << bill.amount << " total change " << change << " donation " << donation;

			if (change + donation > remainder)
			{
				BOOST_LOG_TRIVIAL(error) << "Transaction::ComputeChange build_type " << build_type << " change + donation " << change + donation << " > remainder " << remainder;

				throw txrpc_wallet_error;
			}
		}

		if (change + donation == remainder)
			break;
	}

	return 0;
}

int Transaction::FillOutTx(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, uint64_t dest_chain, TxPay& ts)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FillOutTx build_type " << build_type << " nin " << nin << " nout " << nout;

	CCASSERT(nin <= TX_MAXINPATH);

	uint64_t commitnums[TX_MAXINPATH];

	for (unsigned i = 0; i < nin; ++i)
		commitnums[i] = input_bills[i].commitnum;

	QueryInputResults inputs;

	auto rc = txquery.QueryInputs(commitnums, nin, txparams, inputs);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc) throw txrpc_server_error;
	if (txparams.NotConnected()) throw txrpc_server_error;

	if (!dest_chain)
		dest_chain = txparams.blockchain;

	if (Implement_CCMint(dest_chain) && inputs.param_level < CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
		throw txrpc_tx_rejected;

	// compute and recheck encoded amounts using txparams returned by QueryInputs

	tx_init(ts);

	bigint_t check;
	txparams.ComputeDonation(nout, nin, check);
	ts.donation_fp = tx_amount_encode(check, true, txparams.donation_bits, txparams.exponent_bits);
	tx_amount_decode(ts.donation_fp, check, true, txparams.donation_bits, txparams.exponent_bits);
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FillOutTx donation " << donation << " check " << check << " donation_fp " << ts.donation_fp << " donation_bits " << txparams.donation_bits << " exponent_bits " << txparams.exponent_bits;
	if (donation != check)
	{
		BOOST_LOG_TRIVIAL(warning) << "Transaction::FillOutTx donation encoding mismatch " << donation << " != " << check;

		return 1;	// params may have changed, so retry
	}

	for (unsigned i = 0; i < nout; ++i)
	{
		TxOut& txout = ts.outputs[i];
		Billet& bill = output_bills[i];

		// TODO: ignore outvalmin and outvalmax when asset > 0
		txout.__amount_fp  = tx_amount_encode(bill.amount, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);
		tx_amount_decode(txout.__amount_fp, check, false, txparams.amount_bits, txparams.exponent_bits);
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FillOutTx output " << i << " amount " << bill.amount << " check " << check << " amount_fp " << txout.__amount_fp << " amount_bits " << txparams.amount_bits << " exponent_bits " << txparams.exponent_bits << " outvalmin " << txparams.outvalmin << " outvalmax " << txparams.outvalmax;
		if (bill.amount != check)
		{
			BOOST_LOG_TRIVIAL(warning) << "Transaction::FillOutTx output amount mismatch " << bill.amount << " != " << check;

			return 1;	// params may have changed, so retry
		}
	}

	// set the ts values

	ts.tag_type = CC_TYPE_TXPAY;

	ts.source_chain = txparams.blockchain;
	ts.param_level = inputs.param_level;
	ts.param_time = inputs.param_time;
	ts.amount_bits = txparams.amount_bits;
	ts.donation_bits = txparams.donation_bits;
	ts.exponent_bits = txparams.exponent_bits;
	ts.outvalmin = txparams.outvalmin;
	ts.outvalmax = txparams.outvalmax;
	ts.allow_restricted_addresses = true;
	ts.tx_merkle_root = inputs.merkle_root;

	ts.nout = nout;
	ts.nin = nin;
	ts.nin_with_path = nin;

	uint64_t asset_mask = (txparams.asset_bits < 64 ? ((uint64_t)1 << txparams.asset_bits) - 1 : -1);
	uint64_t amount_mask = (txparams.amount_bits < 64 ? ((uint64_t)1 << txparams.amount_bits) - 1 : -1);

	for (unsigned i = 0; i < nout; ++i)
	{
		TxOut& txout = ts.outputs[i];

		txout.M_pool = txparams.default_output_pool;
		txout.asset_mask = asset_mask;
		txout.amount_mask = amount_mask;
	}

	for (unsigned i = 0; i < nin; ++i)
	{
		Billet& bill = input_bills[i];
		TxIn& txin = ts.inputs[i];

		if (bill.blockchain != dest_chain)
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::FillOutTx input " << i << " blockchain mismatch " << bill.blockchain << " != " << dest_chain;

			throw txrpc_wallet_error;
		}

		if (bill.pool != txparams.default_output_pool && !g_params.foundation_wallet)	//@@! need new tx type
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::FillOutTx input " << i << " pool mismatch " << bill.pool << " != " << txparams.default_output_pool;

			throw RPC_Exception(RPCErrorCode(-32001), "Input bill pool does not match transaction server's default output pool");
		}

		if (bill.asset != 0)
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::FillOutTx input " << i << " asset mismatch " << bill.asset << " != 0";

			throw txrpc_wallet_error;
		}

		// retrieve the spend secret, destnum and paynum

		Secret secret;
		auto rc = dbconn->SecretSelectSecret(&bill.address, TX_ADDRESS_BYTES, secret);
		if (rc) throw txrpc_wallet_db_error;

		rc = Secret::GetParentValue(dbconn, SECRET_TYPE_SPEND, secret.id, txin.params, &txin.secrets[0], sizeof(txin.secrets));
		if (rc) throw txrpc_wallet_db_error;

		CCRandom(&bill.spend_hashkey, TX_HASHKEY_WIRE_BYTES);

		//cerr << "bill " << i << " address " << hex << bill.address << " spend_hashkey " << bill.spend_hashkey << dec << endl;

		if (TRACE_TRANSACTIONS)
		{
			tx_amount_decode(bill.amount_fp, check, false, txparams.amount_bits, txparams.exponent_bits);
			BOOST_LOG_TRIVIAL(trace) << "Transaction::FillOutTx input " << i << " amount " << bill.amount << " check " << check << " amount_fp " << bill.amount_fp << " amount_bits " << txparams.amount_bits << " exponent_bits " << txparams.exponent_bits << " invalmax " << txparams.invalmax;
		}

		txin.__asset = bill.asset;
		txin.__amount_fp = bill.amount_fp;
		txin.invalmax = txparams.invalmax;

		txin.M_pool = bill.pool;
		txin.__M_commitment_iv = bill.commit_iv;
		txin._M_commitment = bill.commitment;
		txin._M_commitnum = bill.commitnum;

		txin.merkle_root = ts.tx_merkle_root;
		txin.enforce_trust_secrets = 1;

		txin.S_hashkey = bill.spend_hashkey;

		txin.pathnum = i + 1;

		for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
			ts.inpaths[i].__M_merkle_path[j] = inputs.merkle_paths[i][j];
	}

	return 0;
}

void Transaction::SetAddresses(DbConn *dbconn, uint64_t dest_chain, Secret &destination, TxPay& ts)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAddresses build_type " << build_type << " nin " << nin << " nout " << nout << " dest_chain " << dest_chain << " destination id " << destination.id;

	for (unsigned i = (build_type == TX_BUILD_FINAL); i < nout; ++i)
	{
		Secret secret;
		SpendSecretParams params;
		memset((void*)&params, 0, sizeof(params));

		auto rc = secret.CreateNewSecret(dbconn, SECRET_TYPE_SELF_ADDRESS, SELF_DESTINATION_ID, dest_chain, params);
		if (rc) throw txrpc_wallet_error;

		TxOut& txout = ts.outputs[i];
		memcpy(&txout.addrparams, &params.addrparams, sizeof(txout.addrparams));

		txout.addrparams.__flags |= BILL_RECV_MASK | BILL_FLAG_TRUSTED | BILL_IS_CHANGE;
	}

	if (build_type == TX_BUILD_FINAL)
	{
		// set destination output

		Secret secret;
		SpendSecretParams params;
		memset((void*)&params, 0, sizeof(params));

		auto rc = secret.CreateNewSecret(dbconn, SECRET_TYPE_SEND_ADDRESS, destination.id, dest_chain, params);
		if (rc) throw txrpc_wallet_error;

		TxOut& txout = ts.outputs[0];
		memcpy(&txout.addrparams, &params.addrparams, sizeof(txout.addrparams));

		txout.addrparams.__flags |= Billet::FlagsFromDestinationType(destination.type) | BILL_FLAG_TRUSTED;
	}
}

int Transaction::CreateTxPay(DbConn *dbconn, TxQuery& txquery, bool async, string& ref_id, const string& encoded_dest, uint64_t dest_chain, const bigint_t& destination, const bigint_t& amount, const string& comment, const string& comment_to, const bool subfee) // throws RPC_Exception
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CreateTxPay async " << async << " ref_id " << ref_id << " dest_chain " << hex << dest_chain << dec << " destination " << destination << " amount " << amount << " subfee " << subfee << " comment " << comment << " comment_to " << comment_to;

	/*
		For privacy, all tx's are 1 to 4 in -> 2 out
			sometimes will need 1 or 2 in for speed or because wallet hold only 1 or 2 unspent billets
		Target mix: 1 in 20%; 2 in 20%; 3 in 20-40%; 4 in 20-40%?
			net degragmenation per tx is 0.6 to 0.8 billets
		if there is no change, a zero value change billet is created
		If payment is too large for one output billet, then send more than one tx
			for privacy, each output billet except the last is the max possible output value
		If payment has too much dynamic range for one output billet, then send more than one tx
		For privacy, donation is always exact
		May need 5 output billets to get enough dynamic range for payment and change
			each billet is 35 bits; decoded is 128 bits, so 4 billets needed to cover full dynamic range
		May need multiple output billets to break input billet larger than max output value
		TODO: make tx's with 3 to 5 output billets as cover traffic for privacy

		reserve funds for amount plus max estimated donation if possible, or at least min estimated donation
		add destination to db, and if that fails retrieve existing destination
		create a tx
		add output billets to get exact amount
			output billets do not yet have addresses
		add up to 4 large input billets until output + donation is covered
			with each billet added, check to adjust fund reservation
			if large billets aren't enough, run a consolidation operation
		randomly add additional small input billets
			with each billet added, check to adjust fund reservation
			TODO: when adding billets, try not to overflow dynamic range
		add change billets until tx is balanced
			with each billet added, check to adjust fund reservation
			change billets do not yet have addresses
		query input serialnums to make sure none already spent
		query input paths
		re-check outputs and donation based on returned params
		assign addresses to outputs and setup polling
		save tx to db
		submit tx
			if error, cancel polling?
			NOTE: if tx fails, there will be a gap in the paynum sequence; attempt to close gap?
		return txid
		when polling detects tx cleared:
			update tx & billet
			update balance

		TODO: add a "fast mode" that attempts to make a tx as fast as possible
			as many input bills as needed, but no more
	*/

	Clear();

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;
	if (txparams.NotConnected())
	{
		rc = g_txparams.UpdateParams(txparams, txquery);
		if (rc || txparams.NotConnected()) throw txrpc_server_error;
	}

	// check dest_chain
	// dest_chain comes from the encoded destination string and can be zero, which means use any blockchain
	// but if not zero, it must match the blockchain of the server to which the wallet is connected

	if (dest_chain && dest_chain != txparams.blockchain)
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Destination blockchain not supported by transaction server");

	// add destination to db, and if that fails retrieve existing destination

	Secret secret;
	secret.type = SECRET_TYPE_SEND_DESTINATION;
	secret.dest_chain = dest_chain;		// save with dest_chain specified in encoded destination string
	secret.value = destination;

	rc = secret.ImportSecret(dbconn);
	if (rc) throw txrpc_wallet_error;

	if (!secret.TypeIsDestination())
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPay secret is not a valid destination; " << secret.DebugString();

		throw txrpc_wallet_error;
	}

	if (!dest_chain)
		dest_chain = txparams.blockchain;	// if the destination string didn't specify a blockchain, use the transaction server blockchain

	rc = secret.CheckForConflict(dbconn, txquery, dest_chain);
	if (rc < 0)
		throw txrpc_wallet_error;
	else if (rc)
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "This destination has already been used by another wallet. To preserve privacy, a new unique destination is required.");

	if (!ref_id.length())
		ref_id = unique_id_generate(dbconn, "R", 0, 2);

	DbConn *thread_dbconn = NULL;
	TxQuery *thread_txquery = NULL;
	TxBuildEntry *entry = NULL;

	Finally finally(boost::bind(&Transaction::CreateTxPayThreadCleanup, &thread_dbconn, &thread_txquery, &entry, false));

	rc = g_txbuildlist.StartBuild(dbconn, ref_id, encoded_dest, dest_chain, destination, amount, &entry, *this);
	if (rc)
	{
		if (entry)
		{
			if (async)
				return 1;

			if (g_interactive)
			{
				lock_guard<FastSpinLock> lock(g_cout_lock);
				cerr << "Waiting for prior transaction with reference id " << ref_id << " to complete" << endl;
			}

			g_txbuildlist.WaitForCompletion(dbconn, entry, *this);
		}
		else if (g_interactive)
		{
			lock_guard<FastSpinLock> lock(g_cout_lock);
			cerr << "Returning results of prior transaction with reference id " << ref_id << endl;
		}

		if (StatusIsNotError())
			return 0;
		else
			throw RPC_Exception(RPC_TRANSACTION_FAILED, "Previously submitted transaction failed");
	}

	CCASSERT(entry);

	if (g_interactive)
	{
		string amts;
		amount_to_string(0, amount, amts);
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Sending " << amts << " units to destination " << encoded_dest << endl;
		if (subfee || comment.length() || comment_to.length())
			cerr << "The \"comment\", \"comment-to\" and \"subtractfeefromamount\" options are not yet implemented and will be ignored." << endl;
		cerr << endl;
	}

	if (g_shutdown)
		throw txrpc_shutdown_error;

	if (!async)
	{
		try
		{
			return DoCreateTxPay(dbconn, txquery, txparams, entry, secret);
		}
		catch (const RPC_Exception& e)
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPay caught exception \"" << e.what() << "\" entry " << entry->DebugString();

			SaveErrorTx(dbconn, entry->ref_id);

			throw;
		}
	}
	else
	{
		thread_dbconn = new DbConn(true);
		CCASSERT(thread_dbconn);

		thread_txquery = g_lpc_service.GetConnection(false);
		if (!thread_txquery)
			throw RPC_Exception(RPC_MISC_ERROR, "Exceeded maximum number of transaction threads");

		auto t = new thread(&Transaction::CreateTxPayThread, thread_dbconn, thread_txquery, txparams, entry, secret);
		if (!t)
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPay error creating transaction thread";

			throw txrpc_wallet_error;
		}

		finally.Clear();

		return 1;
	}
}

void Transaction::CreateTxPayThreadCleanup(DbConn **dbconn, TxQuery **txquery, TxBuildEntry **entry, bool dec_thread_count)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CreateTxPayThreadCleanup dec_thread_count " << dec_thread_count;

	if (*entry)
	{
		g_txbuildlist.ReleaseEntry(*entry);
		*entry = NULL;
	}

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

	if (dec_thread_count)
		--tx_thread_count;
}

void Transaction::Shutdown()
{
	while (true)
	{
		auto n = tx_thread_count.load();

		BOOST_LOG_TRIVIAL(info) << "Transaction::Shutdown " << n << " threads running";

		if (!n)
			return;

		usleep(500*1000);
	}
}

void Transaction::CreateTxPayThread(DbConn *dbconn, TxQuery* txquery, TxParams txparams, TxBuildEntry *entry, Secret secret)
{
	BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPayThread entry " << entry->DebugString();

	++tx_thread_count;

	Finally finally(boost::bind(&Transaction::CreateTxPayThreadCleanup, &dbconn, &txquery, &entry, true));

	try
	{
		Transaction tx;

		tx.DoCreateTxPay(dbconn, *txquery, txparams, entry, secret);
	}
	catch (const RPC_Exception& e)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPayThread caught exception \"" << e.what() << "\" entry " << entry->DebugString();

		//cerr << "Transaction::CreateTxPayThread caught exception: " << e.what() << endl;

		SaveErrorTx(dbconn, entry->ref_id);
	}
}

void Transaction::SaveErrorTx(DbConn *dbconn, const string& ref_id)
{
	if (!ref_id.length())
		return;

	// make sure some transaction is saved so ref_id will show an error

	Transaction tx;

	auto rc = dbconn->TransactionSelectRefId(ref_id.c_str(), tx);
	if (!rc) return;

	tx.Clear();

	tx.ref_id = ref_id;
	tx.type = TX_TYPE_SEND;
	tx.status = TX_STATUS_ERROR;

	tx.SaveOutgoingTx(dbconn);
}

int Transaction::DoCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams &txparams, TxBuildEntry *entry, Secret& secret) // throws RPC_Exception
{
	BOOST_LOG_TRIVIAL(info) << "Transaction::DoCreateTxPay entry " << entry->DebugString();

	deque<Transaction> tx_list(1);

	unique_lock<mutex> lock(billet_allocate_mutex, defer_lock);

	Finally finally(boost::bind(&Transaction::CleanupSubTxs, dbconn, entry->dest_chain, ref(secret), ref(tx_list), ref(lock)));

	uint32_t timeout = time(NULL) + g_params.tx_create_timeout;

	bigint_t tx_round_up = 0UL;

	for (unsigned retry = 0; ; ++retry)
	{
		if (g_shutdown)
			throw txrpc_shutdown_error;

		if (RandTest(RTEST_TX_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPay simulating error pre tx, at shutdown check for retry " << retry;

			throw txrpc_simulated_error;
		}

		if (retry > (TEST_FEWER_RETRIES ? 230 : 1400))
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::DoCreateTxPay reached retry limit " << retry;

			throw txrpc_wallet_error;
		}

		if (retry > 200)
		{
			ccsleep(1);

			if (g_shutdown)
				throw txrpc_shutdown_error;
		}

		auto rc = TryCreateTxPay(dbconn, txquery, txparams, entry, secret, timeout, tx_round_up, tx_list, lock);
		if (!rc) break;

		CheckTimeout(timeout);

		CleanupSubTxs(dbconn, entry->dest_chain, secret, tx_list, lock);
	}

	g_txbuildlist.SetDone(entry);

	finally.Clear();

	*this = tx_list[0];

	return 0;
}

bool Transaction::SubTxIsActive(bool need_intermediate_txs) const
{
	if (!build_type)
		return false;
	else if (need_intermediate_txs)
		return (build_type != TX_BUILD_FINAL);
	else
		return (build_type == TX_BUILD_FINAL);
}

void Transaction::CleanupSubTxs(DbConn *dbconn, uint64_t dest_chain, const Secret &destination, deque<Transaction>& tx_list, unique_lock<mutex>& lock)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CleanupSubTxs size " << tx_list.size();

	bigint_t balance_allocated = 0UL;
	bigint_t balance_pending = 0UL;
	bool need_intermediate_txs = false;

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
		need_intermediate_txs |= tx->SubTxIsActive(true);

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		throw txrpc_wallet_db_error;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		tx->CleanupSubTx(dbconn, destination, need_intermediate_txs, balance_allocated, balance_pending);
	}

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CleanupSubTxs need_intermediate_txs " << need_intermediate_txs << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending;

	uint64_t asset = 0;
	unsigned delaytime = 0;

	// TBD: this will get more complicated if billets have various delaytimes or blockchains

	Total::AddBalances(dbconn, true, TOTAL_TYPE_ALLOCATED_BIT, 0, 0, asset, delaytime, dest_chain, false, balance_allocated);
	Total::AddBalances(dbconn, true, TOTAL_TYPE_PENDING_BIT, 0, 0, asset, delaytime, dest_chain, false, balance_pending);
	Total::AddNoWaitAmounts(balance_pending, false, 0UL, true);

	#if TEST_LOG_BALANCE
	amtint_t balance;
	Total::GetTotalBalance(dbconn, false, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, 0, 0, -1, 0, -1, false);
	BOOST_LOG_TRIVIAL(info) << "Transaction::CleanupSubTxs new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending;
	//cerr << "     CleanupSubTxs new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending << endl;
	#endif

	if (RandTest(RTEST_CUZZ)) sleep(1);

	rc = dbconn->Commit();
	if (rc) throw txrpc_wallet_db_error;

	dbconn->DoDbFinishTx();

	finally.Clear();

	Billet::NotifyNewBillet(false);	// wake up other threads to check if amounts pending are now sufficient
}

void Transaction::CleanupSubTx(DbConn *dbconn, const Secret &destination, bool need_intermediate_txs, bigint_t& balance_allocated, bigint_t& balance_pending)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CleanupSubTx need_intermediate_txs " << need_intermediate_txs << " " << DebugString();

	// for now, if tx was not successfully submitted, release all allocated billets
	//		TODO for future: keep some or all allocated billets and try to resume

	if (build_state < TX_BUILD_SUBMIT_UNKNOWN)
	{
		for (unsigned i = 0; i < nin; ++i)
		{
			Billet& bill = input_bills[i];

			auto rc = dbconn->BilletSelectId(bill.id, bill);
			if (rc)
			{
				BOOST_LOG_TRIVIAL(warning) << "Transaction::CleanupSubTx error reading input " << i << " billet id " << bill.id;

				continue;
			}

			if (bill.status != BILL_STATUS_ALLOCATED)
			{
				BOOST_LOG_TRIVIAL(debug) << "Transaction::CleanupSubTx skipping release of input " << i << " billet id " << bill.id << " status " << bill.status << " amount " << bill.amount;

				continue;
			}

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CleanupSubTx input " << i << " amount " << bill.amount;

			bill.status = BILL_STATUS_CLEARED;

			rc = dbconn->BilletInsert(bill);
			if (rc)
			{
				BOOST_LOG_TRIVIAL(warning) << "Transaction::CleanupSubTx error saving input " << i << " billet id " << bill.id;

				continue;
			}

			if (SubTxIsActive(need_intermediate_txs) && build_state >= TX_BUILD_TOTALED)
			{
				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CleanupSubTx balance_allocated input " << i << " amount " << bill.amount;

				balance_allocated = balance_allocated + bill.amount;
			}
		}

		if (SubTxIsActive(need_intermediate_txs) && build_state >= TX_BUILD_TOTALED)
		{
			for (unsigned i = 0; i < nout; ++i)
			{
				Billet& bill = output_bills[i];

				if (build_state >= TX_BUILD_SAVED)
				{
					auto rc = dbconn->BilletSelectId(bill.id, bill);
					if (rc)
					{
						BOOST_LOG_TRIVIAL(warning) << "Transaction::CleanupSubTx error reading output " << i << " billet id " << bill.id;

						continue;
					}

					if (bill.status != BILL_STATUS_PENDING)
					{
						BOOST_LOG_TRIVIAL(info) << "Transaction::CleanupSubTx skipping removal of output " << i << " billet id " << bill.id << " status " << bill.status << " amount " << bill.amount;

						continue;
					}

					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CleanupSubTx output " << i << " amount " << bill.amount;

					bill.status = BILL_STATUS_ERROR;

					if (RandTest(RTEST_RELEASE_ERROR_TXID))
					{
						// all output billets initially get a txid so the polling thread can find the billet if it gets a matching address query response
						// but if the billet was not successfully submitted, the txid can be released in case the same txid is created by another wallet
						// as currently implemented however, this would result in listtransactions briefly reporting the transaction and its txid,
						// and then the transaction disappearing when the txid is released

						BOOST_LOG_TRIVIAL(info) << "Transaction::CleanupSubTx releasing txid for billet " << bill.DebugString();

						bill.flags |= BILL_FLAG_NO_TXID;
					}

					rc = dbconn->BilletInsert(bill);
					if (rc)
					{
						BOOST_LOG_TRIVIAL(warning) << "Transaction::CleanupSubTx error saving output " << i << " billet id " << bill.id;

						continue;
					}
				}

				if (i > 0 || need_intermediate_txs || destination.type == SECRET_TYPE_SPENDABLE_DESTINATION)
				{
					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CleanupSubTx balance_pending output " << i << " amount " << bill.amount;

					balance_pending = balance_pending + bill.amount;
				}
			}
		}

		while (build_state >= TX_BUILD_SAVED)	// use break on error
		{
			auto rc = dbconn->TransactionSelectId(id, *this);
			if (rc)
			{
				BOOST_LOG_TRIVIAL(warning) << "Transaction::CleanupSubTx error reading tx id " << id;

				break;
			}

			status = TX_STATUS_ERROR;

			rc = dbconn->TransactionInsert(*this);
			if (rc)
			{
				BOOST_LOG_TRIVIAL(warning) << "Transaction::CleanupSubTx error saving tx id " << id;

				break;
			}

			break;
		}
	}

	Clear();
}

int Transaction::TryCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, Secret &destination, const uint32_t timeout, bigint_t& tx_round_up, deque<Transaction>& tx_list, unique_lock<mutex>& lock) // throws RPC_Exception
{
	/*
		Attempt to find input billets for each output billet

		A few things can get in the way:
		1. need to split an input billet first so it can be allocated to more than one sub-transaction
		2. need to wait for a pending billet to clear
		3. not enough balance in economically-spendable billets

		How these are each detected and handled:
		1. If sending payment requires an intermediate tx, and the change from one of the sub-tx's is larger than the next available billet,
			then create the change in an intermediate tx and use it in the final tx
		2. If all available billets have been allocated and there are sufficient pending billets to cover remaining need, then wait
			for billets to become available.
		3. If not sufficient pending billets, then throw insufficient funds exception.

		Return value non-zero tells the calling function to retry.
	*/

	bool test_fail = !RandTest(4);

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
		tx->build_type = TX_BUILD_TYPE_NULL;

	// works fine and has much higher throughput without this lock:
	// if (!lock) lock.lock();

	uint64_t asset = 0;
	unsigned max_delaytime = 0;

	bigint_t balance;
	Total::GetTotalBalance(dbconn, true, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, asset, 0, max_delaytime, txparams.blockchain, txparams.blockchain);

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay timeout " << timeout << " tx_round_up " << tx_round_up << " balance " << balance << " entry " << entry->DebugString();

	if (entry->amount > balance)
		throw txrpc_insufficient_funds;

	bigint_t total_paid = 0UL;
	bigint_t round_up_extra = 0UL;
	bool need_intermediate_txs = false;
	uint64_t billet_count = 0;

	CCASSERT(entry->amount);

	auto tx = tx_list.begin();

	for (unsigned ntx = 0; total_paid < entry->amount; ++ntx)
	{
		if (tx_list.size() >= 10000)	// make this a config param?
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay tx_list.size() " << tx_list.size();

			throw txrpc_wallet_error;	// fail safe
		}

		if (ntx)
		{
			if ((tx + 1) == tx_list.end())
			{
				tx_list.emplace_back();
				tx = tx_list.end() - 1;
			}
			else
			{
				++tx;
				tx->Clear();
			}
		}

		auto amount_fp = tx_amount_encode(entry->amount - total_paid, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);
		tx_amount_decode(amount_fp, tx->SUBTX_AMOUNT, false, txparams.amount_bits, txparams.exponent_bits);

		auto last_round_up_extra = round_up_extra;
		amount_fp = tx_amount_encode(entry->amount - total_paid, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax, 1);
		tx_amount_decode(amount_fp, round_up_extra, false, txparams.amount_bits, txparams.exponent_bits);
		round_up_extra = round_up_extra - tx->SUBTX_AMOUNT;

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::TryCreateTxPay amount " << entry->amount  << " total_paid " << total_paid << " adding subtx " << ntx << " amount " << tx->SUBTX_AMOUNT << " round_up_extra " << round_up_extra;

		bigint_t next_amount = 0UL;

		if (total_paid + tx->SUBTX_AMOUNT < entry->amount)
		{
			next_amount = entry->amount - (total_paid + tx->SUBTX_AMOUNT);

			amount_fp = tx_amount_encode(next_amount, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);
			tx_amount_decode(amount_fp, next_amount, false, txparams.amount_bits, txparams.exponent_bits);

			if (next_amount <= tx_round_up)
			{
				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay next_amount " << next_amount << " <= tx_round_up " << tx_round_up << " -> amount of this output will be rounded up from " << tx->SUBTX_AMOUNT;

				auto amount_fp = tx_amount_encode(entry->amount - total_paid, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax, 1);
				tx_amount_decode(amount_fp, tx->SUBTX_AMOUNT, false, txparams.amount_bits, txparams.exponent_bits);

				round_up_extra = 0UL;
			}
		}

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay added nominal subtx " << ntx << " amount " << tx->SUBTX_AMOUNT;

		CCASSERTZ(tx->id);	// FUTURE: TBD if tx already used
		CCASSERTZ(tx->build_type);
		CCASSERTZ(tx->build_state);
		CCASSERTZ(tx->nin);

		// find inputs for subtx, and compute outputs and donation

		tx->nout = WALLET_TX_MINOUT;
		bigint_t input_total = 0UL;
		bigint_t new_amount, donation;

		CCASSERT(tx->SUBTX_AMOUNT);

		while (true)
		{
			if (g_shutdown)
				throw txrpc_shutdown_error;

			if (test_fail && RandTest(4*RTEST_TX_ERRORS))
			{
				BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating error mid tx, at shutdown check for subtx " << ntx << " nin " << tx->nin;

				throw txrpc_simulated_error;
			}

			unsigned nout = tx->nout;
			unsigned nin = tx->nin;
			txparams.ComputeDonation(nout, nin, donation);
			auto amount_fp = tx_amount_encode(donation, true, txparams.donation_bits, txparams.exponent_bits);
			tx_amount_decode(amount_fp, donation, true, txparams.donation_bits, txparams.exponent_bits);

			auto min_amount = donation;

			++nin;

			txparams.ComputeDonation(nout, nin, donation);
			amount_fp = tx_amount_encode(donation, true, txparams.donation_bits, txparams.exponent_bits);
			tx_amount_decode(amount_fp, donation, true, txparams.donation_bits, txparams.exponent_bits);

			bigint_t req_amount = tx->SUBTX_AMOUNT + donation;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::TryCreateTxPay donation without input " << min_amount << " with input " << donation << " subtx amount " << tx->SUBTX_AMOUNT << " req_amount " << req_amount << " last_round_up_extra " << last_round_up_extra;

			CCASSERT(donation >= min_amount);
			if (nin < 2)
				min_amount = donation + bigint_t(1UL);				// have to at least cover the donation
			else
				min_amount = donation - min_amount + bigint_t(1UL);	// have to at least cover the increase in the donation

			if (next_amount && last_round_up_extra && req_amount > last_round_up_extra && total_paid + last_round_up_extra >= entry->amount && !TEST_NO_ROUND_UP)
			{
				// amount required for this subtx is more than simply rounding up the last subtx, so do the latter instead

				tx_round_up = next_amount;

				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay req_amount " << req_amount << " > last_round_up_extra " << last_round_up_extra << "; setting tx_round_up to " << tx_round_up;

				return 1;	// retry
			}

			if (req_amount > input_total)
				req_amount = req_amount - input_total;
			else
				req_amount = 0UL;

			if (!req_amount)
				min_amount = 0UL;
			else if (req_amount < min_amount)
				req_amount = min_amount;

			bigint_t total_required = 0UL;
			if (total_paid + tx->SUBTX_AMOUNT < entry->amount)
				total_required = entry->amount - (total_paid + tx->SUBTX_AMOUNT);
			total_required = total_required + req_amount;		// total required to finish tx

			auto rc = LocateBillet(dbconn, txparams.blockchain, req_amount, min_amount, tx->input_bills[tx->nin], billet_count, total_required);
			if (rc)
				return WaitNewBillet(total_required, billet_count, timeout, lock, test_fail);

			new_amount = tx->input_bills[tx->nin].amount;
			++tx->nin;

			if (test_fail && RandTest(4*RTEST_TX_ERRORS))
			{
				BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating error mid tx, after LocateBillet succeeded for nin " << tx->nin;

				throw txrpc_simulated_error;
			}

			if (new_amount >= req_amount)
			{
				auto change = new_amount - req_amount;

				// check if the tx has enough output bills to cover the change

				for (unsigned i = 1; i < tx->nout; ++i)
				{
					// TODO: ignore outvalmin and outvalmax when asset > 0
					auto amount_fp = tx_amount_encode(change, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);

					bigint_t val;
					tx_amount_decode(amount_fp, val, false, txparams.amount_bits, txparams.exponent_bits);

					CCASSERT(val <= change);

					change = change - val;
				}

				if (change)
				{
					// have change left over, so need more output bills

					++tx->nout;

					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay residual change " << change << " increasing output billets to " << tx->nout;

					if (tx->nout > TX_MAXOUT)
					{
						BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay nout " << tx->nout << " > " << TX_MAXOUT;

						throw txrpc_wallet_error;
					}

					// release the last added billet
					--tx->nin;
					ReleaseTxBillet(dbconn, tx->input_bills[tx->nin]);

					continue;	// retry
				}
			}

			input_total = input_total + new_amount;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay output amount " << tx->SUBTX_AMOUNT << " donation " << donation << " req_amount " << req_amount << " added input " << tx->nin << " amount " << new_amount;

			if (new_amount >= req_amount || tx->nin >= WALLET_TX_MAXIN || tx->nin >= TX_MAXINPATH)
				break;

			// !!! TODO: randomly add cover inputs
		}

		// recompute donation and change (might reduce donation slightly if fewer change outputs can be used)
		auto rc = tx->ComputeChange(txparams, input_total);
		if (!rc)
		{
			tx->build_type = TX_BUILD_FINAL;
			tx->build_state = TX_BUILD_READY;

			total_paid = total_paid + tx->SUBTX_AMOUNT;
		}
		else
		{
			// not enough inputs were found for the subtx, so consolidate the inputs into one output
			CCASSERT(tx->nin > 1);

			need_intermediate_txs = true;

			tx->build_type = TX_BUILD_CONSOLIDATE;
			tx->build_state = TX_BUILD_READY;

			auto rc = tx->ComputeChange(txparams, input_total);	// recompute with TX_BUILD_CONSOLIDATE
			if (rc || tx->donation >= input_total)
			{
				//cerr << "Transaction::TryCreateTxPay subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << donation << " >= input total " << input_total << endl;

				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::TryCreateTxPay subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << donation << " >= input total " << input_total;

				// this might be the result of rounding, so wait for a new billet and retry
				return WaitNewBillet(1UL, billet_count, timeout, lock, test_fail);
			}

			total_paid = total_paid + input_total - tx->donation;
		}

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::TryCreateTxPay added subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << donation << " output amount " << tx->SUBTX_AMOUNT << " total_paid " << total_paid << " of total amount " << entry->amount;
		//cerr << "Transaction::TryCreateTxPay added subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << donation << " output amount " << tx->SUBTX_AMOUNT << " total_paid " << total_paid << " of total amount " << amount << endl;
	}

	unsigned active_subtx_count = 0;
	bigint_t balance_allocated = 0UL;
	bigint_t balance_pending = 0UL;

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->SubTxIsActive(need_intermediate_txs))
		{
			++active_subtx_count;

			for (unsigned i = 0; i < tx->nin; ++i)
			{
				Billet& bill = tx->input_bills[i];

				CCASSERT(bill.blockchain == entry->dest_chain);
				CCASSERTZ(bill.asset);

				balance_allocated = balance_allocated + bill.amount;
			}

			for (unsigned i = 0; i < tx->nout; ++i)
			{
				if (i > 0 || need_intermediate_txs || destination.type == SECRET_TYPE_SPENDABLE_DESTINATION)
					balance_pending = balance_pending + tx->output_bills[i].amount;
			}
		}
	}

	CCASSERT(active_subtx_count);

	//if (active_subtx_count != 1 || need_intermediate_txs) throw txrpc_block_height_range_err; // for testing

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay output amount " << entry->amount << " total_paid " << total_paid << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending;

	SetPendingBalances(dbconn, entry->dest_chain, balance_allocated, balance_pending);

	Total::AddNoWaitAmounts(balance_pending, true, 0UL, true);

	if (lock) lock.unlock();

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->SubTxIsActive(need_intermediate_txs))
			tx->build_state = TX_BUILD_TOTALED;
	}

	// query all the serialnums to make sure input billets haven't been spent by another wallet
	// 3 reasons for doing this:
	//	(a) don't waste time creating proof if tx will fail when submitted to network
	//	(b) if tx has multiple sub-tx's, minimize the chance that part suceeds and part fails and then there isn't enough funds to complete the tx
	//	(c) to minimize the chance of creating output addresseses and then having the tx fail
	// To accomplish (b), it would be possible to only query the serialnums if the tx has multiple sub-tx's, but that would leak info to the tx server
	// The load on the tx server from this query should be pretty low, since pre-query will prime the tx server's disk cache,
	//	and when tx the is submitted, the serialnum lookup will come out of the cache
	// But this query will slow down the creation of transactions when using a remote transaction server

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	if (RandTest(RTEST_ALLOW_DOUBLE_SPENDS))
	{
		if (RandTest(4))
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay RTEST_ALLOW_DOUBLE_SPENDS skipping input spent check" << ", and then waiting for conflicting tx's to clear...";

			ccsleep(10);	// allow conflicting tx to clear
		}
		else
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay RTEST_ALLOW_DOUBLE_SPENDS skipping input spent check";
		}
	}
	else
	{
		for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
		{
			if (tx->SubTxIsActive(need_intermediate_txs))
			{
				auto rc = Billet::CheckIfBilletsSpent(dbconn, txquery, &tx->input_bills[0], tx->nin, true); // spent or pending
				if (rc > 1)
				{
					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay billet already spent; sleeping a few seconds before retrying...";

					ccsleep(3);	// sleep a few seconds to allow input billet to make progress toward becoming spent, so it is not repeatedly reused in attempts to build a transaction
				}
				if (rc)
					return rc;
			}
		}
	}

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	vector<TxPay> tx_structs(active_subtx_count);

	unsigned si = 0;
	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->SubTxIsActive(need_intermediate_txs))
		{
			TxPay& ts = tx_structs[si++];

			auto rc = tx->FillOutTx(dbconn, txquery, txparams, entry->dest_chain, ts);
			if (rc) return rc;
		}
	}

	if (test_fail && RandTest(RTEST_TX_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating billet spent / amount mismatch retry";

		return 1;
	}

	if ((test_fail && RandTest(RTEST_TX_ERRORS)) || (TEST_FAIL_ALL_TXS && 0))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating error pre address computation, at shutdown check";

		throw txrpc_simulated_error;
	}

	if (g_shutdown)
		throw txrpc_shutdown_error;

	CheckTimeout(timeout);

	si = 0;
	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->SubTxIsActive(need_intermediate_txs))
		{
			TxPay& ts = tx_structs[si++];

			tx->SetAddresses(dbconn, entry->dest_chain, destination, ts);		// do this last to minimize chance of unused addresses
		}
	}

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		if (need_intermediate_txs)
			cerr << "Constructing " << active_subtx_count << " intermediate transaction" << (active_subtx_count > 1 ? "s" : "") << " to split and/or merge billet amounts...\n" << endl;
		else if (active_subtx_count)
			cerr << "Constructing " << active_subtx_count << " transaction" << (active_subtx_count > 1 ? "s" : "") << "...\n" << endl;
	}

	uint64_t parent_id = 0;

	si = 0;
	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->SubTxIsActive(need_intermediate_txs))
		{
			TxPay& ts = tx_structs[si++];

			tx->FinishCreateTx(ts);

			if (si < 2 && g_shutdown)
				throw txrpc_shutdown_error;

			if (si < 2 && !need_intermediate_txs)
				CheckTimeout(timeout);

			if ((test_fail && RandTest(RTEST_TX_ERRORS)) || (TEST_FAIL_ALL_TXS && 0))
			{
				BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating error after create, before save and submit for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;

				throw txrpc_simulated_error;
			}

			// Save tx in db before submitting
			// (If tx submitted first, Poll thread could detect and save billets before this thread, and that would have to be sorted out...)

			if (active_subtx_count > 1 && !need_intermediate_txs)
				tx->parent_id = (si < 2 ? 1 : parent_id);

			if (si < 2 && !need_intermediate_txs)
				tx->ref_id = entry->ref_id;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs << " parent_id " << tx->parent_id;

			auto rc = tx->SaveOutgoingTx(dbconn);
			if (rc < 0)
				throw txrpc_wallet_db_error;
			if (rc)
			{
				if (si < 2 || need_intermediate_txs)
					return rc;

				BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " transaction only partially sent; SaveOutgoingTx error in subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs << " parent_id " << tx->parent_id;

				return 0;
			}

			tx->build_state = TX_BUILD_SAVED;

			if (si < 2 && !need_intermediate_txs)
				parent_id = tx->id;

			// Submit tx to network
			// !!! TODO: add random delay between SubmitTx's for increased privacy?

			uint64_t next_commitnum = 0;

			if ((test_fail && RandTest(RTEST_TX_ERRORS)) || TEST_FAIL_ALL_TXS)
			{
				BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating SubmitTx that actually failed for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;

				throw txrpc_simulated_error;
			}

			if (RandTest(RTEST_TX_ERRORS + 8*0))
			{
				rc = rand() % 3 - 1;
				txquery.m_possibly_sent = rand() & 1;

				cerr << "simulating SubmitTx rc " << rc << " WasPossiblySent " << txquery.WasPossiblySent() << endl;

				if (!rc)
					BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " simulating a tx (that will need to be abandoned) that was not submitted but return code was ok for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;
				else if (rc > 0)
					BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " simulating a tx for which submit failed for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;
				else if (txquery.WasPossiblySent())
					BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " simulating a tx (that will need to be abandoned) that was not submitted but return code was ambiguous for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;
				else
					BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " simulating a tx that was not submitted and return code indicated failure for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;
			}
			else
				rc = txquery.SubmitTx(ts, next_commitnum);

			if (!rc && RandTest(RTEST_TX_ERRORS) && 0)
			{
				// this should never happen, and when simulated, will result in an warning in the log when the tx clears
				// if this tx is to a self destination, manual polling will be required to clear the tx

				BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " simulating SubmitTx that appeared to fail but actually succeeded (and may require manual polling) for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;

				rc = -1;
			}

			/* SubmitTx possible return values:

				0 = submitted
				1 = submitted with error returned -> submit failed
				-1 = maybe
					!WasPossiblySent() = not submitted
					WasPossiblySent() = maybe submitted -> assume submitted
			*/

			//if (rc && (si < 2 || need_intermediate_txs))
			//	throw txrpc_simulated_error;				// for testing--filters out server errors and tx rejected errors

			if (!rc)
			{
				tx->build_state = TX_BUILD_SUBMIT_OK;
			}
			else if (rc > 0)
			{
				BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " SubmitTx returned error: " << txquery.ReadBuf();

				tx->build_state = TX_BUILD_SUBMIT_INVALID;

				if (si < 2 || need_intermediate_txs)
					throw txrpc_tx_rejected;

				BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " transaction only partially sent; SubmitTx and QuerySerialnums error in subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs << " parent_id " << tx->parent_id;

				continue;
			}
			else
			{
				BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " SubmitTx failed";

				if (need_intermediate_txs)
					throw txrpc_server_error;

				if (txquery.WasPossiblySent())
				{
					BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " SubmitTx sent a transaction but did not get a response from the server. The wallet will consider this transaction to have been sent, but if it is never received by the network, it will need to be abandoned.";

					// for now TX_BUILD_SUBMIT_UNKNOWN is handled the same as TX_BUILD_SUBMIT_OK

					tx->build_state = TX_BUILD_SUBMIT_UNKNOWN;
				}
				else
				{
					if (si < 2)
						throw txrpc_server_error;	// report tx error only if tx was definitely not sent

					BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay ref id " << entry->ref_id << " transaction only partially sent; SubmitTx error in subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs << " parent_id " << tx->parent_id;

					continue;
				}
			}

			if (tx->build_state == TX_BUILD_SUBMIT_UNKNOWN)
				CCASSERTZ(need_intermediate_txs);
			else
				CCASSERT(tx->build_state == TX_BUILD_SUBMIT_OK);

			rc = tx->UpdatePolling(dbconn, next_commitnum);
			(void)rc;

			if (!need_intermediate_txs && TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay successful submit amount " << tx->SUBTX_AMOUNT << " paynum " << ts.outputs[0].addrparams.__paynum << " address " << hex << ts.outputs[0].M_address << dec;

			//tx_dump_stream(cout, ts);

			if (g_interactive)
			{
				string amts;
				lock_guard<FastSpinLock> lock(g_cout_lock);
				cerr << "Transaction " << si << " of " << active_subtx_count << " submitted to network." << endl;
				if (need_intermediate_txs)
					cerr << (tx->build_type == TX_BUILD_SPLIT ? "Splitting " : "Merging ") << ts.nin << " input billets into " << ts.nout << " output billets" << endl;
				else
				{
					amount_to_string(asset, tx->SUBTX_AMOUNT, amts);
					cerr << "Sent (private amount = " << amts << ") to newly-generated unique address " << hex << ts.outputs[0].M_address << dec << endl;
				}
				for (unsigned i = 0; i < ts.nin; ++i)
				{
					amount_to_string(asset, tx->input_bills[i].amount, amts);
					cerr << "Input  " << i + 1 << "\n"
					"   (amount private)  (" << amts << ")\n"
					"   billet serial #   " << hex << ts.inputs[i].S_serialnum << dec << endl;
				}
				for (unsigned i = 0; i < ts.nout; ++i)
				{
					amount_to_string(asset, tx->output_bills[i].amount, amts);
					cerr << "Output " << i + 1 << "\n"
					"   (amount private)  (" << amts << ")\n"
					"   billet commitment " << hex << ts.outputs[i].M_commitment << "\n"
					"   encrypted asset   " << ts.outputs[i].M_asset_enc << "\n"
					"   encrypted amount  " << ts.outputs[i].M_amount_enc << "\n"
					"   address           " << ts.outputs[i].M_address << dec << endl;
				}
				amount_to_string(asset, tx->donation, amts);
				cerr << "Donation amount " << amts << endl;
				cerr << endl;
			}
		}
	}

	return need_intermediate_txs;	// retry if need_intermediate_txs is true
}

int Transaction::CreateConflictTx(DbConn *dbconn, TxQuery& txquery, const Billet& input) // throws RPC_Exception
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CreateConflictTx " << input.DebugString();

	Clear();

	build_type = TX_BUILD_CANCEL_TX;

	nin = 1;
	input_bills[0].Copy(input);

	TxParams txparams;
	TxPay ts;
	Secret null_destination;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;
	if (txparams.NotConnected())
	{
		rc = g_txparams.UpdateParams(txparams, txquery);
		if (rc || txparams.NotConnected()) throw txrpc_server_error;
	}

	rc = ComputeChange(txparams, input_bills[0].amount);
	if (rc) throw txrpc_wallet_error;

	rc = FillOutTx(dbconn, txquery, txparams, 0, ts);
	if (rc) throw txrpc_wallet_error;

	if (g_shutdown)
		throw txrpc_shutdown_error;

	SetAddresses(dbconn, txparams.blockchain, null_destination, ts);		// do this last to minimize chance of unused addresses

	FinishCreateTx(ts);

	if (g_shutdown)
		throw txrpc_shutdown_error;

	// Save tx in db before submitting
	// (If tx submitted first, Poll thread could detect and save billets before this thread, and that would have to be sorted out...)

	rc = SaveOutgoingTx(dbconn);
	if (rc < 0)
		throw txrpc_wallet_db_error;
	if (rc)
		return rc;

	// Submit tx to network

	uint64_t next_commitnum = 0;

	rc = txquery.SubmitTx(ts, next_commitnum);

	if (rc < 0)
		BOOST_LOG_TRIVIAL(warning) << "Transaction::CreateConflictTx SubmitTx failed";
	else if (rc)
		BOOST_LOG_TRIVIAL(warning) << "Transaction::CreateConflictTx SubmitTx returned error: " << txquery.ReadBuf();

	if (rc)
	{
		auto rc = dbconn->BeginWrite();
		if (rc)
		{
			dbconn->DoDbFinishTx(-1);

			throw txrpc_wallet_db_error;
		}

		Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

		status = TX_STATUS_ERROR;

		rc = dbconn->TransactionInsert(*this);
		if (rc) throw txrpc_wallet_db_error;

		for (unsigned i = 0; i < nout; ++i)
		{
			Billet& bill = output_bills[i];

			bill.status = BILL_STATUS_ERROR;

			rc = dbconn->BilletInsert(bill);
			if (rc) throw txrpc_wallet_db_error;
		}

		rc = dbconn->Commit();
		if (rc) throw txrpc_wallet_db_error;

		dbconn->DoDbFinishTx();

		finally.Clear();
	}

	if (rc < 0)
		throw txrpc_server_error;
	else if (rc)
		throw txrpc_tx_rejected;

	rc = UpdatePolling(dbconn, next_commitnum);
	(void)rc;

	return 0;
}

int Transaction::CreateTxFromAddressQueryResult(DbConn *dbconn, TxQuery& txquery, const Secret& destination, const Secret& address, QueryAddressResult &result, bool duplicate_txid)
{
	// must be called from inside a BeginWrite

	CCASSERT(destination.id == address.dest_id);

	if (g_params.foundation_wallet && result.pool != CC_MINT_FOUNDATION_POOL)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxFromAddressQueryResult Foundation wallet ignoring output to pool " << result.pool;

		return 0;
	}

	if (Implement_CCMint(result.blockchain) && result.pool == CC_MINT_FOUNDATION_POOL && !g_params.foundation_wallet)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxFromAddressQueryResult non-Foundation wallet ignoring output to Foundation pool";

		return 0;
	}

	if (result.encrypted)
	{
		uint64_t asset_xor, amount_xor;
		compute_amount_pad(result.commit_iv, destination.value, address.number, asset_xor, amount_xor);

		uint64_t mask = -1;
		if (result.amount_bits < 64)
			mask = ((uint64_t)1 << result.asset_bits) - 1;
		result.asset ^= (mask & asset_xor);

		if (result.amount_bits < 64)
			mask = ((uint64_t)1 << result.amount_bits) - 1;
		result.amount_fp ^= (mask & amount_xor);
	}

	bigint_t amount;
	tx_amount_decode(result.amount_fp, amount, false, result.amount_bits, result.exponent_bits);

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CreateTxFromAddressQueryResult amount " << amount << " destination id " << address.dest_id << " paynum " << address.number << " address " << address.value << " duplicate_txid " << duplicate_txid;

	// TODO?: add feature to ignore incoming amounts that are very small, for better performance and to avoid DoS attack

	if (!result.amount_fp)
		return 0;				// ignore zero amounts, for better performance and to avoid DoS attack

	btc_block = g_btc_block.GetCurrentBlock();
	type = TX_TYPE_SEND;
	status = TX_STATUS_CLEARED;
	nout = 1;

	auto rc = dbconn->TransactionInsert(*this);
	if (rc) return rc;

	Billet& txout = output_bills[0];

	txout.create_tx = id;
	txout.flags = Billet::FlagsFromDestinationType(destination.type);
	txout.dest_id = address.dest_id;
	txout.blockchain = result.blockchain;
	txout.address = address.value;
	txout.pool = result.pool;
	txout.asset = result.asset;
	txout.amount_fp = result.amount_fp;
	txout.amount = amount;
	txout.delaytime = 0;							// FUTURE: TBD
	txout.commit_iv = result.commit_iv;
	txout.commitment = result.commitment;

	if (duplicate_txid)
		txout.flags |= BILL_FLAG_NO_TXID;

	return txout.SetStatusCleared(dbconn, result.commitnum);
}

int Transaction::UpdateStatus(DbConn *dbconn, uint64_t bill_id, uint64_t commitnum)
{
	// must be called from inside a BeginWrite

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::UpdateStatus bill id " << bill_id << " commitnum " << commitnum << " tx " << DebugString();

	if (status != TX_STATUS_PENDING && status != TX_STATUS_ABANDONED)
		BOOST_LOG_TRIVIAL(debug) << "Transaction::UpdateStatus billet cleared in transaction id " << id << " with status " << status << "; bill id " << bill_id << " commitnum " << commitnum;

	bool all_cleared = true;
	bool none_cleared = true;
	unsigned bill_index = -1;

	for (unsigned i = 0; i < nout; ++i)
	{
		Billet& bill = output_bills[i];

		if (bill.id == bill_id)
		{
			bill_index = i;

			auto rc = bill.SetStatusCleared(dbconn, commitnum);
			if (rc) return rc;
		}
		else if (bill.BillIsPending())
			all_cleared = false;
		else
			none_cleared = false;
	}

	CCASSERT(bill_index < nout);

	if (none_cleared)
	{
		// move remaining addresses to front of the polling queue

		for (unsigned i = 0; i < nout; ++i)
		{
			Billet& bill = output_bills[i];

			if (bill.BillIsPending())
			{
				Secret secret;

				auto rc = dbconn->SecretSelectSecret(&bill.address, TX_ADDRESS_BYTES, secret);
				if (rc) continue;

				CCASSERT(secret.TypeIsAddress());

				secret.next_check = 1;	// check now

				rc = dbconn->SecretInsert(secret);
				if (rc) continue;
			}
		}
	}

	if (!all_cleared)
		return 0;

	for (unsigned i = 0; i < nin; ++i)
	{
		Billet& bill = input_bills[i];

		// mark all input serialnums as spent
		// !!! TBD: could query the serialnums first just to be sure this transaction was spent
		//	(this function is inside a BeginWrite and would need first drop the write lock before running a query)

		if (bill.status != BILL_STATUS_SPENT)
		{
			auto rc = bill.SetStatusSpent(dbconn, bill.spend_hashkey, bill.spend_tx_commitnum);
			if (rc) return rc;
		}
	}

	if (status != TX_STATUS_PENDING && status != TX_STATUS_ABANDONED)
		BOOST_LOG_TRIVIAL(warning) << "Transaction::UpdateStatus transaction id " << id << " cleared after status " << status;

	btc_block = g_btc_block.GetCurrentBlock();

	status = TX_STATUS_CLEARED;

	return dbconn->TransactionInsert(*this);
}

int Transaction::SetConflicted(DbConn *dbconn, uint64_t tx_id)
{
	// must be called from inside a BeginWrite

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::SetConflicted tx_id " << tx_id;

	Transaction tx;

	auto rc = tx.ReadTx(dbconn, tx_id);
	if (rc) return rc;

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::SetConflicted " << tx.DebugString();

	if (!tx.TxCouldClear())
		return 0;

	uint64_t blockchain = 0;
	bigint_t balance_allocated = 0UL;
	bigint_t balance_pending = 0UL;

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		Billet& bill = tx.input_bills[i];

		CCASSERT(bill.status != BILL_STATUS_PREALLOCATED);

		if (bill.status != BILL_STATUS_ALLOCATED)
			continue;

		// check all spend transactions to see if input should be released

		bool release_input = false;
		uint64_t tx_id = 0;

		while (tx_id < INT64_MAX)
		{
			auto rc = dbconn->BilletSpendSelectBillet(bill.id, tx_id);
			if (rc < 0) return rc;

			if (rc)
				break;

			if (tx_id != tx.id)
			{
				Transaction tx2;

				rc = tx2.ReadTx(dbconn, tx_id);
				if (rc) return rc;

				if (tx2.TxCouldClear())
				{
					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetConflicted tx_id " << tx.id << " input still pending in tx_id " << tx_id << " billet " << bill.DebugString();

					release_input = false;
					break;
				}
			}

			++tx_id;
		}

		if (release_input)
		{
			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetConflicted tx_id " << tx.id << " releasing input billet " << bill.DebugString();

			if (!blockchain)
				blockchain = bill.blockchain;
			else
				CCASSERT(bill.blockchain == blockchain);

			balance_allocated = balance_allocated + bill.amount;

			bill.status = BILL_STATUS_CLEARED;

			rc = dbconn->BilletInsert(bill);
			if (rc) return rc;
		}
	}

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		Billet& bill = tx.output_bills[i];

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetConflicted tx_id " << tx.id << " output billet " << bill.DebugString();

		if (bill.status != BILL_STATUS_PENDING)
			continue;

		if ((bill.flags & BILL_RECV_MASK) && (bill.flags & BILL_FLAG_TRUSTED))
		{
			if (!blockchain)
				blockchain = bill.blockchain;
			else
				CCASSERT(bill.blockchain == blockchain);

			CCASSERTZ(bill.asset);

			balance_pending = balance_pending + bill.amount;
		}

		bill.status = BILL_STATUS_ABANDONED;

		rc = dbconn->BilletInsert(bill);
		if (rc) return rc;
	}

	tx.status = TX_STATUS_CONFLICTED;

	rc = dbconn->TransactionInsert(tx);
	if (rc) return rc;

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::SetConflicted balance_allocated " << balance_allocated << " balance_pending " << balance_pending;

	uint64_t asset = 0;
	unsigned delaytime = 0;

	// TBD: this will get more complicated if billets have various delaytimes or blockchains

	rc = Total::AddBalances(dbconn, false, TOTAL_TYPE_ALLOCATED_BIT, 0, 0, asset, delaytime, blockchain, false, balance_allocated);
	if (rc) return rc;

	rc = Total::AddBalances(dbconn, false, TOTAL_TYPE_PENDING_BIT, 0, 0, asset, delaytime, blockchain, false, balance_pending);
	if (rc) return rc;

	Total::AddNoWaitAmounts(balance_pending, false, 0UL, true);

	#if TEST_LOG_BALANCE
	amtint_t balance;
	Total::GetTotalBalance(dbconn, false, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, 0, 0, -1, 0, -1, false);
	BOOST_LOG_TRIVIAL(info) << "Transaction::SetConflicted new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending;
	//cerr << "Transaction::SetConflicted new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending << endl;
	#endif

	return 0;
}
