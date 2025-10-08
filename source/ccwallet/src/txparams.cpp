/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * txparams.cpp
*/

#include "ccwallet.h"
#include "txparams.hpp"
#include "txquery.hpp"

#include <CCobjects.hpp>
#include <transaction.h>
#include <xtransaction.hpp>

#include <ccserver/connection.hpp>

#define TRACE_TXPARAMS	(g_params.trace_txparams)

TxParamQuery g_txparams;

using namespace snarkfront;

bool TxParams::NotConnected() const
{
	if (!connected) BOOST_LOG_TRIVIAL(warning) << "Transaction server is not synced to the blockchain";

	return !connected;
}

//#include <transaction.hpp> // for testing

unsigned TxParams::ComputeTxSize(unsigned nout, unsigned nin) const
{
	#if 0

	TxPay tx;
	string fn;
	char output[128] = {0};
	uint32_t outsize = sizeof(output);
	char wirebuf[8000];
	int rc;

	tx_init(tx);
	tx.tx_type = tx.tag_type = CC_TYPE_TXPAY;
	tx.nout = tx.nin = tx.nin_with_path = 1;
	tx.inputs[0].pathnum = tx.inputs[1].pathnum = 1;

	rc = txpay_to_wire(fn, tx, 0, output, outsize, wirebuf, sizeof(wirebuf));
	cerr << "txpay_to_wire nout " << tx.nout << " nin " << tx.nin << " size " << *(uint32_t*)wirebuf - TX_POW_SIZE << " rc " << rc << " output " << output << endl;

	tx.nout = 2;
	rc = txpay_to_wire(fn, tx, 0, output, outsize, wirebuf, sizeof(wirebuf));
	cerr << "txpay_to_wire nout " << tx.nout << " nin " << tx.nin << " size " << *(uint32_t*)wirebuf - TX_POW_SIZE << " rc " << rc << " output " << output << endl;

	tx.nin = tx.nin_with_path = 2;
	rc = txpay_to_wire(fn, tx, 0, output, outsize, wirebuf, sizeof(wirebuf));
	cerr << "txpay_to_wire nout " << tx.nout << " nin " << tx.nin << " size " << *(uint32_t*)wirebuf - TX_POW_SIZE << " rc " << rc << " output " << output << endl;

	//txpay_to_wire nout 1 nin 1 size 409 rc 0 output --> base = 409-57-48 = 304
	//txpay_to_wire nout 2 nin 1 size 466 rc 0 output --> nout = 466-409 = 57
	//txpay_to_wire nout 2 nin 2 size 514 rc 0 output -->  nin = 514-466 = 48

	#endif

	unsigned pred_size = 304 + nout*57 + nin*48;

	return pred_size;
}

void TxParams::ComputeDonation(unsigned type, unsigned nbytes, unsigned nout, unsigned nin, bigint_t& donation) const
{
	#define TRACE_COMPUTE_DONATION	0

	if (TRACE_COMPUTE_DONATION) cerr << "ComputeDonation type " << type << " nbytes " << nbytes << " nin " << nin << " nout " << nout << endl;

	bigint_t bigval;

	if (Xtx::TypeHasBareMsg(type))
	{
		donation = 0UL;
		return;
	}

	if (Xtx::TypeIsCrosschain(type))
	{
		tx_amount_decode(donation_per_xcx_req, donation, true, donation_bits, exponent_bits);
		if (TRACE_COMPUTE_DONATION) cerr << "donation_per_xcx_req " << donation << endl;
	}
	else
	{
		tx_amount_decode(donation_per_tx, donation, true, donation_bits, exponent_bits);
		if (TRACE_COMPUTE_DONATION) cerr << "donation_per_tx " << donation << endl;
	}

	tx_amount_decode(donation_per_byte, bigval, true, donation_bits, exponent_bits);
	donation = donation + (bigint_t)(nbytes) * bigval;
	if (TRACE_COMPUTE_DONATION) cerr << "donation_per_byte " << bigval << endl;

	tx_amount_decode(donation_per_output, bigval, true, donation_bits, exponent_bits);
	donation = donation + (bigint_t)(nout) * bigval;
	if (TRACE_COMPUTE_DONATION) cerr << "donation_per_output " << bigval << endl;

	tx_amount_decode(donation_per_input, bigval, true, donation_bits, exponent_bits);
	donation = donation + (bigint_t)(nin) * bigval;
	if (TRACE_COMPUTE_DONATION) cerr << "donation_per_input " << bigval << endl;

	tx_amount_decode(minimum_donation, bigval, true, donation_bits, exponent_bits);
	if (donation < bigval) donation = bigval;
	if (TRACE_COMPUTE_DONATION) cerr << "minimum_donation " << bigval << endl;

	if (TRACE_COMPUTE_DONATION) cerr << "donation " << donation << endl;

	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(trace) << "TxParamQuery::ComputeDonation txparams " << (uintptr_t)this << " type " << type << " nbytes " << nbytes << " nin " << nin << " nout " << nout << " donation " << donation;
}

void TxParamQuery::Reset()
{
	m_params.Clear();
}

// sets param struct from global cached values
// if cached values never set, fetches params from tx server
// TODO: functions that call GetParams might want to call UpdateParams and retry after an error
int TxParamQuery::GetParams(TxParams& txparams, TxQuery& txquery)
{
	unsigned update_counter;

	{
		lock_guard<mutex> lock(m_update_mutex);

		update_counter = m_params.update_counter;

		if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(trace) << "TxParamQuery::GetParams update_counter " << update_counter;

		if (update_counter)
		{
			memcpy(&txparams, &m_params, sizeof(txparams));

			return 0;
		}
	}

	return FetchParams(txparams, txquery);
}

// loads the local param struct with a newer version of the tx params than it currently contains
// if necessary, fetches params from tx server
int TxParamQuery::UpdateParams(TxParams& txparams, TxQuery& txquery)
{
	unsigned update_counter;

	{
		lock_guard<mutex> lock(m_update_mutex);

		update_counter = m_params.update_counter;

		int diff = update_counter - txparams.update_counter;

		if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(trace) << "TxParamQuery::UpdateParams update_counter " << update_counter << " diff " << diff;

		if (update_counter && diff)
		{
			memcpy(&txparams, &m_params, sizeof(txparams));

			return 0;
		}
	}

	return FetchParams(txparams, txquery);
}

// fetches params from tx server
int TxParamQuery::FetchParams(TxParams& txparams, TxQuery& txquery, bool force)
{
	unique_lock<mutex> lock(m_fetch_mutex, defer_lock);

	if (!force)
		lock.lock();	// if not forced, only one thread at a time will query for updated params

	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(trace) << "TxParamQuery::FetchParams force " << force << " update_counter " << txparams.update_counter << " global update_counter " << m_params.update_counter;

	if (!force && txparams.update_counter != m_params.update_counter)
	{
		// another thread has already fetched the params, so just use that

		lock_guard<mutex> lock2(m_update_mutex);	// prevent any other thread from reading while we're updating

		memcpy(&txparams, &m_params, sizeof(txparams));

		return 0;
	}

	vector<char> querybuf(64);

	auto rc = txquery.QueryParams(txparams, querybuf);
	if (rc) return -1;

	// update global cached params in m_params

	lock_guard<mutex> lock2(m_update_mutex);	// prevent any other thread from reading while we're updating

	auto update_counter = m_params.update_counter + 1;
	if (!update_counter)
		++update_counter;

	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(trace) << "TxParamQuery::FetchParams setting update_counter = " << update_counter;

	txparams.update_counter = update_counter;

	memcpy(&m_params, &txparams, sizeof(m_params));

	return 0;
}
