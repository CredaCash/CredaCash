/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * processtx.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <thread>

#define TX_RESULT_SERVER_ERROR								-1

#define TX_RESULT_STRING_CODE_START							-2		// first code with a result string

#define TX_RESULT_PARAM_LEVEL_TOO_OLD						-2

// errors < = PROCESS_RESULT_STOP_THRESHOLD cause the connection to be closed

#define TX_RESULT_PARAM_LEVEL_INVALID						PROCESS_RESULT_STOP_THRESHOLD
#define TX_RESULT_DUPLICATE_SERIALNUM						PROCESS_RESULT_STOP_THRESHOLD - 1
#define TX_RESULT_ALREADY_SPENT								PROCESS_RESULT_STOP_THRESHOLD - 2
#define TX_RESULT_BINARY_DATA_INVALID						PROCESS_RESULT_STOP_THRESHOLD - 3
#define TX_RESULT_PUBLISHED_COMMITMENT_NOT_SUPPORTED		PROCESS_RESULT_STOP_THRESHOLD - 4
#define TX_RESULT_PROOF_VERIFICATION_FAILED					PROCESS_RESULT_STOP_THRESHOLD - 5


class ProcessTx
{
	vector<thread *> m_threads;

	static void ThreadProc();

public:
	void Init();
	void DeInit();

	static int TxEnqueueValidate(DbConn *dbconn, int64_t priority, SmartBuf smartobj, unsigned conn_index, unsigned callback_id);
	static int TxValidate(DbConn *dbconn, struct TxPay& tx, SmartBuf smartobj);
	static const char* ResultString(int result);
};

extern ProcessTx g_processtx;
