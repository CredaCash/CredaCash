/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
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
	memcpy((void*)this, &other, sizeof(*this));
}

string Account::DebugString() const
{
	ostringstream out;

	out << "Account";
	out << " id " << id;
	if (name[0])
		out << " name " << name;

	return out.str();
}
