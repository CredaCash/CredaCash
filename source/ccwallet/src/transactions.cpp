/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
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
#include "exchange.hpp"
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
#include <CCobjects.hpp>
#include <CCmint.h>
#include <transaction.h>
#include <transaction.hpp>
#include <xtransaction-xreq.hpp>
#include <xmatch.hpp>
#include <encode.h>

#include <siphash/siphash.h>

//!#define TEST_NO_ROUND_UP				1
//!#define TEST_FEWER_RETRIES			1
//!#define TEST_FAIL_ALL_TXS			1	// all tx's fail (except mints), so balance should never change--useful for testing that the donation is handled correctly on error
//!#define RTEST_TX_ERRORS				32	// when this is enabled, it's helpful to set tx-polling-addresses=50 and TEST_RANDOM_POLLING = 5 to 20
//!#define RTEST_CREATE_LOOP_BREAK		64	// test early break in StartCreateTxPay inner loop
//!#define RTEST_RELEASE_ERROR_TXID		2	// discard txid after an error
//!#define RTEST_ALLOW_DOUBLE_SPENDS	2
//!#define RTEST_CUZZ					64
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

#ifndef RTEST_CREATE_LOOP_BREAK
#define RTEST_CREATE_LOOP_BREAK		0	// don't test
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

//#define WALLET_TX_MININ	1
#define WALLET_TX_MAXIN		4

#define ASYNC_XPAY_NICE		7	// TODO: make this a config setting

static const string cc_txid_prefix = "CCTX_";
static const string cc_tx_internal_prefix = "CCTX_Internal_";

#define TRACE_TRANSACTIONS	(g_params.trace_transactions)
#define TRACE_BILLETS		(g_params.trace_billets)

static mutex billet_allocate_mutex;
static atomic<unsigned> tx_thread_count(0);

#define SUBTX_AMOUNT	output_bills[0].amount

bigint_t Transaction::SubTxAmount() const
{
	if (Xtx::TypeIsXtx(type))
		return 0UL;
	else
		return SUBTX_AMOUNT;
}

Transaction::Transaction()
{
	Clear();
}

void Transaction::Clear()
{
	memset((void*)this, 0, (uintptr_t)&ref_id - (uintptr_t)this);

	ref_id.clear();
	txbody.clear();
	wire_data.clear();

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
	memcpy((void*)this, &other, (uintptr_t)(&this->ref_id) - (uintptr_t)this);

	ref_id = other.ref_id;
	txbody = other.txbody;
	wire_data = other.wire_data;
	xtx = other.xtx;

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

string Transaction::TypeString(unsigned type)
{
	return Xtx::TypeString(type);
}

string Transaction::StatusString(unsigned status)
{
	static const char *statusstr[TX_STATUS_INVALID + 1] =
	{
		"VOID",
		"Error", "Conflicted", "Abandoned",
		"Reserved", "Pending", "Cleared",
		"INVALID"
	};

	if (status > TX_STATUS_INVALID)
		status = TX_STATUS_INVALID;

	return statusstr[status];
}

string Transaction::DebugString() const
{
	ostringstream out;

	out << "Tx";
	out << " id " << id;
	out << " type " << type << " = " << TypeString();
	out << " status " << status << " = " << StatusString();
	out << " build_type " << build_type;
	out << " build_state " << build_state;
	out << " build_mode " << build_mode;
	out << " parent_id " << parent_id;
	out << " blockchain " << blockchain;
	out << " param_level " << param_level;
	out << " create_time " << create_time;
	out << " btc_block " << btc_block;
	out << " donation " << donation;
	out << " nout " << nout;
	out << " nin " << nin;
	out << " ref_id " << ref_id;
	if (!ref_id.length())
		out << "(none)";
	out << " txbody " << txbody.size();
	out << " wire_data " << wire_data.size();
	out << " txid " << GetBtcTxid();
	if (have_objid)
		out << " objid " << buf2hex(&objid, CC_OID_TRACE_SIZE);
	if (xtx)
		out << " xtx 0x" << hex << xtx << dec;

	return out.str();
}

bool Transaction::IsValid() const
{
	return TypeIsValid(type) && StatusIsValid(status);
}

string Transaction::EncodeInternalTxid() const
{
	if (!id)
		return "TXID_NULL";

	string outs = cc_tx_internal_prefix;

	bigint_t maxval;
	subBigInt(bigint_t(0UL), bigint_t(1UL), maxval, false);

	bigint_t wallet_id = g_params.wallet_id;

	bigint_mask(wallet_id, 48);
	bigint_mask(maxval, 48);

	cc_stringify(base57sym, maxval, false, 0, wallet_id, outs);

	//cerr << "g_params.wallet_id " << hex << g_params.wallet_id << " wallet_id " << wallet_id << " maxval " << maxval << " outs " << outs << dec << endl;

	// encode internal id

	cc_stringify(base57sym, 0UL, false, -1, id, outs);

	//cerr << "id " << id << " outs " << outs << endl;

	// add checksum

	auto hash = siphash(outs.data(), outs.length());
	//cerr << "hash " << hex << hash << dec << endl;
	cc_stringify(base57sym, 0UL, false, 2, hash, outs);

	uint64_t check;
	auto rc = DecodeInternalTxid(outs, check);
	if (rc || check != id)
		cerr << "EncodeInternalTxid error txid " << outs << " rc " << rc << " id " << id << " check " << check << endl;

	return outs;
}

int Transaction::DecodeInternalTxid(const string& txid, uint64_t& id)
{
	id = 0;

	string fn;
	char output[128] = {0};
	uint32_t outsize = sizeof(output);

	unsigned pos = cc_tx_internal_prefix.length();

	if (txid.compare(0, pos, cc_tx_internal_prefix))
		return -2;

	bigint_t maxval;
	subBigInt(bigint_t(0UL), bigint_t(1UL), maxval, false);

	bigint_t wallet_id = g_params.wallet_id;

	bigint_mask(wallet_id, 48);
	bigint_mask(maxval, 48);

	string outs;
	cc_stringify(base57sym, maxval, false, 0, wallet_id, outs);

	if (txid.compare(pos, outs.length(), outs))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeInternalTxid wallet id mismatch";

		return 1;
	}

	pos += outs.length();

	// decode internal id

	if (txid.length() < pos + 3)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeInternalTxid txid too short";

		return -1;
	}

	outs = txid.substr(pos, txid.length() - pos - 2);

	bigint_t bigval;
	auto rc = cc_destringify(fn, base57bin, false, outs.length(), outs, bigval, output, outsize);
	if (rc || bigval > bigint_t((uint64_t)(-1)))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeInternalTxid id decode failed: " << output;

		return -1;
	}

	//cerr << "id " << bigval << " outs " << outs << endl;

	// check checksum

	pos = txid.length() - 2;

	outs.clear();
	auto hash = siphash(txid.data(), pos);
	cc_stringify(base57sym, 0UL, false, 2, hash, outs);
	//cerr << "hash " << hex << hash << dec << " " << outs << " " << instring << endl;
	if (txid.compare(pos, 2, outs))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeInternalTxid checksum mismatch";

		return -1;
	}

	id = BIG64(bigval);

	return 0;
}

string Transaction::EncodeBtcTxid(uint64_t dest_chain, const bigint_t& address, const bigint_t& commitment)
{
	bigint_t m1;
	subBigInt(bigint_t(0UL), bigint_t(1UL), m1, false);

	string outs = cc_txid_prefix;

	// encode dest_chain

	cc_stringify(base57sym, 0UL, false, -1, dest_chain, outs);

	// encode address

	bigint_t maxval = m1;
	bigint_mask(maxval, TX_ADDRESS_BITS);

	cc_stringify(base57sym, maxval, false, 0, address, outs);

	// encode commitment

	auto masked = commitment;
	maxval = m1;

	bigint_mask(masked, TXID_COMMITMENT_BYTES * 8);
	bigint_mask(maxval, TXID_COMMITMENT_BYTES * 8);

	cc_stringify(base57sym, maxval, false, 0, masked, outs);

	// add checksum

	auto hash = siphash(outs.data(), outs.length());
	//cerr << "hash " << hex << hash << dec << endl;
	cc_stringify(base57sym, 0UL, false, 4, hash, outs);

	//cerr << hex << " dest_chain " << dest_chain << " address " << address << " commitment " << commitment << dec << endl;
	//DecodeBtcTxid(outs, dest_chain, m1, maxval);

	return outs;
}

int Transaction::DecodeBtcTxid(const string& txid, uint64_t& id, uint64_t& dest_chain, bigint_t& address, bigint_t& commitment)
{
	id = 0;
	dest_chain = 0;
	address = 0UL;
	commitment = 0UL;

	auto rc = DecodeInternalTxid(txid, id);
	if (rc > 0) return rc;
	if (!rc && id)
		return 0;

	string fn;
	char output[128] = {0};
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
	rc = cc_destringify(fn, base57bin, false, chainlen, instring, bigval, output, outsize);
	if (rc || bigval > bigint_t((uint64_t)(-1)))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::DecodeBtcTxid blockchain decode failed: " << output;

		return -1;
	}

	dest_chain = BIG64(bigval);

	rc = cc_destringify(fn, base57bin, false, 22, instring, address, output, outsize);
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

	rc = cc_destringify(fn, base57bin, false, 22, instring, commitment, output, outsize);
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
	auto hash = siphash(txid.data(), inlen - instring.length());
	cc_stringify(base57sym, 0UL, false, 4, hash, outs);
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
	if (id && !parent_id && nout && !(output_bills[0].flags & (BILL_FLAG_NO_TXID | BILL_IS_CHANGE)))
		return EncodeBtcTxid(output_bills[0].blockchain, output_bills[0].address, output_bills[0].commitment);
	else
		return EncodeInternalTxid();
}

void Transaction::AppendTxBody(TxPay& ts) const
{
	if (txbody.size() >= sizeof(CCObject::Header::tag))
	{
		ts.append_data_length = txbody.size() - sizeof(CCObject::Header::tag);
		memcpy(ts.append_data.data(), txbody.data() + sizeof(CCObject::Header::tag), ts.append_data_length);
	}
}

void Transaction::FinishCreateTx(TxPay& ts, TxParams& txparams, TxBuildEntry *entry, unsigned retry)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FinishCreateTx " << DebugString();

	auto t0 = ccticks();

	CCASSERT(type);

	ts.tag_type = type;

	ts.tx_type = type;

	if (txbody.size() && build_type == TX_BUILD_FINAL)
	{
		if (entry)	// TODO: in future, do this only if xtx has an expiration
		{
			entry->SetTxBody(txparams, retry);

			CCASSERT(txbody.size() == entry->txbody.size());

			txbody = entry->txbody;
		}

		if (type == CC_TYPE_XCX_NAKED_BUY)
		{
			auto rc = Xtx::SetPow(txbody.data(), txbody.size(), txparams.xcx_naked_buy_work_difficulty, entry->xtx->expire_time - txparams.clock_diff - txparams.xcx_minimum_expiration);
			if (g_shutdown) throw txrpc_shutdown_error;
			if (rc > 0) throw txrpc_tx_timeout;
			if (rc) throw txrpc_wallet_error;
		}

		AppendTxBody(ts);

		//cerr << "ts.append_data nbytes " << ts.append_data_length << " data " << buf2hex(ts.append_data.data(), ts.append_data_length) << endl;
	}

	if (entry && build_type == TX_BUILD_FINAL)
	{
		ts.amount_carry_in = entry->xtx->amount_carry_in;
		ts.amount_carry_out = entry->xtx->amount_carry_out;
	}

	string fn;
	char output[128] = {0};
	uint32_t outsize = sizeof(output);

	auto rc = txpay_create_finish(fn, ts, output, outsize);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::FinishCreateTx txpay_create_finish failed: " << output << "; transaction " << DebugString();

		//tx_dump_stream(cout, ts);	// for debugging

		if (output[0])
			throw RPC_Exception(RPC_WALLET_INTERNAL_ERROR, output);
		else
			throw txrpc_wallet_error;
	}

	if (build_type == TX_BUILD_CANCEL_TX)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::FinishCreateTx setting status abandoned " << DebugString();

		// For now, the tx status is TX_STATUS_ABANDONED.
		// The status will change to TX_STATUS_CONFLICTED if/when the tx's input bills are spent in a future transaction.

		status = TX_STATUS_ABANDONED;
	}
	else
		status = TX_STATUS_PENDING;

	param_level = ts.param_level;

	tx_amount_decode(ts.donation_fp, donation, true, ts.donation_bits, ts.exponent_bits);

	nout = ts.nout;

	for (unsigned i = 0; i < nout; ++i)
		output_bills[i].SetFromTxOut(ts, ts.outputs[i]);

	BOOST_LOG_TRIVIAL(info) << "Transaction::FinishCreateTx type " << type << " elapsed ticks " << ccticks_elapsed(t0, ccticks());
}

int Transaction::SaveOutgoingTx(DbConn *dbconn)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::SaveOutgoingTx " << DebugString();

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	CCASSERT(type);

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	CCASSERTZ(id);

	if (have_objid && Xtx::TypeIsXreq(type))
	{
		// make sure Xreq objid is unique
		//	(note: other tx types do not need a unique objid; this allows Xpay msgs to be submitted more than once)

		Transaction tx;

		rc = dbconn->TransactionSelectObjIdDescendingId(objid, INT64_MAX, tx);
		if (rc < 0) return rc;

		if (!rc && tx.status >= TX_STATUS_PENDING)
			return 1;	// objid is not unique
	}

	rc = dbconn->TransactionInsert(*this);

	auto tx_id = id;
	id = 0;				// leave this zero until the tx is committed

	if (rc) return -1;

	// input billets were all marked when allocated, but some could now be deallocated or marked spent

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
			BOOST_LOG_TRIVIAL(info) << "Transaction::SaveOutgoingTx already spent " << check.DebugString();

			//cerr << "Transaction::SaveOutgoingTx billet already spent" << endl;

			return 1;
		}

		if (build_type != TX_BUILD_CANCEL_TX && check.status == BILL_STATUS_CLEARED)
		{
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
		{
			if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(debug) << "Transaction::SaveOutgoingTx setting status abandoned for tx_id " << tx_id << " " << bill.DebugString();

			bill.status = BILL_STATUS_ABANDONED;
		}
		else
			bill.status = BILL_STATUS_PENDING;

		CCASSERTZ(bill.id);

		rc = dbconn->BilletInsert(bill);
		if (rc) return -1;
	}

	if (Xtx::TypeIsXreq(type) && status != TX_STATUS_ERROR)
	{
		CCASSERT(xtx);

		const Xreq& xreq(*Xreq::Cast(xtx));

		if (xreq.type != CC_TYPE_XCX_MINING_TRADE)
		{
			Xmatchreq mreq(xreq, tx_id);

			rc = dbconn->ExchangeRequestInsert(mreq);
			if (rc) return -1;
		}
		else
		{
			Xreq xreq1(xreq);
			Xreq xreq2(xreq);

			xreq1.ConvertTradeToBuy();
			xreq2.ConvertTradeToSell();

			Xmatchreq mreq1(xreq1, tx_id);
			Xmatchreq mreq2(xreq2, tx_id);

			rc = dbconn->ExchangeRequestInsert(mreq1);
			if (rc) return -1;

			rc = dbconn->ExchangeRequestInsert(mreq2);
			if (rc) return -1;
		}
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

	uint64_t now = unixtime();

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

		secret.next_poll = now + 1 + RandTest(2);

		// these lines commented out so tx's sent by a copy of this wallet are detected:
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
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::BeginAndReadTx id " << id << " or_greater " << or_greater;

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

int Transaction::BeginAndReadTxLevel(DbConn *dbconn, uint64_t level, uint64_t last_id)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::BeginAndReadTxLevel level " << level << " last_id " << last_id;

	auto rc = dbconn->BeginRead();
	if (rc)
	{
		Clear();

		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	return ReadTxLevel(dbconn, level, last_id);
}

int Transaction::ReadTx(DbConn *dbconn, uint64_t id, bool or_greater)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTx id " << id << " or_greater " << or_greater;

	Clear();

	auto rc = dbconn->TransactionSelectId(id, *this, or_greater);
	if (rc > 0 && or_greater)
	{
		BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTx id " << id << " returned " << rc;

		return 1;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTx error reading id " << id;

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
		BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTxRefId ref_id " << ref_id << " returned " << rc;

		return 1;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxRefId error reading ref_id " << ref_id;

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
		BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTxIdDescending id " << id << " returned " << rc;

		return 1;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxIdDescending error reading id " << id;

		return -1;
	}

	return ReadTxBillets(dbconn);
}

int Transaction::ReadTxLevel(DbConn *dbconn, uint64_t level, uint64_t last_id)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTxLevel level " << level << " last_id " << last_id;

	Clear();

	auto rc = dbconn->TransactionSelectLevel(level, last_id, *this);
	if (rc > 0)
	{
		BOOST_LOG_TRIVIAL(trace) << "Transaction::ReadTxLevel transaction level " << level << " last_id " << last_id << " returned " << rc;

		return 1;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ReadTxLevel error reading transaction level " << level << " last_id " << last_id;

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

//!!!!! bug: repeated calls to SetAdjustedAmounts

void Transaction::SetAdjustedAmounts(bool incwatch, bigint_t amount_carry_in, bigint_t amount_carry_out)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAdjustedAmounts incwatch " << incwatch << " id " << id << " amount_carry_in " << amount_carry_in << " amount_carry_out " << amount_carry_out;

#if TEST_BIG_DIVISION
	for (unsigned i = 0; i < 8000; ++i)
		TestBigDiv();
#endif

	// the bitcoin-emulation API reports each txout (except change) as a separate transaction with its own txid
	// the donation is assigned pro rata to the non-change txout's + the excess txin amount
	//	Note: if a transaction comes in from outside, the change outputs might not be marked
	// the total transaction amount for the txout is generally the same as the txout amount, except:
	//	if the txin's exceed the txout's, then the excess txin's are assigned pro rata to all the non-zero txout's

	// TODO: needs to be tested to work correctly as more capabilities are added to wallet

	bigint_t total_sent = 0UL;
	bigint_t sum_sent = 0UL;
	bigint_t sum_out = amount_carry_out;
	bigint_t sum_in = amount_carry_in;

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
	if (WeSent(incwatch))						// !!!!! this is likely not the right condition for computing net_donation
		net_donation = donation + excess_in;

	bigint_t total_donation = 0UL;
	bigint_t total_out = 0UL;

	if (!nout)
	{
		adj_donations[0] = net_donation;

		return;
	}

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

static void InitTxPay(TxPay& ts, const TxParams& txparams, const QueryInputResults *inputs = NULL)
{
	tx_init(ts);

	ts.default_domain__ = txparams.default_domain;

	ts.amount_bits = txparams.amount_bits;
	ts.donation_bits = txparams.donation_bits;
	ts.exponent_bits = txparams.exponent_bits;

	ts.source_chain = txparams.blockchain;
	ts.outvalmin = txparams.outvalmin;
	ts.outvalmax = txparams.outvalmax;
	ts.allow_restricted_addresses = true;

	if (inputs)
	{
		ts.param_level = inputs->param_level;
		ts.param_time = inputs->param_time;
		ts.tx_merkle_root = inputs->merkle_root;
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
	if (txparams.NotConnected()) throw txrpc_not_synced_error;

	if (Implement_CCMint(txparams.blockchain))
	{
		if (inputs.param_level >= CC_MINT_COUNT)
			return -1;
	}
	else if (!IsTestnet(txparams.blockchain))
		return -1;

	type = CC_TYPE_MINT;
	blockchain = txparams.blockchain;

	Secret address;
	SpendSecretParams params;

	rc = address.CreateNewSecret(dbconn, SECRET_TYPE_SELF_ADDRESS, MINT_DESTINATION_ID, txparams.blockchain, params);
	if (rc) throw txrpc_wallet_error;

	//tx_dump_spend_secret_params_stream(cerr, params);

	TxPay ts;

	InitTxPay(ts, txparams, &inputs);

	//ts.no_proof = rand() & 3;	// for testing, randomly create a bad mint

	ts.nout = 1;
	TxOut& txout = ts.outputs[0];
	memcpy(&txout.addrparams, &params.addrparams, sizeof(txout.addrparams));

	txout.addrparams.__flags |= BILL_RECV_MASK;

	//tx_dump_address_params_stream(cerr, txout.addrparams);

	txout.M_domain = txparams.default_domain;

	donation = TX_CC_MINT_DONATION;
	ts.donation_fp = tx_amount_encode(donation, true, txparams.donation_bits, txparams.exponent_bits);

	uint64_t asset = 0;
	bigint_t amount;
	amount = TX_CC_MINT_AMOUNT;
	amount = amount - donation;
	txout.__amount_fp = tx_amount_encode(amount, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);

	FinishCreateTx(ts, txparams);

	CCASSERT(txout.M_address == address.value);

	if (g_shutdown)
		throw txrpc_shutdown_error;

	// Save tx in db before submitting
	// (If tx submitted first, Poll thread could detect and save billets before this thread, and that would have to be sorted out...)

	rc = SaveOutgoingTx(dbconn);
	if (rc) throw txrpc_wallet_db_error;

	build_state = TX_BUILD_SAVED;

	// Submit tx to network

	uint64_t next_commitnum;

	rc = TrySubmitTx(txquery, &ts, 0, next_commitnum, "mint_tx", false);

	SetObjId(dbconn);

	if (rc < 0)
	{
		SetStatus(dbconn, TX_STATUS_ERROR, BILL_STATUS_ERROR);

		if (rc == -1 && Implement_CCMint(txparams.blockchain) && !ts.param_level)
			throw RPC_Exception(RPC_VERIFY_REJECTED, "Mint not yet started");

		ThrowSubmitTxException(rc);
	}

	if (IsInteractive())
	{
		string amts;
		amount_to_string(asset, SubTxAmount(), amts);
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Mint transaction submitted to network.  If this transaction is accepted by the blockchain," << endl;
		cerr << amts << " units of currency will be credited to the newly-generated unique address " << hex << ts.outputs[0].M_address << dec << endl;
		for (unsigned i = 0; i < ts.nout; ++i)
		{
			amount_to_string(asset, output_bills[i].amount, amts);
			cerr << "Output " << i + 1 << "\n"
			"   billet commitment " << hex << ts.outputs[i].M_commitment << dec << "\n"
			"   published asset   " << ts.outputs[i].M_asset_enc << "\n"
			"   published amount  " << amts << "\n"
			"   address           " << hex << ts.outputs[i].M_address << dec << endl;
		}
		amount_to_string(asset, donation, amts);
		cerr << "Donation amount " << amts << endl;
		cerr << endl;
	}

	rc = UpdatePolling(dbconn, next_commitnum);
	(void)rc;

	return 0;
}

void Transaction::SetObjId(DbConn *dbconn)
{
	if (!have_objid)
		return;

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetObjId id " << id << " objid " << buf2hex(&objid, CC_OID_TRACE_SIZE);

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		throw txrpc_wallet_db_error;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	Transaction tx;	// use a new tx so values in *this aren't lost

	rc = dbconn->TransactionSelectId(id, tx);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(warning) << "Transaction::SetStatus error reading tx id " << id;

		throw txrpc_wallet_db_error;
	}

	tx.objid = objid;
	tx.have_objid = true;

	rc = dbconn->TransactionInsert(tx);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(warning) << "Transaction::SetStatus error saving tx id " << id;

		throw txrpc_wallet_db_error;
	}

	rc = dbconn->Commit();
	if (rc) throw txrpc_wallet_db_error;

	dbconn->DoDbFinishTx();

	finally.Clear();
}

void Transaction::SetStatus(DbConn *dbconn, unsigned tx_status, unsigned out_bill_status)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetStatus id " << id << " status " << status << " = " << StatusString(status) << " bill_status " << out_bill_status << " = " << Billet::StatusString(out_bill_status);

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
		BOOST_LOG_TRIVIAL(warning) << "Transaction::SetStatus error reading tx id " << id;

		throw txrpc_wallet_db_error;
	}

	status = tx_status;

	rc = dbconn->TransactionInsert(*this);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(warning) << "Transaction::SetStatus error saving tx id " << id;

		throw txrpc_wallet_db_error;
	}

	for (unsigned i = 0; i < nout && out_bill_status; ++i)
	{
		Billet& bill = output_bills[i];

		bill.status = out_bill_status;

		rc = dbconn->BilletInsert(bill);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(warning) << "Transaction::SetStatus error saving output bill id " << bill.id << " in tx id " << id;

			throw txrpc_wallet_db_error;
		}
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

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

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

int Transaction::LocateBillet(DbConn *dbconn, uint64_t blockchain, const bigint_t& amount, const bigint_t& min_amount, const bigint_t& total_required, Billet& bill, uint64_t& billet_count)
{
	// returns:
	//	 0 if billet located and allocated
	//	 1 if no billet could be allocated and amount was added to required (resulting required >= pending)
	//	-1 if no billet could be allocated and not enough pending

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
		if (!rc)
			break;

		rc = dbconn->BilletSelectAmountMax(blockchain, asset, max_delaytime, bill);
		if (rc < 0) throw txrpc_wallet_db_error;
		if (!rc && bill.amount >= min_amount)
			break;

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::LocateBillet no billet found amount " << amount << " min_amount " << min_amount << " total_required " << total_required;

		bill.Clear();

		rc = Total::AddNoWaitAmounts(0UL, true, total_required, true);
		if (!rc)
			return 1;	// if total_required is pending, then wait for it

		return -1;
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

void Transaction::ReleaseBillets(DbConn *dbconn, int start, int count)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::ReleaseBillets start " << start << " count " << count;

	if (count < 1)
		return;

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		throw txrpc_wallet_db_error;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	for (int i = start; i < start + count; ++i)
	{
		Billet& bill = input_bills[i];

		rc = dbconn->BilletSelectId(bill.id, bill);
		if (rc) throw txrpc_wallet_db_error;

		if (bill.status != BILL_STATUS_ALLOCATED)
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::ReleaseBillets skipping release of billet id " << bill.id << " status " << bill.status;

			return;
		}

		bill.status = BILL_STATUS_CLEARED;

		rc = dbconn->BilletInsert(bill);
		if (rc) throw txrpc_wallet_db_error;

		if (RandTest(RTEST_CUZZ)) sleep(1);
	}

	rc = dbconn->Commit();
	if (rc) throw txrpc_wallet_db_error;

	dbconn->DoDbFinishTx();

	finally.Clear();
}

int Transaction::WaitNewBillet(const bigint_t& total_required, const uint64_t billet_count, const uint64_t timeout, bool test_fail) // throws RPC_Exception)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::WaitNewBillet";

	if (test_fail && RandTest(RTEST_TX_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::WaitNewBillet simulating error";

		Total::AddNoWaitAmounts(0UL, true, total_required, false);

		throw txrpc_simulated_error;
	}

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Waiting up to " << g_params.billet_wait_time << " seconds for pending billets to become available...\n" << endl;
	}

	// TODO: adjust the wait time to not exceed the transaction timeout?

	auto rc = Billet::WaitNewBillet(billet_count, g_params.billet_wait_time);
	if (g_shutdown) throw txrpc_shutdown_error;

	Total::AddNoWaitAmounts(0UL, true, total_required, false);

	CheckTimeout(timeout);

	if (rc)
	{
		if (IsInteractive())
		{
			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
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

void Transaction::CheckTimeout(const uint64_t timeout)
{
	int64_t dt = timeout - unixtime();

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CheckTimeout timeout " << timeout << " dt " << dt;

	if (timeout && dt < 0)
	{
		if (IsInteractive())
		{
			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
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

void Transaction::ResetNout(unsigned min)
{
	if (build_type != TX_BUILD_FINAL)
		nout = WALLET_TX_MINOUT;
	else if (Xtx::TypeIsXreq(type))
		nout = 1;
	else if (Xtx::TypeIsXtx(type))
		nout = 0;
	else
		nout = WALLET_TX_MINOUT;

	if (nout < min)
		nout = min;
}

void Transaction::ComputeDonation(TxParams& txparams, bigint_t& donation) const
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ComputeDonation type " << type << " nin " << nin << " nout " << nout;

	CCASSERT(type);

	//donation = bigint_t("1000000000000000000000000000");
	//return;

	donation = 0UL;

	if (Xtx::TypeHasBareMsg(type))
		return;

	TxPay ts;
	uint32_t nbytes;

	InitTxPay(ts, txparams);

	ts.tag_type = type;
	ts.nout = nout;
	ts.nin = nin;
	ts.nin_with_path = nin;

	for (unsigned i = 0; i < nout; ++i)
		ts.outputs[i].M_domain = (output_bills[i].domain ? output_bills[i].domain : txparams.default_domain);

	for (unsigned i = 0; i < nin; ++i)
	{
		ts.inputs[i].pathnum = 1;
		ts.inputs[i].M_domain = (input_bills[i].domain ? input_bills[i].domain : txparams.default_domain);
	}

	AppendTxBody(ts);

	string fn;
	char output[128] = {0};
	uint32_t outsize = sizeof(output);

	auto rc = txpay_to_wire(fn, ts, 0, output, outsize, NULL, 0, &nbytes);
	if (rc < 0)
	{
		BOOST_LOG_TRIVIAL(error) << "Transaction::ComputeDonation " << output;

		throw txrpc_wallet_error;
	}

	if (CCObject::HasPOW(ts.wire_tag))
	{
		CCASSERT(nbytes >= TX_POW_SIZE);
		nbytes -= TX_POW_SIZE;
	}

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ComputeDonation type " << type << " wire tag " << hex << ts.wire_tag << dec << " nin " << nin << " nout " << nout << " nbytes " << nbytes << " computed size " << txparams.ComputeTxSize(nout, nin);
	//tx_dump_stream(cerr, ts);

	txparams.ComputeDonation(type, nbytes, nout, nin, donation);
}

int Transaction::ComputeChange(TxParams& txparams, const bigint_t& carry_in, const bigint_t& carry_out, bigint_t *shortfall)
{
	bigint_t inputs = carry_in;
	bigint_t outputs = carry_out;

	for (unsigned i = 0; i < nin; ++i)
		inputs = inputs + input_bills[i].amount;

	for (unsigned i = 0; i < nout; ++i)
		outputs = outputs + output_bills[i].amount;

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::ComputeChange type " << TypeString() << " build_type " << build_type << " nin " << nin << " nout " << nout << " inputs " << inputs << " outputs " << outputs << " output amount " << SubTxAmount();

	// set donation and change

	auto change_start = nout;

	ResetNout(change_start);

	bigint_t remainder, needed;

	while (true)
	{
		ComputeDonation(txparams, donation);
		auto amount_fp = tx_amount_encode(donation, true, txparams.donation_bits, txparams.exponent_bits);
		tx_amount_decode(amount_fp, donation, true, txparams.donation_bits, txparams.exponent_bits);

		change = 0UL;

		for (unsigned i = change_start; ; ++i)
		{
			remainder = outputs + donation + change;

			if (inputs > remainder)
			{
				remainder = inputs - remainder;
				needed = 0UL;
			}
			else
			{
				needed = remainder - inputs;
				remainder = 0UL;
			}

			if (i >= nout)
				break;

			Billet& bill = output_bills[i];

			// TODO: ignore outvalmin and outvalmax when asset > 0
			amount_fp = tx_amount_encode(remainder, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);

			tx_amount_decode(amount_fp, bill.amount, false, txparams.amount_bits, txparams.exponent_bits);

			change = change + bill.amount;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ComputeChange build_type " << build_type << " nout " << nout << " change billet " << i << " amount " << bill.amount << " total change " << change << " donation " << donation;
		}

		if (!remainder || nout >= TX_MAXOUT)
			break;

		++nout;
	}

	if (shortfall)
		*shortfall = needed;

	int rc = !!remainder || !!needed;

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::ComputeChange build_type " << build_type << " nin " << nin << " send amount " << SubTxAmount() << " remainder " << remainder << " shortfall " << needed << " returning " << rc;

	return rc;
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
	if (txparams.NotConnected()) throw txrpc_not_synced_error;

	blockchain = txparams.blockchain;

	if (!dest_chain)
		dest_chain = txparams.blockchain;

	if (Implement_CCMint(dest_chain) && inputs.param_level < CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
		throw txrpc_tx_rejected;

	// compute and recheck encoded amounts using txparams returned by QueryInputs

	InitTxPay(ts, txparams, &inputs);

	bigint_t check;
	ComputeDonation(txparams, check);
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
		txout.__amount_fp = tx_amount_encode(bill.amount, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);
		tx_amount_decode(txout.__amount_fp, check, false, txparams.amount_bits, txparams.exponent_bits);
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FillOutTx output " << i << " amount " << bill.amount << " check " << check << " amount_fp " << txout.__amount_fp << " amount_bits " << txparams.amount_bits << " exponent_bits " << txparams.exponent_bits << " outvalmin " << txparams.outvalmin << " outvalmax " << txparams.outvalmax;
		if (bill.amount != check)
		{
			BOOST_LOG_TRIVIAL(warning) << "Transaction::FillOutTx output amount mismatch " << bill.amount << " != " << check;

			return 1;	// params may have changed, so retry
		}
	}

	// set the ts values

	ts.nout = nout;
	ts.nin = nin;
	ts.nin_with_path = nin;

	uint64_t asset_mask = (txparams.asset_bits < 64 ? ((uint64_t)1 << txparams.asset_bits) - 1 : -1);
	uint64_t amount_mask = (txparams.amount_bits < 64 ? ((uint64_t)1 << txparams.amount_bits) - 1 : -1);

	for (unsigned i = 0; i < nout; ++i)
	{
		TxOut& txout = ts.outputs[i];

		txout.M_domain = txparams.default_domain;
		txout.asset_mask = asset_mask;
		txout.amount_mask = amount_mask;
	}

	for (unsigned i = 0; i < nin; ++i)
	{
		Billet& bill = input_bills[i];
		TxIn& txin = ts.inputs[i];

		if (!bill.id)
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::FillOutTx input " << i << " null id";

			throw txrpc_wallet_error;
		}

		if (bill.blockchain != dest_chain)
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::FillOutTx input " << i << " blockchain mismatch " << bill.blockchain << " != " << dest_chain;

			throw txrpc_wallet_error;
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

		bill.spend_hashkey.clear();
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

		txin.M_domain = bill.domain;
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
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetAddresses build_type " << build_type << " tx type " << TypeString() << " nin " << nin << " nout " << nout << " dest_chain " << dest_chain << " destination id " << destination.id;

	bool use_destination = (ts.nout && build_type == TX_BUILD_FINAL && !Xtx::TypeIsXtx(type));

	for (unsigned i = use_destination; i < ts.nout; ++i)
	{
		Secret secret;
		SpendSecretParams params;

		auto rc = secret.CreateNewSecret(dbconn, SECRET_TYPE_SELF_ADDRESS, SELF_DESTINATION_ID, dest_chain, params);
		if (rc) throw txrpc_wallet_error;

		TxOut& txout = ts.outputs[i];
		memcpy(&txout.addrparams, &params.addrparams, sizeof(txout.addrparams));

		txout.addrparams.__flags |= BILL_RECV_MASK | BILL_FLAG_TRUSTED | BILL_IS_CHANGE;
	}

	if (use_destination)
	{
		// set destination output

		Secret secret;
		SpendSecretParams params;

		auto rc = secret.CreateNewSecret(dbconn, SECRET_TYPE_SEND_ADDRESS, destination.id, dest_chain, params);
		if (rc) throw txrpc_wallet_error;

		TxOut& txout = ts.outputs[0];
		memcpy(&txout.addrparams, &params.addrparams, sizeof(txout.addrparams));

		txout.addrparams.__flags |= Billet::FlagsFromDestinationType(destination.type) | BILL_FLAG_TRUSTED;
	}
}

static void SetDone(TxBuildEntry *entry)
{
	g_txbuildlist.SetDone(entry);
}

int Transaction::CreateTxPay(DbConn *dbconn, TxQuery& txquery, int mode, string& ref_id, unsigned type, const string& encoded_dest, uint64_t dest_chain, const bigint_t& destination, const bigint_t& amount, const string& comment, const string& comment_to, const bool subfee, Xtx *xtx) // throws RPC_Exception
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CreateTxPay mode " << mode << " ref_id " << ref_id << " type " << TypeString(type) << " dest_chain " << dest_chain << " destination " << buf2hex(&destination, sizeof(destination)) << " amount " << amount << " subfee " << subfee << " comment " << comment << " comment_to " << comment_to;

	/*
		For privacy, all tx's are 1 to 4 in -> 2 out
			sometimes will need 1 or 2 in for speed or because wallet holds only 1 or 2 unspent billets
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
	Secret secret;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;
	if (txparams.NotConnected())
	{
		rc = g_txparams.UpdateParams(txparams, txquery);
		if (rc) throw txrpc_server_error;
		if (txparams.NotConnected()) throw txrpc_not_synced_error;
	}

	if (xtx)
	{
		xtx->amount_bits = txparams.amount_bits;
		xtx->exponent_bits = txparams.exponent_bits;
	}

	// check dest_chain
	// dest_chain comes from the encoded destination string and can be zero, which means use any blockchain
	// but if not zero, it must match the blockchain of the server to which the wallet is connected

	if (dest_chain && dest_chain != txparams.blockchain)
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Destination blockchain not supported by transaction server");

	secret.dest_chain = dest_chain;			// save secret with dest_chain encoded in the destination string

	if (!dest_chain)
		dest_chain = txparams.blockchain;	// if the destination string didn't specify a blockchain, use the transaction server blockchain

	if (type == CC_TYPE_TXPAY)
	{
		// add destination to db, and if that fails retrieve existing destination

		secret.type = SECRET_TYPE_SEND_DESTINATION;
		secret.value = destination;

		rc = secret.ImportSecret(dbconn);
		if (rc) throw txrpc_wallet_error;

		if (!secret.TypeIsDestination())
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPay error secret is not a valid destination; " << secret.DebugString();

			throw txrpc_wallet_error;
		}

		rc = secret.CheckForConflict(dbconn, txquery, dest_chain);
		if (rc < 0)
			throw txrpc_wallet_error;
		else if (rc)
			throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "The destination has already been used by another wallet. To preserve privacy, a new unique destination is required.");
	}

	if (!ref_id.length())
		ref_id = unique_id_generate(dbconn, "R", 0, 2);

	DbConn *thread_dbconn = NULL;
	TxQuery *thread_txquery = NULL;
	TxBuildEntry *entry = NULL;

	Finally finally(boost::bind(&Transaction::CreateTxPayThreadCleanup, &thread_dbconn, &thread_txquery, &entry, false));

	rc = g_txbuildlist.StartBuild(dbconn, txparams, ref_id, mode, type, encoded_dest, dest_chain, destination, amount, xtx, &entry, *this);
	if (rc)
	{
		if (entry)
		{
			if (mode == TX_MODE_ASYNC)
				return 1;
			else if (mode)
				throw RPC_Exception(RPC_INVALID_PARAMETER, "reference id already exists");

			if (IsInteractive())
			{
				lock_guard<mutex> lock(g_cerr_lock);
				check_cerr_newline();
				cerr << "Waiting for prior transaction with reference id " << ref_id << " to complete...\n" << endl;
			}

			g_txbuildlist.WaitForCompletion(dbconn, entry, *this);
		}
		else if (IsInteractive())
		{
			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
			cerr << "Returning results of prior transaction with reference id " << ref_id << endl;
		}

		if (StatusIsNotError())
			return 0;
		else
			throw RPC_Exception(RPC_TRANSACTION_FAILED, "Previously submitted transaction failed.\n");
	}

	CCASSERT(entry);

	Finally finally2(boost::bind(&SetDone, entry));

	if (type == CC_TYPE_TXPAY && IsInteractive())
	{
		string amts;
		amount_to_string(0, amount, amts);
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Sending " << amts << " units to destination " << encoded_dest << endl;
		if (subfee || comment.length() || comment_to.length())
			cerr << "The \"comment\", \"comment-to\" and \"subtractfeefromamount\" options are not yet implemented and will be ignored." << endl;
		cerr << endl;
	}

	if (g_shutdown)
		throw txrpc_shutdown_error;

	if (mode != TX_MODE_ASYNC)
	{
		try
		{
			return DoCreateTxPay(dbconn, txquery, txparams, entry, secret);
		}
		catch (const RPC_Exception& e)
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPay caught exception \"" << e.what() << "\" " << entry->DebugString();

			SaveErrorTx(dbconn, ref_id, type);

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
		finally2.Clear();

		t->detach();
		delete t;

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
	BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPayThread " << entry->DebugString();

	++tx_thread_count;

	Finally finally(boost::bind(&Transaction::CreateTxPayThreadCleanup, &dbconn, &txquery, &entry, true));
	Finally finally2(boost::bind(&SetDone, entry));
	Finally finally3(boost::bind(&CCProof_Free));

	try
	{
		if (ASYNC_XPAY_NICE && Xtx::TypeIsXpay(entry->type))
			set_nice(ASYNC_XPAY_NICE);

		Transaction tx;

		tx.DoCreateTxPay(dbconn, *txquery, txparams, entry, secret);
	}
	catch (const RPC_Exception& e)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxPayThread caught exception \"" << e.what() << "\" " << entry->DebugString();

		//cerr << "Transaction::CreateTxPayThread caught exception: " << e.what() << endl;

		SaveErrorTx(dbconn, entry->ref_id, entry->type);
	}
}

void Transaction::SaveErrorTx(DbConn *dbconn, const string& ref_id, unsigned type)
{
	// make sure there is a saved transaction with ref_id that has an error status

	if (!ref_id.length())
	{
		BOOST_LOG_TRIVIAL(warning) << "Transaction::SaveErrorTx no ref_id";

		return;
	}

	BOOST_LOG_TRIVIAL(info) << "Transaction::SaveErrorTx ref_id " << ref_id;

	Transaction tx;

	auto rc = dbconn->TransactionSelectRefId(ref_id.c_str(), tx);
	if (!rc)
	{
		if (tx.status != TX_STATUS_ERROR)
		{
			tx.status = TX_STATUS_ERROR;

			rc = dbconn->TransactionInsert(tx, true);
			if (!rc)
				BOOST_LOG_TRIVIAL(error) << "Transaction::SaveErrorTx ref_id " << ref_id << " error updating transaction " << tx.DebugString();
		}
	}
	else
	{
		tx.Clear();

		tx.ref_id = ref_id;
		tx.type = type;
		tx.status = TX_STATUS_ERROR;

		tx.SaveOutgoingTx(dbconn);
	}
}

int Transaction::DoCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, Secret& secret) // throws RPC_Exception
{
	BOOST_LOG_TRIVIAL(info) << "Transaction::DoCreateTxPay " << entry->DebugString();

	deque<Transaction> tx_list(1);

	Finally finally(boost::bind(&Transaction::CleanupSubTxs, dbconn, entry->dest_chain, ref(secret), ref(tx_list)));

	TryCreateFn *tryfn = TryCreateTxPay;
	if (Xtx::TypeHasBareMsg(entry->type))
		tryfn = TryCreateBareTx;

	uint64_t timeout = unixtime() + g_params.tx_create_timeout;
	if (!g_params.tx_create_timeout)
		timeout = 0;
	else if (!timeout)
		timeout = 1;

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
			BOOST_LOG_TRIVIAL(info) << "Transaction::DoCreateTxPay error reached retry limit " << retry;

			throw txrpc_wallet_error;
		}

		if (retry > 200)
		{
			sleep(1);

			if (g_shutdown)
				throw txrpc_shutdown_error;
		}

		for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
			tx->build_type = TX_BUILD_TYPE_NULL;

		auto rc = (*tryfn)(dbconn, txquery, txparams, entry, secret, timeout, tx_round_up, tx_list);
		if (!rc) break;

		CheckTimeout(timeout);

		CleanupSubTxs(dbconn, entry->dest_chain, secret, tx_list);
	}

	finally.Clear();

	*this = tx_list[0];

	return 0;
}

bool Transaction::SubTxIsActive(bool need_intermediate_txs) const
{
	if (!(build_type && build_state))
		return false;
	else if (need_intermediate_txs)
		return (build_type != TX_BUILD_FINAL);
	else
		return (build_type == TX_BUILD_FINAL);
}

void Transaction::CleanupSubTxs(DbConn *dbconn, uint64_t dest_chain, const Secret& destination, deque<Transaction>& tx_list)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CleanupSubTxs size " << tx_list.size();

	bigint_t balance_allocated = 0UL;
	bigint_t balance_pending = 0UL;
	bool need_intermediate_txs = false;

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
		need_intermediate_txs |= tx->SubTxIsActive(true);

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

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

void Transaction::CleanupSubTx(DbConn *dbconn, const Secret& destination, bool need_intermediate_txs, bigint_t& balance_allocated, bigint_t& balance_pending)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::CleanupSubTx need_intermediate_txs " << need_intermediate_txs << " " << DebugString();

	// for now, if tx was not successfully submitted, release all allocated billets
	//		TODO for future: keep some or all allocated billets and try to resume

	if (build_state < TX_BUILD_SUBMIT_UNKNOWN)
	{
		for (unsigned i = 0; i < nin; ++i)
		{
			Billet& bill = input_bills[i];

			if (!bill.id)
				continue;

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
					CCASSERT(bill.id);

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

						BOOST_LOG_TRIVIAL(info) << "Transaction::CleanupSubTx releasing txid for " << bill.DebugString();

						bill.flags |= BILL_FLAG_NO_TXID;
					}

					rc = dbconn->BilletInsert(bill);
					if (rc)
					{
						BOOST_LOG_TRIVIAL(warning) << "Transaction::CleanupSubTx error saving output " << i << " billet id " << bill.id;

						continue;
					}
				}

				if (type != CC_TYPE_TXPAY || i > 0 || need_intermediate_txs || destination.type == SECRET_TYPE_SPENDABLE_DESTINATION)
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

uint64_t Transaction::GetExpireTime(TxParams& txparams, TxBuildEntry *entry)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::GetExpireTime " << entry->DebugString();

	uint64_t expire_time = 0;

	if (entry->xtx && entry->xtx->expire_time)
	{
		expire_time = entry->xtx->expire_time - txparams.clock_diff;

		if (Xtx::TypeIsXreq(entry->type))
		{
			auto xreq = Xreq::Cast(entry->xtx);
			expire_time -= xreq->hold_time + XREQ_MIN_POSTHOLD_TIME;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::GetExpireTime " << xreq->DebugString();
		}
	}

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::GetExpireTime returning " << expire_time << "; " << entry->DebugString();

	return expire_time;
}

// create a tx with no inputs or outputs

int Transaction::TryCreateBareTx(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, Secret &destination, const uint64_t timeout, bigint_t& tx_round_up, deque<Transaction>& tx_list) // throws RPC_Exception
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::TryCreateBareTx";

	auto tx = tx_list.begin();

	tx->Clear();

	tx->type = entry->type;
	tx->xtx = entry->xtx;
	tx->txbody = entry->txbody;
	tx->ref_id = entry->ref_id;
	tx->blockchain = txparams.blockchain;
	tx->build_type = TX_BUILD_FINAL;
	tx->build_state = TX_BUILD_READY;
	tx->build_mode = (build_mode_t)entry->mode;

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Constructing a " << tx->TypeString() << " transaction...\n" << endl;
	}

	TxPay ts;

	for (unsigned retry = 0; ; ++retry)
	{
		if (retry)
		{
			if (!Xtx::TypeIsXreq(tx->type))
				throw RPC_Exception(RPC_TRANSACTION_FAILED, "Duplicate transaction");

			if (retry > 120/XTX_TIME_DIVISOR)	// each retry adds XTX_TIME_DIVISOR to the expiration time
				throw RPC_Exception(RPC_WALLET_INTERNAL_ERROR, "Wallet internal error attempting to generate unique transaction");
		}

		InitTxPay(ts, txparams);

		if (g_shutdown)
			throw txrpc_shutdown_error;

		tx->FinishCreateTx(ts, txparams, entry, retry);

		// compute and set objid now and make sure it's unique

		string fn;
		char output[128] = {0};
		uint32_t outsize = sizeof(output);
		char wirebuf[8000];

		auto rc = txpay_to_wire(fn, ts, -1, output, outsize, wirebuf, sizeof(wirebuf));
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "Transaction::TryCreateBareTx txpay_to_wire failed: " << output;

			throw txrpc_wallet_error;
		}

		tx->objid = ts.objid;
		tx->have_objid = ts.have_objid;

		CCASSERT(tx->have_objid);

		if (g_shutdown)
			throw txrpc_shutdown_error;

		// Save tx in db before submitting
		// (If tx submitted first, Poll thread could detect and save billets before this thread, and that would have to be sorted out...)

		rc = tx->SaveOutgoingTx(dbconn);
		if (rc < 0) throw txrpc_wallet_db_error;

		if (!rc)
			break;
	}

	tx->build_state = TX_BUILD_SAVED;

	// Submit tx to network

	auto expire_time = GetExpireTime(txparams, entry);

	uint64_t next_commitnum;

	auto rc = tx->TrySubmitTx(txquery, &ts, expire_time, next_commitnum, entry->ref_id);

	ThrowSubmitTxException(rc);	// clean up will take care of SetStatus(TX_STATUS_ERROR)

	//cerr << "objid's tx " << buf2hex(&tx->objid, sizeof(tx->objid)) << " ts " << buf2hex(&ts.objid, sizeof(ts.objid)) << endl; // for testing

	if (memcmp(&tx->objid, &ts.objid, sizeof(tx->objid)))
	{
		BOOST_LOG_TRIVIAL(warning) << "Transaction::TryCreateBareTx objid miscalculation tx " << buf2hex(&tx->objid, sizeof(tx->objid)) << " ts " << buf2hex(&ts.objid, sizeof(ts.objid));
	}

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		if (rc)
			cerr << "A " << tx->TypeString() << " was submitted to the network, but no acknowledgement was received.\n" << endl;
		else
			cerr << tx->TypeString() << " submitted to network.\n" << endl;
	}

	if (Xtx::TypeIsXreq(tx->type))
	{
		rc = ExchangeRequest::UpdatePollTime(dbconn, tx->id, true, Xreq::Cast(entry->xtx)->hold_time + 2*XCX_MATCHING_SECS_PER_EPOCH + 2*g_params.exchange_poll_time);
		(void)rc;
	}

	return 0;
}

int Transaction::SendPreparedTx(DbConn *dbconn, TxQuery& txquery, TxParams& txparams)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::SendPreparedTx " << DebugString();

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Sending previously prepared " << TypeString() << " transaction...\n" << endl;
	}

	build_mode = TX_MODE_BROADCAST;
	build_state = TX_BUILD_SAVED;

	uint64_t next_commitnum;

	auto rc = TrySubmitTx(txquery, NULL, 0, next_commitnum, ref_id);

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		if (rc)
			cerr << "Previously prepared " << TypeString() << " was submitted to the network, but no acknowledgement was received.\n" << endl;
		else
			cerr << "Previously prepared " << TypeString() << " submitted to network.\n" << endl;
	}

	if (Xtx::TypeIsXreq(type))
	{
		// TODO: restore Xreq and use actual xtx->hold_time
		//rc = ExchangeRequest::UpdatePollTime(dbconn, id, true, Xreq::Cast(entry->xtx)->hold_time + 2*XCX_MATCHING_SECS_PER_EPOCH + 2*g_params.exchange_poll_time);
		rc = ExchangeRequest::UpdatePollTime(dbconn, id, true, XREQ_SIMPLE_HOLD_TIME + 2*XCX_MATCHING_SECS_PER_EPOCH + 2*g_params.exchange_poll_time);
		(void)rc;
	}

	return 0;
}

int Transaction::TryCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, Secret &destination, const uint64_t timeout, bigint_t& tx_round_up, deque<Transaction>& tx_list) // throws RPC_Exception
{
	bool test_fail = !RandTest(4) || TEST_FAIL_ALL_TXS;

	bool need_intermediate_txs = false;

	auto rc = StartCreateTxPay(dbconn, txquery, txparams, entry, timeout, tx_round_up, test_fail, tx_list, need_intermediate_txs);
	if (rc) return rc;

	rc = FinishCreateTxPay(dbconn, txquery, txparams, entry, destination, timeout, test_fail, tx_list, need_intermediate_txs);

	return rc;
}

int Transaction::StartCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, const uint64_t timeout, bigint_t& tx_round_up, bool test_fail, deque<Transaction>& tx_list, bool &need_intermediate_txs) // throws RPC_Exception
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

	uint64_t asset = 0;
	unsigned max_delaytime = 0;

	auto carry_in = entry->xtx->amount_carry_in;
	auto carry_out = entry->xtx->amount_carry_out;

	auto total_paid = carry_in;
	auto total_to_pay = entry->amount + carry_out;

	CCASSERT(total_to_pay);  // triggered by a tx with no inputs or outputs

	bigint_t balance;
	Total::GetTotalBalance(dbconn, true, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, asset, 0, max_delaytime, txparams.blockchain, txparams.blockchain);

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::StartCreateTxPay start timeout " << timeout << " tx_round_up " << tx_round_up << " total_to_pay " << total_to_pay << " total_paid " << total_paid << " balance " << balance << " " << entry->DebugString();

	if (total_to_pay > balance + total_paid)
		throw txrpc_insufficient_funds;

	bigint_t round_up_extra = 0UL;

	auto tx = tx_list.begin();

	for (unsigned ntx = 0; total_paid < total_to_pay; ++ntx)
	{
		if (tx_list.size() >= 10000)	// make this a config param?
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::StartCreateTxPay error tx_list.size() " << tx_list.size();

			throw txrpc_wallet_error;	// fail safe
		}

		if (ntx)
		{
			if (entry->type != CC_TYPE_TXPAY && !need_intermediate_txs)
			{
				BOOST_LOG_TRIVIAL(error) << "Transaction::StartCreateTxPay transaction type " << TypeString(entry->type) << " does not fit in one subtx";

				throw txrpc_wallet_error;
			}

			carry_in = 0UL;
			carry_out = 0UL;

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

		tx->SUBTX_AMOUNT = 0UL;

		auto to_pay = total_to_pay - total_paid;
		auto last_round_up_extra = round_up_extra;
		bigint_t next_amount = 0UL;

		if (to_pay > carry_out)
		{
			auto amount_fp = tx_amount_encode(to_pay, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);
			tx_amount_decode(amount_fp, tx->SUBTX_AMOUNT, false, txparams.amount_bits, txparams.exponent_bits);

			amount_fp = tx_amount_encode(to_pay, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax, 1);
			tx_amount_decode(amount_fp, round_up_extra, false, txparams.amount_bits, txparams.exponent_bits);
			round_up_extra = round_up_extra - tx->SubTxAmount();

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::StartCreateTxPay total_to_pay " << total_to_pay << " total_paid " << total_paid << " adding subtx " << ntx << " amount " << tx->SubTxAmount() << " round_up_extra " << round_up_extra;

			if (tx->SubTxAmount() < to_pay)
			{
				next_amount = to_pay - tx->SubTxAmount();

				amount_fp = tx_amount_encode(next_amount, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax);
				tx_amount_decode(amount_fp, next_amount, false, txparams.amount_bits, txparams.exponent_bits);

				if (next_amount <= tx_round_up)
				{
					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::StartCreateTxPay next_amount " << next_amount << " <= tx_round_up " << tx_round_up << " -> amount of this output will be rounded up from " << tx->SubTxAmount();

					auto amount_fp = tx_amount_encode(to_pay, false, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmin, txparams.outvalmax, 1);
					tx_amount_decode(amount_fp, tx->SUBTX_AMOUNT, false, txparams.amount_bits, txparams.exponent_bits);

					round_up_extra = 0UL;
				}
			}
		}

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::StartCreateTxPay added nominal subtx " << ntx << " amount " << tx->SubTxAmount();

		CCASSERTZ(tx->id);	// FUTURE: what to do if tx already used?
		CCASSERTZ(tx->build_type);
		CCASSERTZ(tx->build_state);
		CCASSERTZ(tx->nin);

		// find inputs for subtx, and compute outputs and donation

		if (need_intermediate_txs)
			tx->type = CC_TYPE_TXPAY;
		else
		{
			tx->type = entry->type;
			tx->xtx = entry->xtx;
			tx->txbody = entry->txbody;
		}

		bigint_t subtx_total = 0UL;
		bigint_t subtx_donation = 0UL;

		while (true)	// loop to add multiple inputs
		{
			++tx->nin;
			bigint_t last_req_amount = 0UL;
			auto last_donation = subtx_donation;
			bool needs_more;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::StartCreateTxPay adding input nin " << tx->nin << " subtx_total " << subtx_total << " total_paid " << total_paid;

			while (true)	// loop to find bill for one input
			{
				if (g_shutdown)
					throw txrpc_shutdown_error;

				if (test_fail && RandTest(4*RTEST_TX_ERRORS))
				{
					BOOST_LOG_TRIVIAL(info) << "Transaction::StartCreateTxPay simulating error mid tx, at shutdown check for subtx " << ntx << " nin " << tx->nin;

					throw txrpc_simulated_error;
				}

				CCASSERT(tx->nin);

				tx->nout = 1;
				bigint_t shortfall;

				needs_more = tx->ComputeChange(txparams, carry_in, carry_out, &shortfall);
				if (!needs_more)
					break;

				auto req_amount = tx->input_bills[tx->nin-1].amount + shortfall;

				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::StartCreateTxPay last req_amount " << last_req_amount << " shortfall " << shortfall << " new req_amount " << req_amount << " donation " << tx->donation << " change " << tx->change;

				if (!shortfall)
					throw txrpc_wallet_error;

				if (last_req_amount >= req_amount)
					break;		// already requested and didn't get it, so need another input bill

				if (tx->nin > 1 && RandTest(RTEST_CREATE_LOOP_BREAK))
				{
					BOOST_LOG_TRIVIAL(info) << "Transaction::StartCreateTxPay testing early loop break for subtx " << ntx << " nin " << tx->nin;

					break;
				}

				if (next_amount && last_round_up_extra && req_amount > last_round_up_extra && total_paid + last_round_up_extra >= total_to_pay && !TEST_NO_ROUND_UP)
				{
					// amount required for this subtx is more than simply rounding up the last subtx, so do the latter instead

					tx_round_up = next_amount;

					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::StartCreateTxPay req_amount " << req_amount << " > last_round_up_extra " << last_round_up_extra << "; setting tx_round_up to " << tx_round_up;

					return 1;	// retry
				}

				// release the last bill and find a new one

				if (tx->input_bills[tx->nin-1].id)
					tx->ReleaseBillets(dbconn, tx->nin-1, 1);

				subtx_donation = tx->donation;
				bigint_t min_amount = 1UL;
				if (subtx_donation > last_donation)
					min_amount = subtx_donation - last_donation + min_amount;

				auto total_plus = to_pay + shortfall;
				auto total_minus = carry_out + tx->SubTxAmount();
				//cerr << " to_pay " << to_pay << " shortfall " << shortfall << " total_plus " << total_plus << endl;
				//cerr << " carry_out " << carry_out << " SubTxAmount() " << tx->SubTxAmount() << " total_minus " << total_minus << endl;
				CCASSERT(total_plus > total_minus);
				auto total_required = total_plus - total_minus;

				CCASSERT(tx->nin);

				uint64_t billet_count = 0;

				auto rc = LocateBillet(dbconn, txparams.blockchain, req_amount, min_amount, total_required, tx->input_bills[tx->nin-1], billet_count);
				if (rc)
					--tx->nin;
				if (rc > 0)
					return WaitNewBillet(total_required, billet_count, timeout, test_fail);
				else if (rc < 0)
				{
					// no more bills in wallet
					// compute max amount that could have been sent

					//cerr << "Transaction::StartCreateTxPay \n total_to_pay \t" << total_to_pay << "\n total_paid \t" << total_paid << "\n nin \t" << tx->nin << "\n input[0] \t" << tx->input_bills[0].amount << "\n to_pay \t" << to_pay << "\n shortfall \t" << shortfall << "\n total_plus \t" << total_plus << "\n total_minus \t" << total_minus << "\n total_required \t" << total_required << endl;

					bigint_t max_avail;

					if (tx->nin)
					{
						tx->nout = 1;
						tx->SUBTX_AMOUNT = 0UL;
						auto needs_more = tx->ComputeChange(txparams, carry_in, carry_out);

						if (!needs_more)
							max_avail = total_paid + tx->change;
						else
							max_avail = 0UL; // error computing max
					}
					else if (total_minus > total_required)
						max_avail = total_paid + total_minus - total_required;
					else
						max_avail = total_paid;

					if (max_avail && max_avail < total_to_pay)
					{
						string str;
						amount_to_string(asset, total_paid + tx->change, str);

						throw RPC_Exception(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds; available amount " + str);
					}

					// some bills may need to be split up to match the tx requirements

					ComputeSplitTx(tx_list, txparams, total_required, max_avail);

					if (!max_avail)
						throw txrpc_insufficient_funds;

					need_intermediate_txs = true;

					return 0;
				}

				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::StartCreateTxPay last req_amount " << last_req_amount << " new req_amount " << req_amount << " found " << tx->input_bills[tx->nin-1].amount;

				last_req_amount = req_amount;
			}

			CCASSERT(tx->nin);

			if (tx->input_bills[tx->nin-1].id)
			{
				auto new_amount = tx->input_bills[tx->nin-1].amount;

				subtx_total = subtx_total + new_amount;

				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::StartCreateTxPay new input " << new_amount << " total inputs " << subtx_total << " donation " << tx->donation << " output amount " << tx->SubTxAmount() << " needs_more " << needs_more;
			}
			else
			{
				if (needs_more)
				{
					BOOST_LOG_TRIVIAL(error) << "Transaction::StartCreateTxPay no billet found for input bill " << tx->nin-1;

					throw txrpc_wallet_error;
				}

				--tx->nin;

				CCASSERT(tx->nin);
			}

			if (!needs_more && !need_intermediate_txs)
			{
				tx->build_type = TX_BUILD_FINAL;
				tx->build_state = TX_BUILD_READY;
				tx->build_mode = (build_mode_t)entry->mode;

				break;
			}
			else if ((!needs_more && need_intermediate_txs && tx->nin > 1) || tx->nin >= WALLET_TX_MAXIN || tx->nin >= TX_MAXINPATH)
			{
				// not enough inputs can be added to cover the output of this subtx, so consolidate the inputs into one output

				// Note on the above conditional test: it says that if a tx is completed and its a consolidate and the last subtx
				// has more than one input, then turn the subtx into a consolidate.  Sometimes this isn't necessary tho.
				// TODO: as an optimization, the wallet could only turn the last subtx into a consolidate if the
				// number of existing consolidate subtx's (not including final subtx) + # inputs in final subtx > 4

				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::StartCreateTxPay changing build_type to TX_BUILD_CONSOLIDATE " << tx->DebugString();

				need_intermediate_txs = true;

				tx->build_type = TX_BUILD_CONSOLIDATE;
				tx->build_state = TX_BUILD_READY;
				tx->type = CC_TYPE_TXPAY;
				tx->txbody.clear();
				tx->nout = 0;

				auto rc = tx->ComputeChange(txparams);	// recompute with TX_BUILD_CONSOLIDATE
				if (rc || tx->donation >= subtx_total)
				{
					//cerr << "Transaction::StartCreateTxPay subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << donation << " >= input total " << subtx_total << endl;

					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::StartCreateTxPay ComputeChange subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << tx->donation << " >= input total " << subtx_total;

					// this might be the result of rounding; hack fix: release all but last input billet and continue

					tx->ReleaseBillets(dbconn, 0, tx->nin - 1);

					tx->build_state = TX_BUILD_STATE_NULL;
					tx->SUBTX_AMOUNT = 0UL;
				}

				break;
			}
			else if (!needs_more)
			{
				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::StartCreateTxPay unexpected condition needs_more " << needs_more << " need_intermediate_txs " << need_intermediate_txs << " nin " << tx->nin;

				throw txrpc_wallet_error;
			}
		}

		total_paid = total_paid + tx->SubTxAmount() + carry_out;

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::StartCreateTxPay added subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << tx->donation << " output amount " << tx->SubTxAmount() << " total_paid " << total_paid << " of total_to_pay " << total_to_pay;
		//cerr << "Transaction::StartCreateTxPay added subtx " << ntx << " build_type " << tx->build_type << " nin " << tx->nin << " nout " << tx->nout << " donation " << tx->donation << " output amount " << tx->SubTxAmount() << " total_paid " << total_paid << " of total_to_pay " << total_to_pay << endl;
	}

	// !!! TODO: randomly add cover inputs

	return 0;
}

int Transaction::FinishCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, Secret &destination, const uint64_t timeout, bool test_fail, deque<Transaction>& tx_list, bool need_intermediate_txs) // throws RPC_Exception
{
	// Return value non-zero tells the calling function to retry.

	uint64_t asset = 0;

	unsigned active_subtx_count = 0;
	bigint_t balance_allocated = 0UL;
	bigint_t balance_pending = 0UL;

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FinishCreateTxPay checking need_intermediate_txs " << need_intermediate_txs << " SubTxIsActive " << tx->SubTxIsActive(need_intermediate_txs) << "; " << tx->DebugString();

		if (tx->SubTxIsActive(need_intermediate_txs))
		{
			//if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FinishCreateTxPay computing balances need_intermediate_txs " << need_intermediate_txs << " active " << tx->DebugString();

			++active_subtx_count;

			for (unsigned i = 0; i < tx->nin; ++i)
			{
				Billet& bill = tx->input_bills[i];

				if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FinishCreateTxPay computing balances input " << bill.DebugString();

				if (!bill.id)
				{
					BOOST_LOG_TRIVIAL(error) << "Transaction::FinishCreateTxPay input " << i << " null id";

					throw txrpc_wallet_error;
				}

				CCASSERT(bill.blockchain == entry->dest_chain);
				CCASSERTZ(bill.asset);

				balance_allocated = balance_allocated + bill.amount;
			}

			for (unsigned i = 0; i < tx->nout; ++i)
			{
				if (tx->type != CC_TYPE_TXPAY || i > 0 || need_intermediate_txs || destination.type == SECRET_TYPE_SPENDABLE_DESTINATION)
				{
					Billet& bill = tx->output_bills[i];

					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::FinishCreateTxPay computing balances output " << bill.DebugString();

					balance_pending = balance_pending + bill.amount;
				}
			}
		}
	}

	CCASSERT(active_subtx_count); // triggered by a tx with no inputs or outputs

	//if (active_subtx_count != 1 || need_intermediate_txs) throw txrpc_block_height_range_err; // for testing

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::FinishCreateTxPay balance_allocated " << balance_allocated << " balance_pending " << balance_pending;

	SetPendingBalances(dbconn, entry->dest_chain, balance_allocated, balance_pending);

	Total::AddNoWaitAmounts(balance_pending, true, 0UL, true);

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

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	if (RandTest(RTEST_ALLOW_DOUBLE_SPENDS))
	{
		if (RandTest(4))
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::FinishCreateTxPay RTEST_ALLOW_DOUBLE_SPENDS skipping input spent check" << ", and then waiting for conflicting tx's to clear...";

			ccsleep(10);	// allow conflicting tx to clear
		}
		else
		{
			BOOST_LOG_TRIVIAL(info) << "Transaction::FinishCreateTxPay RTEST_ALLOW_DOUBLE_SPENDS skipping input spent check";
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
					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::FinishCreateTxPay billet already spent; sleeping a few seconds before retrying...";

					ccsleep(3);	// sleep a few seconds to allow input billet to make progress toward becoming spent, so it is not repeatedly reused in attempts to build a transaction
				}
				if (rc)
					return rc;
			}
		}
	}

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

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
		BOOST_LOG_TRIVIAL(info) << "Transaction::FinishCreateTxPay simulating billet spent / amount mismatch retry";

		return 1;
	}

	if ((test_fail && RandTest(RTEST_TX_ERRORS)) || (TEST_FAIL_ALL_TXS && 0))
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::FinishCreateTxPay simulating error pre address computation, at shutdown check";

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

	uint64_t parent_id = 0;
	uint64_t billet_count = 0;

	si = 0;
	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->SubTxIsActive(need_intermediate_txs))
		{
			if (!si && IsInteractive())
			{
				lock_guard<mutex> lock(g_cerr_lock);
				check_cerr_newline();
				if (need_intermediate_txs)
					cerr << "Constructing " << active_subtx_count << " intermediate transaction" << (active_subtx_count > 1 ? "s" : "") << " to split and/or merge billet amounts...\n" << endl;
				else if (active_subtx_count > 1)
					cerr << "Constructing " << active_subtx_count << " transactions...\n" << endl;
				else
					cerr << "Constructing a " << tx->TypeString() << " transaction...\n" << endl;
			}

			TxPay& ts = tx_structs[si++];

			tx->FinishCreateTx(ts, txparams, entry);

			if (si < 2 && g_shutdown)
				throw txrpc_shutdown_error;

			if (si < 2 && !need_intermediate_txs)
				CheckTimeout(timeout);

			if ((test_fail && RandTest(RTEST_TX_ERRORS)) || (TEST_FAIL_ALL_TXS && 0))
			{
				BOOST_LOG_TRIVIAL(info) << "Transaction::FinishCreateTxPay simulating error after create, before save and submit for subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;

				throw txrpc_simulated_error;
			}

			// Save tx in db before submitting
			// (If tx submitted first, Poll thread could detect and save billets before this thread, and that would have to be sorted out...)

			if (active_subtx_count > 1 && !need_intermediate_txs)
				tx->parent_id = (si < 2 ? 1 : parent_id);

			if (si < 2 && !need_intermediate_txs)
				tx->ref_id = entry->ref_id;

			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::FinishCreateTxPay subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs << " parent_id " << tx->parent_id;

			auto rc = tx->SaveOutgoingTx(dbconn);
			if (rc < 0)
				throw txrpc_wallet_db_error;
			if (rc)
			{
				if (si < 2 || need_intermediate_txs)
					return rc;

				BOOST_LOG_TRIVIAL(warning) << "Transaction::FinishCreateTxPay ref id " << entry->ref_id << " transaction only partially sent; SaveOutgoingTx error in subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs << " parent_id " << tx->parent_id;

				return 0;
			}

			tx->build_state = TX_BUILD_SAVED;

			if (si < 2 && !need_intermediate_txs)
				parent_id = tx->id;

			// Submit tx to network

			uint64_t expire_time = 0;

			if (!need_intermediate_txs)
				expire_time = GetExpireTime(txparams, entry);

			billet_count = Billet::GetBilletAvailableCount();

			uint64_t next_commitnum;

			rc = tx->TrySubmitTx(txquery, &ts, expire_time, next_commitnum, entry->ref_id, test_fail, si, active_subtx_count, need_intermediate_txs);

			tx->SetObjId(dbconn);

			if (rc < 0 && si > 1 && !need_intermediate_txs)
			{
				BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx ref id " << entry->ref_id << " transaction only partially sent; SubmitTx error " << rc << " in subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs << " parent_id " << parent_id;

				continue;
			}

			ThrowSubmitTxException(rc);

			if (tx->build_state == TX_BUILD_SUBMIT_UNKNOWN)
				CCASSERTZ(need_intermediate_txs);
			else
				CCASSERT(tx->build_state == TX_BUILD_SUBMIT_OK);

			if (!need_intermediate_txs && TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::FinishCreateTxPay successful submit amount " << tx->SubTxAmount() << " paynum " << ts.outputs[0].addrparams.__paynum << " address " << buf2hex(&ts.outputs[0].M_address, TX_ADDRESS_BYTES);

			//tx_dump_stream(cout, ts);

			if (IsInteractive())
			{
				string amts;
				lock_guard<mutex> lock(g_cerr_lock);
				check_cerr_newline();
				if (!Xtx::TypeIsXtx(tx->type))
					cerr << "Transaction " << si << " of " << active_subtx_count << " submitted to network" << endl;
				if (need_intermediate_txs)
					cerr << (tx->build_type == TX_BUILD_SPLIT ? "Splitting " : "Merging ") << ts.nin << " input billets into " << ts.nout << " output billets" << endl;
				else if (Xtx::TypeIsXtx(tx->type))
					cerr << tx->TypeString() << " submitted to network" << endl;
				else
				{
					amount_to_string(asset, tx->SubTxAmount(), amts);
					cerr << "Sent (private amount = " << amts << ") to newly-generated unique address " << hex << ts.outputs[0].M_address << dec << endl;
				}
				if (ts.amount_carry_in)
				{
					amount_to_string(asset, ts.amount_carry_in, amts, true);
					cerr << "Amount carried over from prior transactions = " << amts << endl;
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
					"   billet commitment " << hex << ts.outputs[i].M_commitment << dec << "\n"
					"   encrypted asset   " << hex << ts.outputs[i].M_asset_enc << dec << "\n"
					"   encrypted amount  " << hex << ts.outputs[i].M_amount_enc << dec << "\n"
					"   address           " << hex << ts.outputs[i].M_address << dec << endl;
				}
				amount_to_string(asset, tx->donation, amts, true);
				cerr << "Donation amount = " << amts << endl;
				if (ts.amount_carry_out)
				{
					amount_to_string(asset, ts.amount_carry_out, amts, true);
					cerr << "Amount reserved for future outputs = " << amts << endl;
				}
				cerr << endl;
			}

			rc = tx->UpdatePolling(dbconn, next_commitnum);
			(void)rc;

			if (Xtx::TypeIsXreq(tx->type))
			{
				rc = ExchangeRequest::UpdatePollTime(dbconn, tx->id, true, Xreq::Cast(entry->xtx)->hold_time + g_params.exchange_poll_time/2);
				(void)rc;
			}
		}
	}

	if (need_intermediate_txs)
	{
		if (IsInteractive())
		{
			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
			cerr << "Waiting up to " << g_params.billet_wait_time << " seconds for pending billets to become available...\n" << endl;
		}

		auto rc = Billet::WaitNewBillet(billet_count, g_params.billet_wait_time);
		(void)rc;

		if (active_subtx_count > 1)
			ccsleep(10);
	}

	return need_intermediate_txs;	// retry if need_intermediate_txs is true
}

/* TrySubmitTx return values:
	0 = submitted -> success
	1 = maybe sent but don't know -> assume success
	RPC_SERVER_ERROR = network returned an error -> submit failed
	RPC_VERIFY_REJECTED = server returned a failure msg -> submit failed
	RPC_TRANSACTION_EXPIRED = server returned an expired msg -> submit failed
	RPC_WALLET_SIMULATED_ERROR = simulated error -> submit failed
*/

int Transaction::TrySubmitTx(TxQuery& txquery, TxPay *ts, uint64_t expire_time, uint64_t &next_commitnum, const string& report_ref_id, int test_fail, unsigned si, unsigned active_subtx_count, bool need_intermediate_txs)
{
	//return RPC_WALLET_SIMULATED_ERROR; // for testing

	// Submit tx to network
	// !!! TODO: add random delay between SubmitTx's for increased privacy?

	CCASSERT(build_state == TX_BUILD_SAVED);

	int rc;

	if (test_fail < 0)
		test_fail = !RandTest(4);

	if (test_fail && RandTest(RTEST_TX_ERRORS + 8*0))
	{
		rc = rand() % 3 - 1;
		txquery.m_possibly_sent = RandTest(2);
		if (txquery.ReadBufDebug(20))
			strcpy(txquery.ReadBufDebug(20), "(simulated error)");

		if (!rc && type == CC_TYPE_TXPAY) rc = 1;						// avoid testing tx that will need to be abandoned
		if (rc && type == CC_TYPE_TXPAY) txquery.m_possibly_sent = 0;	// avoid testing tx that will need to be abandoned

		cerr << "simulating SubmitTx rc " << rc << " WasPossiblySent " << txquery.WasPossiblySent() << endl;

		if (!rc)
			BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx build_mode " << build_mode << " simulating a tx (that will need to be abandoned) that was not submitted but return code was ok for ref id " << report_ref_id << " subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;
		else if (txquery.WasPossiblySent())
			BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx build_mode " << build_mode << " simulating a tx (that will need to be abandoned) that was not submitted but return code was unknown for ref id " << report_ref_id << " subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;
		else if (rc > 0)
			BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx build_mode " << build_mode << " simulating a tx for which submit failed for ref id " << report_ref_id << " subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;
		else
			BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx build_mode " << build_mode << " simulating a tx that was not submitted and return code indicated failure for ref id " << report_ref_id << " subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;
	}
	else
	{
		auto t0 = ccticks();

		if (build_mode == TX_MODE_PREPARE)
			rc = txquery.PrepareTx(*ts, expire_time, wire_data);
		else if (build_mode == TX_MODE_BROADCAST)
			rc = txquery.SubmitPreparedTx(next_commitnum, wire_data);
		else
			rc = txquery.SubmitTx(*ts, expire_time, next_commitnum);

		BOOST_LOG_TRIVIAL(info) << "Transaction::TrySubmitTx build_mode " << build_mode << " type " << type << " elapsed ticks " << ccticks_elapsed(t0, ccticks());
	}

	if (!rc && RandTest(RTEST_TX_ERRORS) && 0)
	{
		// this should never happen in practice, and when simulated, will result in an warning in the log when the tx clears
		// if this tx is to a self destination, manual polling will be required to clear the tx

		BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx build_mode " << build_mode << " simulating SubmitTx that appeared to fail but actually succeeded (and may require manual polling) for ref id " << report_ref_id << " subtx " << si << " of " << active_subtx_count << " need_intermediate_txs " << need_intermediate_txs;

		rc = -1;
	}

	if (build_mode != TX_MODE_BROADCAST)
	{
		objid = ts->objid;
		have_objid = ts->have_objid;
	}

	/* SubmitTx possible return values:

		 0 = submitted
		 1 = submitted with error returned
		 2 = submitted with tx expired error returned
		-1 = error attempting to submit

		For all return values:
			WasPossiblySent() true -> maybe submitted -> assume submitted
	*/

	BOOST_LOG_TRIVIAL(info) << "Transaction::TrySubmitTx build_mode " << build_mode << " result " << rc << " WasPossiblySent " << txquery.WasPossiblySent() << " need_intermediate_txs " << need_intermediate_txs << " ref id " << report_ref_id << " objid " << buf2hex(&objid, CC_OID_TRACE_SIZE);

	//if (rc && (si < 2 || need_intermediate_txs))
	//	return RPC_WALLET_SIMULATED_ERROR;				// for testing--filters out server errors and tx rejected errors

	if (!rc)
	{
		build_state = TX_BUILD_SUBMIT_OK;

		return 0;
	}

	if (txquery.WasPossiblySent() && !need_intermediate_txs && !Xtx::TypeIsXpay(type))
	{
		if (type == CC_TYPE_TXPAY)
			BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx build_mode " << build_mode << " ref id " << report_ref_id << " SubmitTx sent a transaction but did not get a response from the server. The wallet will consider this transaction to have been sent, but if it is never received by the network, it will need to be abandoned.";

		// for now TX_BUILD_SUBMIT_UNKNOWN is handled the same as TX_BUILD_SUBMIT_OK

		build_state = TX_BUILD_SUBMIT_UNKNOWN;

		return 1;
	}

	if (rc == 2)
	{
		auto dbg_msg = txquery.ReadBufDebug();

		if (dbg_msg && dbg_msg[0])
			BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx build_mode " << build_mode << " ref id " << report_ref_id << " SubmitTx returned timeout error: " << dbg_msg;
		else
			BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx build_mode " << build_mode << " ref id " << report_ref_id << " SubmitTx timed out";

		build_state = TX_BUILD_SUBMIT_INVALID;

		return RPC_TRANSACTION_EXPIRED;
	}

	if (rc > 0)
	{
		BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx build_mode " << build_mode << " ref id " << report_ref_id << " SubmitTx returned error: " << txquery.ReadBufDebug(1, (char*)"(null)");

		build_state = TX_BUILD_SUBMIT_INVALID;

		return RPC_VERIFY_REJECTED;
	}

	if (!g_shutdown)
		BOOST_LOG_TRIVIAL(warning) << "Transaction::TrySubmitTx build_mode " << build_mode << " ref id " << report_ref_id << " SubmitTx failed";

	CCASSERT(build_state == TX_BUILD_SAVED);

	return RPC_SERVER_ERROR;
}

void Transaction::ThrowSubmitTxException(int rc) // throws RPC_Exception
{
	if (rc == RPC_VERIFY_REJECTED) throw txrpc_tx_rejected;
	if (rc == RPC_TRANSACTION_EXPIRED) throw txrpc_tx_expired;
	if (rc == RPC_SERVER_ERROR) throw txrpc_server_error;
	if (rc == RPC_WALLET_SIMULATED_ERROR) throw txrpc_simulated_error;

	CCASSERT(rc >= 0);
}

bool FindSplitMax(deque<Transaction>& tx_list, bigint_t &max_split, Transaction* &max_tx)
{
	max_tx = NULL;
	max_split = 0UL;

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->split > max_split)
		{
			max_tx = &*tx;
			max_split = tx->split;
		}
	}

	if (max_tx)
		max_tx->split = 0UL;	// don't find again

	return !!max_split;
}

void Transaction::ComputeSplitTx(deque<Transaction>& tx_list, TxParams& txparams, const bigint_t& total_required, bigint_t& total_change)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ComputeSplitTx total_required " << total_required;

	total_change = 0UL;

	bigint_t amount_max;
	tx_amount_max(amount_max, txparams.amount_bits, txparams.exponent_bits, txparams.outvalmax);

	bigint_t donation1, donation2;

	auto nbytes = txparams.ComputeTxSize(WALLET_TX_MINOUT, 1);
	txparams.ComputeDonation(0, nbytes, WALLET_TX_MINOUT, 1, donation1);
	auto amount_fp = tx_amount_encode(donation1, true, txparams.donation_bits, txparams.exponent_bits);
	tx_amount_decode(amount_fp, donation1, true, txparams.donation_bits, txparams.exponent_bits);

	nbytes = txparams.ComputeTxSize(WALLET_TX_MINOUT, 2);
	txparams.ComputeDonation(0, nbytes, WALLET_TX_MINOUT, 2, donation2);
	amount_fp = tx_amount_encode(donation2, true, txparams.donation_bits, txparams.exponent_bits);
	tx_amount_decode(amount_fp, donation2, true, txparams.donation_bits, txparams.exponent_bits);

	auto donation = donation1 + donation2;

	//cerr << donation1 << endl;
	//cerr << donation2 << endl;
	//cerr << donation << endl;

	// first do any consolidate tx's

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		tx->split = 0UL;

		CCASSERT(tx->build_state <= TX_BUILD_READY);

		if (tx->build_type != TX_BUILD_CONSOLIDATE || tx->build_state != TX_BUILD_READY || tx->change < donation1 || tx->change >= amount_max)
			continue;

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ComputeSplitTx total_change " << total_change << " consolidate change " << tx->change << " " << tx->DebugString();

		auto rc = tx->FinishSplitTx(txparams, TX_BUILD_CONSOLIDATE, 0UL);
		if (rc)
			continue;

		total_change = total_change + donation2;
	}

	// next do pending transactions not involving a max amount bill

	for (auto tx = tx_list.begin(); tx != tx_list.end(); ++tx)
	{
		if (tx->build_type != TX_BUILD_FINAL || tx->build_state != TX_BUILD_READY || !tx->nin)
			continue;

		if (tx->input_bills[0].amount == amount_max || tx->change < donation)
			continue;

		tx->split = tx->change;

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ComputeSplitTx split " << tx->split << " first input amount " << tx->input_bills[0].amount << " " << tx->DebugString();
	}

	Transaction *tx;
	bigint_t max_split;

	while (total_change < total_required)
	{
		if (!FindSplitMax(tx_list, max_split, tx))
			break;

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::ComputeSplitTx total_change " << total_change << " new change " << max_split << " total_required " << total_required;

		if (tx->input_bills[0].amount > amount_max)
			tx->FinishSplitTx(txparams, TX_BUILD_SPLIT, 0UL);
		else if (tx->output_bills[0].amount + tx->change >= amount_max)
			tx->FinishSplitTx(txparams, TX_BUILD_CONSOLIDATE, 0UL);		// avoid subtx's with 3 outputs
		else
			tx->FinishSplitTx(txparams, TX_BUILD_CONSOLIDATE, donation2);

		if (max_split > donation1 && tx->build_state == TX_BUILD_READY)
			total_change = total_change + max_split - donation1;	// donation1 is min cost to use the change
	}

	// then if necessary split transactions containing max amount bills

	for (auto tx = tx_list.begin(); tx != tx_list.end() && total_change < total_required; ++tx)
	{
		if (tx->build_type != TX_BUILD_FINAL || tx->build_state != TX_BUILD_READY || !tx->nin)
			continue;

		if (tx->input_bills[0].amount != amount_max || tx->nin < 2)
			continue;

		tx->split = tx->input_bills[1].amount;

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::ComputeSplitTx split " << tx->split << " first input amount " << tx->input_bills[0].amount << " " << tx->DebugString();
	}

	bool allow_maxmin = !total_change;

	while (total_change < total_required)
	{
		if (!FindSplitMax(tx_list, max_split, tx))
			break;

		if (max_split < donation)									// donation is min cost to split and use the input
			continue;

		if (max_split == amount_max && !allow_maxmin)				// split amount_max bills only if no other splits have been found
			continue;

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::ComputeSplitTx total_change " << total_change << " found max input split " << max_split << " total_required " << total_required;

		// split the second input into one billet with amount donation2, and another billet for the change

		tx_list.emplace_back();
		auto new_tx = tx_list.end() - 1;

		new_tx->nin = 1;
		new_tx->input_bills[0].Copy(tx->input_bills[1]);
		tx->input_bills[1].Clear();

		new_tx->FinishSplitTx(txparams, TX_BUILD_SPLIT, donation2);

		if (new_tx->build_state == TX_BUILD_READY)
			total_change = total_change + max_split - donation;

		if (max_split != amount_max)
			allow_maxmin = !total_change;
	}
}

int Transaction::FinishSplitTx(TxParams& txparams, build_type_t _build_type, const bigint_t& output)
{
	build_type = _build_type;
	build_state = TX_BUILD_READY;
	type = CC_TYPE_TXPAY;

	txbody.clear();

	if (!output)
		nout = 0;
	else
	{
		nout = 1;
		output_bills[0].amount = output;
	}

	auto rc = ComputeChange(txparams);
	if (rc)
	{
		nout = 0;

		auto rc2 = ComputeChange(txparams);
		if (rc2)
			build_state = TX_BUILD_STATE_NULL;
	}

	return rc;
}

int Transaction::CreateConflictTx(DbConn *dbconn, TxQuery& txquery, const Billet& input) // throws RPC_Exception
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CreateConflictTx " << input.DebugString();

	Clear();

	build_type = TX_BUILD_CANCEL_TX;
	type = CC_TYPE_TXPAY;

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
		if (rc) throw txrpc_server_error;
		if (txparams.NotConnected()) throw txrpc_not_synced_error;
	}

	rc = ComputeChange(txparams);
	if (rc) throw txrpc_wallet_error;

	rc = FillOutTx(dbconn, txquery, txparams, 0, ts);
	if (rc) throw txrpc_wallet_error;

	if (g_shutdown)
		throw txrpc_shutdown_error;

	SetAddresses(dbconn, txparams.blockchain, null_destination, ts);		// do this last to minimize chance of unused addresses

	FinishCreateTx(ts, txparams);

	if (g_shutdown)
		throw txrpc_shutdown_error;

	// Save tx in db before submitting
	// (If tx submitted first, Poll thread could detect and save billets before this thread, and that would have to be sorted out...)

	rc = SaveOutgoingTx(dbconn);
	if (rc < 0)
		throw txrpc_wallet_db_error;
	if (rc)
		return rc;

	build_state = TX_BUILD_SAVED;

	// Submit tx to network

	uint64_t next_commitnum;

	rc = TrySubmitTx(txquery, &ts, 0, next_commitnum, "cancel_tx");

	if (rc < 0)
	{
		SetStatus(dbconn, TX_STATUS_ERROR, BILL_STATUS_ERROR);

		ThrowSubmitTxException(rc);
	}

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		if (rc)
			cerr << "A cancel transaction was submitted to the network, but no acknowledgement was received.\n" << endl;
		else
			cerr << "Cancel transaction submitted.\n" << endl;
	}

	rc = UpdatePolling(dbconn, next_commitnum);
	(void)rc;

	return 0;
}

int Transaction::CreateTxFromAddressQueryResult(DbConn *dbconn, TxQuery& txquery, const Secret& destination, const Secret& address, QueryAddressResult &result, bool duplicate_txid)
{
	// must be called from inside a BeginWrite

	CCASSERT(destination.id == address.dest_id);

	if ((result.is_special_domain || g_params.billet_domain) && result.domain != (unsigned)g_params.billet_domain)
	{
		BOOST_LOG_TRIVIAL(info) << "Transaction::CreateTxFromAddressQueryResult wallet ignoring output to domain " << result.domain;

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

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::CreateTxFromAddressQueryResult amount " << amount << " destination id " << address.dest_id << " paynum " << address.number << " address " << buf2hex(&address.value, TX_ADDRESS_BYTES) << " duplicate_txid " << duplicate_txid;

	// TODO?: add feature to ignore incoming amounts that are very small, for better performance and to avoid DoS attack

	if (!result.amount_fp)
		return 0;				// ignore zero amounts, for better performance and to avoid DoS attack

	btc_block = g_btc_block.GetCurrentBlock();
	type = CC_TYPE_TXPAY;
	status = TX_STATUS_CLEARED;
	nout = 1;

	auto rc = dbconn->TransactionInsert(*this);
	if (rc) return rc;

	Billet& bill = output_bills[0];

	bill.create_tx = id;
	bill.flags = Billet::FlagsFromDestinationType(destination.type);
	bill.dest_id = address.dest_id;
	bill.blockchain = result.blockchain;
	bill.address = address.value;
	bill.domain = result.domain;
	bill.asset = result.asset;
	bill.amount_fp = result.amount_fp;
	bill.amount = amount;
	bill.delaytime = 0;							// FUTURE: TBD
	bill.commit_iv = result.commit_iv;
	bill.commitment = result.commitment;

	if (duplicate_txid)
		bill.flags |= BILL_FLAG_NO_TXID;

	return bill.SetStatusCleared(dbconn, result.commitnum);
}

int Transaction::UpdateStatus(DbConn *dbconn, uint64_t bill_id, uint64_t commitnum)
{
	// must be called from inside a BeginWrite

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::UpdateStatus bill id " << bill_id << " commitnum " << commitnum << " tx " << DebugString();

	if (!TxCouldClear())
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

				secret.next_poll = 1;	// check now

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

	if (!TxCouldClear())
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

		while (tx_id <= INT64_MAX)
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
					if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetConflicted tx_id " << tx.id << " input still pending in tx_id " << tx_id << " " << bill.DebugString();

					release_input = false;
					break;
				}
			}

			++tx_id;
		}

		if (release_input)
		{
			if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetConflicted tx_id " << tx.id << " releasing input " << bill.DebugString();

			CCASSERT(bill.blockchain == tx.blockchain);

			balance_allocated = balance_allocated + bill.amount;

			bill.status = BILL_STATUS_CLEARED;

			rc = dbconn->BilletInsert(bill);
			if (rc) return rc;
		}
	}

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		Billet& bill = tx.output_bills[i];

		if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::SetConflicted tx_id " << tx.id << " output " << bill.DebugString();

		if (bill.status != BILL_STATUS_PENDING)
			continue;

		if ((bill.flags & BILL_RECV_MASK) && (bill.flags & BILL_FLAG_TRUSTED))
		{
			CCASSERT(bill.blockchain == tx.blockchain);
			CCASSERTZ(bill.asset);

			balance_pending = balance_pending + bill.amount;
		}

		if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(debug) << "Transaction::SetConflicted setting status abandoned for tx_id " << tx.id << " " << bill.DebugString();

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

	rc = Total::AddBalances(dbconn, false, TOTAL_TYPE_ALLOCATED_BIT, 0, 0, asset, delaytime, tx.blockchain, false, balance_allocated);
	if (rc) return rc;

	rc = Total::AddBalances(dbconn, false, TOTAL_TYPE_PENDING_BIT, 0, 0, asset, delaytime, tx.blockchain, false, balance_pending);
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

void Transaction::AbandonTx(DbConn *dbconn, uint64_t tx_id, uint64_t dest_chain)
{
	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(trace) << "Transaction::AbandonTx tx_id " << tx_id << " dest_chain " << dest_chain;

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		throw txrpc_wallet_db_error;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	Transaction tx;

	rc = tx.ReadTx(dbconn, tx_id);
	if (rc) throw txrpc_wallet_db_error;

	if (tx.status != TX_STATUS_PENDING)
		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment");

	/*
	For reference, this is the input billet handling elsewhere in the wallet:
		LocateBillet takes input bills with status BILL_STATUS_CLEARED and sets them to BILL_STATUS_ALLOCATED
		ReleaseTxBillet takes input bills with status BILL_STATUS_ALLOCATED and sets them to BILL_STATUS_CLEARED

		CleanupSubTx:
			takes input bills with status BILL_STATUS_ALLOCATED and sets them to BILL_STATUS_CLEARED

		when input bill clears:
			if (status == BILL_STATUS_ALLOCATED)
				Total::AddBalances(dbconn, true, TOTAL_TYPE_ALLOCATED_BIT, 0, 0, asset, delaytime, blockchain, false, amount);

	For reference, this is the output billet handling elsewhere in the wallet:

		SaveOutgoingTx sets output_bills[i].status = BILL_STATUS_PENDING;
			if it's trusted then ???

		CleanupSubTx:
			takes output bills with status BILL_STATUS_PENDING and sets them to BILL_STATUS_ERROR
				if (tx->type != CC_TYPE_TXPAY || i > 0 || need_intermediate_txs || destination.type == SECRET_TYPE_SPENDABLE_DESTINATION)
					balance_pending = balance_pending + bill.amount;

		when output billet clears:
			if (old_status == BILL_STATUS_PENDING && (flags & BILL_RECV_MASK) && (flags & BILL_FLAG_TRUSTED))
				Total::AddBalances(dbconn, true, TOTAL_TYPE_PENDING_BIT, 0, 0, asset, delaytime, blockchain, false, amount);
	*/

	bigint_t balance_allocated = 0UL;
	bigint_t balance_pending = 0UL;

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		Billet& bill = tx.input_bills[i];

		//cout << "input " << i << " " << bill.DebugString();

		if (bill.status != BILL_STATUS_ALLOCATED)
			throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment");

		if (!dest_chain)
			dest_chain = bill.blockchain;

		CCASSERT(bill.blockchain == dest_chain);

		balance_allocated = balance_allocated + bill.amount;

		bill.status = BILL_STATUS_CLEARED;

		rc = dbconn->BilletInsert(bill);
		if (rc) throw txrpc_wallet_db_error;
	}

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		Billet& bill = tx.output_bills[i];

		//cout << "output " << i << " " << bill.DebugString();

		if (bill.status != BILL_STATUS_PENDING)
			throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment");

		if ((bill.flags & BILL_RECV_MASK) && (bill.flags & BILL_FLAG_TRUSTED))
		{
			if (!dest_chain)
				dest_chain = bill.blockchain;

			CCASSERT(bill.blockchain == dest_chain);

			balance_pending = balance_pending + bill.amount;
		}

		if (TRACE_BILLETS) BOOST_LOG_TRIVIAL(debug) << "Transaction::AbandonTx setting status abandoned for tx_id " << tx.id << " " << bill.DebugString();

		bill.status = BILL_STATUS_ABANDONED;

		rc = dbconn->BilletInsert(bill);
		if (rc) throw txrpc_wallet_db_error;
	}

	tx.status = TX_STATUS_ABANDONED;

	rc = dbconn->TransactionInsert(tx);
	if (rc) throw txrpc_wallet_db_error;

	if (TRACE_TRANSACTIONS) BOOST_LOG_TRIVIAL(debug) << "Transaction::AbandonTx tx_id " << tx_id << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending;

	uint64_t asset = 0;
	unsigned delaytime = 0;

	// TBD: this will get more complicated if billets have various delaytimes or blockchains

	Total::AddBalances(dbconn, true, TOTAL_TYPE_ALLOCATED_BIT, 0, 0, asset, delaytime, dest_chain, false, balance_allocated);
	Total::AddBalances(dbconn, true, TOTAL_TYPE_PENDING_BIT, 0, 0, asset, delaytime, dest_chain, false, balance_pending);

	Total::AddNoWaitAmounts(balance_pending, false, 0UL, true);

	#if TEST_LOG_BALANCE
	amtint_t balance;
	Total::GetTotalBalance(dbconn, false, balance, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false, 0, 0, 0, -1, 0, -1, false);
	BOOST_LOG_TRIVIAL(info) << "btc_abandontransaction new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending;
	//cerr << "btc_abandontransaction new balance " << balance << " balance_allocated " << balance_allocated << " balance_pending " << balance_pending << endl;
	#endif

	rc = dbconn->Commit();
	if (rc) throw txrpc_wallet_db_error;

	dbconn->DoDbFinishTx();

	finally.Clear();

	if (balance_allocated)
		Billet::NotifyNewBillet(true);
}
