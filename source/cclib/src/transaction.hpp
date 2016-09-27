/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * transaction.hpp
*/

#pragma once

#include "CCproof.h"

//#define TEST_EXTRA_ON_WIRE	1	// put outvalmin, outvalmax, invalmax, outvals_public, nonfinancial and S_spendspec_hashed on wire to prove they are non-mutable and properly enforced

#ifndef TEST_EXTRA_ON_WIRE
#define TEST_EXTRA_ON_WIRE	0	// don't test
#endif

using namespace snarkfront;

#define MAX_KEYS		32
#define MAX_SCRIPTLEN	260

// we don't use bool's or char's in these structs because operator<< outputs them as binary which is a pita

typedef array<char, 64> ecsig_t;

enum tx_type
{
	TX_PAY = 1
};

struct SpendSecrets
{
	bigint_t ____spend_secret;
	uint16_t ____enforce_spendspec_hash;
};

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
	bigint_t __dest;
	bigint_t __paynum;
	bigint_t M_address;
	uint64_t __value;
	uint64_t M_value_enc;
	bigint_t M_commitment;
};

struct TxIn
{
	SpendSecrets ____spendsecrets;

	bigint_t __paynum;
	uint64_t __value;
	bigint_t __M_commitment_iv;
	bigint_t __M_commitment;

	bigint_t S_serialnum;
	bigint_t S_spendspec_hashed;

	uint16_t pathnum;	// = index + 1
	uint16_t zkindex;
};

struct TxInPath
{
	uint64_t ____merkle_leafindex;
	array<bigint_t, TX_MERKLE_DEPTH> ____merkle_path;
};

struct TxPay
{
	uint32_t zero;	// always zero in case app tries to print buffer as string
	uint32_t tag;
	uint16_t type;
	uint16_t no_precheck;
	uint16_t no_proof;
	uint16_t no_verify;
	uint16_t test_make_bad;
	uint16_t test_uselargerzkkey;
	uint64_t param_level;
	bigint_t merkle_root;
	uint16_t zkkeyid;
	array<bigint_t, ZKPROOF_VALS> zkproof;
	int64_t donation;	// signed integer!
	uint64_t outvalmin;
	uint64_t outvalmax;
	uint64_t invalmax;
	uint16_t outvals_public;
	uint16_t nonfinancial;
	uint16_t nout;
	uint16_t nin;
	uint16_t nin_with_path;
	array<TxOut, TX_MAXOUT> output;
	array<TxIn, TX_MAXIN> input;
	array<TxInPath, TX_MAXINPATH> inpath;
};
