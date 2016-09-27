/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * zkkeys.cpp
*/

#include "CCdef.h"

#include <chrono>

#include "zkkeys.hpp"

unsigned ZKKeyStore::GetKeyId(unsigned keyindex)
{
	if (keyindex == (unsigned)(-1))
		return -1;

	CCASSERT(keyindex < nproof);

	return keytable[keyindex].keyid;
}

void ZKKeyStore::Init(bool reset)
{
	if (reset)
	{
		proofkey.clear();
		verifykey.clear();
		proofkey.resize(nproof);
		verifykey.resize(nverify);
	}

	if (nproof)
		return;

	nproof = 4*(TX_MAXINPATH + 1);
	//nproof = 4*(TX_MAXINPATH/2 + 1);			// for testing
	//nproof = 4*(2 + 1);						// for testing
	//nproof = 1;								// for testing

	nproofsave = 4*(8 + 1);		// !!! allow this to be changed

	keytable.resize(nproof);
	workorder.resize(nproof);
	proofkey.resize(nproof);

	nverify = nproof;	// currently, they are the same
	verifykey.resize(nverify);

	for (unsigned i = 0; i < nproof; ++i)
	{
		keytable[i].keyid = i;	// if mapping ever changes, the verify keys will need their own keytable in SetTxCounts()
		unsigned gen = i;
		//gen = 10;								// for testing
		//gen = 4*(TX_MAXINPATH + 1) - 1;		// for testing
		keytable[i].nout = (gen % 2)*12 + 4;
		keytable[i].nin_with_path = gen / 4;
		unsigned nwo = ((gen / 2) % 2) * 2;
		if (!keytable[i].nin_with_path)
			nwo = (nwo * 6) + 4;
		keytable[i].nin = keytable[i].nin_with_path + nwo;
		unsigned work = keytable[i].nout * 21 + keytable[i].nin_with_path * 380 + nwo * 19; // !!! re-benchmark this and adjust coefficients
		keytable[i].work = work;

		//cerr << "keytable " << i << " keyid " << keytable[i].keyid << " nin_with_path " << keytable[i].nin_with_path << " nin " << keytable[i].nin << " nout " << keytable[i].nout << " work " << keytable[i].work << endl;

		unsigned j = i;
		for ( ; j > 0 && keytable[workorder[j-1]].work > work; --j)
			workorder[j] = workorder[j-1];
		workorder[j] = i;
	}

	//for (unsigned i = 0; i < nproof; ++i)
	//	cerr << "workorder[" << i << "] = keytable index " << workorder[i] << " work " << keytable[workorder[i]].work << endl;
}

string ZKKeyStore::GetKeyFileName(const unsigned keyindex, bool verifykey)
{
	string name = "zkkeys\\";							// !!! make this configuable?
	name += "CC-ZK-";
	if (verifykey)
		name += "Verify";
	else
		name += "Prove";

	name += "-Key-v0-";
	name += to_string(keytable[keyindex].nout);
	name += "-";
	name += to_string(keytable[keyindex].nin_with_path);
	name += "-";
	name += to_string(keytable[keyindex].nin - keytable[keyindex].nin_with_path);
	name += ".dat";

	return name;
}

#if SUPPORT_ZK_KEYGEN

void ZKKeyStore::SaveKeyPair(const unsigned keyindex, const Keypair<ZKPAIRING>& keypair)
{
	CCASSERT(keyindex < nproof);

	string name;
	ofstream fs;

	name = GetKeyFileName(keyindex, false);
	fs.open(name, fstream::binary | fstream::out);
	CCASSERT(fs.is_open());
	keypair.pk().marshal_out_rawspecial(fs);
	fs.close();

	name = GetKeyFileName(keyindex, true);
	fs.open(name, fstream::binary | fstream::out);
	CCASSERT(fs.is_open());
	keypair.vk().marshal_out_rawspecial(fs);
	fs.close();
}

#endif // SUPPORT_ZK_KEYGEN

const shared_ptr<ZKKeyStore::ProvingKey> ZKKeyStore::LoadProofKey(const unsigned keyindex)
{
	CCASSERT(keyindex < nproof);

	auto key = proofkey[keyindex];
	if (key)
		return key;

	key = shared_ptr<ProvingKey>(new ProvingKey);
	if (!key)
	{
		cerr << "*** error allocating proof key" << endl;
		return NULL;
	}

	ifstream fs;
	string name = GetKeyFileName(keyindex, false);
	fs.open(name, fstream::binary | fstream::in);
	if (!fs.is_open())
	{
		//cerr << "LoadProofKey error opening file (file not found?) " << name << endl;
		return NULL;
	}

	//key->m_pk.marshal_in(fs);
	auto rc = key->marshal_in_rawspecial(fs);
	fs.close();
	if (!rc || fs.bad())
	{
		cerr << "*** error reading proof key file " << name << endl;
		return NULL;
	}

	//@cerr << "loaded proof key index " << keyindex << " file " << name << endl;

	if (keyindex < nproofsave)
		proofkey[keyindex] = key;

	return key;
}

const shared_ptr<ZKKeyStore::ProvingKey> ZKKeyStore::GetProofKey(const unsigned keyindex)
{
	return LoadProofKey(keyindex);
}

void ZKKeyStore::UnloadProofKey(const unsigned keyindex)
{
	CCASSERT(keyindex < nproof);

	proofkey[keyindex] = NULL;
}

bool ZKKeyStore::LoadVerifyKey(const unsigned keyid)
{
	CCASSERT(keyid < nverify);

	if (verifykey[keyid])
		return false;

	ifstream fs;
	string name = GetKeyFileName(keyid, true);
	fs.open(name, fstream::binary | fstream::in);

	snarklib::PPZK_VerificationKey<ZKPAIRING> vk;
	auto rc = vk.marshal_in_rawspecial(fs);
	fs.close();

	if (!rc || fs.bad())
		return true;

	//@cerr << "preprocessing verify keyid " << keyid << " file " << name << endl;

	verifykey[keyid] = unique_ptr<snarklib::PPZK_PrecompVerificationKey<ZKPAIRING>>(new snarklib::PPZK_PrecompVerificationKey<ZKPAIRING>(vk));

	//cerr << "done preprocessing verify keyid " << keyid << " file " << name << endl;

	return false;
}

void ZKKeyStore::PreLoadProofKeys()
{
	unsigned nloaded = 0;

	for (unsigned i = 0; i < nproofsave; ++i)
	{
		if (LoadProofKey(i))
			++nloaded;
	}

	CCASSERT(nloaded);
}

void ZKKeyStore::PreLoadVerifyKeys()
{
	unsigned nloaded = 0;

	for (unsigned i = 0; i < nverify; ++i)
	{
		if (!LoadVerifyKey(i))
			++nloaded;
	}

	CCASSERT(nloaded);
}

void ZKKeyStore::SetTxCounts(unsigned keyindex, uint16_t& nout, uint16_t& nin, uint16_t& nin_with_path, bool verify)
{
	// If verify is true, then "keyindex" is a keyid. If keyindex != keyid, then verify will need its own keytable

	if (keyindex < nproof)
	{
		nout = keytable[keyindex].nout;
		nin = keytable[keyindex].nin;
		nin_with_path = keytable[keyindex].nin_with_path;
	}
	else
	{
		nout = 0;
		nin = 0;
		nin_with_path = 0;
	}
}

bool ZKKeyStore::TestKeyFit(const unsigned keyindex, const unsigned nout, const unsigned nin, const unsigned nin_with_path)
{
	CCASSERT(keyindex < nproof);

	return nout <= keytable[keyindex].nout && nin <= keytable[keyindex].nin && nin_with_path <= keytable[keyindex].nin_with_path;
}

unsigned ZKKeyStore::GetKeyIndex(uint16_t& nout, uint16_t& nin, uint16_t& nin_with_path, const bool test_largerkey)
{
	Init();

	if (test_largerkey)
	{
		for (unsigned tries = 0; tries < 2*nproof; ++tries)
		{
			unsigned keyindex = rand() % nproof;

			if (!TestKeyFit(keyindex, nout, nin, nin_with_path))
				continue;

			if (!LoadProofKey(keyindex))
				continue;

			SetTxCounts(keyindex, nout, nin, nin_with_path);

			return keyindex;
		}
	}

	for (unsigned i = 0; i < nproof; ++i)
	{
		unsigned keyindex = workorder[i];

		if (!TestKeyFit(keyindex, nout, nin, nin_with_path))
			continue;

		if (!LoadProofKey(keyindex))
			continue;

		SetTxCounts(keyindex, nout, nin, nin_with_path);

		return keyindex;
	}

	return -1;
}

const snarklib::PPZK_PrecompVerificationKey<ZKPAIRING> ZKKeyStore::GetVerifyKey(const unsigned keyid)
{
	LoadVerifyKey(keyid);

	CCASSERT(verifykey[keyid]);

	return *verifykey[keyid];
}
