/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCparams.h
*/

#pragma once

#define CC_BLOCKLEVEL_WIRE_BYTES	(32/8)
#define CC_BLOCKTIME_WIRE_BYTES		(32/8)

#define MAINNET_BLOCKCHAIN		1
#define TESTNET_BLOCKCHAIN_LO	1000
#define TESTNET_BLOCKCHAIN_HI	1999

#define IsTestnet(chain)		((chain) >= TESTNET_BLOCKCHAIN_LO && (chain) <= TESTNET_BLOCKCHAIN_HI)

#define TX_TYPE_BITS		16		// for future use
#define TX_CHAIN_BITS		32		// for future use
#define TX_REVISION_BITS	32		// for future use
#define TX_REFHASH_BITS		256		// for future use
#define TX_RESERVED_BITS	64		// for future use

#define TX_TYPE_BYTES		((int)((TX_TYPE_BITS + 7) / 8))
#define TX_CHAIN_BYTES		((int)((TX_CHAIN_BITS + 7) / 8))
#define TX_REVISION_BYTES	((int)((TX_REVISION_BITS + 7) / 8))
#define TX_REFHASH_BYTES	((int)((TX_REFHASH_BITS + 7) / 8))
#define TX_RESERVED_BYTES	((int)((TX_RESERVED_BITS + 7) / 8))

#define TX_ACCEPT_REQ_DEST_MASK		0x01F
#define TX_STATIC_ADDRESS_MASK		0xFE0

#define TX_MAXOUT		10
#define TX_MAXIN		8
#define TX_MAXINPATH	8
#define TX_MAX_NASSETS	((TX_MAXOUT < TX_MAXIN ? TX_MAXOUT : TX_MAXIN) + (TX_MAXOUT != TX_MAXIN))

#define TX_MAX_SECRETS				7UL
//#define TX_MAX_SECRETS				3UL	// for testing
#define TX_MAX_SECRET_SLOTS			(TX_MAX_SECRETS + 1)	// 8 slots, 1 for restricted addresses only, 5 for secrets only, 2 can be used for either
#define TX_MAX_RESTRICTED_ADDRESSES	6		// must be <= 2*(TX_MAX_SECRET_SLOTS-1)

//#define TX_CC_MINT_AMOUNT	"50000000000000000000000000000000"
#define TX_CC_MINT_AMOUNT	"1000000000000000000000000000000"
//#define TX_CC_MINT_DONATION	"49000000000000000000000000000000"
#define TX_CC_MINT_DONATION	0UL
#define TX_CC_MINT_EXPONENT	22
#define TX_MINT_NOUT		1					// number of outputs in a TX_MINT
#define TX_MINT_ZKKEY_ID	0

#define TX_MERKLE_DEPTH				40

#define TX_INPUT_BITS					256
#define TX_FIELD_BITS					254
#define TX_BLOCKLEVEL_BITS				40
#define TX_TIME_BITS					32
#define TX_SPEND_SECRETNUM_BITS			18
#define TX_MAX_SECRETS_BITS				3			// must be large enough for TX_MAX_SECRETS
#define TX_DESTNUM_BITS					30
#define TX_DELAYTIME_BITS				8
#define TX_PAYNUM_BITS					20
#define TX_ADDRESS_BITS					128
#define TX_DOMAIN_BITS					20
#define TX_ASSET_BITS					64
#define TX_ASSET_WIRE_BITS				32
#define TX_AMOUNT_BITS					40
#define TX_AMOUNT_EXPONENT_BITS			5
#define TX_AMOUNT_DECODED_BITS			128
#define TX_DONATION_BITS				16
//#define TX_COMMIT_IV_NONCE_BITS		32
#define TX_COMMIT_IV_BITS				128
#define TX_ENC_IV_BITS					24
//#define TX_COMMIT_INDEX_BITS			8
#define TX_COMMITNUM_BITS				(TX_MERKLE_DEPTH + 8)
#define TX_SERIALNUM_BITS				TX_FIELD_BITS
#define TX_HASHKEY_BITS					TX_INPUT_BITS
#define TX_HASHKEY_WIRE_BITS			128
#define TX_MERKLE_BITS					TX_FIELD_BITS

#define TX_INPUT_BYTES				((int)((TX_INPUT_BITS + 7) / 8))
#define TX_BLOCKLEVEL_BYTES			((int)((TX_BLOCKLEVEL_BITS + 7) / 8))
#define TX_TIME_BYTES				((int)((TX_TIME_BITS + 7) / 8))
#define TX_MAX_SECRETS_BYTES		((int)((TX_MAX_SECRETS_BITS + 7) / 8))
#define TX_DELAYTIME_BYTES			((int)((TX_DELAYTIME_BITS + 7) / 8))
#define TX_ADDRESS_BYTES			((int)((TX_ADDRESS_BITS + 7) / 8))
#define TX_DOMAIN_BYTES				((int)((TX_DOMAIN_BITS + 7) / 8))
#define TX_ASSET_BYTES				((int)((TX_ASSET_BITS + 7) / 8))
#define TX_ASSET_WIRE_BYTES			((int)((TX_ASSET_WIRE_BITS + 7) / 8))
#define TX_AMOUNT_BYTES				((int)((TX_AMOUNT_BITS + 7) / 8))
#define TX_AMOUNT_DECODED_BYTES		((int)((TX_AMOUNT_DECODED_BITS + 7) / 8))
#define TX_DONATION_BYTES			((int)((TX_DONATION_BITS + 7) / 8))
#define TX_AMOUNT_EXPONENT_BYTES	((int)((TX_AMOUNT_EXPONENT_BITS + 7) / 8))
#define TX_COMMIT_IV_BYTES			((int)((TX_COMMIT_IV_BITS + 7) / 8))
#define TX_COMMITMENT_BYTES			((int)((TX_FIELD_BITS + 7) / 8))
#define TX_COMMITNUM_BYTES			((int)((TX_COMMITNUM_BITS + 7) / 8))
#define TX_SERIALNUM_BYTES			((int)((TX_SERIALNUM_BITS + 7) / 8))
#define TX_HASHKEY_BYTES			((int)((TX_HASHKEY_BITS + 7) / 8))
#define TX_HASHKEY_WIRE_BYTES		((int)((TX_HASHKEY_WIRE_BITS + 7) / 8))
#define TX_MERKLE_BYTES				((int)((TX_MERKLE_BITS + 7) / 8))

#if TX_ASSET_BITS < 64
#define TX_ASSET_MASK				(((uint64_t)1 << TX_ASSET_BITS) - 1)
#else
#define TX_ASSET_MASK				((uint64_t)(-1))
#endif

#if TX_ASSET_WIRE_BITS < 64
#define TX_ASSET_WIRE_MASK			(((uint64_t)1 << TX_ASSET_WIRE_BITS) - 1)
#else
#define TX_ASSET_WIRE_MASK			((uint64_t)(-1))
#endif

#if TX_AMOUNT_BITS < 64
#define TX_AMOUNT_MASK				(((uint64_t)1 << TX_AMOUNT_BITS) - 1)
#else
#define TX_AMOUNT_MASK				((uint64_t)(-1))
#endif

#define TX_AMOUNT_EXPONENT_MASK		(((uint64_t)1 << TX_AMOUNT_EXPONENT_BITS) - 1)

#if TX_DONATION_BITS < 64
#define TX_DONATION_MASK			(((uint64_t)1 << TX_DONATION_BITS) - 1)
#else
#define TX_DONATION_MASK			((uint64_t)(-1))
#endif

#define TX_DELAYTIME_MASK			(((uint64_t)1 << TX_DELAYTIME_BITS) - 1)

#define MAX_INPUTS_FOR_TESTING		32		// must be power of 2 >= TX_MAXIN, i.e., 32

#define TX_FIELD_MAX				(bigint_t(0UL) - bigint_t(1UL))		// limit inputs to prime field
//#define TX_FIELD_MAX				(bigint_t(0UL))						// for testing: use this definition to allow 256 bit inputs

#define TX_NONFIELD_HI_WORD			((uint64_t)7 << (TX_FIELD_BITS - 3 - 3*64))

#define LIMIT_ZKPROOF_INPUTS_TO_PRIME_FIELD		0

#define ZKPROOF_VALS							9
