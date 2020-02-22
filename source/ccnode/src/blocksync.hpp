/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
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
};

class BlockSyncMsg
{
public:

	uint32_t size;
	uint32_t tag;
	class BlockSyncEntry entry;

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
	uint64_t Init();

	class BlockSyncEntry GetNextEntry();
	void RequeueEntry(class BlockSyncEntry& entry);
};


class BlockSyncConnection : public CCServer::Connection
{
public:
	BlockSyncConnection(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	 :	CCServer::Connection(manager, io_service, connfac)
	{ }

private:

	class BlockSyncMsg req_msg;

	void StartConnection();

	void SendReq();
	void HandleSendMsgWrite(const boost::system::error_code& e, AutoCount pending_op_counter);

	void HandleReadComplete();

	void HandleObjReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter);

	void FinishConnection();
};


class BlockSyncClient : public TorService
{
	CCServer::Service m_service;

	thread m_conn_monitor_thread;

	atomic<unsigned> m_conns_finished;

	void ConnMonitorProc();

	void DoSync();
	void ConnectOutgoing();

public:
	BlockSyncClient(const string& n, const wstring& d, const string& s)
	 :	TorService(n, d, s, true),
		m_service(n),
		m_conns_finished(0)
	{ }

	void ConfigPreset()
	{
		//if (g_store_blocks)
		enabled = true;
	}

	void Start();

	void IncFinishedConns()
	{
		m_conns_finished++;
	}

	void StartShutdown();
	void WaitForShutdown();

	class BlockSyncList m_sync_list;
};

class BlockSyncThread : public CCThread
{
public:
	void ThreadProc(boost::function<void()> threadproc);
};
