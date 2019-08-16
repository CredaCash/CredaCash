/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * walletdb.cpp
*/

#include "ccwallet.h"
#include "walletdb.hpp"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "totals.hpp"

#include <dblog.h>
#include <CCparams.h>

#define TRACE_DBCONN	(g_params.trace_db)

//#define TEST_ENABLE_SQLITE_BUSY			1	// when enabled, mutex problems will result in SQLITE_BUSY errors

#define DB_OPEN_PARAMS			"?cache=private&mode=rw"
#define DB_CREATE_PARAMS		"?cache=private&mode=rwc"

#define IF_NOT_EXISTS_SQL		"if not exists "

#define CREATE_TABLE_SQL		"create table " IF_NOT_EXISTS_SQL
#define CREATE_INDEX_SQL		"create index " IF_NOT_EXISTS_SQL
#define CREATE_INDEX_UNIQUE_SQL	"create unique index " IF_NOT_EXISTS_SQL

//#define REFERENCES_SQL(ref)	"references " ref
#define REFERENCES_SQL(ref)	"integer"

#ifndef TEST_ENABLE_SQLITE_BUSY
#define TEST_ENABLE_SQLITE_BUSY		0	// don't test
#endif

#define DB_TAG			"CredaCash Wallet"
#define DB_SCHEMA		3

#define WALLET_ID_BYTES	(128/8)

//static boost::shared_mutex db_mutex; // v1.69 boost::shared_mutex is buggy--throwing exceptions that exclusive waiter count went negative
static mutex db_mutex;

#if 0
static int db_callback(void* p, int cols, char** vals, char** names)
{
	for (int i = 0; i < cols; ++i)
		printf("%s ", vals[i]);

	printf("\n");

	return 0;
}
#endif

static void OpenDbFile(const char *name, bool createdb, sqlite3** db, bool interactive = false)
{
	string path = boost::locale::conv::utf_to_utf<char>(g_params.app_data_dir);
	path += PATH_DELIMITER;
	path += name;
	path += ".ccw";

	string file = "file:";
	file += path;

	if (createdb)
		file += DB_CREATE_PARAMS;
	else
		file += DB_OPEN_PARAMS;

	if (interactive)
		BOOST_LOG_TRIVIAL(info) << "DbConn::OpenDbFile file " << file;

	auto rc = sqlite3_open(file.c_str(), db);
	if (dbresult(rc))
	{
		*db = NULL;

		string msg = "Unable to ";
		if (createdb)
			msg += "create";
		else
			msg += "open";
		msg += " wallet data file " + path;
		//msg += " -- ";
		//msg += sqlite3_errstr(rc);

		if (!interactive)
			BOOST_LOG_TRIVIAL(error) << "DbConn::OpenDbFile error " << msg;

		throw runtime_error(msg);
	}

	if (interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Opened wallet file \"" << path << "\"\n" << endl;
	}

	CCASSERTZ(dblog(sqlite3_extended_result_codes(*db, 1)));

	const int infinity = 0x70000000;
	CCASSERTZ(dblog(sqlite3_busy_timeout(*db, infinity)));	// so we never get an SQLITE_BUSY error

	CCASSERTZ(dbexec(*db, "PRAGMA foreign_keys = ON;"));
	CCASSERTZ(dbexec(*db, "PRAGMA page_size = 4096;"));
	CCASSERTZ(dbexec(*db, "PRAGMA synchronous = EXTRA;"));
	CCASSERTZ(dbexec(*db, "PRAGMA journal_mode = TRUNCATE;"));

	if (TEST_ENABLE_SQLITE_BUSY)
	{
		BOOST_LOG_TRIVIAL(warning) << "SQLITE_BUSY \"database is locked\" return value are enabled";

		CCASSERTZ(dblog(sqlite3_busy_timeout(*db, 0)));			// must do this last in the initialization sequence so the above statements don't return SQLITE_BUSY
	}
}

void DbConn::Startup(bool createdb, bool resetdb)
{
	BOOST_LOG_TRIVIAL(debug) << "DbConn::Startup createdb " << createdb << " resetdb " << resetdb;

	CCASSERT(sqlite3_threadsafe());
	CCASSERTZ(dblog(sqlite3_config(SQLITE_CONFIG_MULTITHREAD)));

	CCASSERTZ(dblog(sqlite3_config(SQLITE_CONFIG_MEMSTATUS, 0)));
	CCASSERTZ(dblog(sqlite3_config(SQLITE_CONFIG_URI, 1)));
	CCASSERTZ(dblog(sqlite3_initialize()));

	if (createdb)
	{
		OpenDbFile(g_params.wallet_file.c_str(), true, &Wallet_db);

		CreateTables();

		InitDb();

		CloseDb();
	}

	OpenDb(true);

	CheckDb();

	if (resetdb)
		ResetDb();
}

void DbConn::CreateTables()
{
	BOOST_LOG_TRIVIAL(debug) << "DbConn::CreateTables";

	// key-value store for persistent parameters
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Parameters (Key integer not null, Subkey integer not null, Value blob, primary key (Key, Subkey)) without rowid;"));

	// accounts
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Accounts (Id integer primary key not null, Name varchar not null) --without rowid;")); // integer primary key is rowid
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Accounts_Name_Index on Accounts (Name);"));
	CCASSERTZ(dbexec(Wallet_db, "insert or ignore into Accounts values (0, '');"));

	// secrets, destinations (receiving and sending) and addresses (receiving and sending)
	// store the highest level secrets known with their params and the next dependent number, if applicable, for example, store:
	//		root_secret + next spend_secret_number
	//		spend_secret + spend_secret_number
	//		receive_secret + packed_params
	//		pre-destination + packed_params + next destnum
	//		destination + next destnum
	//		pre-address + destination_chain + next paynum
	//		address + paynum
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Secrets (Id integer primary key not null, Parent " REFERENCES_SQL("Secrets(id)") ", Type integer not null, DestinationId " REFERENCES_SQL("Secrets(id)") ", AccountId " REFERENCES_SQL("Accounts(id)") ", PackedParams blob, Number integer, BlockChain integer, Secret blob not null, Label varchar, CreateTime integer not null, FirstReceive integer, LastReceive integer, LastCheck integer, NextCheck integer, QueryCommitnum integer, ExpectedCommitnum integer) --without rowid;")); // integer primary key is rowid
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Secrets_Destination_Index on Secrets (DestinationId) where DestinationId not null;"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Secrets_Secret_Index on Secrets (Secret);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Secrets_Label_Index on Secrets (Label) where Label not null;"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Secrets_AccountId_Index on Secrets (AccountId);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Secrets_CheckTime_Index on Secrets (NextCheck) where NextCheck not null;"));

	// transactions
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Transactions (Id integer primary key not null, Parent " REFERENCES_SQL("Transactions(id)") ", Type integer not null, Status integer not null, CreateTime integer not null, BtcBlockLevel integer, Donation blob, Body varchar) --without rowid;")); // integer primary key is rowid
	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Transactions_Parent_Index on Transactions (Parent) where Parent not null;"));
	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Transactions_Status_Index on Transactions (Status, Id);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Transactions_BlockLevel_Index on Transactions (BtcBlockLevel, Id);"));

	// billets
	// status = {pending, available, allocated, spent}
	#define AMOUNT_ZERO "X'00000000000000000000000000000000'"
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Billets (Id integer primary key not null, Status integer not null, Flags integer not null, CreateTx " REFERENCES_SQL("Transactions(Id)") " not null, DestinationId " REFERENCES_SQL("Secrets(id)") " not null, Blockchain integer not null, Address blob not null, Pool integer not null, Asset integer not null, AmountFp integer not null, Amount blob not null, DelayTime integer not null, CommitIv blob not null, Commitment blob not null, Commitnum integer, Serialnum blob) --without rowid;")); // integer primary key is rowid
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_CreateTx_Index on Billets (CreateTx);"));
	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_Destination_Index on Billets (DestinationId);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Billets_Txid_Index on Billets (Address, substr(Commitment,1," STRINGIFY(TXID_COMMITMENT_BYTES) ")) where not (Flags & " STRINGIFY(BILL_FLAG_NO_TXID) ");")); // could add commitnum to this index to allow dupe txid's
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_Amount_Index on Billets (Asset, Blockchain, Amount, DelayTime) where" /*Amount > " AMOUNT_ZERO " and*/ " Status = " STRINGIFY(BILL_STATUS_CLEARED) ";"));
	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_Pending_Index on Billets (Id) where Status = " STRINGIFY(BILL_STATUS_PENDING) ";"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_PreAllocated_Index on Billets (Id) where Status = " STRINGIFY(BILL_STATUS_PREALLOCATED) ";"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_Allocated_Index on Billets (Id) where Status = " STRINGIFY(BILL_STATUS_ALLOCATED) ";"));

	// it's possible to create a Tx that spends a billet, have that Tx fail because one of the Tx inputs was already spent somewhere else,
	// and then want to spend the billet again in another Tx. In order to handle that without losing the original Tx details,
	// a separate table is created for billet spends
	// Hashkey is the value used in the spend transaction, which is useful as an identifier to detemine which Tx spent the bill
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Billet_Spends (SpendTx " REFERENCES_SQL("Transactions(Id)") " not null, Billet " REFERENCES_SQL("Billets(Id)") " not null, Hashkey blob, primary key (SpendTx, Billet)) without rowid;"));
	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Bill_Spends_Billet_Index on Billet_Spends (Billet);"));

	// balances and amounts received for accounts and destinations
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Totals (Type integer not null, Reference integer, Asset integer not null, Blockchain integer not null, DelayTime integer not null, Total blob not null, primary key (Type, Reference, Asset, DelayTime desc, Blockchain)) --without rowid;")); // use rowid for any future indexes

	BOOST_LOG_TRIVIAL(debug) << "DbConn::CreateTables done";
}

DbConn::DbConn(bool open)
{
	memset(this, 0, sizeof(*this));		// zero all pointers

	if (open)
		OpenDb();
}

DbConn::~DbConn()
{
	CloseDb();
}

void DbConn::OpenDb(bool interactive)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::OpenDb dbconn " << (uintptr_t)this;

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	lock_guard<mutex> lock(db_mutex);

	CCASSERTZ(Wallet_db);

	OpenDbFile(g_params.wallet_file.c_str(), false, &Wallet_db, interactive);

	PrepareDbConn();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::DbConn OpenDb done " << (uintptr_t)this;
}

void DbConn::PrepareDbConn()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::PrepareDbConn dbconn " << (uintptr_t)this;

	CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, "begin;", -1, &db_begin_read, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, "begin immediate;", -1, &db_begin_write, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, "end;", -1, &db_commit, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, "rollback;", -1, &db_rollback, NULL)));

	const string insert_sql = "insert into ";
	const string replace_sql = "insert or replace into ";
	const string _update_sql = "update ";
	const string _select_sql = "select ";

	{
		const string table_sql = "Parameters";
		const string columns_sql = "Key, Subkey, Value";
		const string values_sql = "(?1, ?2, ?3)";
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (replace_sql + table_sql + " (" + columns_sql + ") values " + values_sql + ";").c_str(), -1, &Parameters_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, "select Value from Parameters where Key = ?1 and Subkey = ?2;", -1, &Parameters_select, NULL)));
	}

	{
		const string table_sql = "Accounts";
		const string columns_sql = "Id, Name";
		const string values_sql = "(?1, ?2)";
		const string update_sql = _update_sql + table_sql + " set (" + columns_sql + ") = " + values_sql;
		const string select_sql = _select_sql + columns_sql + " from " + table_sql;
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (insert_sql + table_sql + " (" + columns_sql + ") values " + values_sql + ";").c_str(), -1, &Accounts_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (update_sql + " where Id = ?1;").c_str(), -1, &Accounts_update, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Id >= ?1 order by Id limit 1;").c_str(), -1, &Accounts_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Name = ?1;").c_str(), -1, &Accounts_select_name, NULL)));
	}

	{
		const string table_sql = "Secrets";
		const string columns_sql = "Id, Parent, Type, DestinationId, AccountId, PackedParams, Number, BlockChain, Secret, Label, CreateTime, FirstReceive, LastReceive, LastCheck, NextCheck, QueryCommitnum, ExpectedCommitnum";
		const string values_sql = "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)";
		const string update_sql = _update_sql + table_sql + " set (" + columns_sql + ") = " + values_sql;
		const string select_sql = _select_sql + columns_sql + " from " + table_sql;
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (insert_sql + table_sql + " (" + columns_sql + ") values " + values_sql + ";").c_str(), -1, &Secrets_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (update_sql + " where Id = ?1;").c_str(), -1, &Secrets_update, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Id >= ?1 order by Id limit 1;").c_str(), -1, &Secrets_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Secret = ?1;").c_str(), -1, &Secrets_select_value, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where DestinationId >= ?1 and Id >= ?2 order by DestinationId, Id limit 1;").c_str(), -1, &Secrets_select_destination, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where NextCheck <= ?1 order by NextCheck limit 1;").c_str(), -1, &Secrets_select_next_check, NULL)));
	}

	{
		const string table_sql = "Transactions";
		const string columns_sql = "Id, Parent, Type, Status, CreateTime, BtcBlockLevel, Donation, Body";
		const string values_sql = "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)";
		const string update_sql = _update_sql + table_sql + " set (" + columns_sql + ") = " + values_sql;
		const string select_sql = _select_sql + columns_sql + " from " + table_sql;
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (insert_sql + table_sql +" (" + columns_sql + ") values " + values_sql + ";").c_str(), -1, &Transactions_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (update_sql + " where Id = ?1;").c_str(), -1, &Transactions_update, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Id >= ?1 and Id >= " STRINGIFY(TX_ID_MINIMUM) " order by Id limit 1;").c_str(), -1, &Transactions_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Id <= ?1 order by Id desc limit 1;").c_str(), -1, &Transactions_id_descending_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where BtcBlockLevel >= ?1 and Id >= ?2 and Id >= " STRINGIFY(TX_ID_MINIMUM) " order by BtcBlockLevel, Id limit 1;").c_str(), -1, &Transactions_level_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where BtcBlockLevel <= ?1 and Id <= ?2 order by BtcBlockLevel desc, Id desc limit 1;").c_str(), -1, &Transactions_level_descending_select, NULL)));
	}

	{
		const string table_sql = "Billets";
		const string columns_sql = "Id, Status, Flags, CreateTx, DestinationId, Blockchain, Address, Pool, Asset, AmountFp, Amount, DelayTime, CommitIv, Commitment, Commitnum, Serialnum";
		const string values_sql = "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)";
		const string update_sql = _update_sql + table_sql + " set (" + columns_sql + ") = " + values_sql;
		const string select_sql = _select_sql + columns_sql + " from " + table_sql;
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (insert_sql + table_sql + " (" + columns_sql + ") values " + values_sql + ";").c_str(), -1, &Billets_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (update_sql + " where Id = ?1;").c_str(), -1, &Billets_update, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Id >= ?1 order by Id limit 1;").c_str(), -1, &Billets_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where not (Flags & " STRINGIFY(BILL_FLAG_NO_TXID) ") and Address = ?1 and substr(Commitment,1," STRINGIFY(TXID_COMMITMENT_BYTES) ") = ?2;").c_str(), -1, &Billets_select_txid, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Status = " STRINGIFY(BILL_STATUS_CLEARED) " and Asset = ?1 and Blockchain = ?2 and Amount >= ?3" /*and Amount > " AMOUNT_ZERO*/ " and DelayTime <= ?4 order by Amount limit 1;").c_str(), -1, &Billets_select_amount, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Status = " STRINGIFY(BILL_STATUS_CLEARED) " and Asset = ?1 and Blockchain = ?2 and Amount >= ?3" /*and Amount > " AMOUNT_ZERO*/ " and (Id >= ?4 or Amount > ?3) order by Amount, Id limit 1;").c_str(), -1, &Billets_select_amount_scan, NULL))); // has a funny query plan, so limit use of this query; currently used only by diagnostic RPC listunspent
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Status = " STRINGIFY(BILL_STATUS_CLEARED) " and Asset = ?1 and Blockchain = ?2" /*and Amount > " AMOUNT_ZERO*/ " and DelayTime <= ?3 order by Amount desc limit 1;").c_str(), -1, &Billets_select_amount_max, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where CreateTx = ?1 order by Id limit ?2;").c_str(), -1, &Billets_select_createtx, NULL))); // note: order by id required so output billet ordering is maintained when tx's are saved and read
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (_select_sql + columns_sql + ", Hashkey from " + table_sql + ", Billet_Spends on Billet = Id where SpendTx = ?1 limit ?2;").c_str(), -1, &Billets_select_spendtx, NULL)));
	}

	{
		const string table_sql = "Billet_Spends";
		const string columns_sql = "SpendTx, Billet, Hashkey";
		const string values_sql = "(?1, ?2, ?3)";
		const string update_sql = _update_sql + table_sql + " set (" + columns_sql + ") = " + values_sql;
		const string select_sql = _select_sql + columns_sql + " from " + table_sql;
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (insert_sql + table_sql + " (" + columns_sql + ") values " + values_sql + ";").c_str(), -1, &Billet_Spends_insert, NULL)));
	}

	{
		const string table_sql = "Totals";
		const string columns_sql = "Type, Reference, Asset, DelayTime, Blockchain, Total";
		const string values_sql = "(?1, ?2, ?3, ?4, ?5, ?6)";
		const string select_sql = _select_sql + columns_sql + " from " + table_sql;
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (replace_sql + table_sql + " (" + columns_sql + ") values " + values_sql + ";").c_str(), -1, &Totals_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + " where Type = ?1 and Reference = ?2 and Asset = ?3 and DelayTime = ?4 and Blockchain >= ?5 order by Blockchain limit 1;").c_str(), -1, &Totals_select, NULL)));
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::PrepareDbConn dbconn done " << (uintptr_t)this;
}

void DbConn::InitDb()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::InitDb dbconn " << uintptr_t(this);

	bigint_t wallet_id;
	CCRandom(&wallet_id, WALLET_ID_BYTES);

	string sql, insert("insert or ignore into Parameters values (");

	sql = insert + STRINGIFY(DB_KEY_SCHEMA) ",0,'" DB_TAG "');";
	//cerr << sql << endl;
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

	sql = insert + STRINGIFY(DB_KEY_SCHEMA) ",1," STRINGIFY(DB_SCHEMA) ");";
	//cerr << sql << endl;
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

	sql = insert + STRINGIFY(DB_KEY_WALLET_ID) ",0,X'" + buf2hex(&wallet_id, WALLET_ID_BYTES, 0) + "');";
	//cerr << sql << endl;
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

	sql = insert + STRINGIFY(DB_KEY_BLOCKCHAIN) ",0,X'" + buf2hex(&g_params.blockchain, sizeof(g_params.blockchain), 0) + "');";
	//cerr << sql << endl;
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));
}

#define CHECKDATA_BUFSIZE 100

void DbConn::CheckDb()
{
	BOOST_LOG_TRIVIAL(trace) << "DbConn::CheckDb dbconn " << uintptr_t(this);

	static const char db_tag[] = DB_TAG;
	static const char db_schema[] = STRINGIFY(DB_SCHEMA);

	char tag[CHECKDATA_BUFSIZE];
	char schema[CHECKDATA_BUFSIZE];

	auto rc = ParameterSelect(DB_KEY_SCHEMA, 0, tag, sizeof(tag), true);
	rc |= ParameterSelect(DB_KEY_SCHEMA, 1, schema, sizeof(schema), true);

	if (rc)
		throw runtime_error("Unable to read wallet schema");

	//cerr << "tag " << buf2hex(tag, sizeof(db_tag)) << endl;

	if (strcmp(tag, db_tag) || strcmp(schema, db_schema))
		throw runtime_error("Not a valid wallet file");

	auto blockchain = g_params.blockchain;
	blockchain = -1;

	rc = ParameterSelect(DB_KEY_BLOCKCHAIN, 0, &blockchain, sizeof(blockchain));
	if (rc)
		throw runtime_error("Unable to read wallet blockchain");

	if (blockchain != g_params.blockchain)
	{
		cerr << "ERROR: Configuration blockchain = " << g_params.blockchain << ", but wallet blockchain = " << blockchain << endl;

		throw runtime_error("Chosen blockchain does not match wallet file blockchain");
	}
}

void DbConn::ResetDb()
{
	BOOST_LOG_TRIVIAL(trace) << "DbConn::ResetDb dbconn " << uintptr_t(this);

	CCASSERTZ(dbexec(Wallet_db, "update Billets set Status = " STRINGIFY(BILL_STATUS_PENDING) " where Status = " STRINGIFY(BILL_STATUS_PREALLOCATED) ";"));

	CCASSERTZ(dbexec(Wallet_db, "update Billets set Status = " STRINGIFY(BILL_STATUS_CLEARED) " where Status = " STRINGIFY(BILL_STATUS_ALLOCATED) ";"));

	ostringstream query;
	query << "update Totals set Total = X'00' where Type & " << TOTAL_TYPE_PA_BITS << ";";
	auto str = query.str();

	CCASSERTZ(dbexec(Wallet_db, str.c_str()));

	//cerr << str << endl;
	//cerr << sqlite3_extended_errcode(Wallet_db) << endl;
	//cerr << sqlite3_errmsg(Wallet_db) << endl;
}

void DbConn::ReadWalletId()
{
	g_params.wallet_id = 0UL;

	auto rc = ParameterSelect(DB_KEY_WALLET_ID, 0, &g_params.wallet_id, WALLET_ID_BYTES);
	if (rc)
		throw runtime_error("Unable to read wallet id");

	BOOST_LOG_TRIVIAL(trace) << "DbConn::ReadWalletId wallet_id " << hex << g_params.wallet_id << dec;
}

int DbConn::Exec(const char* sql)
{
	return dbexec(Wallet_db, sql);
}

void DbConn::CloseDb(bool done)
{
	if (!Wallet_db)
		return;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::CloseDb dbconn " << (uintptr_t)this;

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	lock_guard<mutex> lock(db_mutex);

	bool explain = done && 0; // for testing

	//if (explain)
	//	CCASSERTZ(dbexec(Wallet_db, "analyze;"));

	DbFinalize(db_begin_read, explain);
	DbFinalize(db_begin_write, explain);
	DbFinalize(db_commit, explain);
	DbFinalize(db_rollback, explain);

	DbFinalize(Parameters_insert, explain);
	DbFinalize(Parameters_select, explain);

	DbFinalize(Accounts_insert, explain);
	DbFinalize(Accounts_update, explain);
	DbFinalize(Accounts_select, explain);
	DbFinalize(Accounts_select_name, explain);

	DbFinalize(Secrets_insert, explain);
	DbFinalize(Secrets_update, explain);
	DbFinalize(Secrets_select, explain);
	DbFinalize(Secrets_select_value, explain);
	DbFinalize(Secrets_select_destination, explain);
	DbFinalize(Secrets_select_next_check, explain);

	DbFinalize(Transactions_insert, explain);
	DbFinalize(Transactions_update, explain);
	DbFinalize(Transactions_select, explain);
	DbFinalize(Transactions_id_descending_select, explain);
	DbFinalize(Transactions_level_select, explain);
	DbFinalize(Transactions_level_descending_select, explain);

	DbFinalize(Billets_insert, explain);
	DbFinalize(Billets_update, explain);
	DbFinalize(Billets_select, explain);
	DbFinalize(Billets_select_txid, explain);
	DbFinalize(Billets_select_amount, explain);
	DbFinalize(Billets_select_amount_scan, explain);
	DbFinalize(Billets_select_amount_max, explain);
	DbFinalize(Billets_select_createtx, explain);
	DbFinalize(Billets_select_spendtx, explain);
	DbFinalize(Billet_Spends_insert, explain);

	DbFinalize(Totals_insert, explain);
	DbFinalize(Totals_select, explain);

	if (done)
		dbexec(Wallet_db, "PRAGMA journal_mode = DELETE;");

	dblog(sqlite3_close_v2(Wallet_db));

	Wallet_db = NULL;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::CloseDb done dbconn " << (uintptr_t)this;
}

void DbConn::DoDbFinish()
{
	if (RandTest(RTEST_DELAY_DB_RESET)) sleep(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::DoDbFinish dbconn " << uintptr_t(this);

	sqlite3_reset(Parameters_insert);
	sqlite3_reset(Parameters_select);

	sqlite3_reset(Accounts_insert);
	sqlite3_reset(Accounts_update);
	sqlite3_reset(Accounts_select);
	sqlite3_reset(Accounts_select_name);

	sqlite3_reset(Secrets_insert);
	sqlite3_reset(Secrets_update);
	sqlite3_reset(Secrets_select);
	sqlite3_reset(Secrets_select_value);
	sqlite3_reset(Secrets_select_destination);
	sqlite3_reset(Secrets_select_next_check);

	sqlite3_reset(Transactions_insert);
	sqlite3_reset(Transactions_update);
	sqlite3_reset(Transactions_select);
	sqlite3_reset(Transactions_id_descending_select);
	sqlite3_reset(Transactions_level_select);
	sqlite3_reset(Transactions_level_descending_select);

	sqlite3_reset(Billets_insert);
	sqlite3_reset(Billets_update);
	sqlite3_reset(Billets_select);
	sqlite3_reset(Billets_select_txid);
	sqlite3_reset(Billets_select_amount);
	sqlite3_reset(Billets_select_amount_scan);
	sqlite3_reset(Billets_select_amount_max);
	sqlite3_reset(Billets_select_createtx);
	sqlite3_reset(Billets_select_spendtx);
	sqlite3_reset(Billet_Spends_insert);

	sqlite3_reset(Totals_insert);
	sqlite3_reset(Totals_select);
}

void DbConn::DoDbFinishTx(int rollback)
{
	if (RandTest(RTEST_DELAY_DB_RESET)) sleep(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::DoDbFinishTx dbconn " << uintptr_t(this) << " rollback " << rollback;

	sqlite3_reset(db_begin_read);
	sqlite3_reset(db_begin_write);
	sqlite3_reset(db_commit);
	sqlite3_reset(db_rollback);

	if (rollback)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::DoDbFinishTx dbconn " << uintptr_t(this) << " rollback";

		auto rc = sqlite3_step(db_rollback);
		if (rollback > 0)
			dblog(rc, DB_STMT_STEP);
		sqlite3_reset(db_rollback);
	}
}

int DbConn::BeginRead()
{
	// acquire lock -- might solve intermittent sqlite errors
	//lock_guard<boost::shared_mutex> lock(db_mutex);
	lock_guard<mutex> lock(db_mutex);

	if (0 && TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BeginRead starting db read transaction";

	if (dblog(sqlite3_step(db_begin_read), DB_STMT_STEP))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BeginRead error starting db read transaction";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BeginRead started db read transaction";

	return 0;
}

int DbConn::BeginWrite()
{
	// acquire lock -- might solve intermittent sqlite errors
	//lock_guard<boost::shared_mutex> lock(db_mutex);
	lock_guard<mutex> lock(db_mutex);

	if (0 && TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BeginWrite starting db write transaction";

	if (dblog(sqlite3_step(db_begin_write), DB_STMT_STEP))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BeginWrite error starting db write transaction";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::BeginWrite started db write transaction";

	return 0;
}

int DbConn::Commit()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::Commit committing db transaction";

	if (dblog(sqlite3_step(db_commit), DB_STMT_STEP))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::Commit error committing db transaction";

		return -1;
	}

	return 0;
}

#if TEST_DEBUG_WRITE_LOCKING

int DbConn::TestDebugWriteLocking(bool lock_optional)
{
	if (lock_optional)
		return 1;

	sqlite3_reset(db_begin_write);

	auto rc = sqlite3_step(db_begin_write);

	BOOST_LOG_TRIVIAL(trace) << "DbConn::TestDebugWriteLocking rc = " << rc;

	return dbiserr(rc, DB_STMT_STEP);
}
#endif

#if 0

static int test_callback(void *NotUsed, int argc, char **argv, char **colname)
{
	for (int i = 0; i < argc; i++)
		cerr << colname[i] << " = " << (argv[i] ? argv[i] : "NULL") << endl;

	return 0;
}

static void test_exec(sqlite3 *db, const char *sql)
{
	char *errmsg = 0;

	cerr << ">>> " << sql << endl;

	auto rc = sqlite3_exec(db, sql, test_callback, 0, &errmsg);

	cerr << "<<< " << sql << " returned " << rc << " = \"" << (errmsg ? errmsg : "") << "\"" << endl;
}

void DbConn::LockTest()
{
	test_exec(Wallet_db, "PRAGMA lock_status;");

	test_exec(Wallet_db, "begin;");
	test_exec(Wallet_db, "PRAGMA lock_status;");
	test_exec(Wallet_db, "end;");
	test_exec(Wallet_db, "PRAGMA lock_status;");

	test_exec(Wallet_db, "begin deferred;");
	test_exec(Wallet_db, "PRAGMA lock_status;");
	test_exec(Wallet_db, "end;");
	test_exec(Wallet_db, "PRAGMA lock_status;");

	test_exec(Wallet_db, "begin immediate;");
	test_exec(Wallet_db, "PRAGMA lock_status;");
	test_exec(Wallet_db, "end;");
	test_exec(Wallet_db, "PRAGMA lock_status;");

	test_exec(Wallet_db, "begin exclusive;");
	test_exec(Wallet_db, "PRAGMA lock_status;");
	test_exec(Wallet_db, "end;");
	test_exec(Wallet_db, "PRAGMA lock_status;");
}

static void TestThreadProc()
{
	auto dbconn = new DbConn;

	dbconn->TestConcurrent();
}

void DbConn::TestConcurrency()
{
	uint32_t val = 0;
	CCASSERTZ(ParameterInsert(0, 0, &val, sizeof(val), 1));

	vector<thread> threads;

	{
		// hold threads until they are all ready to run
		//lock_guard<boost::shared_mutex> lock(db_mutex);
		lock_guard<mutex> lock(db_mutex);

		for (unsigned i = 0; i < 10; ++i)
			threads.emplace_back(TestThreadProc);
	}

	for (auto t = threads.begin(); t != threads.end(); ++t)
		t->join();

	CCASSERTZ(ParameterSelect(0, 0, &val, sizeof(val)));

	cerr << "TestConcurrency ending val " << val << endl;
}

void DbConn::TestConcurrent()
{
	{
		// hold threads here until they are all ready to run
		//lock_guard<boost::shared_mutex> lock(db_mutex);
		lock_guard<mutex> lock(db_mutex);
	}

	unsigned n = 500;
	uint32_t val, va2;

	while (n)
	{
		switch (rand() & 15)
		{
		case 0:

			ParameterSelect(0, 0, &val, sizeof(val));

			break;

		case 1:

			ParameterInsert(1, 0, &val, sizeof(val), 1);

			break;

		case 2:

			// for read-only, "begin [deferred]" works fine

			CCASSERTZ(dbexec(Wallet_db, "begin;"));

			ParameterSelect(0, 0, &val, sizeof(val));

			CCASSERTZ(dbexec(Wallet_db, "end;"));

			break;

		case 3:
		{
			Finally finally(boost::bind(&DbConn::DoDbFinishTx, this, 1));		// 1 = rollback

			dblog(sqlite3_step(db_begin_read), DB_STMT_STEP);

			ParameterSelect(0, 0, &val, sizeof(val));

			break;
		}

		case 4:

			// read-then-write, requires "begin immediate"
			//	this immediately acquires a reserved lock, which prevents two threads from starting the "begin" at the same time
			//	"begin deferred" results in SQLITE_BUSY errors when writing
			//	makes sense because 2 threads can get read locks but one must then back out of the "begin" for the other to write

			CCASSERTZ(dbexec(Wallet_db, "begin immediate;"));

			ParameterSelect(0, 0, &val, sizeof(val));

			--n;
			++val;

			ParameterInsert(0, 0, &val, sizeof(val)); if (RandTest(2)) {
			ParameterSelect(0, 0, &va2, sizeof(va2)); CCASSERT(va2 == val); }

			CCASSERTZ(dbexec(Wallet_db, "end;"));

			break;

		case 5:
		{
			Finally finally(boost::bind(&DbConn::DoDbFinishTx, this, 1));		// 1 = rollback

			dblog(sqlite3_step(db_begin_write), DB_STMT_STEP);

			ParameterSelect(0, 0, &val, sizeof(val));

			--n;
			++val;

			ParameterInsert(0, 0, &val, sizeof(val)); if (RandTest(2)) {
			ParameterSelect(0, 0, &va2, sizeof(va2)); CCASSERT(va2 == val); }

			dblog(sqlite3_step(db_commit), DB_STMT_STEP);

			DoDbFinishTx();

			finally.Clear();

			break;
		}

		case 6:

			// rollback test

			CCASSERTZ(dbexec(Wallet_db, "begin immediate;"));

			val *= 99;

			ParameterInsert(0, 0, &val, sizeof(val)); if (RandTest(2)) {
			ParameterSelect(0, 0, &va2, sizeof(va2)); CCASSERT(va2 == val); }

			CCASSERTZ(dbexec(Wallet_db, "rollback;"));

			break;

		case 7:
		{
			Finally finally(boost::bind(&DbConn::DoDbFinishTx, this, 1));		// 1 = rollback

			dblog(sqlite3_step(db_begin_write), DB_STMT_STEP);

			val *= 99;

			ParameterInsert(0, 0, &val, sizeof(val)); if (RandTest(2)) {
			ParameterSelect(0, 0, &va2, sizeof(va2)); CCASSERT(va2 == val); }

			break;
		}

		}
	}
}

#endif
