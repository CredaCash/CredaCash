/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * txquery.cpp
*/

#include "ccwallet.h"
#include "txparams.hpp"
#include "txquery.hpp"
#include "transactions.hpp"

#include <jsonutil.h>
#include <txquery.h>
#include <CCobjects.hpp>
#include <transaction.h>
#include <transaction.hpp>
#include <xtransaction.hpp>
#include <xtransaction-xreq.hpp>

#include <ccserver/connection_manager.hpp>

#define TRACE_TXQUERY	(g_params.trace_txquery)
#define TRACE_TXPARAMS	(g_params.trace_txparams)

#define TXCONN_READ_MAX		200000	//@@!
#define TXCONN_WRITE_MAX	8000	//@@!

#define TXQUERY_TARGET_PROPOGATION_TIME		(2*60)
#define TXQUERY_TIMESTAMP_PAST_ALLOWANCE	(40*60 -TXQUERY_TARGET_PROPOGATION_TIME)	// < TRANSACT_TIMESTAMP_PAST_ALLOWANCE, so timestamp is regenerated before the msg would be rejected by the tx server

//!#define RTEST_SIM_RETRY			2
//!#define RTEST_TX_SUBMIT_ERRORS	4
//!#define RTEST_CUZZ			64

#ifndef RTEST_SIM_RETRY
#define RTEST_SIM_RETRY			0	// don't test
#endif

#ifndef RTEST_TX_SUBMIT_ERRORS
#define RTEST_TX_SUBMIT_ERRORS	0	// don't test
#endif

#ifndef RTEST_CUZZ
#define RTEST_CUZZ				0	// don't test
#endif

// unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister
CCServer::ConnectionFactoryInstantiation<TxQuery> TxQuery::txconnfac(TXCONN_READ_MAX, TXCONN_WRITE_MAX, -1, -1, 0, 1, 0);	//@@!
CCServer::ConnectionManagerBase TxQuery::nullconnmgr("TxQuery");

static vector<string> hosts[2];
static bool tx_server_queries_only = false;

using namespace snarkfront;

static void shuffle_hosts_file(bool brelay)
{
	for (unsigned i = 0; i < hosts[brelay].size(); ++i)
	{
		auto j = rand() % hosts[brelay].size();
		auto t = hosts[brelay][i];
		hosts[brelay][i] = hosts[brelay][j];
		hosts[brelay][j] = t;
	}
}

int TxQuery::ReadHostsFile(bool brelay, const wstring& path)
{
	BOOST_LOG_TRIVIAL(trace) << "TxQuery::ReadHostsFile brelay " << brelay << " file \"" << w2s(path) << "\"";

	CCASSERT(path.length());

	hosts[brelay].clear();

	if (!brelay)
		g_txparams.Reset(); // clear the cached parameters from the tx server

	boost::filesystem::ifstream fs;
	fs.open(path, fstream::in);
	if(!fs.is_open())
	{
		if (!brelay)
			cerr << "ERROR opening transaction server hosts file \"" << w2s(path) << "\"" << endl;

		return -1;
	}

	while (!g_shutdown)
	{
		string line;

		fs >> line;

		if (fs.fail() && !fs.eof())
		{
			cerr << "ERROR reading transaction server hosts file \"" << w2s(path) << "\"" << endl;
			return -1;
		}

		boost::trim(line);

		if (line.length())
		{
			BOOST_LOG_TRIVIAL(trace) << "TxQuery::ReadHostsFile read hostname \"" << line << "\"";

			hosts[brelay].push_back(line);
		}

		if (fs.eof())
			break;
	}

	if (hosts[brelay].size() < 1)
	{
		cerr << "ERROR no hostnames found in file file \"" << w2s(path) << "\"" << endl;
		return -1;
	}

	BOOST_LOG_TRIVIAL(debug) << "TxQuery::ReadHostsFile brelay " << brelay << " loaded " << hosts[brelay].size() << " transaction server hostnames";

	shuffle_hosts_file(brelay);

	return 0;
}

static bool use_relay(PowType powtype)
{
	return tx_server_queries_only && powtype == PowType_Tx && hosts[1].size();
}

void TxQuery::AdvanceHost(PowType powtype, bool retry)
{
	auto brelay = use_relay(powtype);

	if (g_params.transact_tor_single_query)
		m_host_index[brelay] = -1;
	else if (retry)
		++m_host_index[brelay];
}

const string& TxQuery::GetHost(PowType powtype)
{
	auto brelay = use_relay(powtype);

	unsigned i = m_host_index[brelay];

	if (i == (unsigned)-1)
		i = rand();

	CCASSERT(hosts[brelay].size());

	i %= hosts[brelay].size();
	m_host_index[brelay] = i;

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery using " << (brelay ? "relay " : "") << "server " << hosts[brelay][i];

	return hosts[brelay][i];
}

int TxQuery::PrepareQuery(PowType powtype, uint64_t expire_time, bool is_retry, vector<char> *pquery)
{
	if (RandTest(RTEST_SIM_RETRY))
		is_retry = true;

	// expire_time is relative to the local clock unixtime()
	// if expire_time > 0, returns 2 when the expire_time is reached

	if (TRACE_TXQUERY && expire_time) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::PrepareQuery powtype " << powtype << " expire_time " << expire_time << " now " << unixtime() << " is_retry " << is_retry << " conn state " << m_conn_state;
	else if (TRACE_TXQUERY)          BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::PrepareQuery powtype " << powtype << " expire_time " << expire_time <<                          " is_retry " << is_retry << " conn state " << m_conn_state;

	if (!pquery)
		pquery = &m_writebuf;

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
		return -1;

	auto difficulty = txparams.query_work_difficulty;
	if (!powtype)
		difficulty = 0;
	if (powtype == PowType_Tx)
	{
		auto tag = *(uint32_t*)(pquery->data() + 4);
		if (Xtx::TypeIsXpay(CCObject::ObjType(tag)))
			difficulty = txparams.xcx_pay_work_difficulty;
		else
			difficulty = txparams.tx_work_difficulty;

		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::PrepareQuery powtype " << powtype << " type " << CCObject::ObjType(tag) << " tag " << hex << tag << " difficulty " << difficulty << dec;
	}

	if (expire_time)
	{
		// adjust expire_time to allow time to send the msg

		auto life_time = expire_time - unixtime();
		if (life_time > 2*TXQUERY_TARGET_PROPOGATION_TIME)
			expire_time -= TXQUERY_TARGET_PROPOGATION_TIME;
		else if (life_time > 0)
			expire_time -= life_time/2;
	}

	auto t0 = ccticks();

	while (true) // use break to exit
	{
		uint64_t now = unixtime();
		if (expire_time && now >= expire_time)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::PrepareQuery reached expire_time " << expire_time << " powtype " << powtype << " difficulty " << difficulty << " elasped time " << ccticks_elapsed(t0, ccticks());

			return 2;
		}

		auto restart = now + TXQUERY_TIMESTAMP_PAST_ALLOWANCE;	// restart before the msg would be rejected by the tx server

		if (expire_time && restart > expire_time)
			restart = expire_time;

		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::PrepareQuery tx_reset_work powtype " << powtype << " now " << now << " restart " << restart << " expire_time " << expire_time;

		uint64_t timestamp = now + txparams.clock_diff;

		rc = tx_reset_work(string(), timestamp, pquery->data(), pquery->size());
		CCASSERTZ(rc);

		while (powtype)
		{
			rc = tx_set_work(string(), 0, TX_POW_NPROOFS, 1 << 26, difficulty, pquery->data(), pquery->size());
			if (g_shutdown) return -1;
			CCASSERT(rc >= 0);

			auto time_left = restart - unixtime();

			//if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::PrepareQuery tx_set_work rc " << rc << " time_left " << time_left;

			if (!rc || time_left <= 0)
				break;
		}

		if (!rc)
			break;
	}

	if (TRACE_TXQUERY && difficulty && expire_time) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::PrepareQuery tx_set_work powtype " << powtype << " difficulty " << difficulty << " elasped time " << ccticks_elapsed(t0, ccticks()) << " expire_time " << expire_time << " now " << unixtime();
	else if (TRACE_TXQUERY && difficulty)          BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::PrepareQuery tx_set_work powtype " << powtype << " difficulty " << difficulty << " elasped time " << ccticks_elapsed(t0, ccticks());

	return 0;
}

int TxQuery::TryQuery(PowType powtype, vector<char> *pquery)
{
	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	WaitForStopped(IsInteractive());

	if (g_shutdown) return -1;

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	m_result_code = -1;
	m_data_written = false;

	if (!pquery)
		pquery = &m_writebuf;

	m_pquery = pquery;

	auto read_count_start = m_read_count;

	if (m_conn_state == CONN_STOPPED)
	{
		InitNewConnection();

		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery posting query; m_stopping " << m_stopping.load();

		int rc;

		if (g_params.transact_tor)
		{
			static const string null;
			const string& torhost = GetHost(powtype);
			const string& toruser = (g_params.transact_tor_single_query ? null : torhost);
			rc = Post("TxQuery::TryQuery", boost::bind(&Connection::HandleConnectOutgoingTor, this, g_params.torproxy_port, ref(torhost), ref(toruser), AutoCount(this)));
		}
		else
			rc = Post("TxQuery::TryQuery", boost::bind(&Connection::HandleConnectOutgoing, this, ref(g_params.transact_host), g_params.transact_port, AutoCount(this)));

		if (rc)
		{
			if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery post failed; m_stopping " << m_stopping.load();

			Stop();
			return -1;
		}

		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery query posted; m_stopping " << m_stopping.load();
	}
	else
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery submitting to open connection not yet supported";

		Stop();
		return -1;	// FUTURE: need a way to submit query to already open connection
	}

	WaitForReadComplete(read_count_start, IsInteractive());	// abort_on_shutdown is false for non interactive queries because these will be closed by Service::StartShutdown() -> StopAllConnections() ?

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::TryQuery result " << m_result_code << " read_count_start " << read_count_start << " m_read_count " << m_read_count << " g_shutdown " << g_shutdown;

	Stop();	// !!! for now

	if (powtype && m_data_written)
		m_possibly_sent = true;		// msg may have been successfully sent

	if (g_shutdown) return -1;

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	return m_result_code;
}

int TxQuery::SubmitQuery(PowType powtype, uint64_t expire_time, bool is_retry, Json::Value *root, vector<char> *pquery, bool skip_prepare, bool debug)
{
	// returns 2 on timeout
	// returns 1 if response is not valid json

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery pquery " << hex << (uintptr_t)pquery << dec << " size " << (pquery ? pquery->size() : m_writebuf.size()) << " skip_prepare " << skip_prepare << " debug " << debug;

	if (root)
		root->clear();

	int result_code = -1;

	AdvanceHost(powtype, is_retry);

	while (true)	// break on error
	{
		int rc = 0;
		if (!skip_prepare)
			rc = PrepareQuery(powtype, expire_time, is_retry, pquery);
		if (!rc)
			rc = TryQuery(powtype, pquery);

		if (rc)
		{
			m_nred = 0;

			if (m_pread)
				m_pread[0] = 0;

			result_code = rc;

			break;
		}

		m_pread[m_nred] = 0;

		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery reply " << m_nred << " bytes";

		if (m_nred < 1)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery empty response";

			break;
		}

		if (debug) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery reply " << m_pread;
		else if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery reply " << m_pread;

		result_code = 1;

		if (m_pread[0] != '{')
			break;

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
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitQuery json parse error " << m_pread;

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

int TxQuery::TxToWire(TxPay& ts)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::TxToWire";

	string fn;
	char output[128] = {0};
	uint32_t outsize = sizeof(output);

	//tx_dump_stream(cerr, ts);

	auto rc = txpay_to_wire(fn, ts, -1, output, outsize, m_writebuf.data(), m_writebuf.size());
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " TxQuery::TxToWire txpay_to_wire failed: " << output;

		m_pread = m_readbuf.data();
		auto nbytes = (outsize < m_readbuf.size() ? outsize : m_readbuf.size() - 1);
		memcpy(m_pread, output, nbytes);
		m_pread[nbytes] = 0;

		return 1;
	}

	return 0;
}

int TxQuery::PrepareTx(TxPay& ts, uint64_t expire_time, vector<char>& wire)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::PrepareTx expire_time " << expire_time;

	auto rc = TxToWire(ts);
	if (rc)
		return rc;

	rc = PrepareQuery(PowType_Tx, expire_time, true); // set is_retry true to force param update
	if (rc)
		return rc;

	auto size = *(uint32_t*)(m_writebuf.data());
	wire.resize(size);
	memcpy(wire.data(), m_writebuf.data(), size);

	return 0;
}

int TxQuery::SubmitTx(TxPay& ts, uint64_t expire_time, uint64_t& next_commitnum, bool debug)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitTx expire_time " << expire_time << " pquery " << " debug " << debug;

	auto rc = TxToWire(ts);
	if (rc)
		return rc;

	return DoSubmitTx(expire_time, next_commitnum, m_writebuf, false, debug);
}

int TxQuery::SubmitPreparedTx(uint64_t& next_commitnum, vector<char>& wire, bool debug)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitPreparedTx debug " << debug;

	return DoSubmitTx(0, next_commitnum, wire, true, debug);
}

int TxQuery::DoSubmitTx(uint64_t expire_time, uint64_t& next_commitnum, vector<char>& wire, bool skip_prepare, bool debug)
{
	//if (RandTest(2)) ccsleep(40);	// for testing

	int result_code = -1;
	next_commitnum = 0;
	m_possibly_sent = false;

	for (int i = 0; i <= g_params.tx_submit_retries; ++i)
	{
		if (g_shutdown) return -1;

		auto already_possibly_sent = m_possibly_sent;

		auto rc = SubmitQuery(PowType_Tx, expire_time, i, NULL, &wire, skip_prepare, debug);

		if (rc > 1)
		{
			result_code = rc;

			break;
		}

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
				string fn;
				char output[1] = {0};
				uint32_t outsize = sizeof(output);
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

			if (!strncmp(m_pread, "INVALID:expired", 15))
			{
				m_possibly_sent = already_possibly_sent;	// restore previous state

				result_code = 2;

				break;
			}

			if (!strncmp(m_pread, "INVALID:", 8))
			{
				m_possibly_sent = already_possibly_sent;	// restore previous state

				result_code = 1;

				break;
			}

			if (!strncmp(m_pread, "UNKNOWN:", 8))
			{
				m_possibly_sent = true;		// tx may have been accepted

				result_code = -1;

				break;
			}

			if (!strncmp(m_pread, "ERROR:", 6))
			{
				m_possibly_sent = already_possibly_sent;	// restore previous state

				result_code = -1;

				break;
			}

			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitTx error unrecognized response " << m_pread;
		}
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::SubmitTx result " << result_code << " next_commitnum " << next_commitnum;

	return result_code;
}

int TxQuery::QueryParams(TxParams& txparams, vector<char> &querybuf)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryParams";

	int result_code = -1;

	auto rc = tx_query_parameters_create(string(), querybuf.data(), querybuf.size());
	CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		if (g_shutdown) return -1;

		Json::Value root;

		auto rc = SubmitQuery(PowType_None, 0, i, &root, &querybuf);
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

		if (result_code) continue;

		if (txparams.queries_only && !txparams.connected && i < g_params.tx_query_retries)
		{
			BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryParams retrying server not connected to network";
			continue;
		}

		result_code = ParseBlockChainStatus(root, txparams.blockchain_status);

		if (result_code) continue;

		result_code = ParseInputParams(root, txparams);

		if (result_code) continue;

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
	// parses StreamParams() output from server transact.cpp

	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams txparams " << (uintptr_t)&txparams;

	string key;
	Json::Value value;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	// from StreamNetParams()

	// note: clock_diff = server_time - local_time
	//		server_time = local_time + clock_diff
	//		local_time = server_time - clock_diff

	key = "server-timestamp";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.clock_diff = BIG64(bigval) - unixtime();
	//cerr << "clock_diff " << txparams.clock_diff << endl;
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams clock_diff " << txparams.clock_diff;

	key = "connected-to-network";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.connected = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams connected " << txparams.connected;

	key = "parameters-last-modified-level";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.params_last_modified_level = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams params_last_modified_level " << txparams.params_last_modified_level;

	key = "blockchain-number";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.blockchain = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams blockchain " << txparams.blockchain;

	if (!txparams.blockchain)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams error invalid " << key << " " << txparams.blockchain;

		return -1;
	}

	// from StreamServerParams()

	key = "server-version";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.server_version = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams server_version " << txparams.server_version;

	key = "server-protocol-version";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.protocol_version = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams protocol_version " << txparams.protocol_version;

	key = "server-queries-only";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		txparams.queries_only = BIG64(bigval);
	}
	else
	{
		txparams.queries_only = false;
	}
	tx_server_queries_only = txparams.queries_only;
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams queries_only " << txparams.queries_only;

	// from StreamTxParams()

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

	key = "xcx-naked-buy-work-difficulty";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.xcx_naked_buy_work_difficulty = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams xcx_naked_buy_work_difficulty " << txparams.xcx_naked_buy_work_difficulty;

	key = "xcx-pay-work-difficulty";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.xcx_pay_work_difficulty = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams xcx_pay_work_difficulty " << txparams.xcx_pay_work_difficulty;

	key = "xcx-request-minimum-expiration-time";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.xcx_minimum_expiration = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams xcx_minimum_expiration " << txparams.xcx_minimum_expiration;

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

	// from StreamAmountBits()

	key = "asset-bits";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 8, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.asset_bits = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams asset_bits " << txparams.asset_bits;

	if (txparams.asset_bits < 8)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams error invalid " << key << " " << txparams.asset_bits;

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
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams error invalid " << key << " " << txparams.amount_bits;

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
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams error invalid " << key << " " << txparams.donation_bits;

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
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams error invalid " << key << " " << txparams.exponent_bits;

		return -1;
	}

	// from StreamDonationParams()

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

	key = "donation-per-crosschain-exchange-request";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.donation_per_xcx_req = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams donation_per_xcx_req " << txparams.donation_per_xcx_req;

	return 0;

missing_key:

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams error missing key " << key;

	return -1;

parse_error:

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseParams error parsing key " << key << " value " << value.asString();

	return -1;
}

int TxQuery::ParseBlockChainStatus(Json::Value& root, BlockChainStatus& blockchain_status)
{
	// parses StreamBlockChainStatus() output from server transact.cpp

	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseBlockChainStatus blockchain_status " << (uintptr_t)&blockchain_status;

	string key;
	Json::Value value;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	key = "blockchain-highest-indelible-level";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_BLOCKLEVEL_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	blockchain_status.last_indelible_level = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseBlockChainStatus last_indelible_level " << blockchain_status.last_indelible_level;

	key = "blockchain-highest-indelible-timestamp";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	blockchain_status.last_indelible_timestamp = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseBlockChainStatus last_indelible_timestamp " << blockchain_status.last_indelible_timestamp;

	key = "blockchain-last-matching-completed-blocktime";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	blockchain_status.last_matching_completed_block_time = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseBlockChainStatus last_matching_completed_block_time " << blockchain_status.last_matching_completed_block_time;

	key = "blockchain-last-matching-start-blocktime";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	blockchain_status.last_matching_start_block_time = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseBlockChainStatus last_matching_start_block_time " << blockchain_status.last_matching_start_block_time;

	return 0;

missing_key:

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseBlockChainStatus error missing key " << key;

	return -1;

parse_error:

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseBlockChainStatus error parsing key " << key << " value " << value.asString();

	return -1;
}

int TxQuery::ParseInputParams(Json::Value& root, TxParams& txparams)
{
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseInputParams txparams " << (uintptr_t)&txparams;

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

	key = "default-output-billet-domain-id";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_DOMAIN_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	txparams.default_domain = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseInputParams default_domain " << txparams.default_domain;

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

	auto rc = tx_query_serialnum_create(string(), blockchain, serialnums, nserials, m_writebuf.data(), m_writebuf.size());
	CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		memset(statuses, 0, sizeof(*statuses) * nserials);
		if (hashkeys)
			memset((void*)hashkeys, 0, sizeof(*hashkeys) * nserials);

		if (g_shutdown) return -1;

		Json::Value root, array, value;
		bigint_t bigval;
		string fn;
		char *output = NULL;
		uint32_t outsize = 0;

		auto rc = SubmitQuery(PowType_Query, 0, i, &root);
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
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums error expected an array";

			continue;
		}

		if (array.size() != nserials)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums error expected an array of size " << nserials;

			continue;
		}

		for (unsigned i = 0; i < nserials; ++i)
		{
			Json::Value& root = array[i];

			key = "serial-number";
			if (!root.removeMember(key, &value))
				goto missing_key;
			rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, bigval, output, outsize);
			if (rc) goto parse_error;

			if (bigval != serialnums[i])
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums error " << key << " " << i << " mismatch " << bigval << " != " << serialnums[i];

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
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums error unrecognized status " << status;

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

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums error missing key " << key;

		continue;

	parse_error:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums error parsing key " << key << " value " << value.asString();

		continue;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QuerySerialnums result " << result_code;

	return result_code;
}


int TxQuery::QueryInputs(const uint64_t *commitnum, const unsigned ncommits, TxParams& txparams, QueryInputResults &inputs)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs ncommits " << ncommits;
	for (unsigned i = 0; i < ncommits; ++i)
	{
	//	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs commitnum[" << i << "] = " << commitnum[i];
	}

	int result_code = -1;

	auto rc = tx_query_inputs_create(string(), txparams.blockchain, commitnum, ncommits, m_writebuf.data(), m_writebuf.size());
	CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		inputs.Clear();

		if (g_shutdown) return -1;

		Json::Value root, array, value;
		bigint_t bigval;
		string fn;
		char *output = NULL;
		uint32_t outsize = 0;

		auto rc = SubmitQuery(PowType_Query, 0, i, &root);
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
		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs merkle_root " << buf2hex(&inputs.merkle_root, TX_MERKLE_BYTES);

		key = "inputs";
		if (!root.removeMember(key, &array))
			goto missing_key;

		if (root.size())
			goto unexpected_key;

		if (!array.isArray())
			goto not_array;

		if (array.size() != ncommits)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs error array size " << array.size() << " != " << ncommits;

			continue;
		}

		CCASSERT(ncommits <= TX_MAXINPATH);

		for (unsigned i = 0; i < ncommits; ++i)
		{
			Json::Value& root = array[i];

			key = "commitment-number";
			if (!root.removeMember(key, &value))
				goto missing_key;
			rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
			if (rc) goto parse_error;

			if (BIG64(bigval) != commitnum[i])
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs error " << key << " " << i << " mismatch " << BIG64(bigval) << " != " << commitnum[i];

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

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs error missing key " << key;

		continue;

	not_array:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs expected an array for key " << key;

		continue;

	parse_error:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs error parsing key " << key << " value " << value.asString();

		continue;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryInputs result " << result_code;

	return result_code;
}

int TxQuery::ParseQueryAddressResults(const bigint_t& address, const uint64_t commitstart, Json::Value root, QueryAddressResults &results)
{
	Json::Value array, value;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	if (root.size() != 1)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryAddressResults error root size " << root.size();

		return -1;
	}

	auto key = root.begin().name();

	if (key != "tx-address-query-report")
		goto unexpected_key;

	root = *root.begin();

	key = "server-timestamp";
	root.removeMember(key, &value);

	key = "address";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_ADDRESS_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (bigval != address)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryAddressResults result address " << hex << bigval << " != " << address << dec;

		return -1;
	}

	key = "commitment-number-start";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (BIG64(bigval) != commitstart)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryAddressResults result commitstart " << BIG64(bigval) << " != " << commitstart;

		return -1;
	}

	key = "more-results-available";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	results.more_results = BIG64(bigval);

	key = "tx-address-query-results";
	if (!root.removeMember(key, &array))
		goto missing_key;

	if (!array.isArray())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryAddressResults expected an array";

		return -1;
	}

	if (array.size() < 1 || array.size() > WALLET_QUERY_ADDRESS_MAX_RESULTS)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryAddressResults invalid result array size " << array.size();

		return -1;
	}

	results.nresults = array.size();

	for (unsigned i = 0; i < results.nresults; ++i)
	{
		Json::Value& root = array[i];
		QueryAddressResult& result = results.results[i];

		key = "blockchain";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.blockchain = BIG64(bigval);

		key = "domain";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), TX_DOMAIN_BITS, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		result.domain = BIG64(bigval);

		key = "is-special-domain";
		if (root.removeMember(key, &value))
		{
			rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
			if (rc) goto parse_error;
			result.is_special_domain = BIG64(bigval);
		}

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

		if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryAddressResults result " << i
			<< " blockchain "		<< result.blockchain
			<< " domain "				<< result.domain
			<< " asset_bits "		<< result.asset_bits
			<< " amount_bits "		<< result.amount_bits
			<< " exponent_bits "	<< result.exponent_bits
			<< " encrypted "		<< result.encrypted
			<< " asset "			<< result.asset
			<< " amount_fp "		<< result.amount_fp
			<< " commit_iv "		<< buf2hex(&result.commit_iv, TX_COMMIT_IV_BYTES)
			<< " commitment "		<< buf2hex(&result.commitment, TX_COMMITMENT_BYTES)
			<< " commitnum "		<< result.commitnum;
	}

	return 0;

	unexpected_key:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryAddressResults error unexpected key " << key;

		return -1;

	missing_key:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryAddressResults error missing key " << key;

		return -1;

	parse_error:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryAddressResults error parsing key " << key << " value " << value.asString();

		return -1;
}

int TxQuery::QueryAddress(uint64_t blockchain, const bigint_t& address, const uint64_t commitstart, QueryAddressResults &results)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress address " << buf2hex(&address, TX_ADDRESS_BYTES) << " commitstart " << commitstart;

	int result_code = -1;

	auto rc = tx_query_address_create(string(), blockchain, address, commitstart, WALLET_QUERY_ADDRESS_MAX_RESULTS, m_writebuf.data(), m_writebuf.size());
	CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		results.Clear();

		if (g_shutdown) return -1;

		Json::Value root;

		auto rc = SubmitQuery(PowType_Query, 0, i, &root);

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

		rc = ParseQueryAddressResults(address, commitstart, root, results);
		if (rc) continue;

		result_code = 0;
		break;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryAddress address " << buf2hex(&address, TX_ADDRESS_BYTES) << " commitstart " << commitstart << " result " << result_code << " nresults " << results.nresults << " more_results " << results.more_results;

	return result_code;
}

void ConvertAmountToFloatString(uint64_t asset, const bigint_t& amount, Json::Value& value)
{
	string amts;

	amount_to_string(asset, amount, amts);

	value = CCXFLOAT_STRING_PREFIX + amts;
}

void SetUniFloatString(Json::Value& value, const UniFloat& val)
{
	value = CCXFLOAT_STRING_PREFIX + val.asFullString();
}

void ChangeFloatToUniFloatString(Json::Value& value)
{
	//cout << "ChangeFloatToUniFloatString " << value << endl;

	SetUniFloatString(value, UniFloat(value.asDouble()));
}

int TxQuery::ParseQueryXreqsResults(const unsigned xcx_type, const bigint_t& min_amount, const bigint_t& max_amount, const double& min_rate, const double& base_costs, const double& quote_costs, const uint64_t base_asset, const uint64_t quote_asset, const string& foreign_asset, unsigned maxret, unsigned offset, QueryXreqsResults &results)
{
	Json::Value value, *array;
	string key;
	bigint_t bigval;

	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	auto select_buyers = !Xtx::TypeIsBuyer(xcx_type);
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults xcx_type " << xcx_type << " select_buyers " << select_buyers;

	if (results.json.size() != 1)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error root size " << results.json.size();

		return -1;
	}

	Json::Value& root = *results.json.begin();

	auto key_str = results.json.begin().name();

	if (key_str != "exchange-requests-query-report")
		goto unexpected_key;

	key = "server-timestamp";
	if (!root.isMember(key))
		goto missing_key;
	rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	results.server_timestamp = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults server_timestamp " << results.server_timestamp;

	key = "blockchain-number";
	if (!root.isMember(key))
		goto missing_key;
	rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	results.blockchain = BIG64(bigval);

	key = "exchange-request-matching-type";
	if (!root.isMember(key))
		goto missing_key;
	rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (BIG64(bigval) != xcx_type)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " " << BIG64(bigval) << " != " << xcx_type;

		return -1;
	}

	root[key + "-label"] = Transaction::TypeString(BIG64(bigval));

	key = "minimum-amount";
	if (!root.isMember(key))
		goto missing_key;
	rc = parse_int_value(fn, key, root[key].asString(), 0, TX_FIELD_MAX, bigval, output, outsize);
	if (rc) goto parse_error;

	if (bigval != min_amount)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " " << bigval << " != " << min_amount;

		return -1;
	}

	ConvertAmountToFloatString(base_asset, bigval, root[key]);

	key = "maximum-amount";
	if (!root.isMember(key))
		goto missing_key;
	rc = parse_int_value(fn, key, root[key].asString(), 0, TX_FIELD_MAX, bigval, output, outsize);
	if (rc) goto parse_error;

	if (bigval != max_amount)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error result " << key << " " << bigval << " != " << max_amount;

		return -1;
	}

	ConvertAmountToFloatString(base_asset, bigval, root[key]);

	key = (select_buyers ? "maximum-rate" : "minimum-rate");
	if (!root.isMember(key))
		goto missing_key;
	value = root[key];
	if (!value.isNumeric()) goto parse_error;

	if (min_rate && select_buyers && value.asDouble() > min_rate)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults warning " << key << " " << value << " > " << min_rate;

		//return -1;
	}
	else if (min_rate && !select_buyers && value.asDouble() < min_rate)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults warning " << key << " " << value << " < " << min_rate;

		//return -1;
	}

	//cout << "key " << key << " val " << root[key] << " = " << root[key].asDouble() << " DBL_MAX " << DBL_MAX << endl;

	ChangeFloatToUniFloatString(root[key]);

	key = (select_buyers ? "maximum-rate-step" : "minimum-rate-step");
	if (!root.isMember(key))
		goto missing_key;
	value = root[key];
	if (!value.isNumeric()) goto parse_error;

	ChangeFloatToUniFloatString(root[key]);

	key = "debug-rate-step-back";
	if (root.isMember(key))
	{
		value = root[key];
		if (!value.isNumeric()) goto parse_error;

		ChangeFloatToUniFloatString(root[key]);
	}

	key = "base-asset";
	if (!root.isMember(key))
		goto missing_key;
	rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (BIG64(bigval) != base_asset)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " " << BIG64(bigval) << " != " << base_asset;

		return -1;
	}

	key = "quote-asset";
	if (!root.isMember(key))
		goto missing_key;
	rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (BIG64(bigval) != quote_asset)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " " << BIG64(bigval) << " != " << quote_asset;

		return -1;
	}

	key = "foreign-asset";
	if (foreign_asset.length() && !root.isMember(key))
		goto missing_key;
	value = root[key];

	if (value.asString() != foreign_asset)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " \"" << value << "\" != \"" << foreign_asset << "\"";

		return -1;
	}

	key = "more-results-available";
	if (!root.isMember(key))
		goto missing_key;
	rc = parse_int_value(fn, key, root[key].asString(), 1, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	results.more_results = BIG64(bigval);

	key = "exchange-requests-query-results";
	if (!root.isMember(key))
		goto missing_key;

	array = &root[key];

	if (!array->isArray())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error expected an array";

		return -1;
	}

	if ( /* array->size() < 0 || */ array->size() > maxret)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error invalid result array size " << array->size();

		return -1;
	}

	results.nresults = array->size();

	for (unsigned i = 0; i < results.nresults; ++i)
	{
		Json::Value& root = (*array)[i];
		Xreq self, other;

		key = "exchange-request-type";
		if (!root.isMember(key))
			goto missing_key;
		rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		other.type = BIG64(bigval);
		root[key + "-label"] = Transaction::TypeString(other.type);

		key = "base-asset";
		if (!root.isMember(key))
			goto missing_key;
		rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		other.base_asset = BIG64(bigval);

		if (BIG64(bigval) != base_asset)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " " << BIG64(bigval) << " != " << base_asset;

			return -1;
		}

		key = "quote-asset";
		if (!root.isMember(key))
			goto missing_key;
		rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		other.quote_asset = BIG64(bigval);

		if (BIG64(bigval) != quote_asset)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " " << BIG64(bigval) << " != " << quote_asset;

			return -1;
		}

		key = "foreign-asset";
		if (foreign_asset.length() && !root.isMember(key))
			goto missing_key;
		value = root[key];

		if (value.asString() != foreign_asset)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " \"" << value << "\" != \"" << foreign_asset << "\"";

			return -1;
		}

		key = "minimum-amount";
		if (!root.isMember(key))
			goto missing_key;
		rc = parse_int_value(fn, key, root[key].asString(), 0, TX_FIELD_MAX, bigval, output, outsize);
		if (rc) goto parse_error;
		other.min_amount = bigval;

		if (max_amount && bigval > max_amount)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " " << bigval << " > " << max_amount;

			return -1;
		}

		ConvertAmountToFloatString(base_asset, bigval, root[key]);

		key = "maximum-amount";
		if (!root.isMember(key))
			goto missing_key;
		rc = parse_int_value(fn, key, root[key].asString(), 0, TX_FIELD_MAX, bigval, output, outsize);
		if (rc) goto parse_error;
		other.max_amount = bigval;

		if (bigval < min_amount)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " " << bigval << " < " << min_amount;

			return -1;
		}

		ConvertAmountToFloatString(base_asset, bigval, root[key]);

		key = "open-amount";
		if (!root.isMember(key))
			goto missing_key;
		rc = parse_int_value(fn, key, root[key].asString(), 0, TX_FIELD_MAX, bigval, output, outsize);
		if (rc) goto parse_error;
		other.open_amount = bigval;

		if (min_amount && bigval < min_amount)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error " << key << " " << bigval << " < " << min_amount;

			return -1;
		}

		ConvertAmountToFloatString(base_asset, bigval, root[key]);

		key = "net-rate-required";
		if (!root.isMember(key))
			goto missing_key;
		value = root[key];
		if (!value.isNumeric()) goto parse_error;
		other.net_rate_required = value.asDouble();

		ChangeFloatToUniFloatString(root[key]);

		key = "open-rate-required";
		if (!root.isMember(key))
			goto missing_key;
		value = root[key];
		if (!value.isNumeric()) goto parse_error;
		other.open_rate_required = value.asDouble();

		if (min_rate && select_buyers && value.asDouble() > min_rate)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults warning select_buyers " << select_buyers << " " << key << " " << value << " > " << min_rate;

			//return -1;
		}
		else if (min_rate && !select_buyers && value.asDouble() < min_rate)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults warning select_buyers " << select_buyers << " " << key << " " << value << " < " << min_rate;

			//return -1;
		}
		//else
		//	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults type " << xcx_type << " " << key << " " << value << " matches " << min_rate;

		ChangeFloatToUniFloatString(root[key]);

		key = "wait-discount";
		if (!root.isMember(key))
			goto missing_key;
		value = root[key];
		if (!value.isNumeric()) goto parse_error;
		other.wait_discount = value.asDouble();

		ChangeFloatToUniFloatString(root[key]);

		key = "base-costs";
		if (!root.isMember(key))
			goto missing_key;
		value = root[key];
		if (!value.isNumeric()) goto parse_error;
		other.base_costs = value.asDouble();

		key = "quote-costs";
		if (!root.isMember(key))
			goto missing_key;
		value = root[key];
		if (!value.isNumeric()) goto parse_error;
		other.quote_costs = value.asDouble();

		key = "pending-match-rate";
		if (root.isMember(key))
		{
			value = root[key];
			if (!value.isNumeric()) goto parse_error;

			if (min_rate && select_buyers && value.asDouble() > min_rate)
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults warning select_buyers " << select_buyers << " " << key << " " << value << " > " << min_rate;

				//return -1;
			}
			else if (min_rate && !select_buyers && value.asDouble() < min_rate)
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults warning select_buyers " << select_buyers << " " << key << " " << value << " < " << min_rate;

				//return -1;
			}
			//else
			//	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults type " << xcx_type << " " << key << " " << value << " matches " << min_rate;

			other.pending_match_rate = value.asDouble();

			ChangeFloatToUniFloatString(root[key]);

			key = "pending-match-amount";
			if (!root.isMember(key))
				goto missing_key;
			rc = parse_int_value(fn, key, root[key].asString(), 0, TX_FIELD_MAX, bigval, output, outsize);
			if (rc) goto parse_error;
			other.pending_match_amount = bigval;

			ConvertAmountToFloatString(base_asset, bigval, root[key]);

			key = "pending-match-hold-time";
			if (!root.isMember(key))
				goto missing_key;
			rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
			if (rc) goto parse_error;
			other.pending_match_hold_time = BIG64(bigval);
		}

		// compute and set best match rate and amount

		self.type = xcx_type;
		self.base_costs = base_costs;
		self.quote_costs = quote_costs;

		auto direction = -Xreq::RateSign(self.IsBuyer()); // sell reqs are more competitive when rounded down; buy reqs up

		bool compete = other.pending_match_rate.asFloat() && other.pending_match_hold_time > 0;
		int rounding = 0;

		while (!compete || other.wait_discount == 0) // use break to exit
		{
			UniFloat pending_match_net_rate, best_amount, best_rate, rate;
			Json::Value best_amount_string;

			// for a competitive request, compute the other req's net rate in the pending match

			if (compete)
			{
				pending_match_net_rate = other.NetRate(other.pending_match_amount, other.pending_match_rate);

				key = "pending-match-net-rate";
				SetUniFloatString(root[key], pending_match_net_rate);
			}

			// compute best match amount = min(max_amount, other.open_amount)

			if (max_amount && max_amount < other.open_amount)
			{
				best_amount = Xtx::asUniFloat(base_asset, max_amount);
				ConvertAmountToFloatString(base_asset, max_amount, best_amount_string);
			}
			else
			{
				best_amount = Xtx::asUniFloat(base_asset, other.open_amount);
				ConvertAmountToFloatString(base_asset, other.open_amount, best_amount_string);
			}

			// compute required match rate

			rate = other.MatchRateRequired(best_amount);
			other.matching_rate_required = rate;

			key = "debug-best-other-matching-rate-required";
			SetUniFloatString(root[key], rate);


			// for a competing match, compute the best available rate
			// (for a non-competing req, the best rate available rate is other.matching_rate_required = rate)

			if (compete)
			{
				rate = UniFloat::Add(other.pending_match_rate, -rate, direction);
				rate = UniFloat::Add(other.pending_match_rate, rate, direction);

				key = "debug-best-matching-rate-required-pre-rounding";
				SetUniFloatString(root[key], rate);
			}

			// compute the net_rate_required for a matching req

			rate = self.NetRate(best_amount, rate);

			key = "debug-best-net-rate-required-pre-rounding";
			SetUniFloatString(root[key], rate);

			if (rate <= 0 || g_shutdown)
			{
				if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults best net_rate_required " << rate << " <= 0 with rounding " << rounding << " pending_match_net_rate " << pending_match_net_rate << " ; self " << self.DebugString() << " ; other " << other.DebugString();

				break;
			}

			auto wire = UniFloat::WireEncode(rate, rounding);
			self.net_rate_required = UniFloat::WireDecode(wire, rounding);

			// compute the match rate

			self.matching_rate_required = self.MatchRateRequired(best_amount);
			key = "debug-best-matching-rate-required";
			SetUniFloatString(root[key], self.matching_rate_required);

			best_rate = UniFloat::Average(self.matching_rate_required, other.matching_rate_required);

			// check that the result is match

			auto self_net_rate = self.NetRate(best_amount, best_rate);
			key = "debug-best-match-net-rate";
			SetUniFloatString(root[key], self_net_rate);

			auto other_net_rate = other.NetRate(best_amount, best_rate);
			key = "debug-best-match-other-net-rate";
			SetUniFloatString(root[key], other_net_rate);

			if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults pending_match_net_rate " << pending_match_net_rate << " best_rate " << best_rate << " self_net_rate " << self_net_rate << " other_net_rate " << other_net_rate << " ; self " << self.DebugString() << " ; other " << other.DebugString();

			if (   self.matching_rate_required.asFloat() * direction < other.matching_rate_required.asFloat() * direction
				|| self.net_rate_required.asFloat() * direction < self_net_rate.asFloat() * direction
				|| other.net_rate_required.asFloat() * direction > other_net_rate.asFloat() * direction
				|| (compete && pending_match_net_rate.asFloat() * direction >= other_net_rate.asFloat() * direction))
			{
				rounding += direction;	// retry with increased rounding

				const int max_retries = 5;
				bool no_match = (rounding < -max_retries || rounding > max_retries);

				//bool log = (no_match || rounding == 2 || rounding == -2);
				//if (log && TRACE_TXQUERY) cout << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults " << (no_match ? "no match" : "retrying") << " with rounding " << rounding << " ; pending_match_net_rate " << pending_match_net_rate << " best_rate " << best_rate << " self_net_rate " << self_net_rate << " other_net_rate " << other_net_rate << " ; self " << self.DebugString() << " ; other " << other.DebugString() << endl;

				if (no_match) // should never happen
				{
					BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults no match with rounding " << rounding << " ; pending_match_net_rate " << pending_match_net_rate << " best_rate " << best_rate << " self_net_rate " << self_net_rate << " other_net_rate " << other_net_rate << " ; self " << self.DebugString() << " ; other " << other.DebugString();

					break;
				}

				if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults retrying with rounding " << rounding << " ; pending_match_net_rate " << pending_match_net_rate << " best_rate " << best_rate << " self_net_rate " << self_net_rate << " other_net_rate " << other_net_rate << " ; self " << self.DebugString() << " ; other " << other.DebugString();

				continue;
			}

			// should never happen
			if (rounding < -1 || rounding > 1)	BOOST_LOG_TRIVIAL(warning)  << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults matched with rounding " << rounding << " ; pending_match_net_rate " << pending_match_net_rate << " best_rate " << best_rate << " self_net_rate " << self_net_rate << " other_net_rate " << other_net_rate << " ; self " << self.DebugString() << " ; other " << other.DebugString();
			else if (TRACE_TXQUERY)				BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults matched with rounding " << rounding << " ; pending_match_net_rate " << pending_match_net_rate << " best_rate " << best_rate << " self_net_rate " << self_net_rate << " other_net_rate " << other_net_rate << " ; self " << self.DebugString() << " ; other " << other.DebugString();

			key = "best-match-net-rate-required";
			SetUniFloatString(root[key], self.net_rate_required);

			best_rate = UniFloat::Average(self.matching_rate_required, other.matching_rate_required);
			key = "best-match-rate";
			SetUniFloatString(root[key], best_rate);

			key = "best-match-amount";
			root[key] = best_amount_string;

			break;
		}
	}

	return 0;

	unexpected_key:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error unexpected key " << key_str;

		return -1;

	missing_key:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error missing key " << key;

		return -1;

	parse_error:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXreqsResults error parsing key " << key << " value " << root[key].asString();

		return -1;
}

int TxQuery::QueryXreqs(const unsigned xcx_type, const bigint_t& min_amount, const bigint_t& max_amount, const double& min_rate, const double& base_costs, const double& quote_costs, const uint64_t base_asset, const uint64_t quote_asset, const string& foreign_asset, unsigned maxret, unsigned offset, unsigned flags, QueryXreqsResults &results)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXreqs xcx_type " << xcx_type << " min_amount " << min_amount << " max_amount " << max_amount << " min_rate " << min_rate << " base_costs " << base_costs << " quote_costs " << quote_costs << " base_asset " << base_asset << " quote_asset " << quote_asset << " foreign_asset " << foreign_asset << " maxret " << maxret << " offset " << offset << " flags " << flags;

	int result_code = -1;

	if (!maxret || maxret > WALLET_QUERY_XREQS_MAX_RESULTS)
		maxret = WALLET_QUERY_XREQS_MAX_RESULTS;

	auto rc = tx_query_xreqs_create(string(), xcx_type, min_amount, max_amount, min_rate, base_asset, quote_asset, foreign_asset, maxret, offset, flags, m_writebuf.data(), m_writebuf.size());
		CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		results.Clear();

		if (g_shutdown) return -1;

		auto rc = SubmitQuery(PowType_Query, 0, i, &results.json);

		if (rc > 0)
		{
			CCASSERT(m_pread == m_readbuf.data());

			m_pread[80] = 0;

			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXreqs unrecognized response (first 80 bytes): " << m_pread;
		}

		if (rc) continue;

		//BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXreqs response: " << m_pread; // for debugging

		rc = ParseQueryXreqsResults(xcx_type, min_amount, max_amount, min_rate, base_costs, quote_costs, base_asset, quote_asset, foreign_asset, maxret, offset, results);
		if (rc) continue;

		result_code = 0;
		break;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXreqs result " << result_code << " nresults " << results.nresults;

	return result_code;
}

int TxQuery::ParseQueryXmatchreq(Json::Value root, Xmatch &match, Xmatchreq &matchreq)
{
	Json::Value value;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	string key = "number";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	matchreq.xreqnum = BIG64(bigval);

	key = "type";
	if (root.removeMember(key, &value))
	{
		match.have_xreqs = true;

		rc = parse_int_value(fn, key, value.asString(), 32, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.type = BIG64(bigval);

		auto isbuyer = Xtx::TypeIsBuyer(matchreq.type);
		const string offered = "offered";
		const string required = "required";

		key = "object-id";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_objid(fn, key, value.asString(), matchreq.objid, output, outsize);
		if (rc) goto parse_error;

		key = "minimum-amount";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, matchreq.min_amount, output, outsize);
		if (rc) goto parse_error;

		key = "maximum-amount";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, matchreq.max_amount, output, outsize);
		if (rc) goto parse_error;

		key = "net-rate-required";
		if (!root.removeMember(key, &value))
			goto missing_key;
		if (!value.isNumeric())
			goto parse_error;
		matchreq.net_rate_required = value.asDouble();

		key = "wait-discount";
		if (!root.removeMember(key, &value))
			goto missing_key;
		if (!value.isNumeric())
			goto parse_error;
		matchreq.wait_discount = value.asDouble();

		key = "base-costs";
		if (!root.removeMember(key, &value))
			goto missing_key;
		if (!value.isNumeric())
			goto parse_error;
		matchreq.base_costs = value.asDouble();

		key = "quote-costs";
		if (!root.removeMember(key, &value))
			goto missing_key;
		if (!value.isNumeric())
			goto parse_error;
		matchreq.quote_costs = value.asDouble();

		key = "consideration-required";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.consideration_required = BIG64(bigval);

		key = "consideration-offered";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.consideration_offered = BIG64(bigval);

		key = "pledge-" + (isbuyer ? offered : required);
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.pledge = BIG64(bigval);

		key = "hold-time";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.hold_time = BIG64(bigval);

		key = "hold-time-required";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.hold_time_required = BIG64(bigval);

		key = "minimum-wait-time";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.min_wait_time = BIG64(bigval);

		key = "accept-time-required";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.accept_time_required = BIG64(bigval);

		key = "accept-time-offered";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.accept_time_offered = BIG64(bigval);

		key = "payment-time-" + (isbuyer ? offered : required);
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.payment_time = BIG64(bigval);

		key = "confirmations-" + (isbuyer ? offered : required);
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		matchreq.confirmations = BIG64(bigval);

		key = "add-immediately-to-blockchain";
		if (root.removeMember(key, &value))
		{
			rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
			if (rc) goto parse_error;
			matchreq.flags.add_immediately_to_blockchain = BIG64(bigval);
		}

		key = "auto-accept-matches";
		if (root.removeMember(key, &value))
		{
			rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
			if (rc) goto parse_error;
			matchreq.flags.auto_accept_matches = BIG64(bigval);
		}

		key = "no-minimum-after-first-match";
		if (root.removeMember(key, &value))
		{
			rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
			if (rc) goto parse_error;
			matchreq.flags.no_minimum_after_first_match = BIG64(bigval);
		}

		key = "must-liquidate-crossing-minimum";
		if (root.removeMember(key, &value))
		{
			rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
			if (rc) goto parse_error;
			matchreq.flags.must_liquidate_crossing_minimum = BIG64(bigval);
		}

		key = "must-liquidate-below-minimum";
		if (root.removeMember(key, &value))
		{
			rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
			if (rc) goto parse_error;
			matchreq.flags.must_liquidate_below_minimum = BIG64(bigval);
		}

		key = "foreign-address";
		if (root.removeMember(key, &value))
			matchreq.foreign_address = value.asString();

		if (root.size())
			goto unexpected_key;
	}

	return 0;

	unexpected_key:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreq error unexpected key " << key;

		return -1;

	missing_key:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreq error missing key " << key;

		return -1;

	parse_error:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreq error parsing key " << key << " value " << value.asString();

		return -1;
}

int TxQuery::ParseQueryXmatch(Json::Value root, Xmatch &match)
{
	Json::Value value;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	string key = "number";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	match.xmatchnum = BIG64(bigval);

	key = "type";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 32, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	match.type = BIG64(bigval);

	key = "status";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 32, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	match.status = BIG64(bigval);

	key = "base-asset";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_ASSET_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	match.xbuy.base_asset = match.xsell.base_asset = BIG64(bigval);

	key = "quote-asset";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), TX_ASSET_BITS, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	match.xbuy.quote_asset = match.xsell.quote_asset = BIG64(bigval);

	key = "foreign-asset";
	if (root.removeMember(key, &value))
		match.xbuy.foreign_asset = match.xsell.foreign_asset = value.asString();

	key = "base-amount";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, match.base_amount, output, outsize);
	if (rc) goto parse_error;

	key = "rate";
	if (!root.removeMember(key, &value))
		goto missing_key;
	if (!value.isNumeric())
		goto parse_error;
	match.rate = value.asDouble();

	key = "accept-time";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	match.accept_time = BIG64(bigval);

	key = "buyer-consideration";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		match.xbuy.match_consideration = BIG64(bigval);
	}

	key = "seller-consideration";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		match.xsell.match_consideration = BIG64(bigval);
	}

	key = "match-pledge";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		match.match_pledge = BIG64(bigval);
	}

	key = "next-deadline";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		match.next_deadline = BIG64(bigval);
	}

	key = "match-timestamp";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		match.match_timestamp = BIG64(bigval);
	}

	key = "accept-timestamp";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		match.accept_timestamp = BIG64(bigval);
	}

	key = "final-timestamp";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		match.final_timestamp = BIG64(bigval);
	}

	key = "amount-paid";
	if (root.removeMember(key, &value))
	{
		if (!value.isNumeric())
			goto parse_error;
		match.amount_paid = value.asDouble();
	}

	key = "mining-amount";
	if (root.removeMember(key, &value))
	{
		if (!value.isNumeric())
			goto parse_error;
		match.mining_amount = value.asDouble();
	}

	key = "buy-request";
	rc = root.removeMember(key, &value);
	if (rc)
	{
		rc = ParseQueryXmatchreq(value, match, match.xbuy);
		if (rc) return rc;
	}

	key = "sell-request";
	rc = root.removeMember(key, &value);
	if (rc)
	{
		rc = ParseQueryXmatchreq(value, match, match.xsell);
		if (rc) return rc;
	}

	if (root.size())
		goto unexpected_key;

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatch parsed " << match.DebugString();

	return 0;

	unexpected_key:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatch error unexpected key " << key;

		return -1;

	missing_key:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatch error missing key " << key;

		return -1;

	parse_error:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatch error parsing key " << key << " value " << value.asString();

		return -1;
}

int TxQuery::ParseQueryXmatchreqResults(uint64_t blockchain, const ccoid_t& objid, uint64_t reqnum, uint64_t matchnum_start, Json::Value root, QueryXmatchreqResults &results)
{
	Json::Value array, value;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	if (root.size() != 1)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults error root size " << root.size();

		return -1;
	}

	auto key = root.begin().name();

	if (key != "exchange-matchreq-query-report")
		goto unexpected_key;

	root = *root.begin();

	key = "server-timestamp";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	results.server_timestamp = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults server_timestamp " << results.server_timestamp;

	key = "blockchain-number";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (BIG64(bigval) != blockchain)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults result blockchain " << BIG64(bigval) << " != " << blockchain;

		return -1;
	}

	rc = ParseBlockChainStatus(root, results.blockchain_status);
	if (rc) return rc;

	if (!reqnum)
	{
		ccoid_t objid_check;

		key = "request-object-id";
		if (!root.removeMember(key, &value))
			goto missing_key;
		rc = parse_objid(fn, key, value.asString(), objid_check, output, outsize);
		if (rc) goto parse_error;

		if (memcmp(&objid, &objid_check, sizeof(objid)))
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults result objid " << buf2hex(&objid_check, sizeof(objid_check)) << " != " << buf2hex(&objid, sizeof(objid));

			return -1;
		}
	}

	key = "request-number";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		results.xreqnum = BIG64(bigval);
	}

	if (reqnum && reqnum != results.xreqnum)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults result xreqnum " << results.xreqnum << " != " << reqnum;

		return -1;
	}

	key = "request-match-number-start";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (BIG64(bigval) != matchnum_start)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults result matchnum_start " << BIG64(bigval) << " != " << matchnum_start;

		return -1;
	}

	if (!results.xreqnum)
		return 0;

	key = "more-results-available";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	results.more_results = BIG64(bigval);

	key = "disposition";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 32, 0UL, bigval, output, outsize);
		if (rc) goto parse_error;
		results.disposition = BIG64(bigval);
	}

	key = "open-amount";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, results.open_amount, output, outsize);
		if (rc) goto parse_error;
	}

	key = "exchange-matchreq-query-results";
	if (!root.removeMember(key, &array))
		goto missing_key;

	if (!array.isArray())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults expected an array";

		return -1;
	}

	if ( /* array.size() < 0 || */ array.size() > WALLET_QUERY_XMATCHREQ_MAX_RESULTS)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults invalid result array size " << array.size();

		return -1;
	}

	results.nresults = array.size();

	for (unsigned i = 0; i < results.nresults; ++i)
	{
		Json::Value& root = array[i];

		key = "match";
		if (!root.removeMember(key, &value))
			goto missing_key;

		if (root.size())
			goto unexpected_key;

		Xmatch& match = results.xmatches[i];

		rc = ParseQueryXmatch(value, match);
		if (rc) return rc;

		if (!match.xbuy.xreqnum && !match.xsell.xreqnum)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults missing matching xreq";

			return -1;
		}

		if (!match.xbuy.xreqnum)
			match.xbuy.xreqnum = results.xreqnum;

		if (!match.xsell.xreqnum)
			match.xsell.xreqnum = results.xreqnum;

		if (match.xbuy.xreqnum != results.xreqnum && match.xsell.xreqnum != results.xreqnum)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults match result xreq mismatch";

			return -1;
		}
	}

	return 0;

	unexpected_key:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults error unexpected key " << key;

		return -1;

	missing_key:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults error missing key " << key;

		return -1;

	parse_error:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchreqResults error parsing key " << key << " value " << value.asString();

		return -1;
}

int TxQuery::QueryXmatchreq(uint64_t blockchain, const ccoid_t& objid, uint64_t reqnum, uint64_t matchnum_start, QueryXmatchreqResults &results)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXmatchreq reqnum " << reqnum << " matchnum_start " << matchnum_start;

	int result_code = -1;

	if (!reqnum)
	{
		auto rc = tx_query_xmatch_objid_create(string(), blockchain, objid, WALLET_QUERY_XMATCHREQ_MAX_RESULTS, m_writebuf.data(), m_writebuf.size());
		CCASSERTZ(rc);
	}
	else
	{
		auto rc = tx_query_xmatch_xreqnum_create(string(), blockchain, reqnum, matchnum_start, WALLET_QUERY_XMATCHREQ_MAX_RESULTS, m_writebuf.data(), m_writebuf.size());
		CCASSERTZ(rc);
	}

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		results.Clear();

		if (g_shutdown) return -1;

		Json::Value root;

		auto rc = SubmitQuery(PowType_Query, 0, i, &root);

		if (rc > 0)
		{
			CCASSERT(m_pread == m_readbuf.data());

			m_pread[80] = 0;

			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXmatchreq unrecognized response (first 80 bytes): " << m_pread;
		}

		if (rc) continue;

		//BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXmatchreq response: " << m_pread; // for debugging

		rc = ParseQueryXmatchreqResults(blockchain, objid, reqnum, matchnum_start, root, results);
		if (rc) continue;

		result_code = 0;
		break;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXmatchreq result " << result_code << " nresults " << results.nresults;

	return result_code;
}

int TxQuery::ParseQueryXmatchResults(uint64_t blockchain, uint64_t matchnum, Json::Value root, QueryXmatchResults &results)
{
	Json::Value value;
	bigint_t bigval;
	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	if (root.size() != 1)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchResults error root size " << root.size();

		return -1;
	}

	auto key = root.begin().name();

	if (key != "exchange-match-query-report")
		goto unexpected_key;

	root = *root.begin();

	key = "server-timestamp";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	results.server_timestamp = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchResults server_timestamp " << results.server_timestamp;

	key = "blockchain-number";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (BIG64(bigval) != blockchain)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchResults result blockchain " << BIG64(bigval) << " != " << blockchain;

		return -1;
	}

	rc = ParseBlockChainStatus(root, results.blockchain_status);
	if (rc) return rc;

	key = "match-number";
	if (!root.removeMember(key, &value))
		goto missing_key;
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;

	if (BIG64(bigval) != matchnum)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchResults result query xmatchnum " << BIG64(bigval) << " != " << matchnum;

		return -1;
	}

	key = "exchange-match-query-results";
	if (!root.removeMember(key, &value))
		goto missing_key;

	if (!value.isObject())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchResults expected an object";

		return -1;
	}

	if (root.size())
		goto unexpected_key;

	rc = ParseQueryXmatch(value, results.xmatch);
	if (rc) return rc;

	if (results.xmatch.xmatchnum != matchnum)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchResults result xmatchnum " << results.xmatch.xmatchnum << " != " << matchnum;

		return -1;
	}

	return 0;

	unexpected_key:

		key = root.begin().name();

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchResults error unexpected key " << key;

		return -1;

	missing_key:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchResults error missing key " << key;

		return -1;

	parse_error:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::ParseQueryXmatchResults error parsing key " << key << " value " << value.asString();

		return -1;
}

int TxQuery::QueryXmatch(uint64_t blockchain, uint64_t matchnum, QueryXmatchResults &results)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXmatch matchnum " << matchnum;

	int result_code = -1;

	auto rc = tx_query_xmatch_xmatchnum_create(string(), blockchain, matchnum, m_writebuf.data(), m_writebuf.size());
	CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		results.Clear();

		if (g_shutdown) return -1;

		Json::Value root;

		auto rc = SubmitQuery(PowType_Query, 0, i, &root);

		if (rc > 0)
		{
			CCASSERT(m_pread == m_readbuf.data());

			m_pread[80] = 0;

			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXmatch unrecognized response (first 80 bytes): " << m_pread;
		}

		if (rc) continue;

		//BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXmatch response: " << m_pread; // for debugging

		rc = ParseQueryXmatchResults(blockchain, matchnum, root, results);
		if (rc) continue;

		result_code = 0;
		break;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXmatch result " << result_code;

	return result_code;
}

int TxQuery::ParseQueryXminingInfoResults(QueryXreqsMiningInfoResults &results)
{
	Json::Value value;
	const char *key;
	bigint_t bigval;

	string fn;
	char *output = NULL;
	uint32_t outsize = 0;
	int rc;

	if (results.json.size() != 1)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXminingInfo error root size " << results.json.size();

		return -1;
	}

	Json::Value& root = *results.json.begin();

	auto key_str = results.json.begin().name();

	if (key_str != "exchange-mining-info-query-results")
		goto unexpected_key;

	key = "server-timestamp";
	if (!root.isMember(key))
		goto missing_key;
	rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	results.server_timestamp = BIG64(bigval);
	if (TRACE_TXPARAMS) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXminingInfo server_timestamp " << results.server_timestamp;

	key = "blockchain-number";
	if (!root.isMember(key))
		goto missing_key;
	rc = parse_int_value(fn, key, root[key].asString(), 64, 0UL, bigval, output, outsize);
	if (rc) goto parse_error;
	results.blockchain = BIG64(bigval);

	key = "mining-fraction-per-match-maximum";
	if (root.isMember(key))
		ChangeFloatToUniFloatString(root[key]);

	key = "mining-fraction-per-match-minimum";
	if (root.isMember(key))
		ChangeFloatToUniFloatString(root[key]);

	return 0;

	unexpected_key:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXminingInfo error unexpected key " << key_str;

		return -1;

	missing_key:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXminingInfo error missing key " << key;

		return -1;

	parse_error:

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXminingInfo error parsing key " << key << " value " << root[key].asString();

		return -1;
}

int TxQuery::QueryXminingInfo(QueryXreqsMiningInfoResults &results)
{
	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXminingInfo";

	int result_code = -1;

	auto rc = tx_query_xmining_info_create(string(), m_writebuf.data(), m_writebuf.size());
		CCASSERTZ(rc);

	for (int i = 0; i <= g_params.tx_query_retries; ++i)
	{
		results.Clear();

		if (g_shutdown) return -1;

		auto rc = SubmitQuery(PowType_Query, 0, i, &results.json);

		if (rc > 0)
		{
			CCASSERT(m_pread == m_readbuf.data());

			m_pread[80] = 0;

			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXminingInfo unrecognized response (first 80 bytes): " << m_pread;
		}

		if (rc) continue;

		//BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXminingInfo response: " << m_pread; // for debugging

		rc = ParseQueryXminingInfoResults(results);
		if (rc) continue;

		result_code = 0;
		break;
	}

	if (TRACE_TXQUERY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxQuery::QueryXminingInfo result " << result_code;

	return result_code;
}
