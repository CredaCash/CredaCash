/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * dblog.cpp
*/

#include <dblog.h>

#include <boost/system/error_code.hpp>
#include <boost/log/trivial.hpp>

bool dbiserr(int rc, db_statement_type st)
{
	return rc && (st < DB_STMT_STEP || rc != SQLITE_DONE) && (st < DB_STMT_SELECT || rc != SQLITE_ROW);
}

int __dblog(const char *file, int line, int rc, db_statement_type st)
{
	if (dbiserr(rc, st))
	{
		BOOST_LOG_TRIVIAL(error) << "Database error " << rc << " at " << file << ":" << line << " --> " << sqlite3_errstr(rc);

		return rc;
	}

	return false;
}

int __dbexec(const char *file, int line, sqlite3 *db, const char *sql, db_statement_type st)
{
	int rc = sqlite3_exec(db, sql, NULL, NULL, NULL);

	if (dbiserr(rc, st))
	{
		BOOST_LOG_TRIVIAL(error) << "Database error " << rc << " at " << file << ":" << line << " in \"" << sql << "\" --> " << sqlite3_errstr(rc);

		return rc;
	}

	return false;
}
