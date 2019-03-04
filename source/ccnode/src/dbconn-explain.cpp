/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * dbconn-explain.cpp
*/

#include "ccnode.h"

#include <dblog.h>
#include <sqlite/sqlite3.h>

mutex g_db_explain_lock;

void DbExplainQueryPlan(sqlite3_stmt *pStmt)
{
	auto zSql = sqlite3_sql(pStmt);
	if (!zSql)
	{
		cerr << "DbExplainQueryPlan error in sqlite3_sql" << endl;
		return;
	}

	//cerr << "EXPLAIN QUERY PLAN " << zSql << endl;

	auto zExplain = sqlite3_mprintf("EXPLAIN QUERY PLAN %s", zSql);
	if (!zExplain)
	{
		cerr << "DbExplainQueryPlan error in sqlite3_mprintf, sql = " << zSql << endl;
		return;
	}

	sqlite3_stmt *pExplain;

	auto rc = sqlite3_prepare_v2(sqlite3_db_handle(pStmt), zExplain, -1, &pExplain, 0);

	if (rc)
	{
		cerr << "DbExplainQueryPlan sqlite3_prepare_v2 failed rc = " << rc << ", sql = " << zExplain << endl;
		return;
	}

	bool first = true;
	while (sqlite3_step(pExplain) == SQLITE_ROW)
	{
		auto iSelectid = sqlite3_column_int(pExplain, 0);
		auto iOrder = sqlite3_column_int(pExplain, 1);
		auto iFrom = sqlite3_column_int(pExplain, 2);
		auto zDetail = (const char *)sqlite3_column_text(pExplain, 3);

		if (first)
		{
			cerr << endl;
			cerr << zExplain << endl;
			first = false;
		}

		cerr << iSelectid << " " << iOrder << " " << iFrom << " " << zDetail << endl;
	}

	sqlite3_free(zExplain);
	sqlite3_finalize(pExplain);
}

void DbFinalize(sqlite3_stmt *pStmt, bool explain)
{
	if (pStmt)
	{
		if (explain)
			DbExplainQueryPlan(pStmt);

		dblog(sqlite3_finalize(pStmt));
	}
}
