/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
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
#include "totals.hpp"
#include "btc_block.hpp"
#include "rpc_errors.hpp"
#include "txparams.hpp"
#include "txquery.hpp"
#include "walletdb.hpp"

#include <CCobjdefs.h>
#include <CCmint.h>
#include <transaction.h>
#include <transaction.hpp>
#include <jsonutil.h>

#include <siphash/siphash.h>

//!#define TEST_NO_ROUND_UP		1
//!#define RTEST_TX_ERRORS		16	// when this is enabled, it's helpful to set polling_addresses = 50 and polling_table[SECRET_TYPE_POLL_ADDRESS][nothing received].first to 5 sec (instead of 60 sec)
//!#define TEST_FAIL_ALL_TXS	1	// all tx's fail (except mints), so balance should never change--useful for testing that the donation is handled correctly on error
//#define TEST_BIG_DIVISION		1

#ifndef TEST_NO_ROUND_UP
#define TEST_NO_ROUND_UP		0	// don't test
#endif

#ifndef RTEST_TX_ERRORS
#define RTEST_TX_ERRORS			0	// don't test
#endif

#ifndef TEST_FAIL_ALL_TXS
#define TEST_FAIL_ALL_TXS		0	// don't test
#endif

#ifndef TEST_BIG_DIVISION
#define TEST_BIG_DIVISION		0	// don't test
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

Transaction::Transaction()
{
	Clear();
}

void Transaction::Clear()
{
	memset(this, 0, (uintptr_t)&body - (uintptr_t)this);

	body.clear();

	we_sent[0] = -1;
	we_sent[1] = -1;
	inputs_involve_watchonly = -1;

	for (unsigned i = 0; i < TX_MAXOUT; ++i)
	{
		output_bills[i].Clear();
		output_destinations[i].Clear();
		output_accounts[i].Clear();

		adj_amounts[i] = 0UL;
		adj_donations[i] = 0UL;
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
	memcpy(this, &other, (uintptr_t)(&this->body) - (uintptr_t)this);

	body = other.body;

	for (unsigned i = 0; i < TX_MAXOUT; ++i)
	{
		output_bills[i].Copy(other.output_bills[i]);
		output_destinations[i].Copy(other.output_destinations[i]);
		output_accounts[i].Copy(other.output_accounts[i]);

		adj_amounts[i] = other.adj_amounts[i];
		adj_donations[i] = other.adj_donations[i];
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
	out << " create_time " << create_time;
	out << " btc_block " << btc_block;
	out << " donation " << hex << donation << dec;
	out << " nout " << nout;
	out << " nin " << nin;
	if (body.length())
		out << " body " << body;

	return out.str();
}

bool Transaction::TypeIsValid(unsigned type)
{
	return type > TX_TYPE_VOID && type < TX_TYPE_INVALID;
}

bool Transaction::StatusIsValid(unsigned status)
{
	return status > TX_STATUS_VOID && status < TX_STATUS_INVALID;
}

bool Transaction::IsValid() const
{
	return TypeIsValid(type) && StatusIsValid(status);
}

string Transaction::EncodeInternalTxid()
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

string Transaction::GetBtcTxid()
{
	if (!parent_id && nout && !(output_bills[0].flags & (BILL_FLAG_NO_TXID | BILL_IS_CHANGE)))
		return EncodeBtcTxid(output_bills[0].blockchain, output_bills[0].address, output_bills[0].commitment);
	else
		return EncodeInternalTxid();
}

void Transaction::SetOutputsFromTx(const TxPay& tx)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetOutputsFromTx";

	// adds output billets to this Transaction object
	// first output should always be change

	type = (tx.tag_type == CC_TYPE_MINT ? TX_TYPE_MINT : TX_TYPE_SEND);
	status = TX_STATUS_PENDING;

	tx_amount_decode(tx.donation_fp, donation, true, tx.donation_bits, tx.exponent_bits);

	nout = tx.nout;

	for (unsigned i = 0; i < nout; ++i)
		output_bills[i].SetFromTxOut(tx, tx.outputs[i]);
}

int Transaction::SaveOutgoingTx(DbConn *dbconn)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::SaveOutgoingTx";

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	rc = dbconn->TransactionInsert(*this);
	if (rc) return -1;

	for (unsigned i = 0; i < nout; ++i)
	{
		if (!output_bills[i].create_tx)
			output_bills[i].create_tx = id;
		else
			CCASSERT(output_bills[i].create_tx == id);

		output_bills[i].status = BILL_STATUS_PENDING;

		rc =  dbconn->BilletInsert(output_bills[i]);
		if (rc) return -1;
	}

	for (unsigned i = 0; i < nin; ++i)
	{
		// this isn't needed because output billets were written to db when allocated
		//rc = dbconn->BilletInsert(input_bills[i]);
		//if (rc) return -1;

		rc = dbconn->BilletSpendInsert(id, input_bills[i].id, &input_bills[i].hashkey);
		if (rc) return -1;
	}

	// commit db writes

	rc = dbconn->Commit();
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::SaveOutgoingTx error committing db transaction";

		return -1;
	}

	dbconn->DoDbFinishTx();

	finally.Clear();

	return 0;
}

int Transaction::UpdatePolling(DbConn *dbconn, uint64_t next_commitnum)
{
	// Updating polling times for output addresses
	// Also sets query_commitnum and expected_commitnum

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::UpdatePolling " << next_commitnum;

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

		auto rc = dbconn->SecretSelectSecret(&bill.address, TX_ADDRESS_BYTES, secret);
		if (rc) continue;

		secret.next_check = now + 1 + (rand() & 1);

		if (secret.type == SECRET_TYPE_SEND_ADDRESS && secret.query_commitnum == 0)
			secret.query_commitnum = next_commitnum;		// start query at this commitnum (skip tx's from other wallets)

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

		rc = dbconn->SecretSelectId(output_bills[i].dest_id, output_destinations[i]);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxBillets error reading destination id " << output_bills[i].dest_id << " for output billet " << i << " id " << output_bills[i].id << " of transaction id " << id;

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
					BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxBillets error reading account id " << output_destinations[i].account_id << " for destination id " << output_destinations[i].id << " for output billet " << i << " id " << output_bills[i].id << " of transaction id " << id;

					return -1;
				}
			}
		}
	}

	for (unsigned i = 0; i < nin; ++i)
	{
		// !!! TODO: destination might be zero?

		rc = dbconn->SecretSelectId(input_bills[i].dest_id, input_destinations[i]);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxBillets error reading destination id " << input_bills[i].dest_id << " for input billet " << i << " id " << input_bills[i].id << " of transaction id " << id;

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
					BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxBillets error reading account id " << input_destinations[i].account_id << " for destination id " << input_destinations[i].id << " for input billet " << i << " id " << input_bills[i].id << " of transaction id " << id;

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

		while (1)
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
	// the total transaction amount for the txout is generally thw same as the txout amount, except:
	//	if the txin's exceed the txout's, then the excess txin's are assigned pro rata to all the non-zero txout's

	// TODO: needs to be tested to work correctedly as more capabilities are added to wallet

	bigint_t sum_sent = 0UL;
	bigint_t sum_out = 0UL;
	bigint_t sum_in = 0UL;

	for (unsigned i = 0; i < nout; ++i)
	{
		if (output_bills[i].asset == 0 && !output_bills[i].BillIsChange())
		{
			sum_sent = sum_sent + output_bills[i].amount;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts output " << i << " amount " << output_bills[i].amount << " sum_sent " << sum_sent;
		}

		if (output_bills[i].asset == 0)
		{
			sum_out = sum_out + output_bills[i].amount;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts output " << i << " amount " << output_bills[i].amount << " sum_out " << sum_out;
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
		net_donation = donation;

	bigint_t total_donation = 0UL;
	bigint_t total_out = 0UL;

	for (unsigned i = 0; i < nout; ++i)
	{
		if (output_bills[i].asset == 0 && !output_bills[i].BillIsChange() && sum_sent)
		{
			adj_donations[i] = (net_donation * output_bills[i].amount + sum_sent/2) / sum_sent;

			total_donation = total_donation + adj_donations[i];

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts output " << i << " adj_donations " << adj_donations[i] << " total_donation " << total_donation;
		}

		if (output_bills[i].asset == 0 && output_bills[i].amount && sum_out)
		{
			adj_amounts[i] = output_bills[i].amount + (excess_in * output_bills[i].amount + sum_out/2) / sum_out;

			total_out = total_out + adj_amounts[i];

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts output " << i << " adj_amount " << adj_amounts[i] << " total_out " << total_out;
		}
	}

	if (sum_sent)
		total_donation = total_donation + (net_donation * excess_in + sum_sent/2) / sum_sent;

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts sum_out " << sum_out << " sum_in " << sum_in << " donation " << donation << " excess_in " << excess_in << " sum_sent " << sum_sent << " total_out " << total_out << " total_donation " << total_donation;

	const bigint_t one = 1UL;

	for (unsigned iter = 0; iter < 2000 && total_donation != net_donation; ++iter)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts net_donation " << net_donation << " total_donation " << total_donation;

		for (unsigned i = 0; i < nout && total_donation < net_donation; ++i)
		{
			if (output_bills[i].asset == 0 && !output_bills[i].BillIsChange())
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

	sum_out = sum_out + excess_in;

	for (unsigned iter = 0; iter < 2000 && total_out != sum_out; ++iter)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts total_out " << total_out << " sum_out " << sum_out;

		for (unsigned i = 0; i < nout && total_out < sum_out; ++i)
		{
			if (output_bills[i].asset == 0 && output_bills[i].amount)
			{
				adj_amounts[i] = adj_amounts[i] + one;
				total_out = total_out + one;
			}
		}

		for (unsigned i = nout; i > 0 && total_out > sum_out; --i)
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

	TxParams txparams;
	QueryInputResults inputs;

	auto rc = txquery.QueryInputs(NULL, 0, txparams, inputs);
	if (rc) throw txrpc_server_error;

	if (Implement_CCMint(txparams.blockchain))
	{
		if (inputs.param_level >= CC_MINT_COUNT)
			return -1;
	}
	else if (!IsTestnet(txparams.blockchain))
		return -1;

	Secret address;
	SpendSecretParams params;
	memset(&params, 0, sizeof(params));

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

	string fn;
	char output[128];
	uint32_t outsize = sizeof(output);

	rc = txpay_create_finish(fn, ts, output, outsize);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::CreateTxMint txpay_create_finish failed: " << output;

		//tx_dump_stream(cout, ts);

		if (output[0])
			throw RPC_Exception(RPCErrorCode(-32001), output);
		else
			throw txrpc_wallet_error;
	}

	CCASSERT(txout.M_address == address.value);

	SetOutputsFromTx(ts);

	// Save ts in db before submitting
	// (If tx submitted first, Poll thread could detect and save billets before this thread, and that would have to be sorted out...)

	rc = SaveOutgoingTx(dbconn);
	if (rc) throw txrpc_wallet_error;

	//BeginAndReadTx(dbconn, id);	// testing
	//BeginAndReadTx(dbconn, id);	// testing

	if (RandTest(RTEST_TX_ERRORS))
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

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		throw txrpc_wallet_db_error;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	rc = Total::AddBalances(dbconn, TOTAL_TYPE_ALLOCATED_BIT, 0, 0, asset, delaytime, blockchain, true, balance_allocated);
	if (rc) throw txrpc_wallet_db_error;

	rc = Total::AddBalances(dbconn, TOTAL_TYPE_PENDING_BIT, 0, 0, asset, delaytime, blockchain, true, balance_pending);
	if (rc) throw txrpc_wallet_db_error;

	#if TEST_LOG_BALANCE
	amtint_t balance;
	Total::GetTotalBalance(dbconn, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, 0, 0, -1, 0, -1, false);
	BOOST_LOG_TRIVIAL(info) << "Transaction::SetPendingBalances new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending;
	//cerr << "SetPendingBalances new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending << endl;
	#endif

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
		rc = dbconn->BilletSelectAmount(blockchain, asset, amount, max_delaytime, bill);
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
				lock_guard<FastSpinLock> lock(g_cout_lock);
				cerr <<

R"(The wallet appears to have sufficient balance, but there are not enough billets available to construct the
transaction. It may be possible to manually solve this problem by sending transactions to yourself to split the wallet
balance into more output billets.)"

				"\n" << endl;
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

	rc = dbconn->Commit();
	if (rc) throw txrpc_wallet_db_error;

	dbconn->DoDbFinishTx();

	finally.Clear();
}

int Transaction::WaitNewBillet(const bigint_t& total_required, const uint64_t billet_count, const uint64_t t0, unique_lock<mutex>& lock) // throws RPC_Exception)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::WaitNewBillet";

	if (lock) lock.unlock();

	if (RandTest(RTEST_TX_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::WaitNewBillet simulating error";

		Total::AddNoWaitAmounts(0UL, true, total_required, false);

		throw txrpc_simulated_error;
	}

	uint64_t dt = time(NULL) - t0;

	if (dt + 2 >= (unsigned)g_params.billet_wait_time)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::WaitNewBillet wait time expired t0 " << t0 << " dt " << dt << " billet_wait_time " << g_params.billet_wait_time << "; throwing txrpc_insufficient_funds";

		throw txrpc_insufficient_funds;
	}

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Waiting for pending billets to become available...\n" << endl;
	}

	auto rc = Billet::WaitNewBillet(billet_count, g_params.billet_wait_time - dt);

	Total::AddNoWaitAmounts(0UL, true, total_required, false);

	if (rc)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::WaitNewBillet billet wait failed; throwing txrpc_insufficient_funds";

		throw txrpc_insufficient_funds;
	}

	//throw txrpc_simulated_error;	// for testing

	return 1;	// a billet cleared, so retry
}

int Transaction::ComputeChange(TxParams& txparams, const bigint_t& input_total)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::ComputeChange build_type " << build_type << " nin " << nin << " input_total " << input_total << " send amount " << SUBTX_AMOUNT;

	if (input_total < SUBTX_AMOUNT && build_type != TX_BUILD_CONSOLIDATE)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(info) << "Transaction::ComputeChange build_type " << build_type << " nin " << nin << " input_total " << input_total << " < send amount " << SUBTX_AMOUNT;

		return 1;
	}

	auto remainder = input_total;

	if (build_type != TX_BUILD_CONSOLIDATE)
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

		for (unsigned i = (build_type != TX_BUILD_CONSOLIDATE); i < nout; ++i)
		{
			// TODO: ignore outvalmin and outvalmax when asset > 0
			amount_fp = tx_amount_encode(remainder - change - donation, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);

			tx_amount_decode(amount_fp, output_bills[i].amount, false, txparams.amount_bits, txparams.exponent_bits);

			change = change + output_bills[i].amount;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ComputeChange build_type " << build_type << " nout " << nout << " change billet " << i << " amount " << output_bills[i].amount << " total change " << change << " donation " << donation;

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

int Transaction::FillOutTx(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, uint64_t dest_chain, TxPay& tx)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FillOutTx build_type " << build_type << " nin " << nin << " nout " << nout;

	CCASSERT(nin <= TX_MAXINPATH);

	uint64_t commitnums[TX_MAXINPATH];

	for (unsigned i = 0; i < nin; ++i)
		commitnums[i] = input_bills[i].commitnum;

	QueryInputResults inputs;

	auto rc = txquery.QueryInputs(commitnums, nin, txparams, inputs);
	if (rc) throw txrpc_server_error;

	if (Implement_CCMint(txparams.blockchain) && inputs.param_level < CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
		throw txrpc_tx_rejected;

	// compute and recheck encoded amounts using txparams returned by QueryInputs

	tx_init(tx);

	bigint_t check;
	txparams.ComputeDonation(nout, nin, check);
	tx.donation_fp = tx_amount_encode(check, true, txparams.donation_bits, txparams.exponent_bits);
	tx_amount_decode(tx.donation_fp, check, true, txparams.donation_bits, txparams.exponent_bits);
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FillOutTx donation " << donation << " check " << check << " donation_fp " << tx.donation_fp << " donation_bits " << txparams.donation_bits << " exponent_bits " << txparams.exponent_bits;
	if (donation != check)
	{
		BOOST_LOG_TRIVIAL(warning) << "Transaction::FillOutTx donation encoding mismatch " << donation << " != " << check;

		return 1;	// params may have changed, so retry
	}

	for (unsigned i = 0; i < nout; ++i)
	{
		TxOut& txout = tx.outputs[i];

		// TODO: ignore outvalmin and outvalmax when asset > 0
		txout.__amount_fp  = tx_amount_encode(output_bills[i].amount, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);
		tx_amount_decode(txout.__amount_fp, check, false, txparams.amount_bits, txparams.exponent_bits);
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FillOutTx output " << i << " amount " << output_bills[i].amount << " check " << check << " amount_fp " << txout.__amount_fp << " amount_bits " << txparams.amount_bits << " exponent_bits " << txparams.exponent_bits << " outvalmin " << txparams.outvalmin << " outvalmax " << txparams.outvalmax;
		if (output_bills[i].amount != check)
		{
			BOOST_LOG_TRIVIAL(warning) << "Transaction::FillOutTx output amount mismatch " << output_bills[i].amount << " != " << check;

			return 1;	// params may have changed, so retry
		}
	}

	// set the tx values

	tx.tag_type = CC_TYPE_TXPAY;

	tx.source_chain = txparams.blockchain;
	tx.param_level = inputs.param_level;
	tx.param_time = inputs.param_time;
	tx.amount_bits = txparams.amount_bits;
	tx.donation_bits = txparams.donation_bits;
	tx.exponent_bits = txparams.exponent_bits;
	tx.outvalmin = txparams.outvalmin;
	tx.outvalmax = txparams.outvalmax;
	tx.allow_restricted_addresses = true;
	tx.tx_merkle_root = inputs.merkle_root;

	tx.nout = nout;
	tx.nin = nin;
	tx.nin_with_path = nin;

	uint64_t asset_mask = (txparams.asset_bits < 64 ? ((uint64_t)1 << txparams.asset_bits) - 1 : -1);
	uint64_t amount_mask = (txparams.amount_bits < 64 ? ((uint64_t)1 << txparams.amount_bits) - 1 : -1);

	for (unsigned i = 0; i < nout; ++i)
	{
		TxOut& txout = tx.outputs[i];

		txout.M_pool = txparams.default_output_pool;
		txout.asset_mask = asset_mask;
		txout.amount_mask = amount_mask;
	}

	for (unsigned i = 0; i < nin; ++i)
	{
		Billet& bill = input_bills[i];
		TxIn& txin = tx.inputs[i];

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
		if (rc) throw txrpc_wallet_error;

		rc = Secret::GetParentValue(dbconn, SECRET_TYPE_SPEND, secret.id, txin.params, &txin.secrets[0], sizeof(txin.secrets));
		if (rc) throw txrpc_wallet_error;

		CCRandom(&bill.hashkey, TX_HASHKEY_WIRE_BYTES);

		//cerr << "bill " << i << " address " << hex << bill.address << " hashkey " << bill.hashkey << dec << endl;

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

		txin.merkle_root = tx.tx_merkle_root;
		txin.enforce_trust_secrets = 1;

		txin.S_hashkey = bill.hashkey;

		txin.pathnum = i + 1;

		for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
			tx.inpaths[i].__M_merkle_path[j] = inputs.merkle_paths[i][j];
	}

	return 0;
}

void Transaction::SetAddresses(DbConn *dbconn, uint64_t dest_chain, Secret &destination, TxPay& tx)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAddresses build_type " << build_type << " nin " << nin << " nout " << nout << " dest_chain " << dest_chain << " destination id " << destination.id;

	for (unsigned i = (build_type == TX_BUILD_FINAL); i < nout; ++i)
	{
		Secret secret;
		SpendSecretParams params;
		memset(&params, 0, sizeof(params));

		auto rc = secret.CreateNewSecret(dbconn, SECRET_TYPE_SELF_ADDRESS, SELF_DESTINATION_ID, dest_chain, params);
		if (rc) throw txrpc_wallet_error;

		TxOut& txout = tx.outputs[i];
		memcpy(&txout.addrparams, &params.addrparams, sizeof(txout.addrparams));

		txout.addrparams.__flags |= BILL_RECV_MASK | BILL_FLAG_TRUSTED | BILL_IS_CHANGE;
	}

	if (build_type == TX_BUILD_FINAL)
	{
		// set destination output

		Secret secret;
		SpendSecretParams params;
		memset(&params, 0, sizeof(params));

		auto rc = secret.CreateNewSecret(dbconn, SECRET_TYPE_SEND_ADDRESS, destination.id, dest_chain, params);
		if (rc) throw txrpc_wallet_error;

		TxOut& txout = tx.outputs[0];
		memcpy(&txout.addrparams, &params.addrparams, sizeof(txout.addrparams));

		txout.addrparams.__flags |= Billet::FlagsFromDestinationType(destination.type) | BILL_FLAG_TRUSTED;
	}
}

void Transaction::CreateTxPay(DbConn *dbconn, TxQuery& txquery, const string& encoded_dest, uint64_t dest_chain, const bigint_t& destination, const bigint_t& amount, const string& comment, const string& comment_to, const bool subfee) // throws RPC_Exception
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CreateTxPay dest_chain " << hex << dest_chain << dec << " destination " << destination << " amount " << amount << " subfee " << subfee << " comment " << comment << " comment_to " << comment_to;

	/*
		For privacy, all tx's are 1 to 4 in -> 2 out
			sometimes will need 1 or 2 in for speed or because wallet hold only 1 or 2 unspent billets
		Target mix: 1 in 20%; 2 in 20%; 3 in 20-40%; 4 in 20-40%?
			net degragmenation per tx is 0.6 to 0.8 billets
		if there is no change, a zero value change billet is created
		If payment is too large one output billet, then send more than one tx
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

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

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

	if (!dest_chain)
		dest_chain = txparams.blockchain;	// if the destination string didn't specify a blockchain, use the transaction server blockchain

	rc = secret.CheckForConflict(dbconn, txquery, dest_chain);
	if (rc < 0)
		throw txrpc_wallet_error;
	else if (rc)
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "This destination has already been used by another wallet. To preserve privacy, a new unique destination is required.");

	if (!secret.TypeIsDestination())
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPay secret is not a valid destination; " << secret.DebugString();

		throw txrpc_wallet_error;
	}

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

	deque<Transaction> tx_list(1);

	unique_lock<mutex> lock(billet_allocate_mutex, defer_lock);

	Finally finally(boost::bind(&Transaction::CleanupSubTxs, dbconn, dest_chain, boost::ref(secret), boost::ref(tx_list), boost::ref(lock)));

	uint64_t t0 = time(NULL);

	bigint_t tx_round_up = 0UL;

	for (unsigned retry = 0; ; ++retry)
	{
		if (retry > 2000 || g_shutdown) throw txrpc_wallet_error;

		if (RandTest(RTEST_TX_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPay simulating pre tx, at shutdown check for retry " << retry;

			throw txrpc_simulated_error;
		}

		auto rc = TryCreateTxPay(dbconn, txquery, txparams, dest_chain, secret, amount, comment, comment_to, subfee, t0, tx_round_up, tx_list, lock);
		if (!rc) break;

		CleanupSubTxs(dbconn, dest_chain, secret, tx_list, lock);
	}

	finally.Clear();

	*this = tx_list[0];
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

	rc = Total::AddBalances(dbconn, TOTAL_TYPE_ALLOCATED_BIT, 0, 0, asset, delaytime, dest_chain, false, balance_allocated);
	if (rc) throw txrpc_wallet_db_error;

	rc = Total::AddBalances(dbconn, TOTAL_TYPE_PENDING_BIT, 0, 0, asset, delaytime, dest_chain, false, balance_pending);
	if (rc) throw txrpc_wallet_db_error;

	Total::AddNoWaitAmounts(balance_pending, false, 0UL, true);

	#if TEST_LOG_BALANCE
	amtint_t balance;
	Total::GetTotalBalance(dbconn, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, 0, 0, -1, 0, -1, false);
	BOOST_LOG_TRIVIAL(info) << "Transaction::CleanupSubTxs new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending;
	//cerr << "     CleanupSubTxs new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending << endl;
	#endif

	rc = dbconn->Commit();
	if (rc) throw txrpc_wallet_db_error;

	dbconn->DoDbFinishTx();

	finally.Clear();

	Billet::NotifyNewBillet(false);	// wake up other threads to check if amounts pending are no longer sufficient
}

void Transaction::CleanupSubTx(DbConn *dbconn, const Secret &destination, bool need_intermediate_txs, bigint_t& balance_allocated, bigint_t& balance_pending)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CleanupSubTx need_intermediate_txs " << need_intermediate_txs << " " << DebugString();

	// for now, if tx was not successfully submitted, release all allocated billets
	//		TODO for future: keep some or all allocated billets and try to resume

	if (build_state < TX_BUILD_SUBMIT_OK)
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
				BOOST_LOG_TRIVIAL(info) << "Transaction::CleanupSubTx skipping release of input " << i << " billet id " << bill.id << " status " << bill.status << " amount " << bill.amount;

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

int Transaction::TryCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, uint64_t dest_chain, Secret &destination, const bigint_t& amount, const string& comment, const string& comment_to, const bool subfee, const uint64_t t0, bigint_t& tx_round_up, deque<Transaction>& tx_list, unique_lock<mutex>& lock) // throws RPC_Exception
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

		Return 1 tells the calling function to retry.
	*/

	// works fine and has much higher throughput without this lock:
	// if (!lock) lock.lock();

	uint64_t asset = 0;
	unsigned max_delaytime = 0;

	bigint_t balance;
	auto rc = Total::GetTotalBalance(dbconn, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, asset, 0, max_delaytime, txparams.blockchain, txparams.blockchain);
	if (rc) throw txrpc_wallet_db_error;

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay dest_chain " << dest_chain << " destination id " << destination.id  << " destination type " << destination.type << " amount " << amount << " subfee " << subfee << " t0 " << t0 << " tx_round_up " << tx_round_up << " balance " << balance << " comment " << comment << " comment_to " << comment_to;

	if (amount > balance)
		throw txrpc_insufficient_funds;

	bigint_t total_paid = 0UL;
	bigint_t round_up_extra = 0UL;
	bool need_intermediate_txs = false;
	uint64_t billet_count = 0;

	CCASSERT(amount);

	auto tx = tx_list.begin();

	for (unsigned ntx = 0; total_paid < amount; ++ntx)
	{
		if (tx_list.size() >= 10000)	// make this a config param?
			throw txrpc_wallet_error;	// fail safe

		if (ntx)
		{
			if ((tx + 1) == tx_list.end())
			{
				tx_list.emplace_back();
				tx = tx_list.end() - 1;
			}
			else
				++tx;
		}

		auto amount_fp = tx_amount_encode(amount - total_paid, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);
		tx_amount_decode(amount_fp, tx->SUBTX_AMOUNT, false, txparams.amount_bits, txparams.exponent_bits);

		auto last_round_up_extra = round_up_extra;
		amount_fp = tx_amount_encode(amount - total_paid, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax, 1);
		tx_amount_decode(amount_fp, round_up_extra, false, txparams.amount_bits, txparams.exponent_bits);
		round_up_extra = round_up_extra - tx->SUBTX_AMOUNT;

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::TryCreateTxPay amount " << amount  << " total_paid " << total_paid << " adding subtx " << ntx << " amount " << tx->SUBTX_AMOUNT << " round_up_extra " << round_up_extra;

		bigint_t next_amount = 0UL;

		if (total_paid + tx->SUBTX_AMOUNT < amount)
		{
			next_amount = amount - (total_paid + tx->SUBTX_AMOUNT);

			amount_fp = tx_amount_encode(next_amount, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);
			tx_amount_decode(amount_fp, next_amount, false, txparams.amount_bits, txparams.exponent_bits);

			if (next_amount <= tx_round_up)
			{
				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay next_amount " << next_amount << " <= tx_round_up " << tx_round_up << " -> amount of this output will be rounded up from " << tx->SUBTX_AMOUNT;

				auto amount_fp = tx_amount_encode(amount - total_paid, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax, 1);
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
				throw txrpc_wallet_error;

			if (RandTest(RTEST_TX_ERRORS))
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

			if (last_round_up_extra && req_amount > last_round_up_extra && total_paid + last_round_up_extra >= amount && !TEST_NO_ROUND_UP)
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
			if (total_paid + tx->SUBTX_AMOUNT < amount)
				total_required = amount - (total_paid + tx->SUBTX_AMOUNT);
			total_required = total_required + req_amount;		// total required to finish tx

			auto rc = LocateBillet(dbconn, txparams.blockchain, req_amount, min_amount, tx->input_bills[tx->nin], billet_count, total_required);
			if (rc)
				return WaitNewBillet(total_required, billet_count, t0, lock);

			new_amount = tx->input_bills[tx->nin].amount;
			++tx->nin;

			if (RandTest(RTEST_TX_ERRORS))
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
						throw txrpc_wallet_error;

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
		rc = tx->ComputeChange(txparams, input_total);
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

			rc = tx->ComputeChange(txparams, input_total);	// recompute with TX_BUILD_CONSOLIDATE
			if (rc || tx->donation >= input_total)
			{
				//cerr << "Transaction::TryCreateTxPay subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << donation << " >= input total " << input_total << endl;

				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::TryCreateTxPay subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << donation << " >= input total " << input_total;

				// this might be the result of rounding, so wait for a new billet and retry
				return WaitNewBillet(1UL, billet_count, t0, lock);
			}

			total_paid = total_paid + input_total - tx->donation;
		}

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::TryCreateTxPay added subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << donation << " output amount " << tx->SUBTX_AMOUNT << " total_paid " << total_paid << " of total amount " << amount;
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
				balance_allocated = balance_allocated + tx->input_bills[i].amount;

			for (unsigned i = 0; i < tx->nout; ++i)
			{
				if (i > 0 || need_intermediate_txs || destination.type == SECRET_TYPE_SPENDABLE_DESTINATION)
					balance_pending = balance_pending + tx->output_bills[i].amount;
			}
		}
	}

	CCASSERT(active_subtx_count);

	//if (active_subtx_count != 1 || need_intermediate_txs) throw txrpc_block_height_range_err; // for testing

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateTxPay output amount " << amount << " total_paid " << total_paid << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending;

	SetPendingBalances(dbconn, dest_chain, balance_allocated, balance_pending);

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

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->SubTxIsActive(need_intermediate_txs))
		{
			auto rc = Billet::CheckIfBilletsSpent(dbconn, txquery, &tx->input_bills[0], tx->nin, true); // spent or pending
			if (rc > 1)
				ccsleep(2);	// sleep a few seconds to allow input billet to make progress toward becoming spent, so it is not repeatedly reused in attempts to build a transaction
			if (rc)
				return rc;
		}
	}

	vector<TxPay> tx_structs(active_subtx_count);

	unsigned si = 0;
	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->SubTxIsActive(need_intermediate_txs))
		{
			TxPay& ts = tx_structs[si++];

			rc = tx->FillOutTx(dbconn, txquery, txparams, dest_chain, ts);
			if (rc) return rc;
		}
	}

	if (RandTest(RTEST_TX_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating billet spent / amount mismatch retry";

		return 1;
	}

	if (RandTest(RTEST_TX_ERRORS) || (TEST_FAIL_ALL_TXS && 0))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating error pre address computation, at shutdown check";

		throw txrpc_simulated_error;
	}

	if (g_shutdown)
		throw txrpc_wallet_error;

	si = 0;
	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->SubTxIsActive(need_intermediate_txs))
		{
			TxPay& ts = tx_structs[si++];

			tx->SetAddresses(dbconn, dest_chain, destination, ts);		// do this last to minimize chance of unused addresses
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

	string fn;
	char output[128];
	uint32_t outsize = sizeof(output);

	uint64_t parent_id = 0;

	si = 0;
	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->SubTxIsActive(need_intermediate_txs))
		{
			TxPay& ts = tx_structs[si++];

			auto rc = txpay_create_finish(fn, ts, output, outsize);
			if (rc)
			{
				BOOST_LOG_TRIVIAL(error) << "Transaction::TryCreateTxPay txpay_create_finish failed for subtx " << si << " of " << active_subtx_count << "; output " << output;

				//cerr << "txpay_create_finish failed: " << output << endl;
				//tx_dump_stream(cout, ts);

				if (output[0])
					throw RPC_Exception(RPCErrorCode(-32001), output);
				else
					throw txrpc_wallet_error;
			}

			tx->SetOutputsFromTx(ts);

			if (si < 2 && g_shutdown)
				throw txrpc_wallet_error;

			if (RandTest(RTEST_TX_ERRORS) || (TEST_FAIL_ALL_TXS && 0))
			{
				BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating error after create, before save and submit for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;

				throw txrpc_simulated_error;
			}

			// Save tx in db before submitting
			// (If tx submitted first, Poll thread could detect and save billets before this thread, and that would have to be sorted out...)

			if (active_subtx_count > 1 && !need_intermediate_txs)
				tx->parent_id = (si < 2 ? 1 : parent_id);

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::TryCreateTxPay subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs << " parent_id " << tx->parent_id;

			rc = tx->SaveOutgoingTx(dbconn);
			if (rc) throw txrpc_wallet_db_error;

			tx->build_state = TX_BUILD_SAVED;

			if (si < 2)
				parent_id = tx->id;

			if (RandTest(RTEST_TX_ERRORS) || (TEST_FAIL_ALL_TXS && 0))
			{
				BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating error after save, before submit for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;

				throw txrpc_simulated_error;
			}

			// Submit tx to network
			// !!! TODO: add random delay between SubmitTx's?

			uint64_t next_commitnum;

			if (RandTest(RTEST_TX_ERRORS) || TEST_FAIL_ALL_TXS)
			{
				BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating SubmitTx that actually failed for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;

				tx->build_state = TX_BUILD_SUBMIT_ERR;
				throw txrpc_simulated_error;
			}

			rc = txquery.SubmitTx(ts, next_commitnum);
			if (rc)
			{
				BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateTxPay SubmitTx did not successfully complete. The network may or may not have received the transaction. If the network did receive the transaction, the wallet will detect it in the blockchain and will then mark the transaction input billets as spent and deduct them from the wallet balance.  In the meantime, before the transaction has cleared, the wallet might attempt to respend the same inputs which would result in a conflicting transaction.  Because this is not currently detected and handled by the wallet, this may result in one or more input billets from the second transaction being deducted from the wallet even though they are not actually spent. This is most likely to occur during high-speed load testing, and can throw off the wallet balance tracking.";
			}
			else if (RandTest(RTEST_TX_ERRORS) && 0) // will throw off balance if enabled
			{
				// note: this test causes input billets to be deallocated then used in another tx
				// resulting in conflicting tx's, only one of which will clear, which throws off the balance tracking

				BOOST_LOG_TRIVIAL(info) << "Transaction::TryCreateTxPay simulating SubmitTx that appeared to fail but actually succeeded for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;

				rc = -1;
			}

			// !!! TODO: distinguish between hard and soft errors?
			//		hard error: tx could not be sent (no connection made), or an error message that definitely indicates tx was rejected
			//		soft error: everything else (for example, tx sent but no response), which indicates tx might have been accepted
			// !!! TODO: If error is "INVALID:already spent" then requery for hashkey to see if this tx succeeded
			// !!! TODO: on soft error, leave input billets allocated for some period, then check if they are spent before deallocating them

			if (rc > 0)
			{
				tx->build_state = TX_BUILD_SUBMIT_INVALID;
				throw txrpc_tx_rejected;
			}
			if (rc < 0)
			{
				tx->build_state = TX_BUILD_SUBMIT_ERR;
				throw txrpc_server_error;
			}

			tx->build_state = TX_BUILD_SUBMIT_OK;

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

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CreateTxFromAddressQueryResult amount " << amount << " destination id " << address.dest_id << " paynum " << address.number << " address " << address.value;

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

int Transaction::UpdateStatus(DbConn *dbconn, uint64_t tx_id, uint64_t bill_id, uint64_t commitnum)
{
	// must be called from inside a BeginWrite

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::UpdateStatus tx id " << tx_id << " bill id " << bill_id << " commitnum " << commitnum;

	Transaction tx;

	auto rc = tx.ReadTx(dbconn, tx_id);
	if (rc) return rc;

	bool all_cleared = true;
	bool none_cleared = true;
	unsigned bill_index = -1;

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		if (tx.output_bills[i].id == bill_id)
		{
			bill_index = i;

			auto rc = tx.output_bills[i].SetStatusCleared(dbconn, commitnum);
			if (rc) return rc;
		}
		else if (tx.output_bills[i].BillIsPending())
			all_cleared = false;
		else
			none_cleared = false;
	}

	CCASSERT(bill_index < tx.nout);

	if (none_cleared)
	{
		// move remaining addresses to front of the polling queue

		for (unsigned i = 0; i < tx.nout; ++i)
		{
			if (tx.output_bills[i].BillIsPending())
			{
				Secret secret;

				rc = dbconn->SecretSelectSecret(&tx.output_bills[i].address, TX_ADDRESS_BYTES, secret);
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

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		Billet& bill = tx.input_bills[i];

		// mark all input serialnum's as spent
		// !!! TBD: could query serialnum's first just to be sure they are spent

		if (bill.status != BILL_STATUS_SPENT)
		{
			auto rc = bill.SetStatusSpent(dbconn);
			if (rc) return rc;
		}
	}

	tx.btc_block = g_btc_block.GetCurrentBlock();

	tx.status = TX_STATUS_CLEARED;

	return dbconn->TransactionInsert(tx);
}
