/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * processtx.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <thread>

#define TX_RESULT_PARAM_LEVEL_TOO_OLD					-1
#define TX_RESULT_ALREADY_SPENT							-2

// errors <= PROCESS_RESULT_STOP_THRESHOLD cause the relay peer connection to be closed

#define TX_RESULT_SERVER_ERROR							PROCESS_RESULT_STOP_THRESHOLD
#define TX_RESULT_PARAM_LEVEL_INVALID					PROCESS_RESULT_STOP_THRESHOLD - 1
#define TX_RESULT_DUPLICATE_SERIALNUM					PROCESS_RESULT_STOP_THRESHOLD - 2
#define TX_RESULT_BINARY_DATA_INVALID					PROCESS_RESULT_STOP_THRESHOLD - 3
#define TX_RESULT_OPTION_NOT_SUPPORTED					PROCESS_RESULT_STOP_THRESHOLD - 4
#define TX_RESULT_INSUFFICIENT_DONATION					PROCESS_RESULT_STOP_THRESHOLD - 5
#define TX_RESULT_PROOF_VERIFICATION_FAILED				PROCESS_RESULT_STOP_THRESHOLD - 6

struct TxPay;

class ProcessTx
{
	vector<thread *> m_threads;

	static void ThreadProc();

public:
	void Init();
	void Stop();
	void DeInit();

	void InitBlockScan();
	void WaitForBlockTxValidation();

	static int TxEnqueueValidate(DbConn *dbconn, bool is_block_tx, bool add_to_relay_objs, int64_t priority, SmartBuf smartobj, unsigned conn_index, uint32_t callback_id);
	static int TxValidate(DbConn *dbconn, TxPay& tx, SmartBuf smartobj);
	static const char* ResultString(int result);
};

extern ProcessTx g_processtx;
