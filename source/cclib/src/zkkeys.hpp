/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * zkkeys.hpp
*/

#pragma once

#include "CCproof.h"
#include "CCproof.hpp"

struct key_table_entry
{
	unsigned keyid;
	unsigned nout;
	unsigned nin;
	unsigned nin_with_path;
	unsigned work;
};

class ZKKeyStore
{
	//typedef Keypair<ZKPAIRING> ProvingKey;
	typedef snarklib::PPZK_ProvingKey<ZKPAIRING> ProvingKey;

	unsigned nproof;
	vector<key_table_entry> keytable;
	vector<unsigned> workorder;
	unsigned nproofsave;
	vector<shared_ptr<ProvingKey>> proofkey;

	unsigned nverify;
	vector<unique_ptr<snarklib::PPZK_PrecompVerificationKey<ZKPAIRING>>> verifykey;

	string GetKeyFileName(const unsigned keyindex, bool verifykey);

	bool TestKeyFit(const unsigned keyindex, const unsigned nout, const unsigned nin, const unsigned nin_with_path);

	const shared_ptr<ZKKeyStore::ProvingKey> LoadProofKey(const unsigned keyindex);

	bool LoadVerifyKey(const unsigned keyid);

public:
	void Init(bool reset = false);

	unsigned GetNKeys()
	{
		return nproof;
	}

	unsigned GetKeyIndex(uint16_t& nout, uint16_t& nin, uint16_t& nin_with_path, const bool test_largerkey = false);
	void SetTxCounts(unsigned keyindex, uint16_t& nout, uint16_t& nin, uint16_t& nin_with_path, bool verify = false);

	const shared_ptr<ProvingKey> GetProofKey(const unsigned keyindex);

	unsigned GetKeyId(unsigned keyindex);
	const snarklib::PPZK_PrecompVerificationKey<ZKPAIRING> GetVerifyKey(const unsigned keyid);

	void PreLoadProofKeys();
	void PreLoadVerifyKeys();

	void UnloadProofKey(const unsigned keyindex);

#if SUPPORT_ZK_KEYGEN
public:
	void SaveKeyPair(const unsigned keyindex, const Keypair<ZKPAIRING>& keypair);
#endif
};
