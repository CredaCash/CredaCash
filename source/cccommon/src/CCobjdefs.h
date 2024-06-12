/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * CCobjdefs.h
*/

#pragma once

#include <cstdint>
#include <array>

// Network Object Types
// These type values are directly stored in the Wallet DB, so they can't be changed without migrating Wallet DB
// Former Wallet DB definintions were:
//#define TX_TYPE_MINT			1
//#define TX_TYPE_SEND			2

#define CC_TYPE_XCX_SELL_BIT		1	// note: xcx buy and sell must differ only by bit 0

#define CC_TYPE_VOID				0
#define CC_TYPE_MINT				1
#define CC_TYPE_TXPAY				2
#define CC_TYPE_BLOCK				3
#define CC_TYPE_XCX_NAKED_BUY		4
#define CC_TYPE_XCX_NAKED_SELL		5
#define CC_TYPE_XCX_SIMPLE_BUY		6
#define CC_TYPE_XCX_SIMPLE_SELL		7
#define CC_TYPE_XCX_REQ_BUY			8
#define CC_TYPE_XCX_REQ_SELL		9
#define CC_TYPE_XCX_ACCEPT			10
#define CC_TYPE_XCX_CANCEL			11
#define CC_TYPE_XCX_PAYMENT			12
#define CC_TYPE_INVALID				13
#define CC_TYPE_XCX_PAY_EXTEND		CC_TYPE_INVALID	// not yet used
#define CC_TYPE_MOVE				CC_TYPE_INVALID	// not yet used

// Object Tags

// CC-Message
#define CC_MSG_HAVE_BLOCK				0xCC4D0001
#define CC_MSG_HAVE_TX					0xCC4D0002

// CC-Command
#define CC_CMD_PING						0xCC4D0001
#define CC_CMD_SEND_LEVELS				0xCC430002
#define CC_CMD_SEND_BLOCK				0xCC430003
#define CC_CMD_SEND_TX					0xCC430004

// CC-Acknowledgment
#define CC_ACK							0xCC530001

// CC-Result
#define CC_RESULT_BUFFER_FULL			0xCC520001
#define CC_RESULT_BUFFER_BUSY			0xCC520002
#define CC_RESULT_SERVER_BUSY			0xCC520003
#define CC_RESULT_SERVER_ERR			0xCC520004
#define CC_RESULT_NO_LEVEL				0xCC520005

// CC-Error
#define CC_ERROR_BAD_CMD				0xCC450001
#define CC_ERROR_BAD_PARAM				0xCC450002
#define CC_NO_OBJ						0xCC450003
#define CC_ERROR_SEND_Q_RESET			0xCC450004

//@@! change these into bit fields

// CC-Objects
#define CC_TAG_TX_STRUCT				0xCCFFFFFF

#define CC_TAG_BLOCK					0xCC000001
#define CC_TAG_BLOCK_FLAG				0x00010000

#define CC_TAG_MINT						0xCC020001
#define CC_TAG_TX						0xCC040001
#define CC_TAG_TX_XDOMAIN				0xCC060001
#define CC_TAG_XCX_REQ_BUY				0xCC080001
#define CC_TAG_XCX_REQ_SELL				0xCC0A0001
#define CC_TAG_XCX_SIMPLE_BUY			0xCC0C0001
#define CC_TAG_XCX_SIMPLE_SELL			0xCC0E0001
#define CC_TAG_XCX_NAKED_BUY			0xCC100001
#define CC_TAG_XCX_NAKED_SELL			0xCC120001
#define CC_TAG_XCX_CANCEL				0xCC140001
#define CC_TAG_XCX_ACCEPT				0xCC160001
#define CC_TAG_XCX_PAYMENT				0xCC180001
#define CC_TAG_LAST_OBJ					CC_TAG_XCX_PAYMENT

// CC-Query
#define CC_TAG_TX_QUERY_PARAMS			0xCC510001
#define CC_TAG_TX_QUERY_ADDRESS			0xCC510002
#define CC_TAG_TX_QUERY_INPUTS			0xCC510003
#define CC_TAG_TX_QUERY_SERIAL			0xCC510004
#define CC_TAG_TX_QUERY_XREQS			0xCC510005
#define CC_TAG_TX_QUERY_XMATCH_OBJID	0xCC510006
#define CC_TAG_TX_QUERY_XMATCH_REQNUM	0xCC510007
#define CC_TAG_TX_QUERY_XMATCH_MATCHNUM	0xCC510008
#define CC_TAG_TX_QUERY_XMINING_INFO	0xCC510009

#define CC_OID_SIZE				(128/8)
#define CC_OID_TRACE_SIZE		10
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
#define TX_POW_NONCE_MASK		((((uint64_t)1 << (TX_POW_NONCE_BITS - 1)) - 1) * 2 + 1)
#define TX_POW_NONCE_SIZE		((TX_POW_NONCE_BITS+7)/8)
#define TX_POW_SIZE				(sizeof(uint64_t) + (TX_POW_NPROOFS) * (TX_POW_NONCE_SIZE)) // 8 + 8*5 = 48

#define TX_MAX_SIZE				(512*1024-128)
#define CC_BLOCK_MAX_SIZE		(32*1024*1024-128)
//#define CC_BLOCK_MAX_SIZE		(60*1024)	// for testing
