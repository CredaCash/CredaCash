/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * processtx.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <thread>

#define TX_RESULT_PARAM_LEVEL_TOO_OLD					-1
#define TX_RESULT_EXPIRED								-2
#define TX_RESULT_ALREADY_SPENT							-3
#define TX_RESULT_ADDRESS_IN_USE						-4
#define TX_RESULT_ALREADY_PAID							-5
#define TX_RESULT_FOREIGN_ERROR							-6

// errors <= PROCESS_RESULT_STOP_THRESHOLD cause the relay peer connection to be closed

#define TX_RESULT_INTERNAL_ERROR						PROCESS_RESULT_STOP_THRESHOLD
#define TX_RESULT_SERVER_ERROR							PROCESS_RESULT_STOP_THRESHOLD - 1
#define TX_RESULT_PARAM_LEVEL_INVALID					PROCESS_RESULT_STOP_THRESHOLD - 2
#define TX_RESULT_DUPLICATE_SERIALNUM					PROCESS_RESULT_STOP_THRESHOLD - 3
#define TX_RESULT_BINARY_DATA_INVALID					PROCESS_RESULT_STOP_THRESHOLD - 4
#define TX_RESULT_OPTION_NOT_SUPPORTED					PROCESS_RESULT_STOP_THRESHOLD - 5
#define TX_RESULT_INSUFFICIENT_DONATION					PROCESS_RESULT_STOP_THRESHOLD - 6
#define TX_RESULT_PROOF_VERIFICATION_FAILED				PROCESS_RESULT_STOP_THRESHOLD - 7
#define TX_RESULT_FOREIGN_VERIFICATION_FAILED			PROCESS_RESULT_STOP_THRESHOLD - 8
#define TX_RESULT_INVALID_VALUE							PROCESS_RESULT_STOP_THRESHOLD - 9

struct TxPay;
class Xtx;
class Xpay;

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

	static int TxEnqueueValidate(DbConn *dbconn, bool is_block_tx, bool add_to_relay_objs, Process_Q_Priority priority, SmartBuf smartobj, unsigned conn_index, uint32_t callback_id);
	static std::shared_ptr<Xtx> ExtractXtx(DbConn *dbconn, const TxPay& txbuf, bool for_pseudo_serialnum = false);
	static bool ExtractXtxFailed(const TxPay& txbuf, bool for_pseudo_serialnum = false);
	static bool CheckTransientDuplicateForeignAddresses(uint64_t foreign_blockchain);
	static int TxValidate(DbConn *dbconn, TxPay& tx, SmartBuf smartobj, uint64_t block_time = 0, bool in_block = false);
	static const char* ResultString(int result);
};

extern ProcessTx g_processtx;
