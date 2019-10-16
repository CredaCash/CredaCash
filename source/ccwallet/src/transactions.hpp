/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * transactions.hpp
*/

#pragma once

#include "amounts.h"

#include <CCparams.h>

#define TX_TYPE_VOID			0
#define TX_TYPE_MINT			1
#define TX_TYPE_SEND			2
#define TX_TYPE_MOVE			3
#define TX_TYPE_INVALID			4

#define TX_STATUS_VOID			0
#define TX_STATUS_ERROR			1
#define TX_STATUS_CONFLICTED	2
#define TX_STATUS_ABANDONED		3
#define TX_STATUS_MOVED			4
#define TX_STATUS_PENDING		5
#define TX_STATUS_CLEARED		6
#define TX_STATUS_INVALID		7

#define TX_ID_MINIMUM			2	// parent_id = 1 in a parent tx

class Billet;
class Secret;
class Account;
struct TxPay;

class DbConn;
class TxQuery;
class TxParams;
class TxBuildEntry;
class QueryAddressResult;

class Transaction
{
	enum build_type_t
	{
		TX_BUILD_TYPE_NULL = 0,
		TX_BUILD_FINAL,
		TX_BUILD_SPLIT,
		TX_BUILD_CONSOLIDATE
	};

	enum build_state_t
	{
		TX_BUILD_STATE_NULL = 0,
		TX_BUILD_READY,
		TX_BUILD_TOTALED,
		TX_BUILD_SAVED,
		TX_BUILD_SUBMIT_INVALID,
		TX_BUILD_SUBMIT_UNKNOWN,
		TX_BUILD_SUBMIT_OK,
	};

public:
	uint64_t id;
	uint64_t parent_id;
	uint64_t param_level;
	uint64_t create_time;
	uint64_t btc_block;
	build_type_t build_type;	// not saved in db
	build_state_t build_state;	// not saved in db
	unsigned type;
	unsigned status;
	unsigned nout;
	unsigned nin;
	snarkfront::bigint_t donation;
	array<snarkfront::bigint_t, TX_MAXOUT> adj_amounts;			// computed = net transaction amount reported by gettransaction
	array<snarkfront::bigint_t, TX_MAXOUT> adj_donations;		// computed = portion of donation allocated this each txout
	string body;	// the constructor memset's to zero all class members above this one
	string ref_id;
	array<Billet, TX_MAXOUT> output_bills;
	array<Secret, TX_MAXOUT> output_destinations;
	array<Account, TX_MAXOUT> output_accounts;
	array<Billet, TX_MAXIN> input_bills;
	array<Secret, TX_MAXIN> input_destinations;
	array<Account, TX_MAXIN> input_accounts;

private:
	int we_sent[2];										// computed
	int inputs_involve_watchonly;						// computed

	void CalcWeSent(bool incwatch);

	void SetMintStatus(DbConn *dbconn, unsigned _status);

	static int LocateBillet(DbConn *dbconn, uint64_t blockchain, const snarkfront::bigint_t& amount, const snarkfront::bigint_t& min_amount, Billet& bill, uint64_t& billet_count, const bigint_t& total_required);
	static void ReleaseTxBillet(DbConn *dbconn, Billet& bill);

	static int WaitNewBillet(const snarkfront::bigint_t& total_required, const uint64_t billet_count, const uint32_t timeout, unique_lock<mutex>& lock, bool test_fail = false);

	static void CheckTimeout(const uint32_t timeout);

	int ComputeChange(TxParams& txparams, const snarkfront::bigint_t& input_total);

	static void SetPendingBalances(DbConn *dbconn, uint64_t blockchain, const snarkfront::bigint_t& balance_allocated, const snarkfront::bigint_t& balance_pending);

	int FillOutTx(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, uint64_t dest_chain, TxPay& tx);
	void SetAddresses(DbConn *dbconn, uint64_t dest_chain, Secret &destination, TxPay& tx);

	bool SubTxIsActive(bool need_intermediate_txs) const;

	static void CreateTxPayThread(DbConn *dbconn, TxQuery* txquery, TxParams txparams, TxBuildEntry *entry, Secret secret);
	static void CreateTxPayThreadCleanup(DbConn **dbconn, TxQuery **txquery, TxBuildEntry **entry, bool dec_thread_count);
	static void SaveErrorTx(DbConn *dbconn, const string& ref_id);

	int DoCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams &txparams, TxBuildEntry *entry, Secret& secret);
	static int TryCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, Secret &destination, const uint32_t timeout, snarkfront::bigint_t& round_up, deque<Transaction>& tx_list, unique_lock<mutex>& lock);
	static void CleanupSubTxs(DbConn *dbconn, uint64_t dest_chain, const Secret &destination, deque<Transaction>& tx_list, unique_lock<mutex>& lock);
	void CleanupSubTx(DbConn *dbconn, const Secret &destination, bool need_intermediate_txs, snarkfront::bigint_t& balance_allocated, snarkfront::bigint_t& balance_pending);

public:
	Transaction();

	static void Shutdown();

	void Clear();
	void Copy(const Transaction& other);
	string DebugString() const;

	static bool TypeIsValid(unsigned type)
	{
		return type > TX_TYPE_VOID && type < TX_TYPE_INVALID;
	}

	static bool StatusIsValid(unsigned status)
	{
		return status > TX_STATUS_VOID && status < TX_STATUS_INVALID;
	}

	bool IsValid() const;

	static bool StatusIsNotError(unsigned status)
	{
		return status > TX_STATUS_ERROR && status < TX_STATUS_INVALID;
	}

	bool StatusIsNotError() const
	{
		return StatusIsNotError(status);
	}

	static bool TxCouldClear(unsigned status)
	{
		return status == TX_STATUS_PENDING || status == TX_STATUS_ABANDONED;
	}

	bool TxCouldClear() const
	{
		return TxCouldClear(status);
	}

	string EncodeInternalTxid() const;
	static string EncodeBtcTxid(uint64_t dest_chain, const snarkfront::bigint_t& address, const snarkfront::bigint_t& commitment);
	static int DecodeBtcTxid(const string& txid, uint64_t& dest_chain, snarkfront::bigint_t& address, snarkfront::bigint_t& commitment);

	string GetBtcTxid() const;

	void SetOutputsFromTx(const TxPay& tx);
	int SaveOutgoingTx(DbConn *dbconn);
	int UpdatePolling(DbConn *dbconn, uint64_t next_commitnum);

	int BeginAndReadTx(DbConn *dbconn, uint64_t id, bool or_greater = false);
	int BeginAndReadTxRefId(DbConn *dbconn, const string& ref_id);
	int BeginAndReadTxIdDescending(DbConn *dbconn, uint64_t id);
	int BeginAndReadTxLevel(DbConn *dbconn, uint64_t level, uint64_t id);

	int ReadTx(DbConn *dbconn, uint64_t id, bool or_greater = false);
	int ReadTxRefId(DbConn *dbconn, const string& ref_id);
	int ReadTxIdDescending(DbConn *dbconn, uint64_t id);
	int ReadTxLevel(DbConn *dbconn, uint64_t level, uint64_t id);
	int ReadTxBillets(DbConn *dbconn);

	bool WeSent(bool incwatch);
	bool InputsInvolveWatchOnly();

	void SetAdjustedAmounts(bool incwatch);

	int CreateTxMint(DbConn *dbconn, TxQuery& txquery);
	int CreateTxPay(DbConn *dbconn, TxQuery& txquery, bool async, string& ref_id, const string& encoded_dest, uint64_t dest_chain, const snarkfront::bigint_t& destination, const snarkfront::bigint_t& amount, const string& comment, const string& comment_to, const bool subfee);

	int CreateTxFromAddressQueryResult(DbConn *dbconn, TxQuery& txquery, const Secret& destination, const Secret& address, QueryAddressResult &result, bool duplicate_txid);

	int UpdateStatus(DbConn *dbconn, uint64_t bill_id, uint64_t commitnum);
	int CheckConflicts(DbConn *dbconn);
	static int SetConflicted(DbConn *dbconn, uint64_t tx_id);
};
