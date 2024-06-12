/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * walletdb-secrets.cpp
*/

#include "ccwallet.h"
#include "walletdb.hpp"
#include "secrets.hpp"

#include <dblog.h>

#define TRACE_DB_READ	(g_params.trace_db_reads)
#define TRACE_DB_WRITE	(g_params.trace_db_writes)

int DbConn::SecretInsert(Secret& secret, bool lock_optional)
{
	CCASSERT(lock_optional || GetTxnState() == SQLITE_TXN_WRITE);

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(trace) << "DbConn::SecretInsert " << secret.DebugString();

	CCASSERT(secret.IsValid());

	if (!secret.create_time)
		secret.create_time = unixtime();

	// Id, Parent, Type, DestinationId, AccountId, PackedParams, Number, BlockChain, Secret, Label, CreateTime, FirstReceive, LastReceive, LastCheck, PollTime, QueryCommitnum, ExpectedCommitnum
	if (secret.id)
	{
		if (dblog(sqlite3_bind_int64(Secrets_insert, 1, secret.id))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Secrets_insert, 1))) return -1;
	}
	if (dblog(sqlite3_bind_int64(Secrets_insert, 2, secret.parent_id))) return -1;
	if (dblog(sqlite3_bind_int(Secrets_insert, 3, secret.type))) return -1;
	if (secret.dest_id)
	{
		if (dblog(sqlite3_bind_int64(Secrets_insert, 4, secret.dest_id))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Secrets_insert, 4))) return -1;
	}
	if (dblog(sqlite3_bind_int64(Secrets_insert, 5, secret.account_id))) return -1;
	if (dblog(sqlite3_bind_blob(Secrets_insert, 6, secret.packed_params, secret.packed_params_bytes, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Secrets_insert, 7, secret.number))) return -1;
	if (dblog(sqlite3_bind_int64(Secrets_insert, 8, secret.dest_chain))) return -1;
	if (dblog(sqlite3_bind_blob(Secrets_insert, 9, &secret.value, secret.ValueBytes(), SQLITE_STATIC))) return -1;
	if (secret.label[0])
	{
		if (dblog(sqlite3_bind_text(Secrets_insert, 10, secret.label, -1, SQLITE_STATIC))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Secrets_insert, 10))) return -1;
	}
	if (dblog(sqlite3_bind_int64(Secrets_insert, 11, secret.create_time))) return -1;
	if (secret.first_receive)
	{
		if (dblog(sqlite3_bind_int64(Secrets_insert, 12, secret.first_receive))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Secrets_insert, 12))) return -1;
	}
	if (secret.last_receive)
	{
		if (dblog(sqlite3_bind_int64(Secrets_insert, 13, secret.last_receive))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Secrets_insert, 13))) return -1;
	}
	if (secret.last_poll)
	{
		if (dblog(sqlite3_bind_int64(Secrets_insert, 14, secret.last_poll))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Secrets_insert, 14))) return -1;
	}
	if (secret.next_poll)
	{
		if (dblog(sqlite3_bind_int64(Secrets_insert, 15, secret.next_poll))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Secrets_insert, 15))) return -1;
	}
	if (dblog(sqlite3_bind_int64(Secrets_insert, 16, secret.query_commitnum))) return -1;
	if (dblog(sqlite3_bind_int64(Secrets_insert, 17, secret.expected_commitnum))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::SecretInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Secrets_insert);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(trace) << "DbConn::SecretInsert constraint violation " << secret.DebugString();

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::SecretInsert sqlite3_changes " << changes << " after insert " << secret.DebugString();

		return -1;
	}

	if (!secret.id)
		secret.id = sqlite3_last_insert_rowid(Wallet_db);

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(debug) << "DbConn::SecretInsert inserted " << secret.DebugString();

	return 0;
}

int DbConn::SecretSelect(sqlite3_stmt *select, Secret& secret, bool expect_row, uint64_t required_id)
{
	int rc;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::SecretSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::SecretSelect returned SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::SecretSelect returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::SecretSelect returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != 17)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::SecretSelect returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	uint64_t id = sqlite3_column_int64(select, 0);

	if (required_id && required_id != id)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::SecretSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::SecretSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";

		return 1;
	}

	// Id, Parent, Type, DestinationId, AccountId, PackedParams, Number, BlockChain, Secret, Label, CreateTime, FirstReceive, LastReceive, LastCheck, PollTime, QueryCommitnum, ExpectedCommitnum
	uint64_t parent_id = sqlite3_column_int64(select, 1);
	unsigned type = sqlite3_column_int(select, 2);
	uint64_t dest_id = sqlite3_column_int64(select, 3);
	uint64_t account_id = sqlite3_column_int64(select, 4);
	auto params_blob = sqlite3_column_blob(select, 5);
	unsigned params_size = sqlite3_column_bytes(select, 5);
	uint64_t number = sqlite3_column_int64(select, 6);
	uint64_t dest_chain = sqlite3_column_int64(select, 7);
	auto value_blob = sqlite3_column_blob(select, 8);
	unsigned value_size = sqlite3_column_bytes(select, 8);
	auto label_text = sqlite3_column_text(select, 9);
	unsigned label_size = sqlite3_column_bytes(select, 9);
	uint64_t create_time = sqlite3_column_int64(select, 10);
	uint64_t first_receive = sqlite3_column_int64(select, 11);
	uint64_t last_receive = sqlite3_column_int64(select, 12);
	uint64_t last_poll = sqlite3_column_int64(select, 13);
	uint64_t next_poll = sqlite3_column_int64(select, 14);
	uint64_t query_commitnum = sqlite3_column_int64(select, 15);
	uint64_t expected_commitnum = sqlite3_column_int64(select, 16);

	if (!Secret::TypeIsValid(type))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::SecretSelect returned invalid type " << type;

		return -1;
	}

	if (params_size > sizeof(secret.packed_params))
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::SecretSelect returned packed params size " << params_size << " > " << sizeof(secret.packed_params);

		return -1;
	}

	if (!value_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::SecretSelect Secret is null";

		return -1;
	}

	if (value_size != Secret::ValueBytes(type))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::SecretSelect returned secret size " << value_size << " != " << Secret::ValueBytes(type);

		return -1;
	}

	if (label_size >= sizeof(secret.label))
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::SecretSelect returned label size " << label_size << " >= " << sizeof(secret.label);

		label_size = sizeof(secret.label) - 1;
	}

	if (dblog(sqlite3_extended_errcode(Wallet_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::SecretSelect simulating database error post-error check";

		return -1;
	}

	secret.id = id;
	secret.parent_id = parent_id;
	secret.type = type;
	secret.dest_id = dest_id;
	secret.account_id = account_id;
	secret.packed_params_bytes = params_size;
	memcpy(secret.packed_params, params_blob, params_size);
	secret.number = number;
	secret.dest_chain = dest_chain;
	memcpy(&secret.value, value_blob, value_size);
	memcpy(secret.label, label_text, label_size);
	secret.label[label_size] = 0;	// ensure null terminated
	secret.create_time = create_time;
	secret.first_receive = first_receive;
	secret.last_receive = last_receive;
	secret.last_poll = last_poll;
	secret.next_poll = next_poll;
	secret.query_commitnum = query_commitnum;
	secret.expected_commitnum = expected_commitnum;

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(debug) << "DbConn::SecretSelect returning " << secret.DebugString();

	return 0;
}

int DbConn::SecretSelectId(uint64_t id, Secret& secret, bool or_greater)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::SecretSelectId id " << id << " or_greater " << or_greater;

	CCASSERT(id);

	secret.Clear();

	// Id >=
	if (dblog(sqlite3_bind_int64(Secrets_select, 1, id))) return -1;

	return SecretSelect(Secrets_select, secret, !or_greater, !or_greater * id);
}

int DbConn::SecretSelectSecret(const void *value, unsigned size, Secret& secret)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	secret.Clear();

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::SecretSelectSecret value " << buf2hex(value, size);

	// Value
	if (dblog(sqlite3_bind_blob(Secrets_select_value, 1, value, size, SQLITE_STATIC))) return -1;

	return SecretSelect(Secrets_select_value, secret);
}

int DbConn::SecretSelectDestination(uint64_t dest_id, uint64_t id, Secret& secret)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	secret.Clear();

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::SecretSelectDestination dest_id " << dest_id << " id " << id;

	// (DestinationId, Id) > (?1, ?2)
	if (dblog(sqlite3_bind_int64(Secrets_select_destination, 1, dest_id))) return -1;
	if (dblog(sqlite3_bind_int64(Secrets_select_destination, 2, id))) return -1;

	return SecretSelect(Secrets_select_destination, secret);
}

int DbConn::SecretSelectNextPoll(uint64_t checktime, Secret& secret)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	secret.Clear();

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::SecretSelectNextPoll checktime " << checktime;

	// PollTime <=
	if (dblog(sqlite3_bind_int64(Secrets_select_next_poll, 1, checktime))) return -1;

	return SecretSelect(Secrets_select_next_poll, secret);
}
