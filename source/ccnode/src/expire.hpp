/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
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
	bool m_expire_age_can_change;

	virtual int GetExpires(int64_t& seqnum, SmartBuf *retobj, uint32_t& next_expires_t0) = 0;
	virtual int DeleteExpires(int64_t& seqnum, SmartBuf smartobj) = 0;

	thread *m_thread;
	DbConn *m_dbconn;

	void ThreadProc();
	void DoExpires();

public:
	ExpireObj(const char *name, int64_t min_seqnum, int64_t max_seqnum, int32_t expire_age, bool expire_age_can_change)
	 :	m_name(name),
		m_min_seqnum(min_seqnum),
		m_max_seqnum(max_seqnum),
		m_default_expire_age(expire_age),
		m_expire_age(expire_age),
		m_expire_age_can_change(expire_age_can_change),
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

	void ChangeExpireAge(unsigned i, int32_t age);
};

extern Expire g_expire;
