/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * walletdb.hpp
*/

#pragma once

#define DB_KEY_SCHEMA			0
#define DB_KEY_WALLET_ID		1
#define DB_KEY_BLOCKCHAIN		2
#define DB_KEY_CHANGE_DEST		3

#include <CCbigint.hpp>

#include <sqlite/sqlite3.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/locks.hpp>

//!#define TEST_DEBUG_WRITE_LOCKING	1
//!#define RTEST_DELAY_DB_RESET		32
//#define RTEST_DB_ERRORS			64

#ifndef TEST_DEBUG_WRITE_LOCKING
#define TEST_DEBUG_WRITE_LOCKING	0	// don't test
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
	sqlite3_stmt *Accounts_update;
	sqlite3_stmt *Accounts_select;
	sqlite3_stmt *Accounts_select_name;

	sqlite3_stmt *Secrets_insert;
	sqlite3_stmt *Secrets_update;
	sqlite3_stmt *Secrets_select;
	sqlite3_stmt *Secrets_select_value;
	sqlite3_stmt *Secrets_select_destination;
	sqlite3_stmt *Secrets_select_next_check;

	sqlite3_stmt *Transactions_insert;
	sqlite3_stmt *Transactions_update;
	sqlite3_stmt *Transactions_select;
	sqlite3_stmt *Transactions_id_descending_select;
	sqlite3_stmt *Transactions_level_select;
	sqlite3_stmt *Transactions_level_descending_select;

	sqlite3_stmt *Billets_insert;
	sqlite3_stmt *Billets_update;
	sqlite3_stmt *Billets_select;
	sqlite3_stmt *Billets_select_txid;
	sqlite3_stmt *Billets_select_amount;
	sqlite3_stmt *Billets_select_amount_scan;
	sqlite3_stmt *Billets_select_amount_max;
	sqlite3_stmt *Billets_select_createtx;
	sqlite3_stmt *Billets_select_spendtx;
	sqlite3_stmt *Billet_Spends_insert;

	sqlite3_stmt *Totals_insert;
	sqlite3_stmt *Totals_select;

	int AccountSelect(sqlite3_stmt *select, Account& account, bool expect_row = false, uint64_t required_id = 0);
	int SecretSelect(sqlite3_stmt *select, Secret& secret, bool expect_row = false, uint64_t required_id = 0);
	int TransactionSelect(sqlite3_stmt *select, Transaction& tx, bool expect_row = false, uint64_t required_id = 0);
	int BilletSelect(sqlite3_stmt *select, bool has_hashkey, Billet& bill, bool expect_row = false, uint64_t required_id = 0);
	int BilletSelectMulti(sqlite3_stmt *select, bool has_hashkeys, unsigned &nbills, Billet *bills, const unsigned maxbills, bool expect_row = false);
	int TotalSelect(sqlite3_stmt *select, Total& total);

public:
	DbConn(bool open = true);
	~DbConn();

	void Startup(bool createdb, bool resetdb);
	void CreateTables();
	void InitDb();
	void CheckDb();
	void ReadWalletId();
	void ResetDb();
	void OpenDb(bool interactive = false);
	void PrepareDbConn();
	void CloseDb(bool done = false);

	#if 0
	void LockTest();
	void TestConcurrency();
	void TestConcurrent();
	#endif

	#if TEST_DEBUG_WRITE_LOCKING
	int TestDebugWriteLocking(bool lock_optional);
	#else
	#define TestDebugWriteLocking(x) (1)
	#endif

	int Exec(const char* sql);

	int BeginRead();
	int BeginWrite();
	int Commit();

	void DoDbFinish();
	void DoDbFinishTx(int rollback = false);

	int ParameterInsert(int key, int subkey, void *value, unsigned valsize, bool lock_optional = false);
	int ParameterSelect(int key, int subkey, void *value, unsigned bufsize, bool add_terminator = false, unsigned *retsize = NULL);

	int AccountInsert(Account& account, bool lock_optional = false);
	int AccountSelectId(uint64_t id, Account& account, bool or_greater = false);
	int AccountSelectName(const char *name, unsigned size, Account& account);

	int SecretInsert(Secret& secret, bool lock_optional = false);
	int SecretSelectId(uint64_t id, Secret& secret, bool or_greater = false);
	int SecretSelectSecret(const void *value, unsigned size, Secret& secret);
	int SecretSelectDestination(uint64_t dest_id, uint64_t next_id, Secret& secret);
	int SecretSelectNextCheck(uint64_t checktime, Secret& secret);

	void TransactionInitDb();
	int TransactionInsert(Transaction& tx, bool lock_optional = false);
	int TransactionSelectId(uint64_t id, Transaction& tx, bool or_greater = false);
	int TransactionSelectIdDescending(uint64_t id, Transaction& tx);
	int TransactionSelectLevel(uint64_t level, uint64_t id, Transaction& tx);
	int TransactionSelectLevelDescending(uint64_t level, uint64_t id, Transaction& tx);

	int BilletInsert(Billet& bill, bool lock_optional = false);
	int BilletSelectId(uint64_t id, Billet& bill, bool or_greater = false);
	int BilletSelectTxid(const void *address, const void *commitment, Billet& bill);
	int BilletSelectAmount(uint64_t blockchain, uint64_t asset, const snarkfront::bigint_t& amount, unsigned delaytime, Billet& bill);
	int BilletSelectAmountScan(uint64_t blockchain, uint64_t asset, const snarkfront::bigint_t& amount, uint64_t id, Billet& bill);
	int BilletSelectAmountMax(uint64_t blockchain, uint64_t asset, unsigned delaytime, Billet& bill);
	int BilletSelectCreateTx(uint64_t id, unsigned &nbills, Billet *bills, const unsigned maxbills);
	int BilletSelectSpendTx(uint64_t id, unsigned &nbills, Billet *bills, const unsigned maxbills);
	int BilletSpendInsert(uint64_t id, uint64_t bill_id, const void *hashkey);

	int TotalInsert(const Total& total, bool lock_optional = false);
	int TotalSelectMatch(bool exact, Total& total);
};

void DbExplainQueryPlan(sqlite3_stmt *pStmt);
void DbFinalize(sqlite3_stmt *pStmt, bool explain);

extern mutex g_db_explain_lock;
