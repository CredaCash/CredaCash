/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * dir.hpp
*/

#pragma once

#define POINTER_BYTES	4
#define EXPIRE_ENTRIES	120
//#define EXPIRE_ENTRIES	20		// for testing

class Dir
{
	typedef uint32_t pointer_t;

#pragma pack(push, 1)

	typedef array<uint8_t, TOR_HOSTNAME_BYTES> hostname_t;

	struct hashentry_t
	{
		pointer_t pointer;
		hostname_t hostname;
	};

#pragma pack(pop)

	char m_label[20];

	hostname_t *m_namelist;
	hashentry_t *m_hashtable;

	uint64_t m_list_size;
	uint64_t m_hash_size;
	unsigned m_list_entries_per_pointer;
	array<uint8_t, 16> m_hash_key;

	uint64_t m_list_head;
	uint64_t m_list_nentries;

	time_t m_expire_end_time;
	uint64_t m_expire_last_list_nentries;
	unsigned m_expire_count_index;
	array<uint64_t, EXPIRE_ENTRIES> m_expire_count;
	thread m_expire_thread;

	mutex m_dir_lock;

	void inline CopyHostname(hostname_t& dest, const hostname_t& src) const
	{
		memcpy(&dest, &src, sizeof(hostname_t));
	}

	bool inline CompareHostnames(const hostname_t& h1, const hostname_t& h2) const
	{
		return !memcmp(&h1, &h2, sizeof(hostname_t));
	}

	void inline ClearHostname(hostname_t& dest) const
	{
		memset(&dest, 0, sizeof(hostname_t));
	}

	bool inline IsClearedHostname(const hostname_t& h) const
	{
		static hostname_t z;

		return CompareHostnames(h, z);
	}

	uint64_t HashIndex(const string& namestr, hostname_t& name) const;
	uint64_t HashIndex2(const string& namestr, const hostname_t& name) const;
	uint64_t Find2(const string& namestr, const hostname_t& name, uint64_t hash_index) const;
	uint64_t FindTable(const string& namestr, const hostname_t& name, uint64_t hash_find) const;
	int Update(const string& namestr, const hostname_t& name, uint64_t hash_find);
	int FindExpire(uint64_t list_find);
	void AddList(const string& namestr, const hostname_t& name, uint64_t hash_find);

	void ExpireHead();

public:
	void Init(const char* label, int memfrac);
	void DeInit();

	int Add(const string& namestr);
	void PickN(unsigned seed, unsigned n, string& namestr, char *buf, unsigned &bufpos);
	//void Delete(const string& namestr);
	//uint64_t Find(const string& namestr) const;

	static int NameToBinary(const string& namestr, hostname_t& name);
	static void NameToString(const hostname_t& name, string& namestr);
	static string NameToHex(const hostname_t& name);

	void ExpireProc();
};
