/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * polling.hpp
*/

#pragma once

class PollThread;
class DbConn;
class TxQuery;

class Polling
{
	vector<PollThread*> m_pthreads;

public:
	Polling()
	{ }

	void Start(unsigned nthreads);
	void StartShutdown();
	void WaitForShutdown();
};

class PollThread
{
	thread *m_thread;
	DbConn *m_dbconn;
	TxQuery *m_txquery;

public:
	PollThread()
	 :	m_thread(NULL),
		m_dbconn(NULL),
		m_txquery(NULL)
	{ }

	void Start();
	void StartShutdown();
	void WaitForShutdown();
	void ThreadProc();
	int DoPoll(uint64_t checktime);
};
