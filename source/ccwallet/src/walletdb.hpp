/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * walletdb.hpp
*/

#pragma once

#define DB_KEY_SCHEMA			0
#define DB_KEY_WALLET_ID		1
#define DB_KEY_BLOCKCHAIN		2
#define DB_KEY_CHANGE_DEST		3
#define DB_KEY_UNIQUE_REFID		4
#define DB_KEY_TOTAL_MINED		5

#define DB_TX_COLS		12
#define DB_REQ_COLS		37
#define DB_MATCH_COLS	21

#include <CCobjdefs.h>
#include <CCbigint.hpp>

#include <sqlite/sqlite3.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>
//#include <boost/thread/shared_mutex.hpp>	// seems to have bugs (throws random exceptions)
#include <boost/thread/locks.hpp>

//#define TEST_EXPLAIN_DB_QUERIES	1

//#define RTEST_DB_ERRORS			64
//#define RTEST_DELAY_DB_RESET		32

#ifndef TEST_EXPLAIN_DB_QUERIES
#define TEST_EXPLAIN_DB_QUERIES		0	// don't test
#endif

#ifndef RTEST_DELAY_DB_RESET
#define RTEST_DELAY_DB_RESET		0	// don't test
#endif

#ifndef RTEST_DB_ERRORS
#define RTEST_DB_ERRORS				0	// don't test
#endif

class Transaction;
class Billet;
class Secret;
class Account;
class Total;
class Xmatchreq;
class Xmatch;

class DbConn
{
	sqlite3 *Wallet_db;

	sqlite3_stmt *db_begin_read;
	sqlite3_stmt *db_begin_write;
	sqlite3_stmt *db_commit;
	sqlite3_stmt *db_rollback;

	sqlite3_stmt *Parameters_insert;
	sqlite3_stmt *Parameters_select;

	sqlite3_stmt *Accounts_insert;
	sqlite3_stmt *Accounts_select;
	sqlite3_stmt *Accounts_select_name;

	sqlite3_stmt *Secrets_insert;
	sqlite3_stmt *Secrets_select;
	sqlite3_stmt *Secrets_select_value;
	sqlite3_stmt *Secrets_select_destination;
	sqlite3_stmt *Secrets_select_next_poll;

	sqlite3_stmt *Transactions_insert;
	sqlite3_stmt *Transactions_select;
	sqlite3_stmt *Transactions_select_refid;
	sqlite3_stmt *Transactions_select_id_descending;
	sqlite3_stmt *Transactions_select_objid_descending_id;
	sqlite3_stmt *Transactions_select_level;
	sqlite3_stmt *Transactions_select_level_descending;

	sqlite3_stmt *Billets_insert;
	sqlite3_stmt *Billets_select;
	sqlite3_stmt *Billets_select_txid;
	sqlite3_stmt *Billets_select_commitnum;
	sqlite3_stmt *Billets_select_unspent;
	sqlite3_stmt *Billets_select_amount;
	sqlite3_stmt *Billets_select_amount_scan;
	sqlite3_stmt *Billets_select_amount_max;
	sqlite3_stmt *Billets_select_createtx;
	sqlite3_stmt *Billets_select_spendtx;
	sqlite3_stmt *Billet_Spends_insert;
	sqlite3_stmt *Billet_Spends_select_billet;

	sqlite3_stmt *Totals_insert;
	sqlite3_stmt *Totals_select;

	sqlite3_stmt *Exchange_Requests_insert;
	sqlite3_stmt *Exchange_Requests_status_update;
	sqlite3_stmt *Exchange_Requests_polling_update;
	sqlite3_stmt *Exchange_Requests_polling_update_txid;
	sqlite3_stmt *Exchange_Requests_select;
	sqlite3_stmt *Exchange_Requests_select_id_descending;
	sqlite3_stmt *Exchange_Requests_select_txid;
	sqlite3_stmt *Exchange_Requests_select_xreqnum;
	sqlite3_stmt *Exchange_Requests_select_next_poll;
	sqlite3_stmt *Exchange_Requests_select_pending;

	sqlite3_stmt *Exchange_Matches_insert;
	sqlite3_stmt *Exchange_Matches_select;
	sqlite3_stmt *Exchange_Matches_select_status;
	sqlite3_stmt *Exchange_Matches_select_deadline;
	sqlite3_stmt *Exchange_Matches_select_next_poll;
	sqlite3_stmt *Exchange_Matches_select_pending;

	int AccountSelect(sqlite3_stmt *select, Account& account, bool expect_row = false, uint64_t required_id = 0);
	int SecretSelect(sqlite3_stmt *select, Secret& secret, bool expect_row = false, uint64_t required_id = 0);
	int TransactionSelectInternal(sqlite3_stmt *select, Transaction& tx, bool expect_row = false, uint64_t required_id = 0, int cs = 0);
	int TransactionSelect(sqlite3_stmt *select, Transaction& tx, bool expect_row = false, uint64_t required_id = 0, int cs = 0);
	int BilletSelect(sqlite3_stmt *select, bool has_spend, Billet& bill, bool expect_row = false, uint64_t required_id = 0);
	int BilletSelectMulti(sqlite3_stmt *select, bool has_spend, unsigned &nbills, Billet *bills, const unsigned maxbills, bool expect_row = false);
	int BilletSpendSelect(sqlite3_stmt *select, uint64_t &tx_id, uint64_t &bill_id, snarkfront::bigint_t *hashkey, uint64_t *tx_commitnum, bool expect_row, uint64_t required_id);
	int TotalSelect(sqlite3_stmt *select, Total& total);

	int ExchangeRequestSelectInternal(sqlite3_stmt *select, Xmatchreq& req, Transaction *tx, int cs, bool expect_row, uint64_t required_id, uint64_t required_tx_id = 0, uint64_t required_reqnum = 0);
	int ExchangeRequestSelect(sqlite3_stmt *select, Xmatchreq& req, Transaction *tx = NULL, bool expect_row = false, uint64_t required_id = 0, uint64_t required_tx_id = 0, uint64_t required_reqnum = 0);

	int ExchangeMatchSelectInternal(sqlite3_stmt *select, Xmatch& match, int cs, bool expect_row, uint64_t required_num);
	int ExchangeMatchSelect(sqlite3_stmt *select, Xmatch& match, bool need_reqs = false, Transaction *txbuy = NULL, Transaction *txsell = NULL, bool expect_row = false, uint64_t required_num = 0);

	static void CheckSchemaUpdateOption();

public:
	DbConn(bool open = true);
	~DbConn();

	void Startup(bool createdb);
	int CreateTables();
	void CreateTransactionsTable(const string table_name);
	void CreateExchangeTables();
	void CreateIndexes();
	void InitDb();
	int CheckDb(bool post_create);
	void ReadWalletId();
	void OpenDb();
	void PrepareDbConnParameters();
	void PrepareDbConn();
	void CloseDb(bool done = false);

	int BackupDb(const char *name);

	#if 0
	void LockTest();
	void TestConcurrency();
	void TestConcurrent();
	#endif

	int Exec(const char* sql);

	int BeginRead();
	int BeginWrite();
	int Commit();

	int GetTxnState()
	{
		return sqlite3_txn_state(Wallet_db, NULL);
	}

	void DoDbFinish();
	void DoDbFinishTx(int rollback = false);

	int ParameterInsert(int key, int subkey, void *value, unsigned valsize, bool lock_optional = false);
	int ParameterSelect(int key, int subkey, void *value, unsigned bufsize, bool add_terminator = false, unsigned *retsize = NULL, bool expect_row = true);

	int AccountInsert(Account& account, bool lock_optional = false);
	int AccountSelectId(uint64_t id, Account& account, bool or_greater = false);
	int AccountSelectName(const char *name, unsigned size, Account& account);

	int SecretInsert(Secret& secret, bool lock_optional = false);
	int SecretSelectId(uint64_t id, Secret& secret, bool or_greater = false);
	int SecretSelectSecret(const void *value, unsigned size, Secret& secret);
	int SecretSelectDestination(uint64_t dest_id, uint64_t id, Secret& secret);
	int SecretSelectNextPoll(uint64_t checktime, Secret& secret);

	void TransactionInitDb();
	int TransactionInsert(Transaction& tx, bool lock_optional = false);
	int TransactionSelectId(uint64_t id, Transaction& tx, bool or_greater = false);
	int TransactionSelectRefId(const char* ref_id, Transaction& tx);
	int TransactionSelectIdDescending(uint64_t id, Transaction& tx);
	int TransactionSelectObjIdDescendingId(const ccoid_t& objid, uint64_t id, Transaction& tx);
	int TransactionSelectLevel(uint64_t level, uint64_t last_id, Transaction& tx);
	int TransactionSelectLevelDescending(uint64_t level, uint64_t id, Transaction& tx);

	int BilletInsert(Billet& bill, bool lock_optional = false);
	int BilletSelectId(uint64_t id, Billet& bill, bool or_greater = false);
	int BilletSelectTxid(const void *address, const void *commitment, Billet& bill);
	int BilletSelectCommitnum(uint64_t commitnum, Billet& bill);
	int BilletSelectUnspent(const snarkfront::bigint_t& amount, uint64_t last_id, Billet& bill);
	int BilletSelectAmount(uint64_t blockchain, uint64_t asset, const snarkfront::bigint_t& amount, unsigned delaytime, Billet& bill);
	int BilletSelectAmountScan(uint64_t blockchain, uint64_t asset, const snarkfront::bigint_t& amount, uint64_t last_id, Billet& bill);
	int BilletSelectAmountMax(uint64_t blockchain, uint64_t asset, unsigned delaytime, Billet& bill);
	int BilletSelectCreateTx(uint64_t id, unsigned &nbills, Billet *bills, const unsigned maxbills);
	int BilletSelectSpendTx(uint64_t id, unsigned &nbills, Billet *bills, const unsigned maxbills);
	int BilletsResetAllocated(bool zero_balance);

	int BilletSpendInsert(uint64_t id, uint64_t bill_id, const void *hashkey, uint64_t tx_commitnum = 0);
	int BilletSpendSelectBillet(uint64_t bill_id, uint64_t &tx_id, snarkfront::bigint_t *hashkey = NULL, uint64_t *tx_commitnum = NULL);

	int TotalInsert(const Total& total, bool lock_optional = false);
	int TotalSelectMatch(bool exact, Total& total);

	int ExchangeRequestInsert(Xmatchreq& req, bool lock_optional = false);
	int ExchangeRequestUpdateStatus(Xmatchreq& req, bool lock_optional = false);
	int ExchangeRequestUpdatePolling(uint64_t id, bool by_txid, uint64_t poll_time);
	int ExchangeRequestSelectId(uint64_t id, Xmatchreq& req, Transaction *tx = NULL, bool or_greater = false);
	int ExchangeRequestSelectIdDescending(uint64_t id, Xmatchreq& req, Transaction *tx = NULL);
	int ExchangeRequestSelectTxId(uint64_t tx_id, uint64_t id, Xmatchreq& req, Transaction *tx = NULL);
	int ExchangeRequestSelectXreqnum(uint64_t xreqnum, Xmatchreq& req, Transaction *tx = NULL);
	int ExchangeRequestSelectNextPoll(uint64_t checktime, Xmatchreq& req, Transaction *tx = NULL);
	int ExchangeRequestsSumPending(uint64_t base_asset, uint64_t quote_asset, const string& foreign_asset, double total_pending[5]);

	int ExchangeMatchInsert(const Xmatch& match, bool lock_optional = false);
	int ExchangeMatchSelectNum(uint64_t matchnum, Xmatch& match, bool need_reqs = false, Transaction *txbuy = NULL, Transaction *txsell = NULL, bool or_greater = false);
	int ExchangeMatchSelectDeadline(uint64_t next_deadline, uint64_t matchnum, Xmatch& match, bool need_reqs = false, Transaction *txbuy = NULL, Transaction *txsell = NULL);
	int ExchangeMatchSelectNextPoll(uint64_t lastblocktime, Xmatch& match, bool need_reqs = false, Transaction *txbuy = NULL, Transaction *txsell = NULL);
	int ExchangeMatchSelectStatus(uint64_t reqnum, uint64_t matchnum_start, uint64_t &matchnum);
	int ExchangeMatchesSumPending(uint64_t base_asset, uint64_t quote_asset, const string& foreign_asset, double total_pending[5]);
};

void DbExplainQueryPlan(const string& explainer, sqlite3_stmt *pStmt);
void DbFinalize(sqlite3_stmt *pStmt, bool explain);
