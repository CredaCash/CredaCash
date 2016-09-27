/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * dir.hpp
*/

#pragma once

#define NAME_CHARS		16
#define NAME_BYTES		10
#define POINTER_BYTES	4

#define EXPIRE_ENTRIES	120
//#define EXPIRE_ENTRIES	20		// for testing

class Dir
{
	typedef uint32_t pointer_t;

#pragma pack(push, 1)

	union hostname_t
	{
		array<uint8_t, NAME_BYTES> bytes;

		struct
		{
			uint64_t a;
			uint16_t b;

		} words;
	};

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

	mutex m_lock;

	void inline ClearHostname(hostname_t& dest) const
	{
		dest.words.a = (uint64_t)(-1);
		dest.words.b = (uint16_t)(-1);
	}

	bool inline IsClearedHostname(const hostname_t& h) const
	{
		return h.words.a == (uint64_t)(-1) && h.words.b == (uint16_t)(-1);
	}

	void inline CopyHostname(hostname_t& dest, const hostname_t& src) const
	{
		dest.words.a = src.words.a;
		dest.words.b = src.words.b;
	}

	bool inline CompareHostnames(const hostname_t& h1, const hostname_t& h2) const
	{
		return h1.words.a == h2.words.a && h1.words.b == h2.words.b;
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
	void PickN(unsigned seed, unsigned n, string& namestr, uint8_t *buf, unsigned &bufpos);
	//void Delete(const string& namestr);
	//uint64_t Find(const string& namestr) const;

	static int NameToBinary(const string& namestr, hostname_t& name);
	static void NameToString(const hostname_t& name, string& namestr);
	static string NameToHex(const hostname_t& name);

	void ExpireProc();
};
