/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * walletdb.cpp
*/

#include "ccwallet.h"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "walletdb.hpp"

#include <CCparams.h>
#include <amounts.h>
#include <xmatch.hpp>
#include <dblog.h>

#define TRACE_DB_READ	(g_params.trace_db_reads)
#define TRACE_DB_WRITE	(g_params.trace_db_writes)

//#define TEST_ENABLE_SQLITE_BUSY	1	// when enabled, mutex problems will result in SQLITE_BUSY errors
//!#define RTEST_CUZZ				64

#define DB_OPEN_PARAMS			"?cache=private&mode=rw"
#define DB_CREATE_PARAMS		"?cache=private&mode=rwc"

#define IF_NOT_EXISTS_SQL		"if not exists "
#define CREATE_TABLE_SQL		"create table " IF_NOT_EXISTS_SQL
#define ALTER_TABLE_SQL			"alter table "
#define CREATE_INDEX_SQL		"create index " IF_NOT_EXISTS_SQL
#define CREATE_INDEX_UNIQUE_SQL	"create unique index " IF_NOT_EXISTS_SQL
#define DROP_INDEX_SQL			"drop index if exists "

//#define REFERENCES_SQL(ref)	"references " ref
#define REFERENCES_SQL(ref)		"integer"

#ifndef TEST_ENABLE_SQLITE_BUSY
#define TEST_ENABLE_SQLITE_BUSY		0	// don't test
#endif

#ifndef RTEST_CUZZ
#define RTEST_CUZZ					0	// don't test
#endif

#define DB_TAG			"CredaCash Wallet"
#define DB_SCHEMA		7

//static boost::shared_mutex db_mutex; // v1.69 boost::shared_mutex is buggy--throwing exceptions that exclusive waiter count went negative
static mutex db_mutex;

using namespace snarkfront;

#if 0
static int db_callback(void* p, int cols, char** vals, char** names)
{
	for (int i = 0; i < cols; ++i)
		printf("%s ", vals[i]);

	printf("\n");

	return 0;
}
#endif

static void OpenDbFile(const char *name, bool createdb, sqlite3** db, bool interactive = false, bool backup = false)
{
	string path;

	if (!backup || name[0] == '.' || (!strchr(name, '/') && !strchr(name, '\\')))
	{
		path = boost::locale::conv::utf_to_utf<char>(g_params.app_data_dir);
		path += PATH_DELIMITER;
	}

	path += name;
	path += ".ccw";
	if (backup)
		path += ".bak";

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

		if (!interactive && !backup)
			BOOST_LOG_TRIVIAL(error) << "DbConn::OpenDbFile error " << msg;

		throw runtime_error(msg);
	}

	if (interactive)
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Opened wallet file \"" << path << "\"\n" << endl;
	}

	CCASSERTZ(dblog(sqlite3_extended_result_codes(*db, 1)));

	const int infinity = 0x70000000;
	CCASSERTZ(dblog(sqlite3_busy_timeout(*db, infinity)));	// so we never get an SQLITE_BUSY error

	CCASSERTZ(dbexec(*db, "PRAGMA encoding = 'UTF-8';"));
	CCASSERTZ(dbexec(*db, "PRAGMA foreign_keys = ON;"));
	CCASSERTZ(dbexec(*db, "PRAGMA page_size = 4096;"));
	CCASSERTZ(dbexec(*db, "PRAGMA journal_mode = PERSIST;"));
	CCASSERTZ(dbexec(*db, "PRAGMA synchronous = NORMAL;"));

	if (TEST_ENABLE_SQLITE_BUSY)
	{
		BOOST_LOG_TRIVIAL(warning) << "SQLITE_BUSY \"database is locked\" return value are enabled";

		CCASSERTZ(dblog(sqlite3_busy_timeout(*db, 0)));			// must do this last in the initialization sequence so the above statements don't return SQLITE_BUSY
	}
}

void DbConn::Startup(bool createdb)
{
	BOOST_LOG_TRIVIAL(info) << "DbConn::Startup createdb " << createdb;

	CCASSERT(sqlite3_threadsafe());
	CCASSERTZ(dblog(sqlite3_config(SQLITE_CONFIG_MULTITHREAD)));

	CCASSERTZ(dblog(sqlite3_config(SQLITE_CONFIG_MEMSTATUS, 0)));
	CCASSERTZ(dblog(sqlite3_config(SQLITE_CONFIG_URI, 1)));
	CCASSERTZ(dblog(sqlite3_initialize()));

	if (createdb)
	{
		OpenDbFile(g_params.wallet_file.c_str(), true, &Wallet_db, true);

		if (!CreateTables())
		{
			InitDb();
		}

		CloseDb();
	}

	for (unsigned i = 0; ;++i)
	{
		OpenDbFile(g_params.wallet_file.c_str(), false, &Wallet_db, i == 0 && !createdb);

		PrepareDbConnParameters();

		if (!CheckDb(true))
			break;

		// retry until schema all updated

		CCASSERTZ(dbexec(Wallet_db, "commit;"));

		CloseDb();

		if (++i > 1000)
			throw runtime_error("Retry error updating wallet schema");
	}

	PrepareDbConn();
}

int DbConn::CreateTables()
{
	BOOST_LOG_TRIVIAL(debug) << "DbConn::CreateTables";

	// key-value store for persistent parameters
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Parameters (Key integer not null, Subkey integer not null, Value blob, primary key (Key, Subkey)) without rowid;"));

	PrepareDbConnParameters();

	if (CheckDb(false))
		return 1;

	// accounts
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Accounts (Id integer primary key not null, Name varchar not null) --without rowid;")); // integer primary key is rowid

	// secrets, destinations (receiving and sending) and addresses (receiving and sending)
	// store the highest level secrets known with their params and the next dependent number, if applicable, for example, store:
	//		root_secret + next spend_secret_number
	//		spend_secret + spend_secret_number
	//		receive_secret + packed_params
	//		pre-destination + packed_params + next destnum
	//		destination + next destnum
	//		pre-address + destination_chain + next paynum
	//		address + paynum
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Secrets (Id integer primary key not null, Parent " REFERENCES_SQL("Secrets(id)") ", Type integer not null, DestinationId " REFERENCES_SQL("Secrets(id)") ", AccountId " REFERENCES_SQL("Accounts(id)") ", PackedParams blob, Number integer, BlockChain integer, Secret blob not null, Label varchar, CreateTime integer not null, FirstReceive integer, LastReceive integer, LastCheck integer, PollTime integer, QueryCommitnum integer, ExpectedCommitnum integer) --without rowid;")); // integer primary key is rowid

	// transactions
	CreateTransactionsTable("Transactions");

	// billets
	// status = {pending, available, allocated, spent}
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Billets (Id integer primary key not null, Status integer not null, Flags integer not null, CreateTx " REFERENCES_SQL("Transactions(Id)") " not null, DestinationId " REFERENCES_SQL("Secrets(id)") " not null, Blockchain integer not null, Address blob not null, Domain integer not null, Asset integer not null, AmountFp integer not null, Amount blob not null, DelayTime integer not null, CommitIv blob not null, Commitment blob not null, Commitnum integer, Serialnum blob) --without rowid;")); // integer primary key is rowid

	// it's possible to create a Tx that spends a billet, have that Tx fail because one of the Tx inputs was already spent somewhere else,
	// and then want to spend the billet again in another Tx. In order to handle that without losing the original Tx details,
	// a separate table is created for billet spends
	// Hashkey is the value used in the spend transaction, which is useful as an identifier to detemine which Tx spent the bill
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Billet_Spends (SpendTx " REFERENCES_SQL("Transactions(Id)") " not null, Billet " REFERENCES_SQL("Billets(Id)") " not null, Hashkey blob, TxCommitnum integer, primary key (SpendTx, Billet)) without rowid;"));

	// balances and amounts received for accounts and destinations
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Totals (Type integer not null, Reference integer, Asset integer not null, DelayTime integer not null, Blockchain integer not null, Total blob not null);")); // rowid is generated by db
	// note: earlier versions of wallet create this compatible schema, which is not updated by CheckDb():
	//CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Totals (Type integer not null, Reference integer, Asset integer not null, Blockchain integer not null, DelayTime integer not null, Total blob not null, primary key (Type, Reference, Asset, DelayTime desc, Blockchain)) --without rowid;")); // use rowid for indexes

	// exchange requests
	CreateExchangeTables();

	BOOST_LOG_TRIVIAL(debug) << "DbConn::CreateTables done";

	return 0;
}

void DbConn::CreateTransactionsTable(const string table_name)
{
	CCASSERTZ(dbexec(Wallet_db, (CREATE_TABLE_SQL + table_name + " (Id integer primary key not null, Parent " REFERENCES_SQL("Transactions(id)") ", Type integer not null, Status integer not null, RefId varchar, Blockchain integer not null /*earlier schema upgrade missing not null*/, ParamLevel integer /*not null in earlier schema*/, CreateTime integer not null, BtcBlockLevel integer, Donation blob, ObjId blob, Body blob /*varchar in earlier schema*/) --without rowid;").c_str())); // integer primary key is rowid
}

void DbConn::CreateExchangeTables()
{
	BOOST_LOG_TRIVIAL(debug) << "DbConn::CreateExchangeTables";

	// exchange requests
	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Exchange_Requests (XId integer primary key not null, "
		"TxId " REFERENCES_SQL("Transactions(id)") " not null, AddressId integer not null, Xreqnum integer not null, QueryXmatchnum integer not null, "
		"Type integer not null, IsBuyer integer check (IsBuyer = 0 or IsBuyer = 1), "
		"Disposition integer not null, ExpireTime integer not null, PollTime integer not null, "
		"BaseAsset integer not null, QuoteAsset integer not null, ForeignAsset string, "
		"MinAmount blob not null, MaxAmount blob not null, "
		"NetRateRequired numeric not null, WaitDiscount numeric not null, BaseCosts numeric not null, QuoteCosts numeric not null, "
		"AddImmediatelyToBlockchain integer not null, AutoAcceptMatches integer not null, NoMinimumAfterFirstMatch integer not null, MustLiquidateCrossingMinimum integer not null, MustLiquidateBelowMinimum integer not null, "
		"ConsiderationRequired integer not null, ConsiderationOffered integer not null, Pledge integer not null, "
		"HoldTime integer not null, HoldTimeRequired integer not null, MinWaitTime integer not null, "
		"AcceptTimeRequired integer not null, AcceptTimeOffered integer not null, "
		"PaymentTime integer not null, Confirmations integer not null, "
		"ForeignAddress blob, " //"Destination blob not null, PubSigningKey blob, "
		"PrivateSigningKey blob, "
		"OpenAmount blob"
		") --without rowid;")); // integer primary key is rowid

	CCASSERTZ(dbexec(Wallet_db, CREATE_TABLE_SQL "Exchange_Matches (Xmatchnum integer primary key not null check(Xmatchnum > 0), "
		"BuyXreqnum integer not null, SellXreqnum integer not null, "
		"Type integer not null, Status integer not null, NextDeadline integer not null, "
		"WalletPaid integer not null, WalletPaymentForeignTxid text, WalletPollTime integer not null, WalletReminderTime integer not null, "
		"MatchTimestamp integer not null, AcceptTimestamp integer not null, FinalTimestamp integer not null, "
		//"BaseAsset integer not null, QuoteAsset integer not null, ForeignAsset string, "
		"BaseAmount blob not null, Rate numeric not null, AmountPaid numeric not null, MiningAmount numeric not null, "
		"AcceptTime integer not null, BuyerConsideration integer not null, SellerConsideration integer not null, "
		"BuyerPledge integer not null) --without rowid;")); // integer primary key is rowid

	BOOST_LOG_TRIVIAL(debug) << "DbConn::CreateExchangeTables done";
}

void DbConn::CreateIndexes()
{
	BOOST_LOG_TRIVIAL(debug) << "DbConn::CreateIndexes";

	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Accounts_Name_Index on Accounts (Name);"));

	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Secrets_Destination_Index on Secrets (DestinationId, Id) where DestinationId not null;"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Secrets_Secret_Index on Secrets (Secret);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Secrets_Label_Index on Secrets (Label) where Label not null;"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Secrets_AccountId_Index on Secrets (AccountId, Id);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Secrets_PollTime_Index on Secrets (PollTime) where PollTime not null;"));

	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Transactions_Parent_Index on Transactions (Parent) where Parent not null;"));
	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Transactions_Status_Index on Transactions (Status, Id);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Transactions_BlockLevel_Index on Transactions (BtcBlockLevel, Id);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Transactions_RefId_Index on Transactions (RefId) where RefId not null;"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Transactions_ObjId_Index on Transactions (ObjId, Id) where ObjId not null;"));
	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Transactions_ParamLevel_Index on Transactions (ParamLevel, Id) where ParamLevel not null and Status = " STRINGIFY(TX_STATUS_PENDING) ";"));

	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_CreateTx_Index on Billets (CreateTx);"));
	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_Destination_Index on Billets (DestinationId, Id);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Billets_Txid_Index on Billets (Address, substr(Commitment,1," STRINGIFY(TXID_COMMITMENT_BYTES) ")) where not (Flags & " STRINGIFY(BILL_FLAG_NO_TXID) ");")); // could add commitnum to this index to allow dupe txid's
	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_Pending_Index on Billets (Id) where Status = " STRINGIFY(BILL_STATUS_PENDING) ";"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Billets_Commitnum_Index on Billets (Commitnum) where Commitnum not null;"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_Unspent_Index on Billets (Amount desc, Id desc) where Status >= " STRINGIFY(BILL_STATUS_PENDING) " and Status <= " STRINGIFY(BILL_STATUS_ALLOCATED) " and Status != " STRINGIFY(BILL_STATUS_SENT) " and Amount > zeroblob(16);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_Amount_Index on Billets (Asset, Blockchain, Amount, Id, DelayTime) where Status = " STRINGIFY(BILL_STATUS_CLEARED) ";"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_PreAllocated_Index on Billets (Id) where Status = " STRINGIFY(BILL_STATUS_PREALLOCATED) ";"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Billets_Allocated_Index on Billets (Id) where Status = " STRINGIFY(BILL_STATUS_ALLOCATED) ";"));

	//CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Bill_Spends_Billet_Index on Billet_Spends (Billet);"));

	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Totals_Index on Totals (Type, Reference, Asset, DelayTime desc, Blockchain);"));

	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Exchange_Requests_TxId_Index on Exchange_Requests (TxId) where TxId != 0;"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_UNIQUE_SQL "Exchange_Requests_Xreqnum_Index on Exchange_Requests (Xreqnum) where Xreqnum != 0;"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Exchange_Requests_PollTime_Index on Exchange_Requests (PollTime) where PollTime > 0;"));

	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Exchange_Matches_BuyXreqnum_Index on Exchange_Matches (BuyXreqnum);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Exchange_Matches_SellXreqnum_Index on Exchange_Matches (SellXreqnum);"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Exchange_Matches_NextDeadline_Index on Exchange_Matches (NextDeadline, Xmatchnum) where NextDeadline > 0;"));
	CCASSERTZ(dbexec(Wallet_db, CREATE_INDEX_SQL "Exchange_Matches_Polling_Index on Exchange_Matches (WalletPollTime, Xmatchnum) where WalletPollTime > 0;"));

	BOOST_LOG_TRIVIAL(debug) << "DbConn::CreateIndexes done";
}

DbConn::DbConn(bool open)
{
	memset((void*)this, 0, sizeof(*this));		// zero all pointers

	if (open)
		OpenDb();
}

DbConn::~DbConn()
{
	CloseDb();
}

void DbConn::OpenDb()
{
	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::OpenDb dbconn " << (uintptr_t)this;

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	lock_guard<mutex> lock(db_mutex);

	//if (RTEST_CUZZ) sleep(1);

	CCASSERTZ(Wallet_db);

	OpenDbFile(g_params.wallet_file.c_str(), false, &Wallet_db);

	PrepareDbConnParameters();

	PrepareDbConn();

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::DbConn OpenDb done " << (uintptr_t)this;
}

/*

General rules for writing queries when integers in db are signed (as in sqlite)

When searching on id-num ascending:

	"id-num >= ?1" is ok in all cases (id-num = INT64_MIN, 0, and INT64_MAX)
		requires incrementing ?1, and therefore difficult to use in compound scan (for example, level and id)
		when ?1 is uint64_t:
			id-num cannot be negative
			loop can terminate post-increment: while (?1 <= INT64_MAX)
		when ?1 is int64_t, loop must terminate pre-increment: if (?1 == INT64_MAX)

	"id-num > ?1" is ok when id/num cannot equal loop start value
		easy to use in compound scan (for example, level and id)
		query will always return no result when ?1 == INT64_MAX, so no loop bound check needed
		search starts at INT64_MIN, or below minimum value in db

When searching on id-num descending:

	"id-num <= ?1" is ok in all cases (id-num = INT64_MIN, 0, and INT64_MAX)
		requires decrementing ?1, and therefore difficult to use in compound scan (for example, level and id)
		when ?1 is uint64_t:
			id-num cannot be negative
			loop must terminate pre-decrement when id-num can equal zero: if (?1 == 0)
			loop can terminate post-decrement when id-num cannot equal zero: while (?1 > 0)
		when ?1 is int64_t, loop must terminate pre-decrement: if (?1 == INT64_MIN)

	"id-num < ?1" is ok when id/num cannot equal loop start value
		easy to use in compound scan (for example, level and id)
		query will always return no result when ?1 == INT64_MIN, so no loop bound check needed
		search starts at INT64_MAX, or above maximum value in db

Conclusions:

- Compound scan ascending when id-num > INT64_MIN
	use >
	search starts at INT64_MIN, or below minimum value in db

- Compound scan descending when id-num < INT64_MAX
	use <
	search starts at INT64_MAX, or above maximum value in db

- Compound scan ascending when id-num can == INT64_MIN:				   --> avoid ascending compound when id-num can == INT64_MIN
	cannot use > and therefore is difficult

- Compound scan descending when id-num can == INT64_MAX:			   --> avoid descending compound when id-num can == INT64_MAX
	cannot use < and therefore is difficult

Non-compound scan can also use < or > by following rules above for compound scan

The below rules are for using <= or >=, which are difficult to use with comound scan

- Scan ascending when id-num >= 0:
	can use >= with uint64_t										<-- PREFERRED for non-compound
		can terminate loop post-increment: while (?1 <= INT64_MAX)

- Scan descending when id-num > 0:
	can use <= with uint64_t										<-- second choice for descending non-compound
		can terminate loop post-increment: while (?1 > 0)

- Scan descending when id-num >= 0:
	can use <= with uint64_t										<-- third choice for descending non-compound when id-num can be zero
		must terminate loop pre-increment: if (?1 == 0)

- Full range:														<-- only choice when id-num can be negative
	use <= or >= with signed int64_t
	for full range: must terminate loop pre-increment/decrement
*/

static const string _insert_sql = "insert into ";
static const string _replace_sql = "insert or replace into ";
static const string _conflict_sql = " on conflict(Id) do update set (";
static const string _update_sql = "update ";
static const string _select_sql = "select ";

static const string transactions_columns_sql = " Id, Parent, Type, Status, RefId, Blockchain, ParamLevel, CreateTime, BtcBlockLevel, Donation, ObjId, Body";

void DbConn::PrepareDbConnParameters()
{
	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::PrepareDbConnParameters dbconn " << (uintptr_t)this;

	const string table_sql = "Parameters";
	const string values_sql = "(?1, ?2, ?3)";
	CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (_replace_sql + table_sql + " values " + values_sql + ";").c_str(), -1, &Parameters_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, "select Value from Parameters where Key = ?1 and Subkey = ?2;", -1, &Parameters_select, NULL)));
}

void DbConn::PrepareDbConn()
{
	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::PrepareDbConn dbconn " << (uintptr_t)this;

	CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, "begin;", -1, &db_begin_read, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, "begin immediate;", -1, &db_begin_write, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, "end;", -1, &db_commit, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, "rollback;", -1, &db_rollback, NULL)));

	{
		const string table_sql = "Accounts";
		const string columns_sql = " Id, Name";
		const string values_sql = "(?1, ?2)";
		const string insert_sql = _insert_sql + table_sql + "(" + columns_sql + ") values " + values_sql + _conflict_sql + columns_sql + ") = " + values_sql + ";";
		const string select_sql = _select_sql + columns_sql + " from " + table_sql + " ";
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (insert_sql).c_str(), -1, &Accounts_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Id >= ?1 order by Id limit 1;").c_str(), -1, &Accounts_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Name = ?1;").c_str(), -1, &Accounts_select_name, NULL)));
	}

	{
		const string table_sql = "Secrets";
		const string columns_sql = " Id, Parent, Type, DestinationId, AccountId, PackedParams, Number, BlockChain, Secret, Label, CreateTime, FirstReceive, LastReceive, LastCheck, PollTime, QueryCommitnum, ExpectedCommitnum";
		const string values_sql = "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)";
		const string insert_sql = _insert_sql + table_sql + "(" + columns_sql + ") values " + values_sql + _conflict_sql + columns_sql + ") = " + values_sql + ";";
		const string select_sql = _select_sql + columns_sql + " from " + table_sql + " ";
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (insert_sql).c_str(), -1, &Secrets_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Id >= ?1 order by Id limit 1;").c_str(), -1, &Secrets_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Secret = ?1;").c_str(), -1, &Secrets_select_value, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where DestinationId not null and (DestinationId, Id) > (?1, ?2) order by DestinationId, Id limit 1;").c_str(), -1, &Secrets_select_destination, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where PollTime <= ?1 order by PollTime limit 1;").c_str(), -1, &Secrets_select_next_poll, NULL)));
	}

	{
		const string table_sql = "Transactions";
		const string columns_sql = transactions_columns_sql;
		const string values_sql = "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)";
		const string insert_sql = _insert_sql + table_sql + "(" + columns_sql + ") values " + values_sql + _conflict_sql + columns_sql + ") = " + values_sql + ";";
		const string select_sql = _select_sql + columns_sql + " from " + table_sql + " where Id >= " STRINGIFY(TX_ID_MINIMUM) " and ";
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (insert_sql).c_str(), -1, &Transactions_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "Id >= ?1 order by Id limit 1;").c_str(), -1, &Transactions_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "RefId = ?1 order by RefId limit 1;").c_str(), -1, &Transactions_select_refid, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "Id <= ?1 order by Id desc limit 1;").c_str(), -1, &Transactions_select_id_descending, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "ObjId = ?1 and Id <= ?2 order by Id desc limit 1;").c_str(), -1, &Transactions_select_objid_descending_id, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "(BtcBlockLevel, Id) > (?1, ?2) order by BtcBlockLevel, Id limit 1;").c_str(), -1, &Transactions_select_level, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "(BtcBlockLevel, Id) < (?1, ?2) order by BtcBlockLevel desc, Id desc limit 1;").c_str(), -1, &Transactions_select_level_descending, NULL)));
	}

	{
		// Note: Amount is a blob, so it must be stored big endian with leading zero in order for < and > operators to work
		const string table_sql = "Billets";
		const string columns_sql = " Id, Status, Flags, CreateTx, DestinationId, Blockchain, Address, Domain, Asset, AmountFp, Amount, DelayTime, CommitIv, Commitment, Commitnum, Serialnum";
		const string values_sql = "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)";
		const string insert_sql = _insert_sql + table_sql + "(" + columns_sql + ") values " + values_sql + _conflict_sql + columns_sql + ") = " + values_sql + ";";
		const string select_sql = _select_sql + columns_sql + " from " + table_sql + " ";
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (insert_sql).c_str(), -1, &Billets_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Id >= ?1 order by Id limit 1;").c_str(), -1, &Billets_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where not (Flags & " STRINGIFY(BILL_FLAG_NO_TXID) ") and Address = ?1 and substr(Commitment,1," STRINGIFY(TXID_COMMITMENT_BYTES) ") = ?2;").c_str(), -1, &Billets_select_txid, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Commitnum = ?1;").c_str(), -1, &Billets_select_commitnum, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Status >= " STRINGIFY(BILL_STATUS_PENDING) " and Status <= " STRINGIFY(BILL_STATUS_ALLOCATED) " and Status != " STRINGIFY(BILL_STATUS_SENT) " and Amount > zeroblob(16) and (Amount, Id) < (?1, ?2) order by Amount desc, Id desc limit 1;").c_str(), -1, &Billets_select_unspent, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Status = " STRINGIFY(BILL_STATUS_CLEARED) " and Asset = ?1 and Blockchain = ?2 and Amount >= ?3 and DelayTime <= ?4 order by Amount limit 1;").c_str(), -1, &Billets_select_amount, NULL))); // query used in BilletSelectAmount which is used in LocateBillet
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Status = " STRINGIFY(BILL_STATUS_CLEARED) " and Asset = ?1 and Blockchain = ?2 and Amount >= ?3 and (Amount, Id) > (?3, ?4) order by Amount, Id limit 1;").c_str(), -1, &Billets_select_amount_scan, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Status = " STRINGIFY(BILL_STATUS_CLEARED) " and Asset = ?1 and Blockchain = ?2 and DelayTime <= ?3 order by Amount desc limit 1;").c_str(), -1, &Billets_select_amount_max, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where CreateTx = ?1 order by Id limit ?2;").c_str(), -1, &Billets_select_createtx, NULL))); // note: order by id required so output billet ordering is maintained when tx's are saved and read
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (_select_sql + columns_sql + ", Hashkey, TxCommitnum from " + table_sql + " join Billet_Spends on Billet = Id where SpendTx = ?1 limit ?2;").c_str(), -1, &Billets_select_spendtx, NULL)));
	}

	{
		const string table_sql = "Billet_Spends";
		const string columns_sql = " SpendTx, Billet, Hashkey, TxCommitnum";
		const string values_sql = "(?1, ?2, ?3, ?4)";
		const string select_sql = _select_sql + columns_sql + " from " + table_sql + " ";
		// Billet_Spends table is insert only, no update
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (_insert_sql + table_sql + "(" + columns_sql + ") values " + values_sql + ";").c_str(), -1, &Billet_Spends_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Billet = ?1 and SpendTx >= ?2 order by SpendTx limit 1;").c_str(), -1, &Billet_Spends_select_billet, NULL)));
	}

	{
		const string table_sql = "Totals";
		const string columns_sql = " Type, Reference, Asset, DelayTime, Blockchain, Total";
		const string values_sql = "(?1, ?2, ?3, ?4, ?5, ?6)";
		const string select_sql = _select_sql + columns_sql + " from " + table_sql + " ";
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (_replace_sql + table_sql + " (" + columns_sql + ") values " + values_sql + ";").c_str(), -1, &Totals_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Type = ?1 and Reference = ?2 and Asset = ?3 and DelayTime = ?4 and Blockchain >= ?5 order by Blockchain limit 1;").c_str(), -1, &Totals_select, NULL)));
	}

	{
		const string table_sql = "Exchange_Requests";
		const string values_sql = "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, ?31, ?32, ?33, ?34, ?35, ?36, ?37)";
		const string update_sql = _update_sql + table_sql + " set ";
		const string select_sql = _select_sql + "* from " + table_sql + " left join Transactions on Id = TxId ";
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (_insert_sql + table_sql + " values " + values_sql + ";").c_str(), -1, &Exchange_Requests_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (update_sql + "(Xreqnum, QueryXmatchnum, Disposition, OpenAmount) = (max(Xreqnum, ?2), ?3, ?4, ?5) where XId = ?1;").c_str(), -1, &Exchange_Requests_status_update, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (update_sql + "PollTime = ?2 where XId = ?1;").c_str(), -1, &Exchange_Requests_polling_update, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (update_sql + "PollTime = ?2 where TxId = ?1 and TxId != 0;").c_str(), -1, &Exchange_Requests_polling_update_txid, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where XId >= ?1 order by XId limit 1;").c_str(), -1, &Exchange_Requests_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where XId <= ?1 order by XId desc limit 1;").c_str(), -1, &Exchange_Requests_select_id_descending, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where TxId = ?1 and TxId != 0 and XId > ?2 order by XId limit 1;").c_str(), -1, &Exchange_Requests_select_txid, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where Xreqnum = ?1 and Xreqnum != 0;").c_str(), -1, &Exchange_Requests_select_xreqnum, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "where PollTime > 0 and PollTime <= ?1 order by PollTime, XId limit 1;").c_str(), -1, &Exchange_Requests_select_next_poll, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (_select_sql + "IsBuyer, NetRateRequired, OpenAmount, Pledge from " + table_sql + " "
			"where PollTime > 0 and TxId > 0 and BaseAsset = ?1 and QuoteAsset = ?2 and ForeignAsset = ?3 and OpenAmount not null "
			"and Disposition >= " STRINGIFY(XMATCH_REQ_DISPOSITION_OPEN_ALL) " and Disposition <= " STRINGIFY(XMATCH_REQ_DISPOSITION_OPEN_PART) " order by PollTime, XId;").c_str(), -1, &Exchange_Requests_select_pending, NULL)));
	}

	{
		const string table_sql = "Exchange_Matches";
		const string values_sql = "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21)";
		const string select_sql = _select_sql + "* from " + table_sql + " "
			"join Exchange_Requests as Xbuy   on  Xbuy.Xreqnum = BuyXreqnum "
			"left join Transactions as TxBuy  on  TxBuy.Id = XBuy.TxId "
			"join Exchange_Requests as Xsell  on Xsell.Xreqnum = SellXreqnum "
			"left join Transactions as TxSell on TxSell.Id = XSell.TxId "
			"where Xbuy.Xreqnum != 0 and Xsell.Xreqnum != 0 and ";
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (_insert_sql + table_sql + " values " + values_sql + " on conflict(Xmatchnum) do update set "
			"BaseAmount = excluded.BaseAmount, Rate = excluded.Rate, AmountPaid = excluded.AmountPaid, MiningAmount = excluded.MiningAmount, Status = excluded.Status, NextDeadline = excluded.NextDeadline, WalletPollTime = excluded.WalletPollTime, AcceptTimestamp = excluded.AcceptTimestamp, FinalTimestamp = excluded.FinalTimestamp, "
			"WalletPaid = min(1, max(0, WalletPaid + excluded.WalletPaid)), WalletPaymentForeignTxid = case when length(excluded.WalletPaymentForeignTxid) > 0 then excluded.WalletPaymentForeignTxid else WalletPaymentForeignTxid end, WalletReminderTime = case when excluded.WalletReminderTime then excluded.WalletReminderTime else WalletReminderTime end;"
			).c_str(), -1, &Exchange_Matches_insert, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "Xmatchnum >= ?1;").c_str(), -1, &Exchange_Matches_select, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "NextDeadline > 0 and (NextDeadline, Xmatchnum) > (?1, ?2) order by NextDeadline, Xmatchnum limit 1;").c_str(), -1, &Exchange_Matches_select_deadline, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (select_sql + "WalletPollTime > 0 and WalletPollTime <= ?1 order by WalletPollTime limit 1;").c_str(), -1, &Exchange_Matches_select_next_poll, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, (_select_sql + "Xbuy.TxId, Xsell.TxId, Rate, BaseAmount, BuyerPledge from " + table_sql + " "
			"join Exchange_Requests as Xbuy   on  Xbuy.Xreqnum = BuyXreqnum "
			"join Exchange_Requests as Xsell  on Xsell.Xreqnum = SellXreqnum "
			"where WalletPollTime > 0 and Xbuy.BaseAsset = ?1 and Xbuy.QuoteAsset = ?2 and Xbuy.ForeignAsset = ?3 "
			"and Status >= " STRINGIFY(XMATCH_STATUS_MATCHED) " and Status <= " STRINGIFY(XMATCH_STATUS_PART_PAID_OPEN) " order by WalletPollTime, Xmatchnum;").c_str(), -1, &Exchange_Matches_select_pending, NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Wallet_db, ( _select_sql + "Xmatchnum from " + table_sql + " "
			"where Status >= " STRINGIFY(XMATCH_STATUS_MATCHED) " "
			"and   Status <= " STRINGIFY(XMATCH_STATUS_PART_PAID_OPEN) " "
			"and (BuyXreqnum = ?1 or SellXreqnum = ?1) and Xmatchnum >= ?2 order by Xmatchnum limit 1;").c_str(), -1, &Exchange_Matches_select_status, NULL)));
	}

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::PrepareDbConn dbconn done " << (uintptr_t)this;
}

void DbConn::InitDb()
{
	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(debug) << "DbConn::InitDb dbconn " << uintptr_t(this);

	bigint_t wallet_id;
	CCRandom(&wallet_id, WALLET_ID_BYTES);

	string sql, insert("insert or ignore into Parameters values (");

	sql = insert + STRINGIFY(DB_KEY_SCHEMA) ",0,'" DB_TAG "');";
	//cerr << sql << endl;
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

	sql = insert + STRINGIFY(DB_KEY_SCHEMA) ",1," STRINGIFY(DB_SCHEMA) ");";
	//cerr << sql << endl;
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

	sql = insert + STRINGIFY(DB_KEY_WALLET_ID) ",0,X'" + buf2hex(&wallet_id, WALLET_ID_BYTES) + "');";
	//cerr << sql << endl;
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

	sql = insert + STRINGIFY(DB_KEY_BLOCKCHAIN) ",0,X'" + buf2hex(&g_params.blockchain, sizeof(g_params.blockchain)) + "');";
	//cerr << sql << endl;
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

	bigint_t zeros = 0UL;
	sql = insert + STRINGIFY(DB_KEY_UNIQUE_REFID) ",0,X'" + buf2hex(&zeros, sizeof(zeros)) + "');";
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

	sql = "insert or ignore into Accounts values (0, '');";
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

	sql = "insert or ignore into Transactions(Id, Type, Status, Blockchain, ParamLevel, CreateTime) values (1,0,0,0,0,0);";
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

	sql = insert + STRINGIFY(DB_KEY_TOTAL_MINED) ",0,X'" + buf2hex(&zeros, AMOUNT_UNSIGNED_PACKED_BYTES) + "');";
	CCASSERTZ(dbexec(Wallet_db, sql.c_str()));
}

#define CHECKDATA_BUFSIZE 100

int DbConn::CheckDb(bool post_create)
{
	BOOST_LOG_TRIVIAL(trace) << "DbConn::CheckDb post_create " << post_create;

	static const char db_tag[] = DB_TAG;
	static const char db_schema[] = STRINGIFY(DB_SCHEMA);	// works because sqlite3_column_blob converts an integer to a string
	static const char schema_error[] = "Unable to read wallet schema";
	static const char begin_exclusive[] = "begin exclusive;";

	char tag[CHECKDATA_BUFSIZE];
	char schema[CHECKDATA_BUFSIZE];

	auto rc = ParameterSelect(DB_KEY_SCHEMA, 1, schema, sizeof(schema), true, NULL, post_create);
	if (rc < 0 || (rc && post_create))
		throw runtime_error(schema_error);

	auto rc2 = ParameterSelect(DB_KEY_SCHEMA, 0, tag, sizeof(tag), true, NULL, post_create);
	if (rc2 < 0 || (rc2 && post_create) || rc2 != rc)
		throw runtime_error(schema_error);
	if (rc)
		return 0;

	if (strcmp(tag, db_tag))
	{
		//cerr << "db_tag " << buf2hex(tag, sizeof(db_tag)) << endl;

		throw runtime_error("Not a valid wallet file");
	}

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

	if (!strcmp(schema, "3"))
	{
		CheckSchemaUpdateOption();

		if (!post_create)
			return 1;

		cerr << "Updating wallet to schema version 4\n" << endl;

		CCASSERTZ(dbexec(Wallet_db, begin_exclusive));

		CCASSERTZ(dbexec(Wallet_db, ALTER_TABLE_SQL "Transactions add column RefId varchar;"));
		CCASSERTZ(dbexec(Wallet_db, ALTER_TABLE_SQL "Transactions add column ParamLevel integer;"));

		CCASSERTZ(dbexec(Wallet_db, "update Billets set Commitnum = null where Commitnum = 0;"));

		bigint_t refid = 0UL;
		CCASSERTZ(ParameterInsert(DB_KEY_UNIQUE_REFID, 0, &refid, sizeof(refid), true));

		CCASSERTZ(dbexec(Wallet_db, "update Parameters set Value = 4 where Key = " STRINGIFY(DB_KEY_SCHEMA) " and Subkey = 1;"));

		return 1;	// retry CheckDb
	}

	if (!strcmp(schema, "4"))
	{
		CheckSchemaUpdateOption();

		if (!post_create)
			return 1;

		cerr << "Updating wallet to schema version 5\n" << endl;

		CCASSERTZ(dbexec(Wallet_db, begin_exclusive));

		CCASSERTZ(dbexec(Wallet_db, ALTER_TABLE_SQL "Billet_Spends add column TxCommitnum integer;"));

		CCASSERTZ(dbexec(Wallet_db, "update Parameters set Value = 5 where Key = " STRINGIFY(DB_KEY_SCHEMA) " and Subkey = 1;"));

		return 1;	// retry CheckDb
	}

	if (!strcmp(schema, "5"))
	{
		CheckSchemaUpdateOption();

		if (!post_create)
			return 1;

		cerr << "Updating wallet to schema version 6\n" << endl;

		CCASSERTZ(dbexec(Wallet_db, begin_exclusive));

		CCASSERTZ(dbexec(Wallet_db, DROP_INDEX_SQL "Billets_Amount_Index;"));
		CCASSERTZ(dbexec(Wallet_db, DROP_INDEX_SQL "Billets_Unspent_Index;"));
		CCASSERTZ(dbexec(Wallet_db, ALTER_TABLE_SQL "Billets rename column Pool to Domain;"));

		CCASSERTZ(dbexec(Wallet_db, DROP_INDEX_SQL "Secrets_CheckTime_Index;"));
		CCASSERTZ(dbexec(Wallet_db, ALTER_TABLE_SQL "Secrets rename column NextCheck to PollTime;"));

		CCASSERTZ(dbexec(Wallet_db, ALTER_TABLE_SQL "Transactions add column Blockchain integer;"));
		CCASSERTZ(dbexec(Wallet_db, ALTER_TABLE_SQL "Transactions add column ObjId blob;"));
		CCASSERTZ(dbexec(Wallet_db, (_update_sql + "Transactions set Blockchain = " + to_string(blockchain) + ";").c_str()));

		string temp_tx_table_name = "Transactions_Copy";
		CreateTransactionsTable(temp_tx_table_name);
		CCASSERTZ(dbexec(Wallet_db, ("delete from " + temp_tx_table_name + ";").c_str()));
		CCASSERTZ(dbexec(Wallet_db, (_insert_sql + temp_tx_table_name + " select" + transactions_columns_sql + " from Transactions;").c_str()));
		CCASSERTZ(dbexec(Wallet_db, "drop table Transactions;"));
		CCASSERTZ(dbexec(Wallet_db, (ALTER_TABLE_SQL + temp_tx_table_name + " rename to Transactions;").c_str()));

		string sql = "update Totals set Total = cast(";
		for (int i = 256/8; i > 0; --i)
			sql += "substr(Total," + to_string(i) + ",1)" + (i > 1 ? " || " : "");
		sql += " as blob) where length(Total);";
		//cout << sql << endl;
		CCASSERTZ(dbexec(Wallet_db, sql.c_str()));

		CreateExchangeTables();

		CCASSERTZ(dbexec(Wallet_db, "update Parameters set Value = 6 where Key = " STRINGIFY(DB_KEY_SCHEMA) " and Subkey = 1;"));

		return 1;	// retry CheckDb
	}

	if (!strcmp(schema, "6"))
	{
		CheckSchemaUpdateOption();

		if (!post_create)
			return 1;

		cerr << "Updating wallet to schema version 7\n" << endl;

		CCASSERTZ(dbexec(Wallet_db, begin_exclusive));

		CCASSERTZ(dbexec(Wallet_db, DROP_INDEX_SQL "Exchange_Requests_TxId_Index;"));

		CCASSERTZ(dbexec(Wallet_db, "update Parameters set Value = 7 where Key = " STRINGIFY(DB_KEY_SCHEMA) " and Subkey = 1;"));

		return 1;	// retry CheckDb
	}

	if (strcmp(schema, db_schema))
		throw runtime_error("Invalid wallet schema");

	bigint_t refid;
	rc = ParameterSelect(DB_KEY_UNIQUE_REFID, 0, &refid, sizeof(refid));
	if (rc)
		throw runtime_error("Unable to read transaction unique RefId");

	CCASSERTZ(dbexec(Wallet_db, "update Transactions set Id = Id where Id = 1;"));
	auto changes = sqlite3_changes(Wallet_db);
	if (changes != 1)
		throw runtime_error("Unable to detect Transaction with Id = 1");

	if (post_create)
		CreateIndexes();

	return 0;
}

void DbConn::CheckSchemaUpdateOption()
{
	if (!g_params.config_options->count("update-wallet"))
	{
		cerr <<

R"(This wallet file must be updated to be used with this version of the software.  Please restart the wallet with the
command line option --update-wallet.  It is strongly recommended that you backup the wallet file before updating.)"

		"\n" << endl;

		throw runtime_error("Wallet file requires updating");
	}
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

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::CloseDb dbconn " << (uintptr_t)this;

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	lock_guard<mutex> lock(db_mutex);

	bool explain = done && TEST_EXPLAIN_DB_QUERIES;

	//if (explain)
	//	CCASSERTZ(dbexec(Wallet_db, "analyze;"));

	DbFinalize(db_begin_read, explain);
	DbFinalize(db_begin_write, explain);
	DbFinalize(db_commit, explain);
	DbFinalize(db_rollback, explain);

	DbFinalize(Parameters_insert, explain);
	DbFinalize(Parameters_select, explain);

	DbFinalize(Accounts_insert, explain);
	DbFinalize(Accounts_select, explain);
	DbFinalize(Accounts_select_name, explain);

	DbFinalize(Secrets_insert, explain);
	DbFinalize(Secrets_select, explain);
	DbFinalize(Secrets_select_value, explain);
	DbFinalize(Secrets_select_destination, explain);
	DbFinalize(Secrets_select_next_poll, explain);

	DbFinalize(Transactions_insert, explain);
	DbFinalize(Transactions_select, explain);
	DbFinalize(Transactions_select_refid, explain);
	DbFinalize(Transactions_select_id_descending, explain);
	DbFinalize(Transactions_select_objid_descending_id, explain);
	DbFinalize(Transactions_select_level, explain);
	DbFinalize(Transactions_select_level_descending, explain);

	DbFinalize(Billets_insert, explain);
	DbFinalize(Billets_select, explain);
	DbFinalize(Billets_select_txid, explain);
	DbFinalize(Billets_select_commitnum, explain);
	DbFinalize(Billets_select_unspent, explain);
	DbFinalize(Billets_select_amount, explain);
	DbFinalize(Billets_select_amount_scan, explain);
	DbFinalize(Billets_select_amount_max, explain);
	DbFinalize(Billets_select_createtx, explain);
	DbFinalize(Billets_select_spendtx, explain);
	DbFinalize(Billet_Spends_insert, explain);
	DbFinalize(Billet_Spends_select_billet, explain);

	DbFinalize(Totals_insert, explain);
	DbFinalize(Totals_select, explain);

	DbFinalize(Exchange_Requests_insert, explain);
	DbFinalize(Exchange_Requests_status_update, explain);
	DbFinalize(Exchange_Requests_polling_update, explain);
	DbFinalize(Exchange_Requests_polling_update_txid, explain);
	DbFinalize(Exchange_Requests_select, explain);
	DbFinalize(Exchange_Requests_select_id_descending, explain);
	DbFinalize(Exchange_Requests_select_txid, explain);
	DbFinalize(Exchange_Requests_select_xreqnum, explain);
	DbFinalize(Exchange_Requests_select_next_poll, explain);
	DbFinalize(Exchange_Requests_select_pending, explain);

	DbFinalize(Exchange_Matches_insert, explain);
	DbFinalize(Exchange_Matches_select, explain);
	DbFinalize(Exchange_Matches_select_status, explain);
	DbFinalize(Exchange_Matches_select_deadline, explain);
	DbFinalize(Exchange_Matches_select_next_poll, explain);
	DbFinalize(Exchange_Matches_select_pending, explain);

	if (done)
	{
		dbexec(Wallet_db, "PRAGMA synchronous = EXTRA;");
		dbexec(Wallet_db, "PRAGMA journal_mode = DELETE;");
	}

	dblog(sqlite3_close_v2(Wallet_db));

	Wallet_db = NULL;

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::CloseDb done dbconn " << (uintptr_t)this;
}

void DbConn::DoDbFinish()
{
	if (RandTest(RTEST_DELAY_DB_RESET)) sleep(1);

	if (0 && TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::DoDbFinish dbconn " << uintptr_t(this);

	sqlite3_reset(Parameters_insert);
	sqlite3_reset(Parameters_select);

	sqlite3_reset(Accounts_insert);
	sqlite3_reset(Accounts_select);
	sqlite3_reset(Accounts_select_name);

	sqlite3_reset(Secrets_insert);
	sqlite3_reset(Secrets_select);
	sqlite3_reset(Secrets_select_value);
	sqlite3_reset(Secrets_select_destination);
	sqlite3_reset(Secrets_select_next_poll);

	sqlite3_reset(Transactions_insert);
	sqlite3_reset(Transactions_select);
	sqlite3_reset(Transactions_select_refid);
	sqlite3_reset(Transactions_select_id_descending);
	sqlite3_reset(Transactions_select_objid_descending_id);
	sqlite3_reset(Transactions_select_level);
	sqlite3_reset(Transactions_select_level_descending);

	sqlite3_reset(Billets_insert);
	sqlite3_reset(Billets_select);
	sqlite3_reset(Billets_select_txid);
	sqlite3_reset(Billets_select_commitnum);
	sqlite3_reset(Billets_select_unspent);
	sqlite3_reset(Billets_select_amount);
	sqlite3_reset(Billets_select_amount_scan);
	sqlite3_reset(Billets_select_amount_max);
	sqlite3_reset(Billets_select_createtx);
	sqlite3_reset(Billets_select_spendtx);
	sqlite3_reset(Billet_Spends_insert);
	sqlite3_reset(Billet_Spends_select_billet);

	sqlite3_reset(Totals_insert);
	sqlite3_reset(Totals_select);

	sqlite3_reset(Exchange_Requests_insert);
	sqlite3_reset(Exchange_Requests_status_update);
	sqlite3_reset(Exchange_Requests_polling_update);
	sqlite3_reset(Exchange_Requests_polling_update_txid);
	sqlite3_reset(Exchange_Requests_select);
	sqlite3_reset(Exchange_Requests_select_id_descending);
	sqlite3_reset(Exchange_Requests_select_txid);
	sqlite3_reset(Exchange_Requests_select_xreqnum);
	sqlite3_reset(Exchange_Requests_select_next_poll);
	sqlite3_reset(Exchange_Requests_select_pending);

	sqlite3_reset(Exchange_Matches_insert);
	sqlite3_reset(Exchange_Matches_select);
	sqlite3_reset(Exchange_Matches_select_status);
	sqlite3_reset(Exchange_Matches_select_deadline);
	sqlite3_reset(Exchange_Matches_select_next_poll);
	sqlite3_reset(Exchange_Matches_select_pending);

	sqlite3_db_release_memory(Wallet_db);
}

void DbConn::DoDbFinishTx(int rollback)
{
	if (RandTest(RTEST_DELAY_DB_RESET)) sleep(1);

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(debug) << "DbConn::DoDbFinishTx dbconn " << uintptr_t(this) << " rollback " << rollback;

	sqlite3_reset(db_begin_read);
	sqlite3_reset(db_begin_write);
	sqlite3_reset(db_commit);
	sqlite3_reset(db_rollback);

	if (rollback)
	{
		if (0 && TRACE_DB_READ) BOOST_LOG_TRIVIAL(debug) << "DbConn::DoDbFinishTx dbconn " << uintptr_t(this) << " rollback";

		auto rc = sqlite3_step(db_rollback);
		if (rollback > 0)
			dblog(rc, DB_STMT_STEP);
		sqlite3_reset(db_rollback);
	}

	sqlite3_db_release_memory(Wallet_db);
}

int DbConn::BeginRead()
{
	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::BeginRead starting db read transaction";

	// acquire lock -- might solve intermittent sqlite errors
	//lock_guard<boost::shared_mutex> lock(db_mutex);
	lock_guard<mutex> lock(db_mutex);

	if (RandTest(RTEST_CUZZ)) sleep(1);

	if (dblog(sqlite3_step(db_begin_read), DB_STMT_STEP))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BeginRead error starting db read transaction";

		return -1;
	}

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::BeginRead started db read transaction";

	return 0;
}

int DbConn::BeginWrite()
{
	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::BeginWrite starting db write transaction";

	// acquire lock -- might solve intermittent sqlite errors
	//lock_guard<boost::shared_mutex> lock(db_mutex);
	lock_guard<mutex> lock(db_mutex);

	if (RandTest(RTEST_CUZZ)) sleep(1);

	if (dblog(sqlite3_step(db_begin_write), DB_STMT_STEP))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BeginWrite error starting db write transaction";

		return -1;
	}

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(debug) << "DbConn::BeginWrite started db write transaction";

	return 0;
}

int DbConn::Commit()
{
	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(debug) << "DbConn::Commit committing db transaction";

	if (RandTest(RTEST_CUZZ)) sleep(1);

	if (dblog(sqlite3_step(db_commit), DB_STMT_STEP))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::Commit error committing db transaction";

		return -1;
	}

	return 0;
}

int DbConn::BackupDb(const char *name)
{
	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(debug) << "DbConn::BackupDb to " << name;

	int result = 0;
	sqlite3 *backup_db = NULL;
	sqlite3_backup *backup_obj = NULL;

	try
	{
		OpenDbFile(name, true, &backup_db, IsInteractive(), true);
	}
	catch (const exception& e)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BackupDb error " << e.what();

		if (IsInteractive())
			cerr << "ERROR: " << e.what() << endl;

		return -1;
	}

	{
		lock_guard<mutex> lock(db_mutex);

		backup_obj = sqlite3_backup_init(backup_db, "main", Wallet_db, "main");

		if (!backup_obj)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::BackupDb sqlite3_backup_init error " << sqlite3_errmsg(backup_db);

			result = -1;
		}
	}

	if (!result)
	{
		auto rc = sqlite3_backup_step(backup_obj, -1);

		if (rc != SQLITE_DONE)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::BackupDb sqlite3_backup_step error " << sqlite3_errstr(rc);

			result = -1;
		}
	}

	if (backup_obj)
	{
		auto rc = sqlite3_backup_finish(backup_obj);

		if (rc != SQLITE_OK)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::BackupDb sqlite3_backup_finish error " << sqlite3_errstr(rc);

			result = -1;
		}
	}

	if (backup_db)
	{
		dbexec(backup_db, "PRAGMA synchronous = EXTRA;");
		dbexec(backup_db, "PRAGMA journal_mode = DELETE;");

		dblog(sqlite3_close_v2(backup_db));
	}

	sqlite3_db_release_memory(Wallet_db);

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::BackupDb done";

	return result;
}

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
