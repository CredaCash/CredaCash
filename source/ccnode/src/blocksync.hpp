/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * blocksync.hpp
*/

#pragma once

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

#include <CCobjdefs.h>

#pragma pack(push, 1)

class BlockSyncEntry
{
public:
	uint64_t level;
	uint16_t nlevels;

	BlockSyncEntry(uint64_t l = 0, uint16_t n = 0)
	 :	level(l),
		nlevels(n)
	{ }

	BlockSyncEntry(const BlockSyncEntry& other)
	 :	level(other.level),
		nlevels(other.nlevels)
	{ }

	BlockSyncEntry& operator= (const BlockSyncEntry& other)
	{
		level = other.level;
		nlevels = other.nlevels;
		return *this;
	}
};

class BlockSyncMsg
{
public:

	uint32_t size;
	uint32_t tag;
	BlockSyncEntry entry;

	BlockSyncMsg()
	:	size(sizeof(BlockSyncMsg)),
		tag(CC_CMD_SEND_LEVELS)
	{ }
};

#pragma pack(pop)

class BlockSyncList
{
	deque<BlockSyncEntry> m_list;
	uint64_t m_next_level;

	FastSpinLock m_block_sync_list_lock;

public:
	BlockSyncList()
	 :	m_block_sync_list_lock(__FILE__, __LINE__)
	{ }

	void Init(int64_t level);

	bool HasRequeues();

	BlockSyncEntry GetNextEntry(const string& name, int conn_index);
	void RequeueEntry(const string& name, int conn_index, const BlockSyncEntry& entry);
};


class BlockSyncConnection : public CCServer::Connection
{
public:
	BlockSyncConnection(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	 :	CCServer::Connection(manager, io_service, connfac)
	{
		m_read_after_write = true;
	}

	void HandleValidateDone(uint64_t level, uint32_t callback_id, int64_t result);

private:

	mutex req_lock;

	BlockSyncMsg m_cur_req_msg;
	BlockSyncMsg m_next_req_msg;

	atomic_flag m_read_in_progress;

	atomic<int> m_validations_pending;

	bool m_has_requeues;
	bool m_finished;

	void StartConnection();

	void CheckSendReq(unique_lock<mutex> &lock);

	void StartNextRead();

	void HandleReadComplete();

	void HandleObjReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter);

	bool SetValidationTimer();

	void FinishConnection();
};


class BlockSyncClient : public TorService
{
	CCServer::Service m_service;

	thread m_conn_monitor_thread;

	atomic<int> m_nfinished;

	void ConnMonitorProc();

	void DoSync();
	void ConnectOutgoing();

public:
	BlockSyncClient(const string& n, const wstring& d, const string& s)
	 :	TorService(n, d, s, true),
		m_service(n),
		m_nfinished(0)
	{ }

	void ConfigPreset()
	{
		//if (g_store_blocks)
		enabled = true;
	}

	void Start();

	unsigned ConnectedCount();
	unsigned UnconnectedCount(int outcount = -1);

	void SignalFinished();
	bool IsFinishing();

	void StartShutdown();
	void WaitForShutdown();

	BlockSyncList m_sync_list;
};

class BlockSyncThread : public CCThread
{
public:
	void ThreadProc(boost::function<void()> threadproc);
};
