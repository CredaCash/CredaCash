/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * txquery.cpp
*/

#include "ccwallet.h"
#include "billets.hpp"
#include "txparams.hpp"
#include "txquery.hpp"

#include <CCobjdefs.h>
#include <jsonutil.h>
#include <txquery.h>
#include <transaction.h>
#include <ccserver/connection_manager.hpp>

#define TRACE_TXQUERY	(g_params.trace_txquery)
#define TRACE_TXPARAMS	(g_params.trace_txparams)

#define TXCONN_READ_MAX		20000	//@@!
#define TXCONN_WRITE_MAX	8000	//@@!

//!#define RTEST_TX_SUBMIT_ERRORS	1
//!#define RTEST_CUZZ			32

#ifndef RTEST_TX_SUBMIT_ERRORS
#define RTEST_TX_SUBMIT_ERRORS	0	// don't test
#endif

#ifndef RTEST_CUZZ
#define RTEST_CUZZ				0	// don't test
#endif

// unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister
CCServer::ConnectionFactoryInstantiation<TxQuery> TxQuery::txconnfac(TXCONN_READ_MAX, TXCONN_WRITE_MAX, -1, -1, 0, 1, 0);	//@@!
CCServer::ConnectionManagerBase TxQuery::nullconnmgr("TxQuery");

static vector<string> hosts;

using namespace snarkfront;

int TxQuery::ReadHostsFile(const wstring &path)
{
	BOOST_LOG_TRIVIAL(trace) << "TxQuery::ReadHostsFile file \"" << w2s(path) << "\"";

	CCASSERT(path.length());

	boost::filesystem::ifstream fs;
	fs.open(path, fstream::in);
	if(!fs.is_open())
	{
		cerr << "ERROR opening transaction server hosts file \"" << w2s(path) << "\"" << endl;
		return -1;
	}

	while (true)
	{
		string line;

		fs >> line;

		if (fs.fail() && !fs.eof())
		{
			cerr << "ERROR reading transaction server hosts file \"" << w2s(path) << "\"" << endl;
			return -1;
		}

		boost::trim(line);

		if (line.length() > 0)
		{
			BOOST_LOG_TRIVIAL(trace) << "TxQuery::ReadHostsFile read hostname \"" << line << "\"";

			hosts.push_back(line);
		}

		if (fs.eof())
			break;
	}

	if (hosts.size() < 1)
	{
		cerr << "ERROR no hostnames found in file file \"" << w2s(path) << "\"" << endl;
		return -1;
	}

	BOOST_LOG_TRIVIAL(debug) << "TxQuery::ReadHostsFile loaded " << hosts.size() << " transaction server hostnames";

	return 0;
}

void TxQuery::ClearHost()
{
	m_host_index = -1;
}

const string& TxQuery::GetHost()
{
	unsigned i = m_host_index;

	if (i >= hosts.size())
	{
		i = m_host_index = rand() % hosts.size();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery using server " << hosts[i];
	}

	return hosts[i];
}

int TxQuery::TryQuery(PowType powtype, bool is_retry, vector<char> *pquery)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery powtype " << powtype << " is_retry " << is_retry << " conn state " << m_conn_state;

	// get or update params if needed

	TxParams txparams;
	int rc = 0;

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	if (powtype && is_retry)
		rc = g_txparams.UpdateParams(txparams, *this);
	else if (powtype)
		rc = g_txparams.GetParams(txparams, *this);
	else
		txparams.clock_diff = 0;

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	if (rc)
	{
		ClearHost();
		return -1;
	}

	// send query

	if (!pquery)
		pquery = &m_writebuf;

	uint64_t timestamp = time(NULL) + txparams.clock_diff;

	rc = tx_reset_work(string(), timestamp, (char*)pquery->data(), pquery->size());
	CCASSERTZ(rc);

	if (powtype)
	{
		// TODO: implement a timeout
		auto rc = tx_set_work(string(), 0, TX_POW_NPROOFS, -1, (powtype == PowType_Tx ? txparams.tx_work_difficulty : txparams.query_work_difficulty), (char*)pquery->data(), pquery->size());
		CCASSERTZ(rc);
	}

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	WaitForStopped(g_interactive);

	if (g_shutdown) return -1;

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	m_result_code = -1;
	m_data_written = false;

	m_pquery = pquery;

	auto read_count_start = m_read_count;

	if (m_conn_state == CONN_STOPPED)
	{
		InitNewConnection();

		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery posting query m_stopping " << m_stopping.load();

		static const string null;

		if (g_params.transact_tor)
			rc = Post("TxQuery::TryQuery", boost::bind(&Connection::HandleConnectOutgoingTor, this, g_params.torproxy_port, ref(GetHost()), (g_params.transact_tor_single_query ? ref(null) : ref(GetHost())), AutoCount(this)));
		else
			rc = Post("TxQuery::TryQuery", boost::bind(&Connection::HandleConnectOutgoing, this, ref(g_params.transact_host), g_params.transact_port, AutoCount(this)));

		if (rc)
		{
			if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery post failed m_stopping " << m_stopping.load();

			Stop();
			ClearHost();
			return -1;
		}

		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery query posted m_stopping " << m_stopping.load();
	}
	else
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery submitting to open connection not yet supported";

		Stop();
		ClearHost();
		return -1;	// FUTURE: need a way to submit query to already open connection
	}

	WaitForReadComplete(read_count_start, g_interactive);

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery result " << m_result_code << " read_count_start " << read_count_start << " m_read_count " <<m_read_count << " g_shutdown " << g_shutdown;

	Stop();	// !!! for now

	if (m_data_written && powtype)
		m_possibly_sent = true;

	if (g_shutdown) return -1;

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	if (m_result_code)
			ClearHost();

	return m_result_code;
}

int TxQuery::SubmitQuery(PowType powtype, bool is_retry, Json::Value *root, vector<char> *pquery)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery pquery " << hex << (uintptr_t)pquery << dec << " size " << (pquery ? pquery->size() : m_writebuf.size());

	int result_code = -1;

	if (root)
		root->clear();

	while (true)	// break on error
	{
		auto rc = TryQuery(powtype, is_retry, pquery);

		if (rc) break;

		m_pread[m_nred] = 0;

		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery reply " << m_nred << " bytes";
		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery reply " << m_pread;

		if (m_nred < 1)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery empty response";

			break;
		}

		if (m_pread[0] != '{')
		{
			result_code = 1;

			break;
		}

		if (root)
		{
			if (m_nred < 2 || !(m_pread[m_nred-1] == '}' || (m_pread[m_nred-1] == '\0' && m_pread[m_nred-2] == '}')))
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery incomplete response";

				break;
			}

			Json::CharReaderBuilder builder;
			Json::CharReaderBuilder::strictMode(&builder.settings_);

			auto reader = builder.newCharReader();

			bool rc;

			try
			{
				rc = reader->parse(m_pread, m_pread + m_nred, root, NULL);
			}
			catch (...)
			{
				rc = false;
			}

			delete reader;

			if (!rc)
			{
				BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery json parse error " << m_pread;

				root->clear();

				break;
			}
		}

		result_code = 0;
		break;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery result " << result_code;

	return result_code;
}

int TxQuery::SubmitTx(const TxPay& tx, uint64_t& next_commitnum)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitTx";

	int result_code = -1;
	next_commitnum = 0;
	m_possibly_sent = false;

	string fn;
	char output[128];
	uint32_t outsize = sizeof(output);

	//tx_dump_stream(cerr, tx);

	auto rc = txpay_to_wire(fn, tx, -1, output, outsize, m_writebuf.data(), m_writebuf.size());
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitTx txpay_to_wire failed: " << output;

		return -1;
	}

	for (int i = 0; i <= g_params.tx_submit_retries; ++i)
	{
		if (g_shutdown) return -1;

		if (g_params.transact_tor_single_query)
			ClearHost();

		auto rc = SubmitQuery(PowType_Tx, i);

		if (!i && RandTest(RTEST_TX_SUBMIT_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitTx double submitting tx (simulating submit communication loss)";

			ccsleep(20);

			continue;
		}

		if (rc > 0)
		{
			if (!strncmp(m_pread, "OK:", 3))
			{
				string key;
				bigint_t bigval;
				rc = parse_int_value(fn, key, &m_pread[3], 64, 0UL, bigval, output, outsize);
				if (rc)
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitTx error parsing commitment number " << m_pread;
				else
					next_commitnum = BIG64(bigval);

				result_code = 0;

				break;
			}

			if (!strncmp(m_pread, "INVALID:already spent", 21))
			{
				result_code = 0;

				break;
			}

			if (!strncmp(m_pread, "ERROR:server not connected", 26))
			{
				m_possibly_sent = false;
				result_code = -1;

				break;
			}

			if (!strncmp(m_pread, "INVALID:", 8) || !strncmp(m_pread, "ERROR:", 6))
			{
				result_code = 1;

				break;
			}

			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitTx unrecognized response " << m_pread;
		}
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitTx result " << result_code << " next_commitnum " << next_commitnum;

	return result_code;
}

int TxQuery::QueryParams(TxParams& txparams, vector<char> &querybuf)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryParams";

	int result_code = -1;

	auto rc = tx_query_parameters_create(string(), (char*)querybuf.data(), querybuf.size());
	CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		if (g_shutdown) return -1;

		Json::Value root;

		auto rc = SubmitQuery(PowType_None, i, &root, &querybuf);
		if (rc) continue;

		if (root.size() != 1)
		{
			BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryParams error root size " << root.size();
			continue;
		}

		auto key = root.begin().name();

		if (key != "tx-parameters-query-results")
			goto unexpected_key;

		root = *root.begin();

		result_code = ParseParams(root, txparams);

		if (result_code) break;

		result_code = ParseInputParams(root, txparams);

		if (result_code) break;

		if (root.empty())
			break;

	unexpected_key:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryParams error unexpected key " << key;

		continue;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryParams result " << result_code;

	return result_code;
}

int TxQuery::ParseParams(Json::Value& root, TxParams& txparams)
{
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams txparams " << (uintptr_t)this;

	string key;
	Json::Value value;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	key = "timestamp";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.clock_diff = BIG64(bigval) - time(NULL); // TODO: compensate for estimate of transmission time?
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams clock_diff " << txparams.clock_diff;

	key = "server-version";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.server_version = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams server_version " << txparams.server_version;

	key = "protocol-version";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.protocol_version = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams protocol_version " << txparams.protocol_version;

	key = "effective-level";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.effective_level = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams effective_level " << txparams.effective_level;

	key = "query-work-difficulty";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.query_work_difficulty = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams query_work_difficulty " << txparams.query_work_difficulty;

	key = "tx-work-difficulty";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.tx_work_difficulty = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams tx_work_difficulty " << txparams.tx_work_difficulty;

	key = "blockchain-number";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.blockchain = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams blockchain " << txparams.blockchain;

	if (!txparams.blockchain)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams invalid blockchain " << txparams.blockchain;

		return -1;
	}

	key = "blockchain-highest-indelible-level";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_BLOCKLEVEL_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.block_level = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams block_level " << txparams.block_level;

	key = "merkle-tree-oldest-commitment-number";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.oldest_commitnum = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams oldest_commitnum " << txparams.oldest_commitnum;

	key = "merkle-tree-next-commitment-number";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.next_commitnum = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams next_commitnum " << txparams.next_commitnum;

	key = "connected-to-network";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.connected = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams connected " << txparams.connected;

	key = "asset-bits";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 8, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.asset_bits = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams asset_bits " << txparams.asset_bits;

	if (txparams.asset_bits < 8)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams invalid asset_bits " << txparams.asset_bits;

		return -1;
	}

	key = "amount-bits";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 8, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.amount_bits = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams amount_bits " << txparams.amount_bits;

	if (txparams.amount_bits < 8)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams invalid amount_bits " << txparams.amount_bits;

		return -1;
	}

	key = "donation-bits";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 8, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.donation_bits = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams donation_bits " << txparams.donation_bits;

	if (txparams.donation_bits < 8)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams invalid donation_bits " << txparams.donation_bits;

		return -1;
	}

	key = "exponent-bits";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 8, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.exponent_bits = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams exponent_bits " << txparams.exponent_bits;

	if (txparams.exponent_bits < 2)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams invalid exponent_bits " << txparams.exponent_bits;

		return -1;
	}

	key = "minimum-donation-per-transaction";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.minimum_donation = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams minimum_donation " << txparams.minimum_donation;

	key = "donation-per-transaction";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.donation_per_tx = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams donation_per_tx " << txparams.donation_per_tx;

	key = "donation-per-byte";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.donation_per_byte = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams donation_per_byte " << txparams.donation_per_byte;

	key = "donation-per-output";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.donation_per_output = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams donation_per_output " << txparams.donation_per_output;

	key = "donation-per-input";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.donation_per_input = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams donation_per_input " << txparams.donation_per_input;

	return 0;

missing_key:

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams error missing key " << key;

	return -1;

parse_error:

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams error parsing key " << key << " value " << value.asString();

	return -1;
}

int TxQuery::ParseInputParams(Json::Value& root, TxParams& txparams)
{
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseInputParams txparams " << (uintptr_t)this;

	string key;
	Json::Value value;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	key = "minimum-output-exponent";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_EXPONENT_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.outvalmin = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseInputParams outvalmin " << txparams.outvalmin;

	key = "maximum-output-exponent";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_EXPONENT_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.outvalmax = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseInputParams outvalmax " << txparams.outvalmax;

	key = "maximum-input-exponent";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_EXPONENT_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.invalmax = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseInputParams invalmax " << txparams.invalmax;

	key = "default-output-pool";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_POOL_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.default_output_pool = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseInputParams default_output_pool " << txparams.default_output_pool;

	return 0;

missing_key:

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseInputParams error missing key " << key;

	return -1;

parse_error:

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseInputParams error parsing key " << key << " value " << value.asString();

	return -1;
}

int TxQuery::QuerySerialnums(uint64_t blockchain, const bigint_t *serialnums, unsigned nserials, uint16_t *statuses, bigint_t *hashkeys, uint64_t *tx_commitnums)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums nserials " << nserials;

	int result_code = -1;

	memset(statuses, 0, sizeof(*statuses) * nserials);
	if (hashkeys)
		memset((void*)hashkeys, 0, sizeof(*hashkeys) * nserials);

	auto rc = tx_query_serialnum_create(string(), blockchain, serialnums, nserials, m_writebuf.data(), m_writebuf.size());
	CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		if (g_shutdown) return -1;

		Json::Value root, array, value;
		bigint_t bigval;
		string fn;
		char *output = NULL;
		uint32_t outsize = 0;

		if (g_params.transact_tor_single_query)
			ClearHost();

		auto rc = SubmitQuery(PowType_Query, i, &root);
		if (rc) continue;

		if (root.size() != 1)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums error root size " << root.size();
			continue;
		}

		auto key = root.begin().name();

		if (key != "tx-serial-number-query-results")
			goto unexpected_key;

		array = *root.begin();

		if (!array.isArray())
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums expected an array";

			continue;
		}

		if (array.size() != nserials)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums expected an array of size " << nserials;

			continue;
		}

		for (unsigned i = 0; i < nserials; ++i)
		{
			root = array[i];

			key = "serial-number";
			if (!root.removeMember(key, &value))
				goto missing_key;
			rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, bigval, output, outsize);
			if (rc) goto parse_error;

			if (bigval != serialnums[i])
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums serialnum " << i << " mismatch " << bigval << " != " << serialnums[i];

				continue;
			}

			key = "status";
			if (!root.removeMember(key, &value))
				goto missing_key;
			auto status = value.asString();
			if (status == "unspent")
				statuses[i] = SERIALNUM_STATUS_UNSPENT;
			else if (status == "pending")
				statuses[i] = SERIALNUM_STATUS_PENDING;
			else if (status == "indelible")
				statuses[i] = SERIALNUM_STATUS_SPENT;
			else
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums unrecognized status " << status;

				continue;
			}

			if (statuses[i] == SERIALNUM_STATUS_SPENT)
			{
				key = "hashkey";
				if (!root.removeMember(key, &value))
					goto missing_key;
				rc = parse_int_value(fn, key, value.asString(), TX_HASHKEY_BITS, 0UL, bigval, output, outsize);
				if (rc) goto parse_error;
				if (hashkeys)
					hashkeys[i] = bigval;

				key = "transaction-commitment-number";
				if (root.removeMember(key, &value))	// !!! optional key for now; may be required in the future
				{
					rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
					if (rc) goto parse_error;
					if (tx_commitnums)
						tx_commitnums[i] = BIG64(bigval);
				}
				else if (tx_commitnums)
					tx_commitnums[i] = 0;
			}

			if (root.size())
				goto unexpected_key;
		}

		result_code = 0;

		break;

	unexpected_key:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums error unexpected key " << key;

		continue;

	missing_key:

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums error missing key " << key;

		continue;

	parse_error:

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums error parsing key " << key << " value " << value.asString();

		continue;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums result " << result_code;

	return result_code;
}


int TxQuery::QueryInputs(const uint64_t *commitnum, const unsigned ncommits, TxParams& txparams, QueryInputResults &inputs)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs ncommits " << ncommits;

	int result_code = -1;

	memset((void*)&inputs, 0, sizeof(inputs));

	auto rc = tx_query_inputs_create(string(), txparams.blockchain, commitnum, ncommits, m_writebuf.data(), m_writebuf.size());
	CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		if (g_shutdown) return -1;

		Json::Value root, array, value;
		bigint_t bigval;
		string fn;
		char *output = NULL;
		uint32_t outsize = 0;

		if (g_params.transact_tor_single_query)
			ClearHost();

		auto rc = SubmitQuery(PowType_Query, i, &root);
		if (rc) continue;

		if (root.size() != 1)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs error root size " << root.size();
			continue;
		}

		auto key = root.begin().name();

		if (key != "tx-input-query-report")
			goto unexpected_key;

		root = *root.begin();

		rc = ParseParams(root, txparams);		// FUTURE: update global txparams
		if (rc) continue;

		key = "tx-input-query-results";
		if (!root.removeMember(key, &value))
			goto missing_key;

		if (root.size())
			goto unexpected_key;

		root = value;

		rc = ParseInputParams(root, txparams);
		if (rc) continue;

		key = "parameter-level";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), TX_BLOCKLEVEL_BITS, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		inputs.param_level = BIG64(bigval);
		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs param_level " << inputs.param_level;

		key = "parameter-time";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), TX_TIME_BITS, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		inputs.param_time = BIG64(bigval);
		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs param_time " << inputs.param_time;

		key = "merkle-root";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, inputs.merkle_root, output, outsize);
		if (rc) goto parse_error;
		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs merkle_root " << inputs.merkle_root;

		key = "inputs";
		if (!root.removeMember(key, &array))
			goto missing_key;

		if (root.size())
			goto unexpected_key;

		if (!array.isArray())
			goto not_array;

		if (array.size() != ncommits)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs array size " << array.size() << " != " << ncommits;

			continue;
		}

		CCASSERT(ncommits <= TX_MAXINPATH);

		for (unsigned i = 0; i < ncommits; ++i)
		{
			root = array[i];

			key = "commitment-number";
			if (!root.removeMember(key, &value))
				goto missing_key;
			rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
			if (rc) goto parse_error;

			if (BIG64(bigval) != commitnum[i])
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs commitnum " << i << " mismatch " << BIG64(bigval) << " != " << commitnum[i];

				continue;
			}

			key = "merkle-path";
			if (!root.removeMember(key, &value))
				goto missing_key;

			if (root.size())
				goto unexpected_key;

			if (!value.isArray())
				goto not_array;

			if (value.size() != TX_MERKLE_DEPTH)
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs array size " << value.size() << " != " << TX_MERKLE_DEPTH;

				continue;
			}

			for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
			{
				rc = parse_int_value(fn, key, value[j].asString(), 0, TX_FIELD_MAX, inputs.merkle_paths[i][j], output, outsize);
				if (rc) goto parse_error;
			}
		}

		result_code = 0;

		break;

	unexpected_key:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs error unexpected key " << key;

		continue;

	missing_key:

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs error missing key " << key;

		continue;

	not_array:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs expected an array for key " << key;

		continue;

	parse_error:

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs error parsing key " << key << " value " << value.asString();

		continue;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs result " << result_code;

	return result_code;
}

int TxQuery::ParseQueryAddressQueryResults(const bigint_t& address, const uint64_t commitstart, Json::Value root, QueryAddressResults &results)
{
	Json::Value array, value;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	if (root.size() != 1)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress error root size " << root.size();

		return -1;
	}

	auto key = root.begin().name();

	if (key != "tx-address-query-report")
		goto unexpected_key;

	root = *root.begin();

	key = "address";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_ADDRESS_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (bigval != address)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress result address " << hex << bigval << " != " << address << dec;

		return -1;
	}

	key = "commitment-number-start";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (BIG64(bigval) != commitstart)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress result commitstart " << BIG64(bigval) << " != " << commitstart;

		return -1;
	}

	key = "more-results-available";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	results.more_results = BIG64(bigval);

	key = "tx-address-query-results";
	if (!root.removeMember(key, &array))
		goto missing_key;

	if (array.size() < 1 || array.size() > WALLET_MAX_QUERY_ADDRESS_RESULTS)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress invalid result array size " << array.size();

		return -1;
	}

	results.nresults = array.size();

	for (unsigned i = 0; i < results.nresults; ++i)
	{
		root = array[i];
		QueryAddressResult& result = results.results[i];

		key = "blockchain";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.blockchain = BIG64(bigval);

		key = "pool";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), TX_POOL_BITS, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.pool = BIG64(bigval);

		key = "asset-bits";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 8, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.asset_bits = BIG64(bigval);

		key = "amount-bits";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 8, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.amount_bits = BIG64(bigval);

		key = "exponent-bits";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 8, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.exponent_bits = BIG64(bigval);

		key = "encrypted";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.encrypted = BIG64(bigval);

		if (result.encrypted)
			key = "encrypted-asset";
		else
			key = "asset";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), result.asset_bits, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.asset = BIG64(bigval);

		if (result.encrypted)
			key = "encrypted-amount";
		else
			key = "amount";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), result.amount_bits, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.amount_fp = BIG64(bigval);

		key = "commitment-iv";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), TX_COMMIT_IV_BITS, 0UL, result.commit_iv, output, outsize);
		if (rc) goto parse_error;

		key = "commitment";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, result.commitment, output, outsize);
		if (rc) goto parse_error;

		key = "commitment-number";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.commitnum = BIG64(bigval);

		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress result " << i << hex
			<< " blockchain "		<< result.blockchain
			<< " pool "				<< result.pool
			<< " asset_bits "		<< result.asset_bits
			<< " amount_bits "		<< result.amount_bits
			<< " exponent_bits "	<< result.exponent_bits
			<< " encrypted "		<< result.encrypted
			<< " asset "			<< result.asset
			<< " amount_fp "		<< result.amount_fp
			<< " commit_iv "		<< result.commit_iv
			<< " commitment "		<< result.commitment
			<< " commitnum "		<< result.commitnum
			<< dec;
	}

	return 0;

	unexpected_key:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress error unexpected key " << key;

		return -1;

	missing_key:

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress error missing key " << key;

		return -1;

	parse_error:

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress error parsing key " << key << " value " << value.asString();

		return -1;
}

int TxQuery::QueryAddress(uint64_t blockchain, const bigint_t& address, const uint64_t commitstart, QueryAddressResults &results)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress address " << hex << address << dec << " commitstart " << commitstart;

	int result_code = -1;

	auto rc = tx_query_address_create(string(), blockchain, address, commitstart, WALLET_MAX_QUERY_ADDRESS_RESULTS, m_writebuf.data(), m_writebuf.size());
	CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		if (g_shutdown) return -1;

		memset((void*)&results, 0, sizeof(results));

		Json::Value root;

		if (g_params.transact_tor_single_query)
			ClearHost();

		auto rc = SubmitQuery(PowType_Query, i, &root);

		if (rc > 0)
		{
			CCASSERT(m_pread == m_readbuf.data());

			m_pread[80] = 0;

			if (!strcmp(m_pread, "Not Found"))
			{
				result_code = 0;

				break;
			}

			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress unrecognized response (first 80 bytes): " << m_pread;
		}

		if (rc) continue;

		rc = ParseQueryAddressQueryResults(address, commitstart, root, results);
		if (rc) continue;

		result_code = 0;
		break;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress result " << result_code << " nresults " << results.nresults;

	return result_code;
}
