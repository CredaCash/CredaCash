/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * zkkeys.cpp
*/

#include "cclib.h"
#include "CCboost.hpp"
#include "zkkeys.hpp"

static mutex keylock;

unsigned ZKKeyStore::GetKeyId(unsigned keyindex)
{
	if ((int)keyindex == -1)
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

	nproof = (4 * 2 + 8) + (5 * 2 + 5);	// all keys, for testing and keygen
	nproof = (4 * 2 + 8);				// all keys with full merkle paths; comment out for keygen
	nproof = 4;	// since v1, the wallet has only used 4 keys
	//nproof = 4 * 2;					// for beta release

	nproofsave = 8;						// keep memory requirement low //@@! make this a config option?
	nproofsave = 0; // minimize use of page file
	//nproofsave = nproof;				// for releases with smaller keysets, and for benchmarking, and for mint
	//nproofsave = 0;					// for keygen

	keytable.resize(nproof);
	workorder.resize(nproof);
	proofkey.resize(nproof);

	nverify = nproof;	// currently, they are the same
	verifykey.resize(nverify);

	for (unsigned i = 0; i < nproof; ++i)
	{
		keytable[i].keyid = i;	// if mapping ever changes, the verify keys will need their own keytable in SetTxCounts()

		// ninw = {[0], 1, 2, 3, 4 [5, 6, 7, 8]}
		// nout = {2, 5, [10]}
		// nwo =  {0, [4]}

		unsigned nout, ninw, nwo = 0;

		if (i < 4*2)
		{
			ninw = i % 4 + 1;
			nout = (i / 4) * 3 + 2;
		}
		else if (i < 4*2 + 8)
		{
			nout = 10;
			ninw = i - 4*2 + 1;
		}
		else
		{
			nwo = 4;

			unsigned j = i - (4*2 + 8);

			if (j < 5*2)
			{
				ninw = j % 5 + 0;
				nout = (j / 5) * 3 + 2;
			}
			else
			{
				nout = 10;
				ninw = j - 5*2;

				if (ninw + nwo > 8)
					nwo = 8 - ninw;
			}
		}

		if (nproof < 3)
		{
			nout = 2;
			nwo = !i;
			ninw = i;
		}

		keytable[i].nout = nout;
		keytable[i].nin = ninw + nwo;
		keytable[i].nin_with_path = ninw;

		unsigned work = 12 * keytable[i].nout + 45 * nwo + 114 * ninw;
		keytable[i].work = work;

		//cerr << "keyid " << (i > 9 ? "" : " ") << keytable[i].keyid << " nin_with_path " << keytable[i].nin_with_path << " nin " << keytable[i].nin << " nout " << keytable[i].nout << " work " << keytable[i].work << endl;

		unsigned j = i;
		for ( ; j > 0 && keytable[workorder[j-1]].work > work; --j)
			workorder[j] = workorder[j-1];
		workorder[j] = i;
	}

	//for (unsigned i = 0; i < nproof; ++i)
	//	cerr << "workorder[" << i << "] = keytable index " << workorder[i] << " work " << keytable[workorder[i]].work << endl;
}

static wstring key_path;

void ZKKeyStore::SetKeyFilePath(const wstring& path)
{
	if (path.length())
		key_path = path;
	else
	{
#ifdef _WIN32
		auto env = _wgetenv(WIDE(KEY_PATH_ENV_VAR));
		if (env)
			key_path = env;
#else
		auto env = getenv(KEY_PATH_ENV_VAR);
		if (env)
			key_path = s2w(env);
#endif
	}
}

wstring ZKKeyStore::GetKeyFileName(const unsigned keyindex, bool verify)
{
	wstring name = key_path;

	if (!name.length())
		name = L"zkkeys";

	name += WIDE(PATH_DELIMITER);
	name += L"CC-ZK-";
	if (verify)
		name += L"Verify";
	else
		name += L"Prove";

	name += L"-Key-";
	name += to_wstring(keytable[keyindex].keyid);
	name += L"-";
	name += to_wstring(keytable[keyindex].nout);
	name += L"-";
	name += to_wstring(keytable[keyindex].nin_with_path);
	name += L"-";
	name += to_wstring(keytable[keyindex].nin - keytable[keyindex].nin_with_path);
	name += L".dat";

	//cerr << "key file " << w2s(name) << endl;

	return name;
}

#if TEST_SUPPORT_ZK_KEYGEN

void ZKKeyStore::SaveKeyPair(const unsigned keyindex, const Keypair<ZKPAIRING>& keypair)
{
	CCASSERT(keyindex < nproof);

	boost::filesystem::ofstream fs;

	auto name = GetKeyFileName(keyindex, false);
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

#endif // TEST_SUPPORT_ZK_KEYGEN

shared_ptr<const ZKKeyStore::ProveKey> ZKKeyStore::LoadProofKey(const unsigned keyindex)
{
	CCASSERT(keyindex < nproof);

	auto key = proofkey[keyindex];
	if (key)
		return key;

	lock_guard<mutex> lock(keylock);

	key = proofkey[keyindex];
	if (key)
		return key;

	key = shared_ptr<ProveKey>(new ProveKey);
	if (!key)
	{
		cerr << "*** error allocating proof key" << endl;

		return NULL;
	}

	boost::filesystem::ifstream fs;
	auto name = GetKeyFileName(keyindex, false);
	fs.open(name, fstream::binary | fstream::in);
	if (!fs.is_open())
	{
		//cerr << "LoadProofKey error opening file (file not found?) " << w2s(name) << endl;

		return NULL;
	}

	//key->m_pk.marshal_in(fs);
	auto rc = key->marshal_in_rawspecial(fs);
	fs.close();
	if (!rc || fs.bad())
	{
		cerr << "*** error reading proof key file " << w2s(name) << endl;

		return NULL;
	}

	//@cerr << "loaded proof key index " << keyindex << " file " << w2s(name) << endl;

	if (keyindex < nproofsave)
		proofkey[keyindex] = key;

	return key;
}

shared_ptr<const ZKKeyStore::ProveKey> ZKKeyStore::GetProofKey(const unsigned keyindex)
{
	return LoadProofKey(keyindex);
}

void ZKKeyStore::UnloadProofKey(const unsigned keyindex)
{
	CCASSERT(keyindex < nproof);

	lock_guard<mutex> lock(keylock);

	proofkey[keyindex] = NULL;
}

bool ZKKeyStore::LoadVerifyKey(const unsigned keyid)
{
	CCASSERT(keyid < nverify);

	if (verifykey[keyid])
		return false;

	lock_guard<mutex> lock(keylock);

	if (verifykey[keyid])
		return false;

	boost::filesystem::ifstream fs;
	auto name = GetKeyFileName(keyid, true);
	fs.open(name, fstream::binary | fstream::in);

	snarklib::PPZK_VerificationKey<ZKPAIRING> vk;
	auto rc = vk.marshal_in_rawspecial(fs);
	fs.close();

	if (!rc || fs.bad())
		return true;

	//@cerr << "preprocessing verify keyid " << keyid << " file " << w2s(name) << endl;

	verifykey[keyid] = shared_ptr<VerifyKey>(new VerifyKey(vk));

	//cerr << "done preprocessing verify keyid " << keyid << " file " << w2s(name) << endl;

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

void ZKKeyStore::PreLoadVerifyKeys(bool require_all)
{
	unsigned nloaded = 0;

	for (unsigned i = 0; i < nverify; ++i)
	{
		if (!LoadVerifyKey(i))
			++nloaded;
	}

	if (require_all)
		CCASSERT(nloaded == nverify);
	else
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

shared_ptr<const ZKKeyStore::VerifyKey> ZKKeyStore::GetVerifyKey(const unsigned keyid)
{
	LoadVerifyKey(keyid);

	return verifykey[keyid];
}
