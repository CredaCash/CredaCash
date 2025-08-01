/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * dbconn-explain.cpp
*/

#include "ccnode.h"
#include "dbconn.hpp"

#include <dblog.h>
#include <sqlite/sqlite3.h>

mutex g_db_explain_lock;

void DbExplainQueryPlan(const string& explainer, sqlite3_stmt *pStmt)
{
	sqlite3_stmt *pExplain;

	auto zSql = sqlite3_sql(pStmt);
	if (!zSql)
	{
		cerr << "DbExplainQueryPlan error in sqlite3_sql" << endl;
		return;
	}

	auto explainstr = explainer + zSql;

	auto rc = sqlite3_prepare_v2(sqlite3_db_handle(pStmt), explainstr.c_str(), -1, &pExplain, 0);

	if (rc)
	{
		cerr << "DbExplainQueryPlan sqlite3_prepare_v2 failed rc = " << rc << ", sql = " << explainstr << endl;
		return;
	}

	bool first = true;

	while (sqlite3_step(pExplain) == SQLITE_ROW)
	{
		if (first)
		{
			cerr << endl;
			cerr << explainstr << endl;
			first = false;
		}

		for (int i = 0; i < sqlite3_data_count(pExplain); ++i)
		{
			auto s = sqlite3_column_text(pExplain, i);
			if (s)
				cerr << " " << s;
		}

		cerr << endl;
	}

	sqlite3_finalize(pExplain);
}

void DbFinalize(sqlite3_stmt *pStmt, bool explain)
{
	if (pStmt)
	{
		if (explain)
		{
			DbExplainQueryPlan("EXPLAIN QUERY PLAN ", pStmt);
			//DbExplainQueryPlan("EXPLAIN ", pStmt);
		}

		dblog(sqlite3_finalize(pStmt));
	}
}
