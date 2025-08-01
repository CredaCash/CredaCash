/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * process_queue.h
*/

#pragma once

#define PROCESS_Q_TYPE_BLOCK		0
#define PROCESS_Q_TYPE_TX			1
#define PROCESS_Q_N					2

#define PROCESS_Q_STATUS_PENDING	0
#define PROCESS_Q_STATUS_HOLD		1
#define PROCESS_Q_STATUS_VALID		2	// only used for blocks
#define PROCESS_Q_STATUS_INVALID	3	// only used for blocks
#define PROCESS_Q_STATUS_DONE_FLAG	4	// only used for blocks

enum Process_Q_Priority
{
	PROCESS_Q_PRIORITY_BLOCK_HI,
	PROCESS_Q_PRIORITY_BLOCK,
	PROCESS_Q_PRIORITY_BLOCK_TX,
	PROCESS_Q_PRIORITY_X_REQ_HI,
	PROCESS_Q_PRIORITY_X_REQ,
	PROCESS_Q_PRIORITY_TX_HI,
	PROCESS_Q_PRIORITY_TX
};
