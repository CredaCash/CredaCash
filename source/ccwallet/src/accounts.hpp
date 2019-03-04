/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * accounts.hpp
*/

#pragma once

class Account
{
public:
	static const unsigned MAX_NAME_LEN = 127;

	uint64_t id;
	char name[MAX_NAME_LEN+1];

	Account();

	void Clear();
	void Copy(const Account& other);
	string DebugString() const;
};
