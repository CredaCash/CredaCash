/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * dir.cpp
*/

#include "CCdef.h"
#include "dir.hpp"

#include <CCutil.h>
#include <siphash/siphash.h>

#define TRACE_EXPIRE	0

#define NULL_INDEX						((uint64_t)(-1))

#define N_SPECIAL_HASH_POINTER_VALS		2
#define HASH_POINTER_NULL				((pointer_t)(-1))
#define HASH_POINTER_CHAINED			((pointer_t)(-2))

#define SECONDS_PER_EXPIRE_COUNT	60
//#define SECONDS_PER_EXPIRE_COUNT	4	// for testing

void Dir::DeInit()
{
	if (m_expire_thread.joinable())
	{
		BOOST_LOG_TRIVIAL(info) << m_label << ": waiting for expire thread...";
		m_expire_thread.join();
	}

	BOOST_LOG_TRIVIAL(info) << m_label << ": cleaning up...";

	lock_guard<mutex> lock(m_lock);

#if 0	// for testing
	for (uint64_t i = 0; i < m_hash_size; ++i)
	{
		pointer_t p = m_hashtable[i].pointer;

		if (p != HASH_POINTER_NULL)
			BOOST_LOG_TRIVIAL(warning) << m_label << ": warning hashtable at index " << i << " is not NULL";
	}
#endif

	if (m_namelist)
	{
		free(m_namelist);
		m_namelist = NULL;
	}

	BOOST_LOG_TRIVIAL(info) << m_label << ": done";
}

void Dir::Init(const char* label, int memfrac)
{
	CCASSERT(!m_namelist);

	strcpy(m_label, label);

	uint64_t nbytes = ((uint64_t)g_datamem) << 30;
	nbytes = (nbytes * memfrac) / 1000;
	//nbytes = 50*(2*NAME_BYTES + POINTER_BYTES);	// for testing
	uint64_t hash_entries = nbytes/(unsigned)((NAME_BYTES + POINTER_BYTES) + NAME_BYTES * g_hashfill/100U);
	uint64_t hash_bytes = hash_entries * (NAME_BYTES + POINTER_BYTES);
	uint64_t list_entries = (nbytes - hash_bytes) / NAME_BYTES;
	uint64_t list_bytes = list_entries * NAME_BYTES;
	nbytes = hash_bytes + list_bytes;

	BOOST_LOG_TRIVIAL(info) << m_label << ": Hash table entries " << hash_entries << " nbytes " << hash_bytes;
	BOOST_LOG_TRIVIAL(info) << m_label << ": List entries " << list_entries << " nbytes " << list_bytes;
	BOOST_LOG_TRIVIAL(info) << m_label << ": Total memory " << nbytes;

	CCASSERT(sizeof(hostname_t) == NAME_BYTES);
	CCASSERT(sizeof(pointer_t) == POINTER_BYTES);
	CCASSERT(sizeof(hashentry_t) == NAME_BYTES + POINTER_BYTES);

	char *p = (char *)malloc(nbytes);
	if (!p)
	{
			BOOST_LOG_TRIVIAL(fatal) << m_label << ": FATAL ERROR: Unable to allocate memory for directory data";
			exit(-1);
			throw exception();
	}

	memset(p, -1, nbytes);	// sets all values to -1

	m_namelist = (hostname_t *)(p);
	m_hashtable = (hashentry_t *)(p + list_bytes);

	m_list_size = list_entries;
	m_hash_size = hash_entries;

	m_list_entries_per_pointer = ((m_list_size - 1 + N_SPECIAL_HASH_POINTER_VALS) >> (8*POINTER_BYTES)) + 1;

	BOOST_LOG_TRIVIAL(info) << m_label << ": List entries per hash table pointer = " << m_list_entries_per_pointer;

	if (m_list_entries_per_pointer != 1)
	{
		BOOST_LOG_TRIVIAL(fatal) << m_label << ": FATAL ERROR: code has not been tested for list_entries_per_pointer > 1";
		exit(-1);
		throw exception();
	}

	random_device rd;

	if (rd.entropy() < 8*sizeof(unsigned) - 1)
		BOOST_LOG_TRIVIAL(warning) << m_label << " random_device reports low entropy of " << rd.entropy();

	for (unsigned i = 0; i < sizeof(m_hash_key); ++i)
	{
		m_hash_key[i] = rd();
		//BOOST_LOG_TRIVIAL(trace) << m_label << ": hashkey[" << i << "] = " << m_hash_key[i];
	}

	m_expire_end_time = time(NULL) + g_expire * (unsigned)SECONDS_PER_EXPIRE_COUNT / (unsigned)EXPIRE_ENTRIES;

	//return;	// for testing

	BOOST_LOG_TRIVIAL(trace) << m_label << ": creating thread for ExpireProc this = " << this;
	thread worker(&Dir::ExpireProc, this);
	m_expire_thread.swap(worker);
}

uint64_t Dir::HashIndex(const string& namestr, hostname_t& name) const
{
	if (NameToBinary(namestr, name))
		return NULL_INDEX;

	return HashIndex2(namestr, name);
}

uint64_t Dir::HashIndex2(const string& namestr, const hostname_t& name) const
{
	uint64_t hash_index = sip_hash24(&m_hash_key[0], &name.bytes[0], NAME_BYTES, false) % m_hash_size;

	//BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " hashes to " << hash_index;

	return hash_index;
}

// returns expiration seconds from now
int Dir::Add(const string& namestr)
{
	hostname_t name;

	uint64_t hash_index = HashIndex(namestr, name);
	if (hash_index == NULL_INDEX)
		return -1;

	lock_guard<mutex> lock(m_lock);

	// prepare to add to hashlist

	if (m_list_nentries == m_list_size)
	{
		BOOST_LOG_TRIVIAL(trace) << m_label << ": no room in hashlist for hostname " << namestr;
		return (unsigned)(-1);
	}

	// prepare to add to hashtable

	uint64_t hash_find = hash_index;
	uint64_t hash_add = NULL_INDEX;

	while (true)
	{
		pointer_t p = m_hashtable[hash_find].pointer;
		//BOOST_LOG_TRIVIAL(trace) << m_label << ": hashtable index " << hash_find << " pointer " << p;

		if (p == HASH_POINTER_NULL)
		{
			if (hash_add == NULL_INDEX)
				hash_add = hash_find;

			break;
		}

		if (p == HASH_POINTER_CHAINED)
		{
			if (hash_add == NULL_INDEX)
				hash_add = hash_find;
		}
		else if (CompareHostnames(m_hashtable[hash_find].hostname, name))
		{
			BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " already in hashtable at " << hash_find << " (start " << hash_index << ")";
			return Update(namestr, name, hash_find);
		}

		if (++hash_find == m_hash_size)
			hash_find = 0;
	}

	// add to hashlist and update hashindex

	AddList(namestr, name, hash_add);

	BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " added to hashtable at index " << hash_add << " (initial index " << hash_index << ")";

	return g_expire * SECONDS_PER_EXPIRE_COUNT;
}

int Dir::Update(const string& namestr, const hostname_t& name, uint64_t hash_find)
{
	uint64_t list_find = FindTable(namestr, name, hash_find);
	if (list_find == NULL_INDEX)
		return -1;

	int expires = FindExpire(list_find);

	if (expires < 0 || expires * 4 > g_expire * SECONDS_PER_EXPIRE_COUNT)
	{
		BOOST_LOG_TRIVIAL(trace) << m_label << ": keeping existing entry for hostname " << namestr << " that expires in " << expires;

		return expires;
	}

	// clear existing entry
	ClearHostname(m_namelist[list_find]);

	// add to hashlist and update hashindex

	AddList(namestr, name, hash_find);

	BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " entry expiring in " << expires << " updated in hashtable at index " << hash_find;

	return g_expire * SECONDS_PER_EXPIRE_COUNT;
}

void Dir::AddList(const string& namestr, const hostname_t& name, uint64_t hash_find)
{
	// add to hashlist

	uint64_t list_index = m_list_head + m_list_nentries;
	if (list_index >= m_list_size)
		list_index -= m_list_size;
	++m_list_nentries;

	CopyHostname(m_namelist[list_index], name);

	BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " added to namelist at index " << list_index << " (head " << m_list_head << " nentries " << m_list_nentries << ")";

	// update hashtable

	m_hashtable[hash_find].pointer = (pointer_t)(list_index / m_list_entries_per_pointer);
	CopyHostname(m_hashtable[hash_find].hostname, name);
}


void Dir::PickN(unsigned seed, unsigned n, string& namestr, uint8_t *buf, unsigned &bufpos)
{
	auto ne = m_list_nentries;

	if (ne == 0)
		return;

	if (n > ne)
		n = ne;

	uint64_t pos = seed % ne;

	bool need_comma = false;

	for (unsigned i = 0; i < n; ++i)
	{
		uint64_t nextpos = (pos + ne / n) % ne;
		if (ne == 1)
			++nextpos;

		while (pos != nextpos)
		{
			hostname_t& hostname = m_namelist[m_list_head + pos];
			if (IsClearedHostname(hostname))
			{
				pos = (pos + 1) % ne;
				continue;
			}

			if (need_comma)
				buf[bufpos++] = ',';

			need_comma = true;

			buf[bufpos++] = '"';

			NameToString(hostname, namestr);
			memcpy(&buf[bufpos], &namestr[0], namestr.size());
			bufpos += namestr.size();

			buf[bufpos++] = '"';

			break;
		}

		pos = nextpos;
	}
}

// not used
#if 0	// note: if this were used, pointer = HASH_POINTER_NULL would need to be fixed
void Dir::Delete(const string& namestr)
{
	hostname_t name;

	uint64_t hash_index = HashIndex(namestr, name);
	if (hash_index == NULL_INDEX)
		return;

	lock_guard<mutex> lock(m_lock);

	uint64_t hash_find = Find2(namestr, name, hash_index);
	if (hash_find == NULL_INDEX)
		return;

	uint64_t list_find = FindTable(namestr, name, hash_find);

	m_hashtable[hash_find].pointer = HASH_POINTER_NULL;

	BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " deleted from hashtable at index " << hash_find;

	if (list_find == NULL_INDEX)
		return;

	// we just clear the list entry at this point -- to keep expiration simple, that is the only way they come out
	ClearHostname(m_namelist[list_find]);

	BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " cleared from hashlist at index " << list_find << " (head " << m_list_head << " nentries " << m_list_nentries << ")";
}
#endif

void Dir::ExpireHead()
{
	lock_guard<mutex> lock(m_lock);

	if (m_list_nentries == 0)
		return;

	uint64_t list_find = m_list_head;
	const hostname_t& name = m_namelist[list_find];

	if (++m_list_head == m_list_size)
		m_list_head = 0;
	--m_list_nentries;

	if (IsClearedHostname(name))
	{
		BOOST_LOG_TRIVIAL(trace) << m_label << ": cleared entry expired from hashlist at index " << list_find << " (new head " << m_list_head << " nentries " << m_list_nentries << ")";

		return;
	}

	string namestr(NAME_CHARS, 0);
	NameToString(name, namestr);

	uint64_t hash_index = HashIndex2(namestr, name);

	uint64_t hash_find = Find2(namestr, name, hash_index);
	if (hash_find == NULL_INDEX)
		return;

	// mark temporarily as chained to right
	m_hashtable[hash_find].pointer = HASH_POINTER_CHAINED;

	BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " expired from hashlist at index " << list_find << " (new head " << m_list_head << " nentries " << m_list_nentries << ") and hashtable at index " << hash_find;

	// look to right to see if there are any entries in use
	while (true)
	{
		if (++hash_find == m_hash_size)
			hash_find = 0;

		pointer_t p = m_hashtable[hash_find].pointer;

		if (p == HASH_POINTER_NULL)
			break;	// found no entries in use

		if (p != HASH_POINTER_CHAINED)
			return;	// found an entry in use, so leave expired entry as HASH_POINTER_CHAINED
	}

	// found no entry in use on right, so all pointers to left of the HASH_POINTER_NULL should also be set to HASH_POINTER_NULL
	while (true)
	{
		if (--hash_find >= m_hash_size)
			hash_find = m_hash_size - 1;

		pointer_t p = m_hashtable[hash_find].pointer;

		if (p != HASH_POINTER_CHAINED)
			return;	// found an entry in use so we're done

		m_hashtable[hash_find].pointer = HASH_POINTER_NULL;

		BOOST_LOG_TRIVIAL(trace) << m_label << ": hashtable at index " << hash_find << " set to NULL";
	}
}

void Dir::ExpireProc()
{
	BOOST_LOG_TRIVIAL(trace) << m_label << ": expire thread this=" << this;

	uint64_t expired = 0;

	while (!g_shutdown)
	{
		time_t now = time(NULL);
		int delta_t = (int)m_expire_end_time - (int)now;
		if (delta_t < -600)
			m_expire_end_time = now - 600;

		uint64_t nexpire = m_expire_count[m_expire_count_index] - expired;

		if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << m_label << ": left to expire " << nexpire << " in time " << delta_t;

		if (delta_t > 1)
			nexpire /= delta_t;

		if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << m_label << ": expiring " << nexpire;

		for (uint64_t i = 0; i < nexpire; ++i)
			ExpireHead();

		expired += nexpire;

		if (delta_t <= 0)
		{
			m_expire_end_time += g_expire * SECONDS_PER_EXPIRE_COUNT / (unsigned)EXPIRE_ENTRIES;

			CCASSERT(expired == m_expire_count[m_expire_count_index]);

			if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << m_label << ": adding to expire " << expired << " + " << m_list_nentries << " - " << m_expire_last_list_nentries;

			lock_guard<mutex> lock(m_lock);

			m_expire_count[m_expire_count_index] = expired + m_list_nentries - m_expire_last_list_nentries;
			m_expire_last_list_nentries = m_list_nentries;
			expired = 0;

			if (++m_expire_count_index == EXPIRE_ENTRIES)
				m_expire_count_index = 0;
		}

		sleep(1);
	}
}

int Dir::FindExpire(uint64_t list_find)
{
	uint64_t list_hi = m_list_head;
	unsigned expire_count_index = m_expire_count_index;
	unsigned period = 0;

	while (true)
	{
		uint64_t list_lo = list_hi;
		list_hi += m_expire_count[expire_count_index];

		if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << m_label << ": period " << period << " expire_count_index " << expire_count_index << " expire_count " << m_expire_count[expire_count_index] << " list_lo " << list_lo << " list_find " << list_find << " list_hi " << list_hi;

		if (list_lo <= list_find && list_find < list_hi)
			return period * g_expire * SECONDS_PER_EXPIRE_COUNT / EXPIRE_ENTRIES;

		if (++expire_count_index == EXPIRE_ENTRIES)
				expire_count_index = 0;

		if (++period == EXPIRE_ENTRIES)
			return -1;
	}
}

#if 0 // not used
uint64_t Dir::Find(const string& namestr) const
{
	hostname_t name;

	uint64_t hash_index = HashIndex(namestr, name);
	if (hash_index == NULL_INDEX)
		return hash_index;

	return Find2(namestr, name, hash_index);
}
#endif

uint64_t Dir::Find2(const string& namestr, const hostname_t& name, uint64_t hash_index) const
{
	uint64_t hash_find = hash_index;

	while (true)
	{
		pointer_t p = m_hashtable[hash_find].pointer;

		if (p == HASH_POINTER_NULL)
		{
			BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " not found in hashtable (start " << hash_index << " stop " << hash_find << ")";
			return NULL_INDEX;
		}

		if (p != HASH_POINTER_CHAINED && CompareHostnames(m_hashtable[hash_find].hostname, name))
		{
			BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " found in hashtable at " << hash_find << " (start " << hash_index << ")";
			return hash_find;
		}

		if (++hash_find == m_hash_size)
			hash_find = 0;
	}
}

uint64_t Dir::FindTable(const string& namestr, const hostname_t& name, uint64_t hash_find) const
{
	pointer_t p = m_hashtable[hash_find].pointer;
	if (p >= m_list_size)
	{
		BOOST_LOG_TRIVIAL(error) << m_label << ": ERROR in FindTable: invalid pointer for hostname " << namestr << " hash index " << hash_find << " list size " << m_list_size;
		return NULL_INDEX;
	}

	uint64_t list_index = p * m_list_entries_per_pointer;
	uint64_t list_find = list_index;

	for (unsigned i = 0; ; )
	{
		if (CompareHostnames(m_namelist[list_find], name))
		{
			BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " found in hashlist at " << list_find << " (start " << list_index << ")";
			return list_find;
		}

		if (++i == m_list_entries_per_pointer)
			break;

		if (++list_find == m_list_size)
			list_find = 0;
	}

	BOOST_LOG_TRIVIAL(trace) << m_label << ": hostname " << namestr << " not found in hashlist (start " << list_index << " stop " << list_find << ")";

	return NULL_INDEX;
}

int Dir::NameToBinary(const string& namestr, hostname_t& name)
{
	if (namestr.size() != NAME_CHARS)
	{
		BOOST_LOG_TRIVIAL(trace) << "invalid length hostname " << namestr;
		return -1;
	}

	unsigned index = 0;
	unsigned bits = 0;
	unsigned accum = 0;

	for (auto c : namestr)
	{
		if (c < '2')
			goto charerror;
		else if (c <= '7')
			c += 26 - '2';
		else if (c < 'A')
			goto charerror;
		else if (c <= 'Z')
			c -= 'A';
		else if (c < 'a')
			goto charerror;
		else if (c <= 'z')
			c -= 'a';
		else
			goto charerror;

		accum = (accum << 5) | c;
		bits += 5;
		if (bits >= 8)
		{
			bits -= 8;
			name.bytes[index++] = accum >> bits;
		}
	}

	//BOOST_LOG_TRIVIAL(trace) << "hostname " << namestr << " hex " << NameToHex(name);

	return 0;

charerror:
	BOOST_LOG_TRIVIAL(trace) << "invalid character hostname " << namestr;
	return -1;
}

void Dir::NameToString(const hostname_t& name, string& namestr)
{
	namestr.erase();

	unsigned bits = 0;
	unsigned accum = 0;

	for (unsigned i = 0; i < NAME_BYTES; ++i)
	{
		uint8_t b = name.bytes[i];
		accum = (accum << 8) | b;
		bits += 8;
		while (bits >= 5)
		{
			bits -= 5;
			uint8_t c = (accum >> bits) & 31;
			if (c < 26)
				c += 'a';
			else
				c += '2' - 26;

			namestr.push_back(c);
		}
	}

	//namestr.push_back(0);

	//BOOST_LOG_TRIVIAL(trace) << "hostname hex " << NameToHex(name) << " string " << namestr;

	CCASSERT(bits == 0);

	//return namestr;
}

string Dir::NameToHex(const hostname_t& name)
{
	return buf2hex(&name, NAME_BYTES);
}
