/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * relay.hpp
*/

#pragma once

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

#include "relay_request_params.h"

#include <CCobjdefs.h>
#include <ObjQueue.hpp>

class RelayConnection : public CCServer::Connection
{
public:
	RelayConnection(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	 :	CCServer::Connection(manager, io_service, connfac),
		private_peer_index(-1),
		request_queue_lock(__FILE__, __LINE__),
		request_param_queue(sizeof(relay_request_params_extended_t), CC_TX_SEND_MAX),
		send_queue_lock(__FILE__, __LINE__),
		send_queue(sizeof(ccoid_t), CC_TX_SEND_MAX)
	{ }

	int private_peer_index;

private:

	unsigned peer_error_count;

	int64_t db_next_new_block_seqnum;
	int64_t db_next_new_xreq_seqnum;
	int64_t db_next_new_tx_seqnum;
	array<uint8_t, CC_HAVE_MAX_MSG_SIZE> announce_msg_buf;

	array<uint8_t, CC_SEND_MAX_MSG_SIZE> request_msg_buf;
	relay_request_param_buf_t req_param_buf;
	atomic_flag request_msg_buf_in_use;

	atomic<int> request_objs_pending;
	atomic<int64_t> request_bytes_pending;
	FastSpinLock request_queue_lock;
	ObjQueue request_param_queue;
	uint32_t last_valid_obj_time;

	atomic_flag send_one;
	FastSpinLock send_queue_lock;
	ObjQueue send_queue;

	void StartConnection();

	void HandleReadComplete();
	void HandleMsgReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter);
	void CheckForDownload();
	void HandleSendMsgWrite(const boost::system::error_code& e, AutoCount pending_op_counter);

	void CheckToSend();
	void HandleObjWrite(const boost::system::error_code& e, SmartBuf smartobj, AutoCount pending_op_counter);

	bool SetHeartbeatTimer();
	void HandleHeartbeat(const boost::system::error_code& e, AutoCount pending_op_counter);
	void HandleAnnounceMsgWrite(const boost::system::error_code& e, AutoCount pending_op_counter);

	void FinishConnection();
};


class RelayService : public TorService
{
	CCServer::Service m_service;

	bool m_bprivate;

	thread m_conn_monitor_thread;

	void ConnMonitorProc();
	void ConnectOutgoing();

	void PrivateConnMonitorProc();
	void PrivateConnectOutgoing(int peer);

	int LoadPrivateHosts();
	int GetNextPrivateConnectPeer(uint32_t& when);
	void PrivateConnectReschedule(int peer);

	int m_nprivhosts;
	vector<string> m_privhosts;
	vector<uint32_t> m_connect_error_count;
	vector<uint32_t> m_connect_time;

public:
	RelayService(const string& n, const wstring& d, const string& s, bool b)
	 :	TorService(n, d, s),
		m_service(n),
		m_bprivate(b),
		m_nprivhosts(0),
		priv_host_index(-1)
	{ }

	// TODO: instead of making this class polymorphic, make a RelayServiceBase class that is subclassed for RelayService and PrivateRelayService

	wstring priv_hosts_file;
	int priv_host_index;

	void ConfigPreset()
	{
		if (m_bprivate)
		{
			tor_new_hostname = false;
		}
		else
		{
			tor_service = true;
			tor_new_hostname = true;
		}
	}

	void ConfigPrivateRelay();

	void ConfigPostset()
	{
		if (m_bprivate)
			tor_advertise = false;
	}

	void Start();

	void PrivateConnected(int peer);
	void PrivateDisconnected(int peer);

	void StartShutdown();
	void WaitForShutdown();
};

class RelayThread : public CCThread
{
public:
	void ThreadProc(boost::function<void()> threadproc);
};
