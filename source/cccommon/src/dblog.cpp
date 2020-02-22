/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * dblog.cpp
*/

#include "CCdef.h"
#include "dblog.h"

#include <boost/log/trivial.hpp>

using namespace boost::log::trivial;

bool dbiserr(int rc, db_statement_type st)
{
	return dbresult(rc) && (st < DB_STMT_STEP || dbresult(rc) != SQLITE_DONE) && (st < DB_STMT_SELECT || dbresult(rc) != SQLITE_ROW);
}

int __dblog(const char *file, int line, int rc, db_statement_type st)
{
	if (dbiserr(rc, st))
	{
		BOOST_LOG_TRIVIAL(error) << "Database error " << rc << " at " << file << ":" << line << " --> " << sqlite3_errstr(rc);

		return dbresult(rc);
	}

	return false;
}

int __dbexec(const char *file, int line, sqlite3 *db, const char *sql, db_statement_type st)
{
	int rc = sqlite3_exec(db, sql, NULL, NULL, NULL);

	if (dbiserr(rc, st))
	{
		BOOST_LOG_TRIVIAL(error) << "Database error " << rc << " at " << file << ":" << line << " in \"" << sql << "\" --> " << sqlite3_errstr(rc);

		return dbresult(rc);
	}

	return false;
}
