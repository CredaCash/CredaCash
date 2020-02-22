/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * accounts.cpp
*/

#include "ccwallet.h"
#include "accounts.hpp"
#include "walletdb.hpp"

#include <dblog.h>

#define TRACE_ACCOUNTS	(g_params.trace_accounts)

Account::Account()
{
	Clear();
}

void Account::Clear()
{
	memset((void*)this, 0, sizeof(*this));
}

void Account::Copy(const Account& other)
{
	memcpy(this, &other, sizeof(*this));
}

string Account::DebugString() const
{
	ostringstream out;

	out << "id " << id;
	if (name[0])
		out << " name " << name;

	return out.str();
}
