/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * expire.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <SmartBuf.hpp>

class ExpireObj
{
friend class Expire;
protected:
	const char *m_name;
	int64_t m_min_seqnum;
	int64_t m_max_seqnum;
	int32_t m_default_expire_age;
	volatile int32_t m_expire_age;
	bool m_is_valid_objs;

	virtual int GetExpires(int64_t& seqnum, SmartBuf *retobj, ccoid_t& oid, uint32_t& next_expires_t0) = 0;
	virtual int DeleteExpires(int64_t& seqnum, SmartBuf smartobj) = 0;

	thread *m_thread;
	DbConn *m_dbconn;

	void ThreadProc();
	void DoExpires();

public:
	ExpireObj(const char *name, int64_t min_seqnum, int64_t max_seqnum, int32_t expire_age, bool is_valid_objs)
	 :	m_name(name),
		m_min_seqnum(min_seqnum),
		m_max_seqnum(max_seqnum),
		m_default_expire_age(expire_age),
		m_expire_age(expire_age),
		m_is_valid_objs(is_valid_objs),
		m_thread(NULL),
		m_dbconn(NULL)
	{ }

	virtual ~ExpireObj() = default;

	void Start();
	void Stop();
};

class Expire
{
	vector<ExpireObj*> m_expireobjs;

public:
	void Init();
	void DeInit();

	int32_t GetExpireAge(unsigned i);
	void ChangeExpireAge(unsigned i, int32_t age);
};

extern Expire g_expire;
