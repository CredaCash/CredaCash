/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
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
	typedef snarklib::PPZK_ProvingKey<ZKPAIRING> ProveKey;
	typedef snarklib::PPZK_PrecompVerificationKey<ZKPAIRING> VerifyKey;

	unsigned nproof, nproofsave;
	vector<key_table_entry> keytable;
	vector<unsigned> workorder;
	vector<shared_ptr<ProveKey>> proofkey;

	unsigned nverify;
	vector<shared_ptr<VerifyKey>> verifykey;

	string GetKeyFileName(const unsigned keyindex, bool verify);

	bool TestKeyFit(const unsigned keyindex, const unsigned nout, const unsigned nin, const unsigned nin_with_path);

	shared_ptr<const ProveKey> LoadProofKey(const unsigned keyindex);

	bool LoadVerifyKey(const unsigned keyid);

public:
	void Init(bool reset = false);

	unsigned GetNKeys()
	{
		return nproof;
	}

	unsigned GetKeyId(unsigned keyindex);
	unsigned GetKeyIndex(uint16_t& nout, uint16_t& nin, uint16_t& nin_with_path, const bool test_largerkey = false);
	void SetTxCounts(unsigned keyindex, uint16_t& nout, uint16_t& nin, uint16_t& nin_with_path, bool verify = false);

	shared_ptr<const ProveKey> GetProofKey(const unsigned keyindex);
	shared_ptr<const VerifyKey> GetVerifyKey(const unsigned keyid);

	void PreLoadProofKeys();
	void PreLoadVerifyKeys(bool require_all);

	void UnloadProofKey(const unsigned keyindex);

#if TEST_SUPPORT_ZK_KEYGEN
public:
	void SaveKeyPair(const unsigned keyindex, const Keypair<ZKPAIRING>& keypair);
#endif
};
