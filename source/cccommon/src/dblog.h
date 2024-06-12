/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * dblog.h
*/

#pragma once

#include <sqlite/sqlite3.h>

enum db_statement_type
{
	DB_STMT_REG,
	DB_STMT_STEP,
	DB_STMT_SELECT
};

#define dbresult(rc) ((rc) & 0xFF)

bool dbiserr(int rc, db_statement_type st = DB_STMT_REG);

#define dblog(...) __dblog(__FILE__, __LINE__, __VA_ARGS__)
#define dbexec(...) __dbexec(__FILE__, __LINE__, __VA_ARGS__)

int __dblog(const char *file, int line, int rc, db_statement_type st = DB_STMT_REG);

int __dbexec(const char *file, int line, sqlite3 *db, const char *sql, db_statement_type st = DB_STMT_REG);
