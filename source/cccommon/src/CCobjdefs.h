/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCobjdefs.h
*/

#pragma once

#include <cstdint>
#include <array>

// CC-Message
#define CC_MSG_HAVE_BLOCK		0xCC4D0001
#define CC_MSG_HAVE_TX			0xCC4D0002

// CC-Command
#define CC_CMD_SEND_LEVELS		0xCC430001
#define CC_CMD_SEND_BLOCK		0xCC430002
#define CC_CMD_SEND_TX			0xCC430003

// CC-Success
#define CC_SUCCESS				0xCC530001

// CC-Result
#define CC_RESULT_BUFFER_FULL	0xCC520001
#define CC_RESULT_BUFFER_BUSY	0xCC520002
#define CC_RESULT_SERVER_BUSY	0xCC520003
#define CC_RESULT_SERVER_ERR	0xCC520004
#define CC_RESULT_NO_LEVEL		0xCC520005

// CC-Error
#define CC_ERROR_BAD_CMD		0xCC450001
#define CC_ERROR_BAD_PARAM		0xCC450002
#define CC_ERROR_NO_OBJ			0xCC450003
#define CC_ERROR_SEND_Q_RESET	0xCC450004

// CC-Objects
#define CC_TAG_BLOCK			0xCC010001
#define CC_TAG_TX_STRUCT		0xCC020001
#define CC_TAG_TX_WIRE			0xCC030001
#define CC_TAG_TX_BLOCK			0xCC040001

// CC-Query
#define CC_TAG_TX_QUERY_PARAMS	0xCC510001
#define CC_TAG_TX_QUERY_ADDRESS	0xCC510002
#define CC_TAG_TX_QUERY_INPUTS	0xCC510003
#define CC_TAG_TX_QUERY_SERIAL	0xCC510004


#define CC_OID_SIZE				(128/8)
typedef std::array<uint8_t, CC_OID_SIZE> ccoid_t;

#define CC_MSG_HEADER_SIZE		(2*sizeof(uint32_t))

#if TEST_SMALL_BUFS
#define CC_HAVE_MAX				5
#else
#define CC_HAVE_MAX				100
#endif
#define CC_HAVE_MAX_MSG_SIZE	(CC_MSG_HEADER_SIZE + ( (CC_HAVE_MAX)*((CC_OID_SIZE) + sizeof(uint32_t) + sizeof(uint64_t)) ) + 20)

#if TEST_SMALL_BUFS
#define CC_TX_SEND_MAX			8
#else
#define CC_TX_SEND_MAX			32
//#define CC_TX_SEND_MAX		128		// for speed testing
#endif
#define CC_SEND_MAX_MSG_SIZE	(CC_MSG_HEADER_SIZE + ((CC_TX_SEND_MAX)*((CC_OID_SIZE))) + 2)

#define CC_MAX_MSG_SIZE			CC_HAVE_MAX_MSG_SIZE

#define TX_POW_NPROOFS			8
#define TX_POW_NONCE_BITS		40
#define TX_POW_NONCE_MASK		((((uint64_t)1 << ((TX_POW_NONCE_BITS) - 1)) - 1) * 2 + 1)
#define TX_POW_NONCE_SIZE		((TX_POW_NONCE_BITS)/8)
#define TX_POW_SIZE				(sizeof(uint64_t) + (TX_POW_NPROOFS) * (TX_POW_NONCE_SIZE))

#define TX_MAX_SIZE				(512*1024-128)
#define CC_BLOCK_MAX_SIZE		(32*1024*1024-128)
//#define CC_BLOCK_MAX_SIZE		(60*1024)	// for testing
