/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * dbconn.cpp
*/

#include "ccnode.h"
#include "dbconn.hpp"
#include "dbparamkeys.h"

#include <CCobjdefs.h>
#include <dblog.h>

#define TRACE_DBCONN	1

//#define TEST_DECORATE_DB_FILENAMES		1

//#define TEST_ENABLE_SQLITE_BUSY			1	// when enabled, mutex problems will result in SQLITE_BUSY errors

#define DB_TAG			"CredaCash Node Database"
#define DB_SCHEMA		3

#define DB_OPEN_WAL_PARAMS	".db?cache=private"

#define DB_OPEN_TEMP_PARAMS	".db?cache=shared&mode=memory"
//#define DB_OPEN_TEMP_PARAMS	".db?cache=shared"	// for testing

static const char* Persistent_Data = "CCdata";
static const char* Temp_Serials = "Temp_Serials";
static const char* Relay_Objs = "Relay_Objs";
static const char* Process_Q[PROCESS_Q_N] = {"Process_Q_Tx", "Process_Q_Blocks"};
static const char* Valid_Objs = "Valid_Objs";

#define IF_NOT_EXISTS_SQL		"if not exists "

#define CREATE_TABLE_SQL		"create table " IF_NOT_EXISTS_SQL
#define ALTER_TABLE_SQL			"alter table "
#define CREATE_INDEX_SQL		"create index " IF_NOT_EXISTS_SQL
#define CREATE_INDEX_UNIQUE_SQL	"create unique index " IF_NOT_EXISTS_SQL

#ifndef TEST_DECORATE_DB_FILENAMES
#define TEST_DECORATE_DB_FILENAMES	0	// don't enable
#endif

#ifndef TEST_ENABLE_SQLITE_BUSY
#define TEST_ENABLE_SQLITE_BUSY		0	// don't test
#endif

static void OpenDbFile(const char *name, sqlite3** db, bool create, bool wal = false, bool sync = true)
{
	string file = "file:";

	file += boost::locale::conv::utf_to_utf<char>(g_params.app_data_dir);

	file += PATH_DELIMITER;

	if (TEST_DECORATE_DB_FILENAMES)	// && g_witness.IsWitness())
	{
		file += to_string(g_params.base_port);
		file += "-";
	}

	file += name;

	if (wal)
		file += DB_OPEN_WAL_PARAMS;
	else
		file += DB_OPEN_TEMP_PARAMS;

	//if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "OpenDbFile wal " << wal << << " sync " << sync << " file " << file;

	CCASSERTZ(dblog(sqlite3_open(file.c_str(), db)));

	CCASSERTZ(dblog(sqlite3_extended_result_codes(*db, 1)));

	const int infinity = 0x70000000;
	CCASSERTZ(dblog(sqlite3_busy_timeout(*db, infinity)));	// so we never get an SQLITE_BUSY error

	CCASSERTZ(dbexec(*db, "PRAGMA foreign_keys = ON;"));

	if (wal)
	{
		CCASSERTZ(dbexec(*db, "PRAGMA page_size = 16384;"));
		CCASSERTZ(dbexec(*db, "PRAGMA journal_mode = WAL;"));
		CCASSERTZ(dbexec(*db, "PRAGMA wal_autocheckpoint = OFF;"));	// no auto-checkpoints
		if (sync)
			CCASSERTZ(dbexec(*db, "PRAGMA synchronous = NORMAL;"));		// can lose data that has not yet been checkpointed
		else
			CCASSERTZ(dbexec(*db, "PRAGMA synchronous = OFF;"));		// temporary db only
	}
	else
	{
		CCASSERTZ(dbexec(*db, "PRAGMA read_uncommitted = TRUE;"));	// needed for shared memory files and operations that loop through db doing read/modify/write
	}

	if (TEST_ENABLE_SQLITE_BUSY)
	{
		BOOST_LOG_TRIVIAL(warning) << "SQLITE_BUSY \"database is locked\" return value are enabled";

		CCASSERTZ(dblog(sqlite3_busy_timeout(*db, 0)));			// must do this last in the initialization sequence so the above statements don't return SQLITE_BUSY
	}

	//if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "OpenDbFile done file " << file;
}

void DbConnBasePersistData::OpenDb(bool create)
{
	OpenDbFile(Persistent_Data, &Persistent_db, create, true, true);
}

void DbConnBaseTempSerials::OpenDb(bool create)
{
	OpenDbFile(Temp_Serials, &Temp_Serials_db, create);
}

void DbConnBaseRelayObjs::OpenDb(bool create)
{
	OpenDbFile(Relay_Objs, &Relay_Objs_db, create);
}

void DbConnBaseProcessQ::OpenDb(unsigned type, bool create)
{
	OpenDbFile(Process_Q[type], &Process_Q_db[type], create);
}

void DbConnBaseValidObjs::OpenDb(bool create)
{
	OpenDbFile(Valid_Objs, &Valid_Objs_db, create);
}

void DbConnBasePersistData::DeInit()
{
	if (Persistent_db)
		dblog(sqlite3_close_v2(Persistent_db));
	Persistent_db = NULL;
}

void DbConnBaseTempSerials::DeInit()
{
	if (Temp_Serials_db)
		dblog(sqlite3_close_v2(Temp_Serials_db));
	Temp_Serials_db = NULL;
}

void DbConnBaseRelayObjs::DeInit()
{
	if (Relay_Objs_db)
		dblog(sqlite3_close_v2(Relay_Objs_db));
	Relay_Objs_db = NULL;
}

void DbConnBaseProcessQ::DeInit()
{
	for (unsigned i = 0; i < PROCESS_Q_N; ++i)
	{
		if (Process_Q_db[i])
			dblog(sqlite3_close_v2(Process_Q_db[i]));
		Process_Q_db[i] = NULL;
	}
}

void DbConnBaseValidObjs::DeInit()
{
	if (Valid_Objs_db)
		dblog(sqlite3_close_v2(Valid_Objs_db));
	Valid_Objs_db = NULL;
}

void DbInit::DeInit()
{
	BOOST_LOG_TRIVIAL(debug) << "DbInit::DeInit";

	DbConnBasePersistData::DeInit();
	DbConnBaseTempSerials::DeInit();
	DbConnBaseRelayObjs::DeInit();
	DbConnBaseProcessQ::DeInit();
	DbConnBaseValidObjs::DeInit();

	BOOST_LOG_TRIVIAL(debug) << "DbInit::DeInit done";
}

void DbInit::OpenDbs()
{
	DbConnBasePersistData::OpenDb();
	DbConnBaseTempSerials::OpenDb();
	DbConnBaseRelayObjs::OpenDb();
	for (unsigned i = 0; i < PROCESS_Q_N; ++i)
		DbConnBaseProcessQ::OpenDb(i);
	DbConnBaseValidObjs::OpenDb();
}

void DbInit::CreateDBs()
{
	BOOST_LOG_TRIVIAL(debug) << "DbInit::CreateDBs";

	CCASSERT(sqlite3_threadsafe());
	CCASSERTZ(dblog(sqlite3_config(SQLITE_CONFIG_MULTITHREAD)));

	CCASSERTZ(dblog(sqlite3_config(SQLITE_CONFIG_MEMSTATUS, 0)));
	CCASSERTZ(dblog(sqlite3_config(SQLITE_CONFIG_URI, 1)));
	CCASSERTZ(dblog(sqlite3_initialize()));
	CCASSERTZ(dblog(sqlite3_enable_shared_cache(1)));

	OpenDbs();

	// this table is a key-value store for persistent parameters
	CCASSERTZ(dbexec(Persistent_db, CREATE_TABLE_SQL "Parameters (Key integer not null, Subkey integer not null, Value blob, primary key (Key, Subkey)) without rowid;"));

	// this table contains the blockchain
	CCASSERTZ(dbexec(Persistent_db, CREATE_TABLE_SQL "Blockchain (Level integer primary key not null, Block blob not null) --without rowid;")); // integer primary key is rowid

	// this table contains the spent serialnums from indelible transactions
	CCASSERTZ(dbexec(Persistent_db, CREATE_TABLE_SQL "Serialnums (Serialnum blob primary key not null, HashKey blob) without rowid;"));
	if (SQLITE_OK != sqlite3_table_column_metadata(Persistent_db, NULL, "Serialnums", "TxCommitnum", NULL, NULL, NULL, NULL, NULL))
		CCASSERTZ(dbexec(Persistent_db, ALTER_TABLE_SQL "Serialnums add column TxCommitnum integer;"));

	// this table contains the Merkle tree of all commitments
	// note: tree entries are stored in (Height, Offset) sort order, so updating the Merkle tree should take about 40 disk seeks, one
	// for each Height value, roughly independent of the number of commitments per block.  The tradeoff though is that fetching a single
	// path will also take 40 disk seeks, but we are willing to allow path lookups to slow down under heavy load if that allows transaction
	// processing throughput to be constant with load.
	// The alternate would be to store the entries in (Offset, Height) order would would allow lookups to be processed in an average of
	// 40/2 = 20 disk seeks (and even better when the database is young), while update time would be linear with load (an average of
	// one disk seek per commitment).
	// One speed up might be to not store the lower inner levels of the tree, and instead compute them on-the-fly when needed.
	// This would save one disk seek for each level not stored, and saving in storage space, at the cost of a higher CPU load.
	CCASSERTZ(dbexec(Persistent_db, CREATE_TABLE_SQL "Commit_Tree (Height integer not null, Offset integer not null, Data blob not null, primary key (Height, Offset)) without rowid;"));

	// this table contains recent roots of the Merkle tree of all commitments
	// a transaction is valid if it references a "recent" Merkle root which is used in the "commitment-iv" when computing commitment values
	// look up is by Level
	CCASSERTZ(dbexec(Persistent_db, CREATE_TABLE_SQL "Commit_Roots (Level integer primary key not null, Timestamp integer not null, NextCommitnum integer not null, MerkleRoot blob not null) --without rowid;")); // integer primary key is rowid
	CCASSERTZ(dbexec(Persistent_db, CREATE_INDEX_UNIQUE_SQL "Commit_Roots_Commitnum_Index on Commit_Roots (NextCommitnum);"));

	// this table contains the information needed to spend a transaction output
	// it is used by the transaction server
	// lookup is by Address, Commitnum
	// ParamLevel corresponds to the Level key in the Commit_Roots table, i.e., the commitment_iv used to generate the Commitment can be looked up in Commit_Roots at Level = ParamLevel
	// Commitnum corresponds to the Offset key in the Commit_Tree table, i.e., the Commitment's Merkle path can be looked up in Commit_Tree starting at Offset=Commitnum
	CCASSERTZ(dbexec(Persistent_db, CREATE_TABLE_SQL "Tx_Outputs (Address blob not null, Pool integer, AssetEnc integer, AmountEnc integer, ParamLevel integer not null, Commitnum integer not null, primary key (Address, Commitnum)) without rowid;"));

	// this table contains the spent serialnums from delible blocks and the delible blocks in which they appear
	// the same serialnum can exist in more than one delible block, so (Serialnum, Blockp) is used to make the primary key unique
	// the block Level is included in the table to prune old entries
	// the block being validated and the block being built do not yet have a permanent blockp, so blockp for these blocks is set to a small constant and level is set to -1
	//	that allows all blockp's to be updated if the block is kept, or deleted if the block is discarded
	//	the negative level allows allows a partial index to be created to speed this up, and prevents these entries from being deleted when the table is pruned by level
	CCASSERTZ(dbexec(Temp_Serials_db, CREATE_TABLE_SQL "Temp_Serials (Serialnum blob not null, Blockp blob not null, Level integer not null default 0, primary key (Serialnum, Blockp)) without rowid;"));
	CCASSERTZ(dbexec(Temp_Serials_db, CREATE_INDEX_SQL "Temp_Serials_Special_Block_Index on Temp_Serials (Blockp) where Level = 0;"));

	// this table lists all ObjId's that have been seen by the Relay system
	// Seqnum gives the priority order in which the objects should be downloaded
	// ObjId_Index facilitates looking up existing Seqnum for an ObjId
	// Download_Index facilitates finding next objects to download
	CCASSERTZ(dbexec(Relay_Objs_db, CREATE_TABLE_SQL "Relay_Objs (Seqnum integer primary key not null, Time integer not null, ObjId blob not null, Status integer not null, Timeout integer not null default (strftime('%s','now'))) --without rowid;")); // integer primary key is rowid
	CCASSERTZ(dbexec(Relay_Objs_db, CREATE_INDEX_UNIQUE_SQL "Relay_Objs_ObjId_Index on Relay_Objs (ObjId);"));

	// this table lists the object id's announced by each peer
	// Seqnum is the same value in both this table and Relay_Objs
	// the table includes the size announced by the peer, and for blocks, the block params announced by the peer
	//	note that a malicious peer could misrepresent the object params, so the object params announced by each peer are stored separately so one peer can't affect the data sent by others
	// Seqnum primary key facilitates updating and deleting entries by Seqnum
	// Peer_Index facilitates finding objects to download, and deleting all entries from a particular peer
	//	it also prevents a malicious peer from overwhelming us with block announcements since it is a unque index on (Level, Witness, Peer)
	//	note that by rule, each witness can create no more than one block at each level
	CCASSERTZ(dbexec(Relay_Objs_db, CREATE_TABLE_SQL "Relay_Peers (Seqnum integer primary key not null, Peer integer not null, Size integer not null, Level integer not null, PeerStatus integer not null, PriorOid blob, Witness integer) --without rowid;")); // integer primary key is rowid
	CCASSERTZ(dbexec(Relay_Objs_db, CREATE_INDEX_SQL "Relay_Peers_Peer_Index on Relay_Peers (Peer, PeerStatus, Seqnum, Level);"));

	// these tables hold Tx's and blocks queued for processing
	// they are placed in separate databases for better concurrency
	// the primary key ObjId is used when deleting objects by ObjId
	// Priority_Index facilitates selecting the next object to process
	// Level_Index facilitates deleting block data by level
	// PriorObj_Index facilitates updating the status when the prior block becomes valid
	// when operating as a witness, blocks are left in this table after validation for use chosing a block to build on
	// AuxInt is used by the witness to hold the block score
	for (unsigned i = 0; i < PROCESS_Q_N; ++i)
	{
		CCASSERTZ(dbexec(Process_Q_db[i], CREATE_TABLE_SQL "Process_Q (ObjId blob primary key not null, PriorOid blob, Level integer, AuxInt integer, ConnId integer, CallbackId integer, Bufp blob not null, Status integer not null, Priority integer not null) without rowid;"));
		CCASSERTZ(dbexec(Process_Q_db[i], CREATE_INDEX_SQL "Process_Q_Priority_Index on Process_Q (Status, Priority, Level desc);"));
		CCASSERTZ(dbexec(Process_Q_db[i], CREATE_INDEX_SQL "Process_Q_PriorObj_Index on Process_Q (PriorOid) where Status = " STRINGIFY(PROCESS_Q_STATUS_HOLD) ";"));
		CCASSERTZ(dbexec(Process_Q_db[i], CREATE_INDEX_SQL "Process_Q_Level_Index on Process_Q (Level) where Level not null;"));
	}

	// this table holds all valid objects
	// the Seqnum allows detection and announcement of new objects
	// the Obj_Index facilitates retrieval when a peer requests an object by ObjId
	CCASSERTZ(dbexec(Valid_Objs_db, CREATE_TABLE_SQL "Valid_Objs (Seqnum integer primary key not null, Time integer not null, ObjId blob not null, Bufp blob not null) --without rowid;")); // integer primary key is rowid
	CCASSERTZ(dbexec(Valid_Objs_db, CREATE_INDEX_UNIQUE_SQL "Valid_Objs_ObjId_Index on Valid_Objs (ObjId);"));
}

void DbInit::InitDb()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbInit::InitDb";

	string sql("insert or ignore into Parameters values (" STRINGIFY(DB_KEY_SCHEMA));

	CCASSERTZ(dbexec(Persistent_db, (sql + ", 0, '" DB_TAG "');").c_str()));
	CCASSERTZ(dbexec(Persistent_db, (sql + ", 1, " STRINGIFY(DB_SCHEMA) ");").c_str()));
}

#define CHECKDATA_BUFSIZE 100

static int checkdata_callback(void *appdata, int nresults, char **results, char **colnames)
{
	auto output = (char*)appdata;

	output[0] = 0;

	if (nresults != 1)
		return -1;

	if (strlen(results[0]) >= CHECKDATA_BUFSIZE)
		return -1;

	strcpy(output, results[0]);

	return 0;
}

void DbInit::CheckDb()
{
	BOOST_LOG_TRIVIAL(trace) << "DbInit::CheckDb";

	static const char db_tag[] = DB_TAG;
	static const char db_schema[] = STRINGIFY(DB_SCHEMA);

	char tag[CHECKDATA_BUFSIZE];
	char schema[CHECKDATA_BUFSIZE];

	string sql = "select Value from Parameters where Key = " STRINGIFY(DB_KEY_SCHEMA) " and Subkey = ";

	auto rc = sqlite3_exec(Persistent_db, (sql + "0").c_str(), checkdata_callback, tag, NULL);
	rc |= sqlite3_exec(Persistent_db, (sql + "1").c_str(), checkdata_callback, schema, NULL);

	if (rc)
		throw runtime_error("unable to read database schema");

	//cerr << "tag " << buf2hex(tag, sizeof(db_tag)) << endl;

	if (strcmp(tag, db_tag) || strcmp(schema, db_schema))
		throw runtime_error("not a valid node database file");
}
