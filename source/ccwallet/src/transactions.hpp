/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * transactions.hpp
*/

#pragma once

#include <CCparams.h>
#include <CCobjdefs.h>
#include <amounts.h>

#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"

// Transaction statuses as stored in the Wallet DB

#define TX_STATUS_VOID			0
#define TX_STATUS_ERROR			1	// permanent: tx creation encountered an error
#define TX_STATUS_CONFLICTED	2	// permanent: a conflicting tx included in indelible block
#define TX_STATUS_ABANDONED		3	// transitory: abandoned tx may later clear or become conflicted
#define TX_STATUS_RESERVED		4	// reserved for expired?
#define TX_STATUS_PENDING		5	// transitory: pending tx may clear, become conflicted, or be abandoned
#define TX_STATUS_CLEARED		6	// permanent: tx included in indelible block
#define TX_STATUS_MOVED			7	// not yet implemented
#define TX_STATUS_INVALID		TX_STATUS_MOVED

#define TX_ID_MINIMUM			2	// parent_id = 1 in a parent tx

class Xtx;
struct TxPay;

class DbConn;
class TxQuery;
class TxParams;
class TxBuildEntry;
class QueryAddressResult;

class Transaction
{
public:
	enum build_mode_t
	{
		TX_MODE_NORMAL = 0,
		TX_MODE_ASYNC,
		TX_MODE_PREPARE,	// don't send
		TX_MODE_BROADCAST	// send prepared tx
	};

	enum build_type_t
	{
		TX_BUILD_TYPE_NULL = 0,
		TX_BUILD_FINAL,
		TX_BUILD_SPLIT,
		TX_BUILD_CONSOLIDATE,
		TX_BUILD_CANCEL_TX
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

	uint64_t id;
	uint64_t parent_id;
	uint64_t blockchain;
	uint64_t param_level;
	uint64_t create_time;
	uint64_t btc_block;
	Xtx		 *xtx;
	build_type_t build_type;	// not saved in db
	build_state_t build_state;	// not saved in db
	build_mode_t build_mode;		// not saved in db
	unsigned type;
	unsigned status;
	unsigned nout;
	unsigned nin;
	uint16_t have_objid;
	ccoid_t objid;
	snarkfront::bigint_t donation;
	snarkfront::bigint_t change;// not saved in db
	snarkfront::bigint_t split;	// not saved in db
	array<snarkfront::bigint_t, TX_MAXOUT> adj_amounts;			// computed = net transaction amount reported by gettransaction
	array<snarkfront::bigint_t, TX_MAXOUT> adj_donations;		// computed = portion of donation allocated this each txout
	string ref_id;	// the constructor memset's to zero all class members above this one
	vector<char> txbody;
	vector<char> wire_data;
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

	void SetObjId(DbConn *dbconn);
	void SetStatus(DbConn *dbconn, unsigned tx_status, unsigned out_bill_status = 0);

	static int LocateBillet(DbConn *dbconn, uint64_t blockchain, const snarkfront::bigint_t& amount, const snarkfront::bigint_t& min_amount, const snarkfront::bigint_t& total_required, Billet& bill, uint64_t& billet_count);
	void ReleaseBillets(DbConn *dbconn, int start, int count);

	static int WaitNewBillet(const snarkfront::bigint_t& total_required, const uint64_t billet_count, const uint64_t timeout, bool test_fail = false);

	static void CheckTimeout(const uint64_t timeout);

	bigint_t SubTxAmount() const;

	void ResetNout(unsigned min = 0);
	void ComputeDonation(TxParams& txparams, bigint_t& donation) const;
	int ComputeChange(TxParams& txparams, const snarkfront::bigint_t& carry_in = 0UL, const snarkfront::bigint_t& carry_out = 0UL, snarkfront::bigint_t *shortfall = NULL);

	static void SetPendingBalances(DbConn *dbconn, uint64_t blockchain, const snarkfront::bigint_t& balance_allocated, const snarkfront::bigint_t& balance_pending);

	int FillOutTx(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, uint64_t dest_chain, TxPay& ts);
	void SetAddresses(DbConn *dbconn, uint64_t dest_chain, Secret &destination, TxPay& ts);

	static uint64_t GetExpireTime(TxParams& txparams, TxBuildEntry *entry);

	bool SubTxIsActive(bool need_intermediate_txs) const;

	static void CreateTxPayThread(DbConn *dbconn, TxQuery* txquery, TxParams txparams, TxBuildEntry *entry, Secret secret);
	static void CreateTxPayThreadCleanup(DbConn **dbconn, TxQuery **txquery, TxBuildEntry **entry, bool dec_thread_count);
	static void SaveErrorTx(DbConn *dbconn, const string& ref_id, unsigned type);

	int DoCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, Secret& secret);
	static void CleanupSubTxs(DbConn *dbconn, uint64_t dest_chain, const Secret& destination, deque<Transaction>& tx_list);
	void CleanupSubTx(DbConn *dbconn, const Secret& destination, bool need_intermediate_txs, snarkfront::bigint_t& balance_allocated, snarkfront::bigint_t& balance_pending);

	typedef int TryCreateFn(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, Secret &destination, const uint64_t timeout, snarkfront::bigint_t& round_up, deque<Transaction>& tx_list);
	static TryCreateFn TryCreateBareTx;
	static TryCreateFn TryCreateTxPay;

	static int StartCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, const uint64_t timeout, bigint_t& tx_round_up, bool test_fail, deque<Transaction>& tx_list, bool &need_intermediate_txs);
	static int FinishCreateTxPay(DbConn *dbconn, TxQuery& txquery, TxParams& txparams, TxBuildEntry *entry, Secret &destination, const uint64_t timeout, bool test_fail, deque<Transaction>& tx_list, bool need_intermediate_txs);

	static void ComputeSplitTx(deque<Transaction>& tx_list, TxParams& txparams, const snarkfront::bigint_t& total_required, snarkfront::bigint_t& total_change);
	int FinishSplitTx(TxParams& txparams, build_type_t build_type, const snarkfront::bigint_t& output);

	int TrySubmitTx(TxQuery& txquery, TxPay *ts, uint64_t expire_time, uint64_t &next_commitnum, const string& report_ref_id, int test_fail = -1, unsigned si = 1, unsigned active_subtx_count = 1, bool need_intermediate_txs = false);
	static void ThrowSubmitTxException(int rc);

public:
	Transaction();

	static void Shutdown();

	void Clear();
	void Copy(const Transaction& other);
	string DebugString() const;

	static string TypeString(unsigned type);

	string TypeString() const
	{
		return TypeString(type);
	}

	static string StatusString(unsigned status);

	string StatusString() const
	{
		return StatusString(status);
	}

	static bool TypeIsValid(unsigned type)
	{
		return type > CC_TYPE_VOID && type < CC_TYPE_INVALID && type != CC_TYPE_BLOCK;
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
	static int DecodeInternalTxid(const string& txid, uint64_t& id);

	static string EncodeBtcTxid(uint64_t dest_chain, const snarkfront::bigint_t& address, const snarkfront::bigint_t& commitment);
	static int DecodeBtcTxid(const string& txid, uint64_t& id, uint64_t& dest_chain, snarkfront::bigint_t& address, snarkfront::bigint_t& commitment);

	string GetBtcTxid() const;

	void AppendTxBody(TxPay& ts) const;
	void FinishCreateTx(TxPay& ts, TxParams& txparams, TxBuildEntry *entry = NULL, unsigned retry = 0);
	int SaveOutgoingTx(DbConn *dbconn);
	int UpdatePolling(DbConn *dbconn, uint64_t next_commitnum);

	int BeginAndReadTx(DbConn *dbconn, uint64_t id, bool or_greater = false);
	int BeginAndReadTxRefId(DbConn *dbconn, const string& ref_id);
	int BeginAndReadTxIdDescending(DbConn *dbconn, uint64_t id);
	int BeginAndReadTxLevel(DbConn *dbconn, uint64_t level, uint64_t last_id);

	int ReadTx(DbConn *dbconn, uint64_t id, bool or_greater = false);
	int ReadTxRefId(DbConn *dbconn, const string& ref_id);
	int ReadTxIdDescending(DbConn *dbconn, uint64_t id);
	int ReadTxLevel(DbConn *dbconn, uint64_t level, uint64_t last_id);
	int ReadTxBillets(DbConn *dbconn);

	bool WeSent(bool incwatch);
	bool InputsInvolveWatchOnly();

	void SetAdjustedAmounts(bool incwatch, bigint_t amount_carry_in = 0UL, bigint_t amount_carry_out = 0UL);

	int CreateTxMint(DbConn *dbconn, TxQuery& txquery);
	int CreateTxPay(DbConn *dbconn, TxQuery& txquery, int mode, string& ref_id, unsigned type, const string& encoded_dest, uint64_t dest_chain, const snarkfront::bigint_t& destination, const snarkfront::bigint_t& amount, const string& comment, const string& comment_to, const bool subfee, Xtx *xtx = NULL);

	int CreateConflictTx(DbConn *dbconn, TxQuery& txquery, const Billet& input);

	int CreateTxFromAddressQueryResult(DbConn *dbconn, TxQuery& txquery, const Secret& destination, const Secret& address, QueryAddressResult &result, bool duplicate_txid);

	int SendPreparedTx(DbConn *dbconn, TxQuery& txquery, TxParams& txparams);

	int UpdateStatus(DbConn *dbconn, uint64_t bill_id, uint64_t commitnum);
	static int SetConflicted(DbConn *dbconn, uint64_t tx_id);
	static void AbandonTx(DbConn *dbconn, uint64_t tx_id, uint64_t dest_chain = 0);
};
