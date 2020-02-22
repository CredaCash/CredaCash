/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * transaction.hpp
*/

#pragma once

#include "CCproof.h"

//#define TEST_EXTRA_ON_WIRE	1	// put all public tx values on wire to prove they are non-mutable and properly enforced

#ifndef TEST_EXTRA_ON_WIRE
#define TEST_EXTRA_ON_WIRE	0	// don't test
#endif

using namespace snarkfront;

#define MAX_KEYS		32
#define MAX_SCRIPTLEN	260

// bool's and char's aren't used in these structs because operator<< outputs them as binary instead of text

typedef array<char, 64> ecsig_t;

struct AddressParams
{
	// RULE tx output: M_address = zkhash(#dest, dest_chain, #paynum)

	uint64_t __dest_id;		// used only by wallet
	uint64_t __flags;		// used only by wallet

	bigint_t __dest;

	uint64_t dest_chain;
	uint32_t __paynum;
};

struct SpendSecretParams
{
	uint16_t ____nsecrets;	// # of secrets
	uint16_t ____nraddrs;	// # of restricted addresses

	// RULE tx input: @receive_secret = zkhash(@monitor_secret[0], @enforce_spendspec_with_spend_secret, @enforce_spendspec_with_trust_secret, @required_spendspec_hash, @allow_master_secret, @allow_freeze, @allow_trust_unfreeze, @require_public_hashkey, @restrict_addresses, @spend_locktime, @trust_locktime, @spend_delaytime, @trust_delaytime)

	uint16_t ____enforce_spendspec_with_spend_secrets;		// bool
	uint16_t ____enforce_spendspec_with_trust_secrets;		// bool
	bigint_t ____required_spendspec_hash;		// field		// used only to compute __dest for a TxOut
	uint16_t ____allow_master_secret;			// bool
	uint16_t ____allow_freeze;					// bool
	uint16_t ____allow_trust_unfreeze;			// bool
	uint16_t ____require_public_hashkey;		// bool
	uint16_t ____restrict_addresses;			// bool
	uint64_t ____spend_locktime;
	uint64_t ____trust_locktime;
	uint16_t ____spend_delaytime;				// uint8_t
	uint16_t ____trust_delaytime;				// uint8_t

	// RULE tx input: #dest = zkhash(@receive_secret, @monitor_secret[1..R], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets, @destnum)

	array<uint16_t, TX_MAX_SECRETS> ____use_spend_secret; // bool
	array<uint16_t, TX_MAX_SECRETS> ____use_trust_secret; // bool

	uint16_t ____required_spend_secrets;
	uint16_t ____required_trust_secrets;
	uint32_t ____destnum;
	uint16_t __acceptance_required;				// only used by wallet
	uint16_t __static_address;					// only used by wallet

	AddressParams addrparams;	// only __paynum used for TxIn, but entire struct used by wallet
};

struct SpendSecret
{
	// RULE tx input: @spend_secret[0] = zkhash(@root_secret, @spend_secret_number)
	uint32_t ____spend_secret_number;			// uint32_t

	uint16_t ____have_master_secret;			// bool
	uint16_t ____have_root_secret;				// bool
	uint16_t ____have_spend_secret;				// bool
	uint16_t ____have_trust_secret;				// bool
	uint16_t ____have_monitor_secret;			// bool
	uint16_t ____have_restricted_address;		// bool
	uint16_t ____have_receive_secret;			// bool

	bigint_t ____master_secret;					// 256-bits
	bigint_t ____root_secret;					// field
	bigint_t ____spend_secret;					// 256-bits
	bigint_t ____trust_secret;					// 256-bits
	bigint_t ____monitor_secret;				// 256-bits
	bigint_t ____receive_secret;				// field
};

typedef array<SpendSecret, TX_MAX_SECRET_SLOTS> SpendSecrets;

struct SpendScript
{
	uint16_t len;
	uint8_t opcodes[MAX_SCRIPTLEN];
};

struct SpendSpec
{
	uint16_t S_nkeys;
	array<bigint_t, MAX_KEYS> S_pubkey;
	uint64_t sigmask;
	array<ecsig_t, MAX_KEYS> signature;
	SpendScript script;
};

struct TxOut
{
	AddressParams addrparams;

	uint16_t no_address;	// bool
	bigint_t M_address;
	uint16_t acceptance_required;
	uint32_t repeat_count;

	uint32_t M_pool;

	uint64_t __asset;
	uint16_t no_asset;		// bool
	uint64_t asset_mask;
	uint64_t __asset_pad;	// only needed for make_bad
	uint64_t M_asset_enc;

	uint64_t __amount_fp;
	uint16_t no_amount;		// bool
	uint64_t amount_mask;
	uint64_t __amount_pad;	// only needed for make_bad
	uint64_t M_amount_enc;

	//bigint_t M_commitment_iv;
	bigint_t M_commitment;
};

struct TxIn
{
	uint16_t enforce_master_secret;			// bool
	uint16_t enforce_spend_secrets;			// bool
	uint16_t enforce_trust_secrets;			// bool
	uint16_t enforce_freeze;				// bool
	uint16_t enforce_unfreeze;				// bool

	SpendSecretParams params;
	SpendSecrets secrets;

	uint16_t ____have_master_secret_valid;	// bool
	uint16_t ____have_spend_secrets_valid;	// bool
	uint16_t ____have_trust_secrets_valid;	// bool
	uint16_t ____master_secret_valid;		// bool
	uint16_t ____spend_secrets_valid;		// bool
	uint16_t ____trust_secrets_valid;		// bool

	bigint_t merkle_root;
	uint16_t invalmax;
	uint16_t delaytime;

	// RULE tx input: M_commitment = zkhash(M_commitment_iv, #dest, #paynum, M_pool, #asset, #amount)

	uint32_t M_pool;
	uint64_t __asset;
	uint64_t __amount_fp;
	bigint_t __M_commitment_iv;
	//uint16_t __M_commitment_index;
	bigint_t _M_commitment;			// may be public if published in the tx
	uint64_t _M_commitnum;			// may be public if published in the tx

	uint16_t no_serialnum;			// has no serialnum if it doesn't have a commitnum assigned
	bigint_t S_serialnum;
	bigint_t S_hashkey;
	bigint_t S_spendspec_hashed;

	uint16_t pathnum;	// = index + 1
	uint16_t zkindex;
};

struct TxInPath
{
	array<bigint_t, TX_MERKLE_DEPTH> __M_merkle_path;
};

struct TxPay
{
	// not on wire:
	uint32_t zero;	// always zero in case app tries to print buffer as string
	uint16_t no_precheck;
	uint16_t no_proof;
	uint16_t no_verify;
	uint16_t test_uselargerzkkey;
	uint32_t test_make_bad;
	uint64_t random_seed;

	// copied to default values of individual inputs and outputs
	uint16_t have_dest_chain__;
	uint16_t have_output_pool__;
	uint16_t have_acceptance_required__;
	uint16_t have_invalmax__;
	uint16_t have_delaytime__;
	uint64_t dest_chain__;
	uint32_t output_pool__;
	uint16_t acceptance_required__;
	uint16_t invalmax__;		// from param_level
	uint16_t delaytime__;

	// on wire (explicitly or implicitly)
	uint32_t tag;
	uint16_t tag_type;			// !!! not the right name for this
	uint16_t zkkeyid;
	array<bigint_t, ZKPROOF_VALS> zkproof;

	uint16_t amount_bits;		// property of blockchain / from param_level -- currently used only by wallet (most of code uses compile time value TX_AMOUNT_BITS)
	uint16_t donation_bits;		// property of blockchain / from param_level -- currently used only by wallet (most of code uses compile time value TX_DONATION_BITS)
	uint16_t exponent_bits;		// property of blockchain / from param_level -- currently used only by wallet (most of code uses compile time value TX_EXPONENT_BITS)

	// inputs to zero knowledge proof (non-malleable)
	uint16_t tx_type;
	uint64_t source_chain;
	uint64_t param_level;
	uint64_t param_time;		// from param_level
	uint32_t revision;
	uint64_t expiration;
	bigint_t refhash;
	uint64_t reserved;
	uint64_t donation_fp;
	uint16_t outvalmin;			// from param_level
	uint16_t outvalmax;			// from param_level
	uint16_t have_allow_restricted_addresses__;
	uint16_t allow_restricted_addresses;
	bigint_t tx_merkle_root;	// from param_level; used to compute M_commitment_iv
	//uint32_t M_commitment_iv_nonce;
	uint16_t override_commitment_iv__;
	bigint_t M_commitment_iv;	// computed

	uint16_t ____nsecrets;
	uint16_t ____nraddrs;
	uint16_t __nassets;
	array<uint64_t, TX_MAX_NASSETS> __asset_list;

	uint16_t nout;
	uint16_t nin;
	uint16_t nin_with_path;
	array<TxOut, TX_MAXOUT> outputs;
	array<TxIn, TX_MAXIN> inputs;
	array<TxInPath, TX_MAXINPATH> inpaths;
};

#define SECRET_TYPE_MAIN		0
#define SECRET_TYPE_MULTI		1

const char* compute_or_verify_secrets(const SpendSecretParams& params, SpendSecret& secrets, bool no_precheck);

unsigned restricted_address_secret_index(unsigned slot);
bool restricted_address_slot_open(const SpendSecretParams& params, unsigned slot);
void set_restricted_address(SpendSecrets& secrets, unsigned slot, const bigint_t& value);
void get_restricted_address(const SpendSecrets& secrets, unsigned slot, bigint_t& value);

void compute_destination(const SpendSecretParams& params, const SpendSecrets& secrets, bigint_t& destination);
void compute_address(const bigint_t& destination, uint64_t destination_chain, uint64_t paynum, bigint_t& address);
void compute_amount_pad(const bigint_t& commit_iv, const bigint_t& dest, const uint32_t paynum, uint64_t& asset_xor, uint64_t& amount_xor);
void compute_commitment(const bigint_t& commit_iv, const bigint_t& dest, const uint32_t paynum, const uint32_t pool, const uint64_t asset, const uint64_t amount_fp, bigint_t& commitment);
void compute_serialnum(const bigint_t& monitor_secret, const bigint_t& commitment, uint64_t commitnum, bigint_t& serialnum);
