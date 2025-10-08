/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
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

	void StartPolling(unsigned nthreads);
	void StartShutdown();
	void WaitForShutdown();

	static uint64_t EstimatedBlocktime(uint64_t checktime, uint64_t *conservative_lastblocktime = NULL);
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

	void StartPollThread();
	void StartShutdown();
	void WaitForShutdown();
	void ThreadProc();
	int DoPoll(uint64_t checktime);
};
