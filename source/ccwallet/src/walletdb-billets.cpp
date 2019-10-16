/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * walletdb-billets.cpp
*/

#include "ccwallet.h"
#include "walletdb.hpp"
#include "billets.hpp"
#include "amounts.h"
#include "totals.hpp"

#include <dblog.h>
#include <CCparams.h>

#define TRACE_DBCONN	(g_params.trace_db)

using namespace snarkfront;

int DbConn::BilletsResetAllocated(bool zero_balance)
{
	BOOST_LOG_TRIVIAL(trace) << "BilletsResetAllocated zero_balance " << zero_balance;

	auto rc = dbexec(Wallet_db, "update Billets set Status = " STRINGIFY(BILL_STATUS_PENDING) " where Status = " STRINGIFY(BILL_STATUS_PREALLOCATED) ";");
	if (rc) return rc;

	rc = dbexec(Wallet_db, "update Billets set Status = " STRINGIFY(BILL_STATUS_CLEARED) " where Status = " STRINGIFY(BILL_STATUS_ALLOCATED) ";");
	if (rc) return rc;

	ostringstream query;
	query << "update Totals set Total = X'00' where ";
	if (zero_balance)
		query << "Type = 0 or ";
	query << "(Type & " << TOTAL_TYPE_PA_BITS << ");";
	auto str = query.str();

	rc = dbexec(Wallet_db, str.c_str());
	if (rc) return rc;

	//cerr << str << endl;
	//cerr << sqlite3_extended_errcode(Wallet_db) << endl;
	//cerr << sqlite3_errmsg(Wallet_db) << endl;

	return 0;
}

int DbConn::BilletInsert(Billet& bill, bool lock_optional)
{
	CCASSERT(TestDebugWriteLocking(lock_optional));

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletInsert " << bill.DebugString();

	CCASSERT(bill.IsValid());

	packed_unsigned_amount_t packed_amount;
	CCASSERTZ(pack_unsigned_amount(bill.amount, packed_amount));

	sqlite3_stmt *insert_update = NULL;

	// Id, Status, Flags, CreateTx, DestinationId, Blockchain, Address, Pool, Asset, AmountFp, Amount, DelayTime, CommitIv, Commitment, Commitnum, Serialnum
	if (bill.id)
	{
		insert_update = Billets_update;

		if (dblog(sqlite3_bind_int64(insert_update, 1, bill.id))) return -1;
	}
	else
	{
		insert_update = Billets_insert;

		if (dblog(sqlite3_bind_null(insert_update, 1))) return -1;
	}
	if (dblog(sqlite3_bind_int(insert_update, 2, bill.status))) return -1;
	if (dblog(sqlite3_bind_int(insert_update, 3, bill.flags))) return -1;
	if (dblog(sqlite3_bind_int64(insert_update, 4, bill.create_tx))) return -1;
	if (dblog(sqlite3_bind_int64(insert_update, 5, bill.dest_id))) return -1;
	if (dblog(sqlite3_bind_int64(insert_update, 6, bill.blockchain))) return -1;
	if (dblog(sqlite3_bind_blob(insert_update, 7, &bill.address, TX_ADDRESS_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(insert_update, 8, bill.pool))) return -1;
	if (dblog(sqlite3_bind_int64(insert_update, 9, bill.asset))) return -1;
	if (dblog(sqlite3_bind_int64(insert_update, 10, bill.amount_fp))) return -1;
	if (dblog(sqlite3_bind_blob(insert_update, 11, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(insert_update, 12, bill.delaytime))) return -1;
	if (dblog(sqlite3_bind_blob(insert_update, 13, &bill.commit_iv, TX_COMMIT_IV_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(insert_update, 14, &bill.commitment, TX_COMMITMENT_BYTES, SQLITE_STATIC))) return -1;
	if (bill.commitnum)
	{
		if (dblog(sqlite3_bind_int64(insert_update, 15, bill.commitnum))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(insert_update, 15))) return -1;
	}
	if (bill.HasSerialnum())
	{
		if (dblog(sqlite3_bind_blob(insert_update, 16, &bill.serialnum, TX_SERIALNUM_BYTES, SQLITE_STATIC))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(insert_update, 16))) return -1;
	}

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::BilletInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(insert_update);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::BilletInsert constraint violation " << bill.DebugString();

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletInsert sqlite3_changes " << changes << " after insert " << bill.DebugString();

		return -1;
	}

	if (!bill.id)
		bill.id = sqlite3_last_insert_rowid(Wallet_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::BilletInsert inserted " << bill.DebugString();

	return 0;
}

int DbConn::BilletSelect(sqlite3_stmt *select, bool has_hashkey, Billet& bill, bool expect_row, uint64_t required_id)
{
	int rc;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::BilletSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::BilletSelect select returned SQLITE_DONE";
		else if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelect select returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != 16 + has_hashkey)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect select returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	// Id, Status, Flags, CreateTx, DestinationId, Blockchain, Address, Pool, Asset, AmountFp, Amount, DelayTime, CommitIv, Commitment, Commitnum, Serialnum, and possibly Hashkey
	uint64_t id = sqlite3_column_int64(select, 0);

	if (required_id && required_id != id)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::BilletSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";
		else if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";

		return 1;
	}

	unsigned status = sqlite3_column_int(select, 1);
	unsigned flags = sqlite3_column_int(select, 2);
	uint64_t create_tx = sqlite3_column_int64(select, 3);
	uint64_t dest_id = sqlite3_column_int64(select, 4);
	uint64_t blockchain = sqlite3_column_int64(select, 5);
	auto address_blob = sqlite3_column_blob(select, 6);
	uint32_t pool = sqlite3_column_int(select, 7);
	uint64_t asset = sqlite3_column_int64(select, 8);
	uint64_t amount_fp = sqlite3_column_int64(select, 9);
	auto amount_blob = sqlite3_column_blob(select, 10);
	unsigned delaytime = sqlite3_column_int(select, 11);
	auto commit_iv_blob = sqlite3_column_blob(select, 12);
	auto commitment_blob = sqlite3_column_blob(select, 13);
	uint64_t commitnum = sqlite3_column_int64(select, 14);
	auto serialnum_blob = sqlite3_column_blob(select, 15);
	const void *hashkey_blob = NULL;
	if (has_hashkey)
		hashkey_blob = sqlite3_column_blob(select, 16);

	if (!Billet::StatusIsValid(status))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect select returned invalid status " << status;

		return -1;
	}

	if (!address_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect address_blob is null";

		return -1;
	}

	unsigned address_size = sqlite3_column_bytes(select, 6);
	if (address_size != TX_ADDRESS_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect select returned address size " << address_size << " != " << TX_ADDRESS_BYTES;

		return -1;
	}

	if (!amount_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect amount is null";

		return -1;
	}

	unsigned amount_size = sqlite3_column_bytes(select, 10);
	if (amount_size != AMOUNT_UNSIGNED_PACKED_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect select returned amount size " << amount_size << " != " << AMOUNT_UNSIGNED_PACKED_BYTES;

		return -1;
	}

	if (!commit_iv_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect commit_iv is null";

		return -1;
	}

	unsigned commit_iv_size = sqlite3_column_bytes(select, 12);
	if (commit_iv_size != TX_COMMIT_IV_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect select returned commitment iv size " << commit_iv_size << " != " << TX_COMMIT_IV_BYTES;

		return -1;
	}

	if (!commitment_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect commitment is null";

		return -1;
	}

	unsigned commitment_size = sqlite3_column_bytes(select, 13);
	if (commitment_size != TX_COMMITMENT_BYTES)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::BilletSelect select returned commitment size " << commitment_size << " != " << TX_COMMITMENT_BYTES;

		return -1;
	}

	bool has_serialnum = Billet::HasSerialnum(status, flags);

	if (has_serialnum && !serialnum_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect serialnum is null; status " << status << " flags " << flags;

		return -1;
	}

	unsigned serialnum_size = (has_serialnum ? sqlite3_column_bytes(select, 15) : 0);
	if (has_serialnum && serialnum_size != TX_SERIALNUM_BYTES)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::BilletSelect select returned serialnum size " << serialnum_size << " != " << TX_SERIALNUM_BYTES;

		return -1;
	}

	if (has_hashkey && !hashkey_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelect hashkey is null; status " << status;

		return -1;
	}

	unsigned hashkey_size = (has_hashkey ? sqlite3_column_bytes(select, 16) : 0);
	if (has_hashkey && hashkey_size != TX_HASHKEY_BYTES)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::BilletSelect select returned hashkey size " << hashkey_size << " != " << TX_HASHKEY_BYTES;

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Wallet_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::BilletSelect simulating database error post-error check";

		return -1;
	}

	bill.id = id;
	bill.status = status;
	bill.flags = flags;
	bill.create_tx = create_tx;
	bill.dest_id = dest_id;
	bill.blockchain = blockchain;
	memcpy((void*)&bill.address, address_blob, TX_ADDRESS_BYTES);
	bill.pool = pool;
	bill.asset = asset;
	bill.amount_fp = amount_fp;
	unpack_unsigned_amount(amount_blob, bill.amount);
	bill.delaytime = delaytime;
	memcpy((void*)&bill.commit_iv, commit_iv_blob, TX_COMMIT_IV_BYTES);
	memcpy((void*)&bill.commitment, commitment_blob, TX_COMMITMENT_BYTES);
	bill.commitnum = commitnum;
	if (serialnum_blob)
		memcpy((void*)&bill.serialnum, serialnum_blob, TX_SERIALNUM_BYTES);
	if (hashkey_blob)
		memcpy((void*)&bill.hashkey, hashkey_blob, TX_HASHKEY_BYTES);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::BilletSelect returning " << bill.DebugString();

	return 0;
}

int DbConn::BilletSelectMulti(sqlite3_stmt *select, bool has_hashkeys, unsigned &nbills, Billet *bills, const unsigned maxbills, bool expect_row)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectMulti has_hashkeys " << has_hashkeys << " maxbills " << maxbills;

	nbills = 0;

	while (nbills <= maxbills)
	{
		bills[nbills].Clear();

		auto rc = BilletSelect(select, has_hashkeys, bills[nbills]);
		if (rc < 0) return rc;

		if (rc)
			break;

		if (nbills == maxbills)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSelectMulti overrun error maxbills " << maxbills;

			return -1;
		}

		++nbills;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectMulti maxbills " << maxbills << " returning nbills " << nbills;

	return 0;
}

int DbConn::BilletSelectId(uint64_t id, Billet& bill, bool or_greater)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectId id " << id << " or_greater = " << or_greater;

	CCASSERT(id);

	bill.Clear();

	// >= Id
	if (dblog(sqlite3_bind_int64(Billets_select, 1, id))) return -1;

	return BilletSelect(Billets_select, false, bill, !or_greater, !or_greater * id);
}

int DbConn::BilletSelectTxid(const void *address, const void *commitment, Billet& bill)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectTxid address " << buf2hex(address, TX_ADDRESS_BYTES) << " commitment " << buf2hex(commitment, TXID_COMMITMENT_BYTES);

	bill.Clear();

	// Address, Commitment
	if (dblog(sqlite3_bind_blob(Billets_select_txid, 1, address, TX_ADDRESS_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Billets_select_txid, 2, commitment, TXID_COMMITMENT_BYTES, SQLITE_STATIC))) return -1;

	return BilletSelect(Billets_select_txid, false, bill);
}

int DbConn::BilletSelectCommitnum(uint64_t commitnum, Billet& bill)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectCommitnum commitnum " << commitnum;

	bill.Clear();

	// Commitnum
	if (dblog(sqlite3_bind_int64(Billets_select_commitnum, 1, commitnum))) return -1;

	return BilletSelect(Billets_select_commitnum, false, bill);
}

int DbConn::BilletSelectUnspent(const bigint_t& amount, uint64_t id, Billet& bill)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	bill.Clear();

	packed_unsigned_amount_t packed_amount;

	if (amount)
		CCASSERTZ(pack_unsigned_amount(amount, packed_amount));
	else
		memset((void*)&packed_amount, -1, AMOUNT_UNSIGNED_PACKED_BYTES);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectUnspent amount " << amount << " id " << id << " packed_amount " << buf2hex(&packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES, 0);

	// <= Amount, >= Id
	if (dblog(sqlite3_bind_blob(Billets_select_unspent, 1, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Billets_select_unspent, 2, id))) return -1;

	return BilletSelect(Billets_select_unspent, false, bill);
}

int DbConn::BilletSelectAmount(uint64_t blockchain, uint64_t asset, const bigint_t& amount, unsigned delaytime, Billet& bill)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectAmount blockchain " << blockchain << " asset " << asset << " amount " << amount << " delaytime " << delaytime;

	bill.Clear();

	packed_unsigned_amount_t packed_amount;
	CCASSERTZ(pack_unsigned_amount(amount, packed_amount));

	// Asset, Blockchain, >= Amount, <= DelayTime
	if (dblog(sqlite3_bind_int64(Billets_select_amount, 1, asset))) return -1;
	if (dblog(sqlite3_bind_int64(Billets_select_amount, 2, blockchain))) return -1;
	if (dblog(sqlite3_bind_blob(Billets_select_amount, 3, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(Billets_select_amount, 4, delaytime))) return -1;

	return BilletSelect(Billets_select_amount, false, bill);
}

int DbConn::BilletSelectAmountScan(uint64_t blockchain, uint64_t asset, const bigint_t& amount, uint64_t id, Billet& bill)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectAmountScan blockchain " << blockchain << " asset " << asset << " amount " << amount << " id " << id;

	bill.Clear();

	packed_unsigned_amount_t packed_amount;
	CCASSERTZ(pack_unsigned_amount(amount, packed_amount));

	// Asset, Blockchain, >= Amount, >= Id
	if (dblog(sqlite3_bind_int64(Billets_select_amount_scan, 1, asset))) return -1;
	if (dblog(sqlite3_bind_int64(Billets_select_amount_scan, 2, blockchain))) return -1;
	if (dblog(sqlite3_bind_blob(Billets_select_amount_scan, 3, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(Billets_select_amount_scan, 4, id))) return -1;

	return BilletSelect(Billets_select_amount_scan, false, bill);
}

int DbConn::BilletSelectAmountMax(uint64_t blockchain, uint64_t asset, unsigned delaytime, Billet& bill)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectAmount blockchain " << blockchain << " asset " << asset << " delaytime " << delaytime;

	bill.Clear();

	// Asset, Blockchain, <= DelayTime
	if (dblog(sqlite3_bind_int64(Billets_select_amount_max, 1, asset))) return -1;
	if (dblog(sqlite3_bind_int64(Billets_select_amount_max, 2, blockchain))) return -1;
	if (dblog(sqlite3_bind_int(Billets_select_amount_max, 3, delaytime))) return -1;

	return BilletSelect(Billets_select_amount_max, false, bill);
}

int DbConn::BilletSelectCreateTx(uint64_t id, unsigned &nbills, Billet *bills, const unsigned maxbills)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectCreateTx id " << id << " maxbills " << maxbills;

	nbills = 0;

	// Id, limit
	if (dblog(sqlite3_bind_int64(Billets_select_createtx, 1, id))) return -1;
	if (dblog(sqlite3_bind_int(Billets_select_createtx, 2, maxbills + 1))) return -1;

	return BilletSelectMulti(Billets_select_createtx, false, nbills, bills, maxbills, true);
}

int DbConn::BilletSelectSpendTx(uint64_t id, unsigned &nbills, Billet *bills, const unsigned maxbills)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSelectSpendTx id " << id << " maxbills " << maxbills;

	nbills = 0;

	// Id, limit
	if (dblog(sqlite3_bind_int64(Billets_select_spendtx, 1, id))) return -1;
	if (dblog(sqlite3_bind_int(Billets_select_spendtx, 2, maxbills + 1))) return -1;

	return BilletSelectMulti(Billets_select_spendtx, true, nbills, bills, maxbills);
}
