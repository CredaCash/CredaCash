/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCproof.cpp
*/

#include "cclib.h"
#include "zkhash.hpp"
#include "CCproof.h"
#include "CCproof.hpp"
#include "transaction.hpp"
#include "transaction.h"
#include "zkkeys.hpp"
#include "CompressProof.hpp"

#include <CCobjdefs.h>
#include <blake2/blake2.h>
#include <SpinLock.hpp>

//@@! before release, check all test defs: regexp ^#define TEST.*[1-9]|^#define RTEST.*[1-9]|^#define TRACE.*[1-9]

//!#define TEST_PUBLIC_INPUTS_UNBOUNDED	1
//!#define TEST_RANDOM_ANYVALS			1

#ifdef CC_DLL_EXPORTS
#define TEST_SHOW_GEN_BENCHMARKS	1	// show benchmarks in DLL build; TODO: make this a json option (ok for production release)
#define TEST_SHOW_VERIFY_BENCHMARKS	1	// show benchmarks in DLL build (ok for production release)
#define TEST_DEBUG_CONSTRAINTS		1	// show constraints in DLL build (ok for production release)
#endif

//#define TEST_DEBUG_CONSTRAINTS	1	// show constraints that not satisfied when constructing zkproof

#ifndef TEST_PUBLIC_INPUTS_UNBOUNDED
#define TEST_PUBLIC_INPUTS_UNBOUNDED	0	// don't test
#endif

#ifndef TEST_RANDOM_ANYVALS
#define TEST_RANDOM_ANYVALS			0	// don't test
#endif

#ifndef TEST_DEBUG_CONSTRAINTS
#define TEST_DEBUG_CONSTRAINTS		0	// don't test
#endif

#ifndef TEST_SHOW_GEN_BENCHMARKS
#define TEST_SHOW_GEN_BENCHMARKS	0	// don't show
#endif

#ifndef TEST_SHOW_VERIFY_BENCHMARKS
#define TEST_SHOW_VERIFY_BENCHMARKS	0	// don't show
#endif

CCHasher::HashBases CCHasher::hashbases;

typedef bigint_x<BN128_FR> ZKVAR;
//typedef field_x<BN128_FR> ZKVAR;
typedef CCHasher::zk::HashInput<ZKPAIRING> ZKHashInput;
typedef CCHasher::zk::Hasher<ZKPAIRING> ZKHasher;
typedef CCHasher::BitConstraints<ZKPAIRING,ZKVAR,ZKVAR> ZKConstraints;
typedef CCHasher::ASTValue<ZKVAR, bigint_t> ZKValue;

#define constrain_zero(x, ...) constrain_zero_pairing<ZKPAIRING>(x, __LINE__, index, ##__VA_ARGS__)

template <typename PAIRING, typename T>
void constrain_zero_pairing(const AST_Var<T>& x, unsigned line, unsigned index, unsigned i = 999, unsigned j = 999)
{
	TL<R1C<typename PAIRING::Fr>>::singleton()->setFalse(x->r1Terms()[0]);

	if (TEST_DEBUG_CONSTRAINTS && ZKValue::value(x))
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();

		if (i < 999 && j < 999)
			cout << "CCProof constraint false at line " << line << " index " << index << " " << i << " " << j << endl;
		else if (i < 999)
			cout << "CCProof constraint false at line " << line << " index " << index << " " << i << endl;
		else
			cout << "CCProof constraint false at line " << line << " index " << index << endl;
	}
}

// !!! test changing these:
int g_multiExp_nthreads;	// global to set # of multiExp threads
int g_multiExp_nice;		// global to set # of multiExp thread priority

static ZKKeyStore keystore;

CCPROOF_API CCProof_Init(const wstring& proof_key_dir)
{
	static FastSpinLock init_lock(__FILE__, __LINE__);

	lock_guard<FastSpinLock> lock(init_lock);

	static bool binited = false;

	//cerr << "CCProof_Init binit " << binit << endl;

	if (binited)
		return 0;

	//cerr << "CCProof_Init" << endl;

	init_BN128();
	ZKHasher::Init();

	//for (unsigned i = 0; i < 20; ++i)	// for testing
	//	cerr << hashbases.bigint(i) << " " << hashbases[i] << " " << BN128_FR(hashbases.bigint(i)) << endl;

	keystore.Init();

	keystore.SetKeyFilePath(proof_key_dir);

	#if 0	// for benchmarking
	CCProof_PreloadVerifyKeys();
	keystore.PreLoadProofKeys();
	#endif

	binited = true;

	return 0;
}

struct TxOutZKPub
{
	ZKVAR enforce;					// bool

	ZKVAR dest_chain;				// uint64_t
	ZKVAR M_domain;					// uint32_t

	ZKVAR enforce_address;			// bool
	ZKVAR M_address;				// bigint_t

	ZKVAR acceptance_required;		// bool
	ZKVAR multiplier;				// bigint_t

	ZKVAR enforce_asset;			// bool
	ZKVAR asset_mask;				// uint64_t
	ZKVAR M_asset_enc;				// uint64_t

	ZKVAR M_amount_enc;				// uint64_t
	ZKVAR enforce_amount;			// bool
	ZKVAR amount_mask;				// uint64_t

	//nZKVAR __M_commitment_index;	// uint8_t
	ZKVAR M_commitment;				// bigint_t

	vector<ZKVAR> dest_chain_bits;
	vector<ZKVAR> M_domain_bits;
	vector<ZKVAR> asset_mask_bits;
	vector<ZKVAR> amount_mask_bits;
	vector<ZKVAR> M_asset_enc_bits;
	vector<ZKVAR> M_amount_enc_bits;
	//vector<ZKVAR> M_commitment_iv_bits;
	//vector<nZKVAR> M_commitment_index_bits;
};

struct TxOutZKPriv
{
	array<ZKVAR, TX_MAX_NASSETS> __is_asset;	// bool

	ZKVAR __dest;			// bigint_t
	ZKVAR __paynum;			// uint32_t
	ZKVAR __asset;			// uint64_t
	ZKVAR __amount_fp;		// uint64_t
	ZKVAR __amount_int;

	ZKVAR __asset_xor;		// uint64_t
	ZKVAR __amount_xor;		// uint64_t

	vector<ZKVAR> __dest_bits;
	vector<ZKVAR> __paynum_bits;
	vector<ZKVAR> __asset_bits;
	vector<ZKVAR> __amount_fp_bits;

	vector<ZKVAR> __asset_xor_bits;
	vector<ZKVAR> __amount_xor_bits;
};

struct TxInZKPub
{
	ZKVAR enforce;					// bool

	ZKVAR enforce_master_secret;	// bool
	ZKVAR enforce_spend_secrets;	// bool
	ZKVAR enforce_trust_secrets;	// bool
	ZKVAR enforce_freeze;			// bool
	ZKVAR enforce_unfreeze;			// bool

	ZKVAR merkle_root;				// bigint_t
	ZKVAR invalmax;					// uint8_t
	ZKVAR delaytime;					// uint8_t

	ZKVAR M_domain;					// uint32_t

	ZKVAR enforce_public_commitment; // bool
	ZKVAR enforce_public_commitnum; // bool
	ZKVAR M_commitment;				// bigint_t; this is a public input only if the commitment is published when spending, instead of proving the Merkle path
	ZKVAR M_commitnum;				// uint64_t; this is a public input only if the billet has a serialnum and the commitment is published when spending, instead of proving the Merkle path

	ZKVAR enforce_serialnum;		// bool
	ZKVAR S_serialnum;				// bigint_t
	ZKVAR S_hashkey;				// bigint_t
	ZKVAR S_spendspec_hashed;		// bigint_t

	vector<ZKVAR> M_domain_bits;
};

struct TxInZKPriv
{
	uint16_t nsecrets;			// could be different for each input (although it is not currently)

	array<ZKVAR, TX_MAX_NASSETS> __is_asset;	// bool

	ZKVAR __asset;				// uint64_t
	ZKVAR __amount_fp;			// uint64_t
	ZKVAR __amount_int;
	ZKVAR __M_commitment_iv;	// bigint_t
	//nZKVAR __M_commitment_index;	// uint8_t

	ZKVAR __M_commitment;		// bigint_t
	ZKVAR __M_commitnum;		// uint64_t

	ZKVAR ____master_secret;						// bigint_t
	ZKVAR ____spend_secret_number;					// uint16_t
	ZKVAR ____required_spendspec_hash;				// bigint_t
	vector<ZKVAR> ____enforce_spendspec_with_spend_secrets_bit;	// bool
	vector<ZKVAR> ____enforce_spendspec_with_trust_secrets_bit;	// bool
	vector<ZKVAR> ____allow_master_secret_bit;		// bool
	vector<ZKVAR> ____allow_freeze_bit;				// bool
	vector<ZKVAR> ____allow_trust_unfreeze_bit;		// bool
	vector<ZKVAR> ____require_public_hashkey_bit;	// bool
	vector<ZKVAR> ____restrict_addresses_bit;		// bool
	ZKVAR ____spend_locktime;
	ZKVAR ____trust_locktime;
	ZKVAR ____spend_delaytime;						// uint8_t
	ZKVAR ____trust_delaytime;						// uint8_t

	vector<ZKVAR> ____use_spend_secret_bits;		// bool
	vector<ZKVAR> ____use_trust_secret_bits;		// bool
	ZKVAR ____required_spend_secrets;				// uint16_t
	ZKVAR ____required_trust_secrets;				// uint16_t
	ZKVAR ____destnum;								// uint32_t
	ZKVAR __paynum;									// uint32_t

	ZKVAR ____master_secret_valid;					// bool
	ZKVAR ____spend_secrets_valid;					// bool
	ZKVAR ____trust_secrets_valid;					// bool
	array<ZKVAR, TX_MAX_SECRETS> ____secret_valid;	// bool

	array<ZKVAR, TX_MAX_SECRETS> ____spend_secret;	// bigint_t
	array<ZKVAR, TX_MAX_SECRETS> ____trust_secret;	// bigint_t
	array<ZKVAR, TX_MAX_SECRET_SLOTS> ____monitor_secret;// bigint_t

	array<vector<ZKVAR>, TX_MAXOUT> ____output_address_matches; // bool

	vector<ZKVAR> __asset_bits;
	vector<ZKVAR> __amount_fp_bits;
	vector<ZKVAR> __M_commitment_iv_bits;
	//vector<nZKVAR> __M_commitment_index_bits;

	vector<ZKVAR> __M_commitment_bits;
	vector<ZKVAR> __M_commitnum_bits;

	vector<ZKVAR> ____master_secret_bits;
	vector<ZKVAR> ____spend_secret_number_bits;
	vector<ZKVAR> ____required_spendspec_hash_bits;
	vector<ZKVAR> ____spend_locktime_bits;
	vector<ZKVAR> ____trust_locktime_bits;
	vector<ZKVAR> ____spend_delaytime_bits;
	vector<ZKVAR> ____trust_delaytime_bits;

	vector<ZKVAR> ____required_spend_secrets_bits;
	vector<ZKVAR> ____required_trust_secrets_bits;
	vector<ZKVAR> ____destnum_bits;
	vector<ZKVAR> __paynum_bits;

	array<vector<ZKVAR>, TX_MAX_SECRETS> ____spend_secret_bits;
	array<vector<ZKVAR>, TX_MAX_SECRETS> ____trust_secret_bits;

	array<ZKVAR, TX_MAX_SECRET_SLOTS> ____monitor_secret_lo;
	array<ZKVAR, TX_MAX_SECRET_SLOTS> ____monitor_secret_hi;

	array<vector<ZKVAR>, TX_MAX_SECRET_SLOTS> ____monitor_secret_lo_bits;
	array<vector<ZKVAR>, TX_MAX_SECRET_SLOTS> ____monitor_secret_hi_bits;
};

struct TxInPathZK
{
	ZKVAR enforce_path;					// bool

	vector<ZKVAR> __M_merkle_path;	// bigint_t
};

struct TxPayZK
{
	uint16_t nout;
	uint16_t nin;
	uint16_t nin_with_path;
	uint16_t nassets;
	uint16_t nsecrets;
	uint16_t nraddrs;
	uint16_t nrouts;

	struct
	{
		ZKVAR tx_type;
		ZKVAR source_chain;			// for future use
		ZKVAR param_level;			// uint64_t
		ZKVAR param_time;			// uint64_t
		ZKVAR revision;				// for future use
		ZKVAR expiration;			// for future use
		ZKVAR refhash;				// for future use
		ZKVAR reserved;				// for future use

		ZKVAR donation;				// int64_t
		ZKVAR outvalmin;			// uint8_t
		ZKVAR outvalmax;			// uint8_t
		ZKVAR allow_restricted_addresses;	// bool

		ZKVAR M_commitment_iv;		// bigint_t

		vector<ZKVAR> M_commitment_iv_bits;
		vector<ZKVAR> M_encrypt_iv_bits;

	} publics;

	struct
	{
		array<ZKVAR, TX_MAX_NASSETS - 1> __tx_asset;	// uint64_t

	} privates;

	vector<TxOutZKPub>	output_public;
	vector<TxInZKPub>	input_public;
	vector<TxOutZKPriv>	output_private;
	vector<TxInZKPriv>	input_private;
	array<TxInPathZK, TX_MAXINPATH> inpaths;

	//vector<ZKVAR> test_var;
};

// functions to generate same random values when creating proof and when verifying proof

thread_local static bigint_t rand_seed;
thread_local static int rand_count;
thread_local static int rand_count_priv;

static void init_rand_seed(uint64_t seed)
{
	rand_seed = seed;
	rand_count = 0;
	rand_count_priv = -999999;

	//cerr << "init_rand_seed " << hex << seed << dec << endl;
}

static void get_rand_val(bigint_t& var, int *pcount = NULL)
{
	if (!pcount)
		pcount = &rand_count;

	++*pcount;

	auto rc = blake2s(&var, sizeof(var), pcount, sizeof(*pcount), &rand_seed, sizeof(rand_seed));
	CCASSERTZ(rc);

	//cerr << "get_rand_val count " << *pcount << " pcount " << hex << (uintptr_t)pcount << " rval " << var << dec << endl;
}

static void bless_input(bool pubvar, int& badsel, ZKVAR& var, const bigint_t& val2, unsigned nbits, const char *input, bool nobad = 0, bool anyval = 0, bool nomod = 0, const bigint_t *pbadval = NULL)
{
	//cerr << "_bless_input " << input << " pubvar " << pubvar << " badsel " << badsel << " nobad " << nobad << " anyval " << anyval << endl;

	/*
	Parameters used for testing:
	badsel is a counter; when it equals zero and if nobad and anyval are false, then bless_input will bless
		the variable with a value that should make the proof invalid (it is an error if it does not)
	nobad true indicates this variable cannot be made bad.  For unused transaction inputs and outputs, the caller does not need to worry
		about nobad because badsel will never be zero.  Therefore nobad does not need to account for the general "enforce" variable.
		Note that nobad only operates during proof construction, so it can take into account the values of both published and hidden inputs.
	anyval true indicates the variable can take any value. When TEST_RANDOM_ANYVALS is true,
		this is tested by setting the variable to a pseudorandom value.  Anyval is used in both proving and verifying, so its value
		should ponly depend on published tx values, not hidden values, to ensure anyval has the same value when proving and verifying.
		The pseudorandom generator for the varialbe value is seeded from tx.random_seed, and both the prover and verifier should set this
		seed to the same value so that the same pseudorandom values are used for both the public inputs	when both proving the verifing.
		In a production build, when anyval is true, the variable is set to zero.
	nomod true indicates the proof will succeed if the variable is set to its intended value plus the prime modulus.  This is the case for
		variables that are not broken down into a multibit field.  Otherwise, when testing bad values, bless_input will try the
		intended value plus the prime modulus to ensure this fails.
	*/

	CCASSERT(nbits <= sizeof(bigint_t) * CHAR_BIT);

	bigint_t blessval = val2;

	#if LIMIT_ZKPROOF_INPUTS_TO_PRIME_FIELD
	if (nbits > TX_FIELD_BITS)
	{
		blessval = blessval * bigint_t(1UL);	// take the input value modolo the prime
		nbits = TX_FIELD_BITS;
	}
	#endif

	if (anyval)
	{
		blessval = 0UL;
		if (nbits >= TX_FIELD_BITS)
			BIGWORD(blessval, 3) = TX_NONFIELD_HI_WORD;
	}

	if (anyval && TEST_RANDOM_ANYVALS)
	{
		// this variable can take any value, so set it to a random value to ensure it can in fact take any value

		if (1||pubvar)				// makes diff's easier to read if private inputs get same random values
			get_rand_val(blessval);	// public inputs need same value when proving and verifying
		else
			blessval.randomize();

		bigint_mask(blessval, nbits);

		//cerr << "_bless_input " << input << " nbits " << nbits << " random value " << hex << blessval << dec << endl;
	}

	if (!badsel)
	{
		//cerr << "_bless_input makebad badsel " << badsel << " nobad " << nobad << " anyval " << anyval << " nomod " << nomod << " " << input << endl;

		bigint_t rval;
		get_rand_val(rval);		// generate a pseudorandom value for use below

		if (nobad || anyval)
		{
			// this variable can't be made bad, so defer to the next one
			++badsel;
		}
		else
		{
			unsigned bit1 = 999;
			unsigned bit2 = 999;

			if (pbadval)
				blessval = *pbadval;
			else if (!pubvar && !nomod && !(BIG64(rval) & 7))
			{
				// test adding the prime modulus to hidden input values
				// this should result in a bad proof if the value is used as a zkhash input
				//	if the value is not used in an extractBits operation, the proof might still be good, in which case, nomod = 1 can be set for that input to bypass this test
				// public input vars are checked externally, which means it should not be possible to add the prime modulus to them, so they aren't tested

				bigint_t prime = bigint_t(0UL) - bigint_t(1UL);
				addBigInt(prime, bigint_t(1UL), prime, false);

				//cerr << "prime = " << hex << prime << dec << endl;
				cerr << "val = " << hex << blessval << dec << endl;

				addBigInt(blessval, prime, blessval, false);

				cerr << "val + prime = " << hex << blessval << dec << endl;
			}
			else
			{
				// test a random input value

				auto rbits = nbits;
				if (rbits < 256)
					++rbits;						// also try flipping a bit outside the input's field width
				bit1 = BIGWORD(rval, 1) % rbits;
				if (BIG64(rval) & 4)
				{
					bit2 = BIGWORD(rval, 2) % rbits;
					if (bit2 == bit1)
						bit2 = 999;
				}

				BIGWORD(blessval, bit1/64) ^= (uint64_t)1 << (bit1 & 63);
				if (bit2 < 256)
					BIGWORD(blessval, bit2/64) ^= (uint64_t)1 << (bit2 & 63);
			}

			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
			cout << "CCProof test makebad changed " << input << " bit " << bit1 << " and bit " << bit2 << " to " << hex << blessval << dec << endl;
		}
	}

	if (pubvar && !TEST_PUBLIC_INPUTS_UNBOUNDED)
	{
		if (nbits >= TX_FIELD_BITS)
		{
			#if LIMIT_ZKPROOF_INPUTS_TO_PRIME_FIELD
			// Make sure val is less than the prime modulus.  The values of public inputs are all known and should all be valid,
			// so this is just a double-check to make sure none of the public inputs have an unexpected value when verifying a proof

			CCASSERTZ(blessval > bigint_t(0UL) - bigint_t(1UL));
			#endif
		}
		else
		{
			// Make sure val fits inside nbits.  The values of public inputs are all known and should all be valid,
			// so this is just a double-check to make sure none of the public inputs have an unexpected value when verifying a proof

			//cerr << "_bless_input " << input << " nbits " << nbits << " value " << hex << blessval << dec << endl;

			for (int i = 0; i < blessval.numberLimbs(); ++i)
			{
				//cerr << "_bless_input premask nbits " << nbits << " word " << i << " clear <= " << i * sizeof(mp_limb_t) * CHAR_BIT << " mask < " << (i + 1) * sizeof(mp_limb_t) * CHAR_BIT << endl;

				if (nbits <= i * sizeof(mp_limb_t) * CHAR_BIT)
				{
					//cerr << "_bless_input nbits " << nbits << " word " << i << " cleared val " << hex << blessval << dec << endl;

					CCASSERTZ(BIGWORD(blessval, i));
				}
				else if (nbits < (i + 1) * sizeof(mp_limb_t) * CHAR_BIT)
				{
					auto mask = ((mp_limb_t)1 << (nbits % (sizeof(mp_limb_t) * CHAR_BIT))) - 1;

					//cerr << "_bless_input nbits " << nbits << " word " << i << " mask " << hex << mask << " wordval " << BIGWORD(blessval, i) << dec << endl;

					CCASSERTZ(BIGWORD(blessval, i) & ~mask);
				}
			}
		}
	}

	if (0) // for testing -- change to 0 for release
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "bless_input badsel " << badsel << " anyval " << anyval << " " << input << " " << hex << blessval << dec << endl;
		cout << "bless_input badsel " << badsel << " anyval " << anyval << " " << input << " " << hex << blessval << dec << endl;
	}

	bless(var, blessval);

	--badsel;
}

static void bless_tx_public_inputs(TxPayZK& zk, const TxPay& tx, int& badsel)
{
	#if 0 // test hash
	bigint_t tx_type;
	subBigInt(bigint_t(0UL), bigint_t(1UL), tx_type, false);
	//tx_type = bigint_t(0UL) - bigint_t(1UL);
	//addBigInt(tx_type, bigint_t(1UL + 0xfe), tx_type, false);
	bless_input(1, notbad, zk.publics.tx_type, tx_type, TX_INPUT_BITS, "tx tx_type (never bad)");										// test_hash -- don't count
	return;
	#endif

	//cerr << "bless_tx_public_inputs badsel " << badsel << endl;

	int notbad = -99;	// used for values that do not always make with proof invalid when they are changed
	bool nobad, anyval;	// note: for public inputs, anyval cannot depend on a private input, since private input values may not be available when verifying

	bless_input(1, notbad, zk.publics.tx_type, tx.tx_type, TX_TYPE_BITS, "tx tx_type (never bad)");											// tx (never bad)
	bless_input(1, notbad, zk.publics.source_chain, tx.source_chain, TX_CHAIN_BITS, "tx source_chain (never bad)");							// tx (never bad)
	bless_input(1, notbad, zk.publics.param_level, tx.param_level, TX_BLOCKLEVEL_BITS, "tx param_level (never bad)");						// tx (never bad)
	bless_input(1, notbad, zk.publics.param_time, tx.param_time, TX_TIME_BITS, "tx param_time (never bad)");								// tx (never bad)
	bless_input(1, notbad, zk.publics.revision, tx.revision, TX_REVISION_BITS, "tx revision (never bad)");									// tx (never bad)
	bless_input(1, notbad, zk.publics.expiration, tx.expiration, TX_TIME_BITS, "tx expiration (never bad)");								// tx (never bad)
	bless_input(1, notbad, zk.publics.refhash, tx.refhash, TX_REFHASH_BITS, "tx refhash (never bad)");										// tx (never bad)
	bless_input(1, notbad, zk.publics.reserved, tx.reserved, TX_RESERVED_BITS, "tx reserved (never bad)");									// tx (never bad)

	bigint_t donation;
	tx_amount_decode(tx.donation_fp, donation, true, TX_DONATION_BITS, TX_AMOUNT_EXPONENT_BITS);
	donation = donation + tx.amount_carry_out - tx.amount_carry_in;
	bless_input(1, badsel, zk.publics.donation, donation, TX_INPUT_BITS, "tx donation");													// tx

	nobad = true;	// outvalmin can't be made bad if there are no outputs or all output amounts are at max
	for (unsigned i = 0; i < tx.nout; ++i)
	{
		if (tx.outputs[i].__asset == 0 && tx_amount_decode_exponent(tx.outputs[i].__amount_fp, TX_AMOUNT_EXPONENT_BITS) < TX_AMOUNT_EXPONENT_MASK)
			nobad = false;
	}
	if (tx.nout)
	{
		bigint_t badval = TX_AMOUNT_EXPONENT_MASK + 1UL;
		bless_input(1, badsel, zk.publics.outvalmin, tx.outvalmin, TX_AMOUNT_EXPONENT_BITS, "tx outvalmin", nobad, 0, 0, &badval);			// tx when outputs
	}
	else
	{
		anyval = true;
		bless_input(1, badsel, zk.publics.outvalmin, 0UL, TX_AMOUNT_EXPONENT_BITS, "tx outvalmin when no outputs", 0, anyval);					// tx when no outputs
	}

	nobad = true;	// outvalmax can't be made bad if there are no outputs or all output amounts are zero
	for (unsigned i = 0; i < tx.nout; ++i)
	{
		if (tx.outputs[i].__asset == 0 && tx_amount_decode_exponent(tx.outputs[i].__amount_fp, TX_AMOUNT_EXPONENT_BITS) > 0)
			nobad = false;
	}
	if (tx.nout)
	{
		bigint_t badval = 0UL;
		bless_input(1, badsel, zk.publics.outvalmax, tx.outvalmax, TX_AMOUNT_EXPONENT_BITS, "tx outvalmax", nobad, 0, 0, &badval);			// tx when outputs
		bless_input(1, badsel, zk.publics.M_commitment_iv, tx.M_commitment_iv, TX_COMMIT_IV_BITS, "tx M_commitment_iv");					// tx when outputs
	}
	else
	{
		anyval = true;
		bless_input(1, badsel, zk.publics.outvalmax, 0UL, TX_AMOUNT_EXPONENT_BITS, "tx outvalmax when no outputs", 0, anyval);				// tx when no outputs
		bless_input(1, badsel, zk.publics.M_commitment_iv, 0UL, TX_COMMIT_IV_BITS, "tx M_commitment_iv", 0, anyval);						// tx when no outputs
	}

	bless_input(1, notbad, zk.publics.allow_restricted_addresses, tx.allow_restricted_addresses, 1, "tx allow_restricted_addresses (never bad)");	// tx (never bad)
}

static uint64_t compute_bad_mask(uint64_t mask, uint64_t pad)
{
	#if TX_ASSET_BITS > 64
	#error TX_ASSET_BITS > 64
	#endif
	#if TX_AMOUNT_BITS > 64
	#error TX_AMOUNT_BITS > 64
	#endif

	// xor = mask & pad
	// make mask bad by flipping a random bit in mask, where the corresponding bit in pad is set

	//cerr << "compute_bad_mask mask " << hex << mask << " pad " << pad << dec << endl;

	for (unsigned i = 0; i < 200 && pad; ++i)
	{
		bigint_t rndval;
		get_rand_val(rndval, &rand_count_priv);		// this code path depends on the values of private inputs that aren't available when verifying, so don't disturb the main pseudorandom counter used to generate values for public inputs
		uint64_t bit = (uint64_t)1 << (BIG64(rndval) & 63);
		if (bit & pad)
			return mask ^ bit;
	}

	return mask ^ pad;
}

static void bless_output_public_inputs(TxOutZKPub& zkoutpub, const TxOut& txout, bool enforce, int& badsel)
{
	int notbad = -99;	// used for values that do not always make with proof invalid when they are changed

	//cerr << "bless_output_public_inputs badsel " << badsel << endl;

	bool nobad, anyval;	// note: for public inputs, anyval cannot depend on a private input, since private input values may not be available when verifying

	bless(zkoutpub.enforce, enforce);

	unsigned enforce_address = (1U - txout.no_address) * enforce;

	anyval = !enforce_address;
	bless_input(1, badsel, zkoutpub.dest_chain, txout.addrparams.dest_chain, TX_CHAIN_BITS, "output dest_chain", 0, anyval);				// output

	bless_input(1, badsel, zkoutpub.M_domain, txout.M_domain, TX_DOMAIN_BITS, "output M_domain");												// output

	nobad = enforce_address;	// enforce_address can't be made bad if the address is valid
	bless_input(1, badsel, zkoutpub.enforce_address, enforce_address, 1, "output enforce_address", nobad);								// output

	anyval = !enforce_address;
	bless_input(1, badsel, zkoutpub.M_address, txout.M_address, TX_ADDRESS_BITS, "output M_address", 0, anyval);						// output

	if (enforce)
	{
		nobad = txout.acceptance_required || (BIG64(txout.addrparams.__dest) & TX_ACCEPT_REQ_DEST_MASK);
		bless_input(1, badsel, zkoutpub.acceptance_required, txout.acceptance_required, 1, "output acceptance_required", nobad);		// output when enforced
	}
	else
	{
		bless_input(1, badsel, zkoutpub.acceptance_required, 1UL, 1, "output acceptance_required when not enforced");					// output when not enforced
	}

	unsigned multiplier = (1U + txout.repeat_count) * enforce;
	bless_input(1, notbad, zkoutpub.multiplier, multiplier, TX_FIELD_BITS, "output multiplier");										// output (never bad)

	unsigned enforce_asset = (1U - txout.no_asset) * enforce;
	nobad = enforce_asset;			// changing enforce_asset from true to false doesn't make it bad
	bless_input(1, badsel, zkoutpub.enforce_asset, enforce_asset, 1, "output enforce_asset", nobad);									// output
	if (enforce_asset)
	{
		// note RULE tx output: if enforce_asset, then M_asset_enc = #asset ^ #asset_xor
		// note RULE tx output:	where #asset_xor = asset_mask & (zkhash(M_encrypt_iv, #dest, #paynum))
		nobad = !txout.__asset_pad;	// mask can't be made bad when pad is 0
		bigint_t badval;
		if (!badsel) badval = compute_bad_mask(txout.asset_mask, txout.__asset_pad);
		bless_input(1, badsel, zkoutpub.asset_mask, txout.asset_mask, TX_ASSET_BITS, "output asset_mask", nobad, 0, 0, &badval);		// output when enforced
		bless_input(1, badsel, zkoutpub.M_asset_enc, txout.M_asset_enc, TX_ASSET_BITS, "output M_asset_enc");							// output when enforced
	}
	else
	{
		// note RULE tx output: if !enforce_asset, then M_asset_enc = #asset ^ (asset_mask & #asset)
		nobad = !txout.__asset;	// mask can't be made bad when pad is 0
		bigint_t badval;
		if (!badsel) badval = compute_bad_mask(txout.asset_mask, txout.__asset);
		bless_input(1, badsel, zkoutpub.asset_mask, TX_ASSET_MASK, TX_ASSET_BITS, "output asset_mask when asset not enforced", nobad, 0, 0, &badval); // output when not enforced
		bless_input(1, badsel, zkoutpub.M_asset_enc, 0UL, TX_ASSET_BITS, "output M_asset_enc when asset not enforced");					// output when not enforced
	}

	unsigned enforce_amount = (1U - txout.no_amount) * enforce;
	nobad = enforce_amount;			// changing enforce_amount from true to false doesn't make it bad
	bless_input(1, badsel, zkoutpub.enforce_amount, enforce_amount, 1, "output enforce_amount", nobad);									// output
	if (enforce_amount)
	{
		// note RULE tx output: if enforce_amount, then M_amount_enc = #amount ^ #amount_xor
		// note RULE tx output:	where #amount_xor = amount_mask & (zkhash(M_encrypt_iv, #dest, #paynum) >> TX_ASSET_BITS)
		nobad = !txout.__amount_pad;	// mask can't be made bad when pad is 0
		bigint_t badval;
		if (!badsel) badval = compute_bad_mask(txout.amount_mask, txout.__amount_pad);
		bless_input(1, badsel, zkoutpub.amount_mask, txout.amount_mask, TX_AMOUNT_BITS, "output amount_mask", nobad, 0, 0, &badval); // output when enforced
		bless_input(1, badsel, zkoutpub.M_amount_enc, txout.M_amount_enc, TX_AMOUNT_BITS, "output M_amount_enc");						// output when enforced
	}
	else
	{
		// note RULE tx output: if !enforce_amount, then M_amount_enc = #amount ^ (amount_mask & #amount)
		nobad = !txout.__amount_fp;	// mask can't be made bad when pad is 0
		bigint_t badval;
		if (!badsel) badval = compute_bad_mask(txout.amount_mask, txout.__amount_fp);
		bless_input(1, badsel, zkoutpub.amount_mask, TX_AMOUNT_MASK, TX_AMOUNT_BITS, "output amount_mask when amount not enforced", nobad, 0, 0, &badval); // output when not enforced
		bless_input(1, badsel, zkoutpub.M_amount_enc, 0UL, TX_AMOUNT_BITS, "output M_amount_enc when amount not enforced");				// output when not enforced
	}

	//nbless_input(zkoutpub.M_commitment_index, index, TX_COMMIT_INDEX_BITS, "output commitment index");
	bless_input(1, badsel, zkoutpub.M_commitment, txout.M_commitment, TX_FIELD_BITS, "output M_commitment");							// output
}

static void bless_input_public_inputs(TxInZKPub& zkinpub, const TxIn& txin, bool enforce, int& badsel)
{
	//cerr << "bless_input_public_inputs badsel " << badsel << endl;

	int notbad = -99;	// used for values that do not always make with proof invalid when they are changed
	bool nobad, anyval;	// note: for public inputs, anyval cannot depend on a private input, since private input values may not be available when verifying

	bless(zkinpub.enforce, enforce);

	if (enforce)
	{
		nobad = txin.enforce_master_secret || txin.____master_secret_valid;  // enforce_master_secret can't be made bad when master_secret is valid
		bless_input(1, badsel, zkinpub.enforce_master_secret, txin.enforce_master_secret, 1, "input enforce_master_secret", nobad);		// input when input enforced

		nobad = txin.enforce_spend_secrets || txin.____master_secret_valid || txin.____spend_secrets_valid;  // enforce_spend_secrets can't be made bad when spend_secrets are valid
		bless_input(1, badsel, zkinpub.enforce_spend_secrets, txin.enforce_spend_secrets, 1, "input enforce_spend_secrets", nobad);		// input when input enforced

		nobad = txin.enforce_trust_secrets || txin.____master_secret_valid || txin.____spend_secrets_valid || txin.____trust_secrets_valid;  // enforce_trust_secrets can't be made bad when spend_secrets or trust_secrets are valid
		bless_input(1, badsel, zkinpub.enforce_trust_secrets, txin.enforce_trust_secrets, 1, "input enforce_trust_secrets", nobad);		// input when input enforced

		nobad = txin.enforce_freeze || txin.params.____allow_freeze;  // enforce_freeze can't be made bad when it is allowed
		bless_input(1, badsel, zkinpub.enforce_freeze, txin.enforce_freeze, 1, "input enforce_freeze", nobad);							// input when input enforced

		nobad = txin.enforce_unfreeze || txin.____master_secret_valid || (txin.____trust_secrets_valid && txin.params.____allow_trust_unfreeze); // enforce_unfreeze can't be made bad when it is allowed
		bless_input(1, badsel, zkinpub.enforce_unfreeze, txin.enforce_unfreeze, 1, "input enforce_unfreeze", nobad);					// input when input enforced

		nobad = (txin.__asset != 0 || tx_amount_decode_exponent(txin.__amount_fp, TX_AMOUNT_EXPONENT_BITS) == 0);	// invalmax can't be made bad if input amount is zero
		bigint_t badval = 0UL;
		bless_input(1, badsel, zkinpub.invalmax, txin.invalmax, TX_AMOUNT_EXPONENT_BITS, "input invalmax", nobad, 0, 0, &badval);				// input when input enforced

		bool spend_bad = txin.____spend_secrets_valid && txin.params.____spend_delaytime;
		bool trust_bad = txin.____trust_secrets_valid && txin.params.____trust_delaytime;
		if ((spend_bad && !trust_bad) || (spend_bad && trust_bad && RandTest(2)))
		{
			badval = txin.params.____spend_delaytime - 1UL;
			nobad = false;
		}
		else if (trust_bad)
		{
			badval = txin.params.____trust_delaytime - 1UL;
			nobad = false;
		}
		else
		{
			nobad = true;
		}
		bless_input(1, badsel, zkinpub.delaytime, txin.delaytime, TX_DELAYTIME_BITS, "input delaytime", nobad, 0, 0, &badval);				// input when input enforced

		bless_input(1, badsel, zkinpub.M_domain, txin.M_domain, TX_DOMAIN_BITS, "input M_domain");												// input when input enforced

		nobad = !txin.no_serialnum;
		bless_input(1, badsel, zkinpub.enforce_serialnum, 1UL - txin.no_serialnum, 1, "input enforce_serialnum", nobad);				// input when input enforced
	}
	else
	{
		anyval = true;
		bless_input(1, badsel, zkinpub.enforce_master_secret, 0UL, 1, "input enforce_master_secret when input not enforced");			// input when input not enforced
		bless_input(1, badsel, zkinpub.enforce_spend_secrets, 0UL, 1, "input enforce_spend_secrets when input not enforced");			// input when input not enforced
		bless_input(1, badsel, zkinpub.enforce_trust_secrets, 0UL, 1, "input enforce_trust_secrets when input not enforced");			// input when input not enforced
		bless_input(1, badsel, zkinpub.enforce_freeze, 0UL, 1, "input enforce_freeze when input not enforced");							// input when input not enforced
		bless_input(1, badsel, zkinpub.enforce_unfreeze, 0UL, 1, "input enforce_unfreeze when input not enforced");						// input when input not enforced
		bless_input(1, badsel, zkinpub.invalmax, 0UL, TX_AMOUNT_EXPONENT_BITS, "input invalmax when input not enforced", 0, anyval);			// input when input not enforced
		bless_input(1, badsel, zkinpub.delaytime, 0UL, TX_DELAYTIME_BITS, "input delaytime when input not enforced", 0, anyval);			// input when input not enforced
		bless_input(1, badsel, zkinpub.M_domain, 0UL, TX_DOMAIN_BITS, "input M_domain when input not enforced", 0, anyval);					// input when input not enforced
		bless_input(1, badsel, zkinpub.enforce_serialnum, 0UL, 1, "input enforce_serialnum when input not enforced");					// input when input not enforced
	}

	unsigned enforce_public_commitment = !txin.pathnum && enforce;
	nobad = enforce_public_commitment;
	bless_input(1, badsel, zkinpub.enforce_public_commitment, enforce_public_commitment, 1, "input enforce_public_commitment", nobad);	// input

	anyval = !enforce_public_commitment;
	bless_input(1, badsel, zkinpub.M_commitment, txin._M_commitment, TX_FIELD_BITS, "input public M_commitment", 0, anyval);			// input

	unsigned enforce_public_commitnum = (!txin.pathnum && enforce ? 1U - txin.no_serialnum : 0);
	nobad = enforce_public_commitnum || (!txin.pathnum && txin.no_serialnum && !TEST_RANDOM_ANYVALS); // makebad will fail if private commitnum is also zero
	bless_input(1, badsel, zkinpub.enforce_public_commitnum, enforce_public_commitnum, 1, "input enforce_public_commitnum", nobad);		// input

	anyval = !enforce_public_commitnum;
	bless_input(1, badsel, zkinpub.M_commitnum, txin._M_commitnum, TX_COMMITNUM_BITS, "input public M_commitnum", 0, anyval);			// input

	anyval = enforce_public_commitment;
	bless_input(1, badsel, zkinpub.merkle_root, txin.merkle_root, TX_FIELD_BITS, "input merkle_root", 0, anyval);						// input

	anyval = txin.no_serialnum || !enforce;
	bless_input(1, badsel, zkinpub.S_serialnum, txin.S_serialnum, TX_SERIALNUM_BITS, "input S_serialnum", 0, anyval);					// input

	nobad = !(txin.params.____require_public_hashkey && txin.____spend_secrets_valid);
	anyval = !enforce;
	bless_input(1, badsel, zkinpub.S_hashkey, txin.S_hashkey, TX_HASHKEY_BITS, "input S_hashkey", nobad, anyval);						// input

	anyval = !enforce;
	bless_input(1, notbad, zkinpub.S_spendspec_hashed, txin.S_spendspec_hashed, TX_INPUT_BITS, "input S_spendspec_hashed (never bad)", 0, anyval);	// input (never bad)
}

static void bless_tx_private_inputs(TxPayZK& zk, const TxPay& tx, int& badsel)
{
	//cerr << "bless_tx_private_inputs badsel " << badsel << endl;

	const bool nomod = true;

	for (unsigned i = 1; i < zk.nassets; ++i)	// asset[0] is assumed == 0
	{
		auto asset = tx.__asset_list[i];
		bool anyval = true;	// can't be bad if it's not used

		for (unsigned j = 0; j < tx.nout; ++j)
		{
			if (asset == tx.outputs[j].__asset)
				anyval = false;
		}

		for (unsigned j = 0; j < tx.nin; ++j)
		{
			if (asset == tx.inputs[j].__asset)
				anyval = false;
		}

		bless_input(0, badsel, zk.privates.__tx_asset[i-1], asset, TX_ASSET_BITS, "tx __tx_asset", 0, anyval, nomod);						// tx * (nassets-1)*(nassets > 0)
	}
}

static void bless_output_private_inputs(const TxPayZK& zk, TxOutZKPriv& zkoutpriv, const TxPay& tx, const TxOut& txout, bool enforce, int& badsel)
{
	//cerr << "bless_output_private_inputs badsel " << badsel << endl;

	const bool nomod = true;

	for (unsigned i = 0; i < zk.nassets; ++i)
		bless_input(0, badsel, zkoutpriv.__is_asset[i], (unsigned)(txout.__asset == tx.__asset_list[i]), 1, "output __is_asset", 0, 0, nomod); // output * nassets

	bless_input(0, badsel, zkoutpriv.__dest, txout.addrparams.__dest, TX_FIELD_BITS, "output __dest");										// output
	bless_input(0, badsel, zkoutpriv.__paynum, txout.addrparams.__paynum, TX_PAYNUM_BITS, "output __paynum");								// output
	bless_input(0, badsel, zkoutpriv.__asset, txout.__asset, TX_ASSET_BITS, "output __asset");											// output
	bless_input(0, badsel, zkoutpriv.__amount_fp, txout.__amount_fp, TX_AMOUNT_BITS, "output __amount_fp");								// output

	unsigned enforce_asset = (1U - txout.no_asset) * enforce;
	if (enforce_asset)
		bless_input(0, badsel, zkoutpriv.__asset_xor, txout.M_asset_enc ^ txout.__asset, TX_ASSET_BITS, "output __asset_xor");			// output when enforced
	else
		bless_input(0, badsel, zkoutpriv.__asset_xor, txout.__asset, TX_ASSET_BITS, "output __asset_xor when asset not enforced");		// output when not enforced

	unsigned enforce_amount = (1U - txout.no_amount) * enforce;
	if (enforce_amount)
		bless_input(0, badsel, zkoutpriv.__amount_xor, txout.M_amount_enc ^ txout.__amount_fp, TX_AMOUNT_BITS, "output __amount_xor");	// output when enforced
	else
		bless_input(0, badsel, zkoutpriv.__amount_xor, txout.__amount_fp, TX_AMOUNT_BITS, "output __amount_xor when amount not enforced");	// output when not enforced
}

static void bless_input_private_inputs(const TxPayZK& zk, const TxInZKPub& zkinpub, TxInZKPriv& zkinpriv, const TxPay& tx, const TxIn& txin, bool enforce, bool has_path, int& badsel)
{
	//cerr << "bless_input_private_inputs badsel " << badsel << endl;

	bool nobad, anyval;
	const bool nomod = true;

	zkinpriv.nsecrets = zk.nsecrets;

	for (unsigned i = 0; i < zk.nassets; ++i)
		bless_input(0, badsel, zkinpriv.__is_asset[i], (unsigned)(txin.__asset == tx.__asset_list[i]), 1, "input __is_asset", 0, 0, nomod); // input * nassets


	bless_input(0, badsel, zkinpriv.__asset, txin.__asset, TX_ASSET_BITS, "input __asset");												// input
	bless_input(0, badsel, zkinpriv.__amount_fp, txin.__amount_fp, TX_AMOUNT_BITS, "input __amount_fp");								// input
	bless_input(0, badsel, zkinpriv.__M_commitment_iv, txin.__M_commitment_iv, TX_COMMIT_IV_BITS, "input __M_commitment_iv");			// input
	//nbless_input(zkinpriv.__M_commitment_index, txin.__M_commitment_index, TX_COMMIT_INDEX_BITS, "input __M_commitment_index");

	anyval = !txin.pathnum && txin.no_serialnum;
	bless_input(0, badsel, zkinpriv.__M_commitnum, txin._M_commitnum, TX_COMMITNUM_BITS, "input private __M_commitnum", 0, anyval);			// input

	anyval = !enforce;
	bless_input(0, badsel, zkinpriv.__M_commitment, txin._M_commitment, TX_FIELD_BITS, "input private __M_commitment", 0, anyval, nomod);	// input


	anyval = !txin.____master_secret_valid;
	bless_input(0, badsel, zkinpriv.____master_secret, txin.secrets[0].____master_secret, TX_INPUT_BITS, "input ____master_secret", 0, anyval);								// input
	bless_input(0, badsel, zkinpriv.____spend_secret_number, txin.secrets[0].____spend_secret_number, TX_SPEND_SECRETNUM_BITS, "input ____spend_secret_number", 0, anyval); // input

	zkinpriv.____enforce_spendspec_with_spend_secrets_bit.resize(1);
	bless_input(0, badsel, zkinpriv.____enforce_spendspec_with_spend_secrets_bit[0], txin.params.____enforce_spendspec_with_spend_secrets, 1, "input ____enforce_spendspec_with_spend_secrets", 0, 0, nomod);		// input

	zkinpriv.____enforce_spendspec_with_trust_secrets_bit.resize(1);
	bless_input(0, badsel, zkinpriv.____enforce_spendspec_with_trust_secrets_bit[0], txin.params.____enforce_spendspec_with_trust_secrets, 1, "input ____enforce_spendspec_with_trust_secrets", 0, 0, nomod);		// input

	bless_input(0, badsel, zkinpriv.____required_spendspec_hash, txin.params.____required_spendspec_hash, TX_INPUT_BITS, "input ____required_spendspec_hash", 0, 0, nomod);								// input

	zkinpriv.____allow_master_secret_bit.resize(1);
	bless_input(0, badsel, zkinpriv.____allow_master_secret_bit[0], txin.params.____allow_master_secret, 1, "input ____allow_master_secret", 0, 0, nomod);				// input

	zkinpriv.____allow_freeze_bit.resize(1);
	bless_input(0, badsel, zkinpriv.____allow_freeze_bit[0], txin.params.____allow_freeze, 1, "input ____allow_freeze", 0, 0, nomod);									// input

	zkinpriv.____allow_trust_unfreeze_bit.resize(1);
	bless_input(0, badsel, zkinpriv.____allow_trust_unfreeze_bit[0], txin.params.____allow_trust_unfreeze, 1, "input ____allow_trust_unfreeze", 0, 0, nomod);		// input

	zkinpriv.____require_public_hashkey_bit.resize(1);
	bless_input(0, badsel, zkinpriv.____require_public_hashkey_bit[0], txin.params.____require_public_hashkey, 1, "input ____require_public_hashkey", 0, 0, nomod);	// input

	zkinpriv.____restrict_addresses_bit.resize(1);
	bless_input(0, badsel, zkinpriv.____restrict_addresses_bit[0], txin.params.____restrict_addresses, 1, "input ____restrict_addresses", 0, 0, nomod);				// input

	bless_input(0, badsel, zkinpriv.____spend_locktime, txin.params.____spend_locktime, TX_TIME_BITS, "input ____spend_locktime");									// input
	bless_input(0, badsel, zkinpriv.____trust_locktime, txin.params.____trust_locktime, TX_TIME_BITS, "input ____trust_locktime");									// input
	bless_input(0, badsel, zkinpriv.____spend_delaytime, txin.params.____spend_delaytime, TX_DELAYTIME_BITS, "input ____spend_delaytime");							// input
	bless_input(0, badsel, zkinpriv.____trust_delaytime, txin.params.____trust_delaytime, TX_DELAYTIME_BITS, "input ____trust_delaytime");							// input

	bless_input(0, badsel, zkinpriv.____required_spend_secrets, txin.params.____required_spend_secrets, TX_MAX_SECRETS_BITS, "input ____required_spend_secrets");	// input
	bless_input(0, badsel, zkinpriv.____required_trust_secrets, txin.params.____required_trust_secrets, TX_MAX_SECRETS_BITS, "input ____required_trust_secrets");	// input

	bless_input(0, badsel, zkinpriv.____destnum, txin.params.____destnum, TX_DESTNUM_BITS, "input ____destnum");													// input
	bless_input(0, badsel, zkinpriv.__paynum, txin.params.addrparams.__paynum, TX_PAYNUM_BITS, "input __paynum");													// input

	nobad = !(txin.enforce_master_secret || (txin.enforce_spend_secrets && !txin.____spend_secrets_valid) || (txin.enforce_trust_secrets && !txin.____spend_secrets_valid && !txin.____trust_secrets_valid) || (txin.enforce_unfreeze && (!txin.____trust_secrets_valid || !txin.params.____allow_trust_unfreeze)));
	bless_input(0, badsel, zkinpriv.____master_secret_valid, txin.____master_secret_valid, 1, "input ____master_secret_valid", nobad, 0, nomod);					// input

	nobad = !(txin.enforce_spend_secrets && !txin.____master_secret_valid);
	bless_input(0, badsel, zkinpriv.____spend_secrets_valid, txin.____spend_secrets_valid, 1, "input ____spend_secrets_valid", nobad, 0, nomod);					// input

	nobad = !((txin.enforce_trust_secrets && !txin.____master_secret_valid && !txin.____spend_secrets_valid) || (txin.enforce_unfreeze && !txin.____master_secret_valid));
	bless_input(0, badsel, zkinpriv.____trust_secrets_valid, txin.____trust_secrets_valid, 1, "input ____trust_secrets_valid", nobad, 0, nomod);					// input


	for (unsigned j = 0; j < zkinpriv.nsecrets; ++j)
	{
		unsigned secret_valid = (txin.____spend_secrets_valid && txin.secrets[j].____have_spend_secret) || (!txin.____spend_secrets_valid && txin.secrets[j].____have_trust_secret);
		secret_valid &= !txin.secrets[j].____have_restricted_address;
		nobad = secret_valid;	// changing value from valid to not might not make it bad
		if (j == 0)
			nobad |= txin.____master_secret_valid;	// changing value from not valid to valid has no effect if ____master_secret_valid is set
		bless_input(0, badsel, zkinpriv.____secret_valid[j], secret_valid, 1, "input ____secret_valid", nobad, 0, nomod);								// input * nsecrets

		anyval = !((j == 0 && txin.____master_secret_valid) || (txin.____spend_secrets_valid && secret_valid)); // anyval 0 if spend_secret enforced
		bless_input(0, badsel, zkinpriv.____spend_secret[j], txin.secrets[j].____spend_secret, TX_INPUT_BITS, "input ____spend_secret", 0, anyval);					// input * nsecrets

		anyval &= !secret_valid; // anyval 0 if spend_secret or trust_secret enforced
		bless_input(0, badsel, zkinpriv.____trust_secret[j], txin.secrets[j].____trust_secret, TX_INPUT_BITS, "input ____trust_secret", 0, anyval);							// input * nsecrets
	}

	zkinpriv.____use_spend_secret_bits.resize(TX_MAX_SECRETS);
	zkinpriv.____use_trust_secret_bits.resize(TX_MAX_SECRETS);

	for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
	{
		bless_input(0, badsel, zkinpriv.____use_spend_secret_bits[j], txin.params.____use_spend_secret[j], 1, "input ____use_spend_secret", 0, 0, nomod);					// input * TX_MAX_SECRETS
		bless_input(0, badsel, zkinpriv.____use_trust_secret_bits[j], txin.params.____use_trust_secret[j], 1, "input ____use_trust_secret", 0, 0, nomod);					// input * TX_MAX_SECRETS
	}

	for (unsigned j = 0; j < TX_MAX_SECRET_SLOTS; ++j)
		bless_input(0, badsel, zkinpriv.____monitor_secret[j], txin.secrets[j].____monitor_secret, TX_INPUT_BITS, "input ____monitor_secret");								// input * TX_MAX_SECRET_SLOTS

	// note RULE tx input: where @enforce_restricted_addresses = @restrict_addresses and not @master_secret_valid and not enforce_freeze

	bool enforce_restricted_addresses = txin.params.____restrict_addresses && ! txin.____master_secret_valid && ! txin.enforce_freeze;

	for (unsigned i = 0; i < zk.nrouts; ++i)
	{
		zkinpriv.____output_address_matches[i].resize(zk.nraddrs);

		unsigned matches = 0;
		for (unsigned j = 0; j < zk.nraddrs; ++j)
		{
			if (i < tx.nout && restricted_address_slot_open(txin.params, j))
			{
				bigint_t raddress;
				get_restricted_address(txin.secrets, j, raddress);

				bool match = !tx.outputs[i].no_address && (tx.outputs[i].M_address == raddress);
				matches += match;
				if (badsel >= 0 && enforce_restricted_addresses)
					cerr << "enforce_restricted_addresses outputi " << i << " addri " << j << " match " << match << " matches " << matches << endl;
			}
		}

		nobad = !(enforce_restricted_addresses && matches == 1);

		for (unsigned j = 0; j < zk.nraddrs; ++j)
		{
			unsigned matches = 0;
			if (i < tx.nout && restricted_address_slot_open(txin.params, j))
			{
				bigint_t raddress;
				get_restricted_address(txin.secrets, j, raddress);

				matches = !tx.outputs[i].no_address && (tx.outputs[i].M_address == raddress);
			}

			bless_input(0, badsel, zkinpriv.____output_address_matches[i][j], matches, 1, "input ____output_address_matches", nobad, 0, nomod);								// input * nrouts*nraddrs
		}
	}
}

static void bless_public_inputs(TxPayZK& zk, TxPay& tx, int& badsel)
{
	int notbad = -99;	// used for values that do not always make with proof invalid when they are changed

	bless_tx_public_inputs(zk, tx, badsel);

	for (unsigned i = 0; i < zk.nout; ++i)
	{
		if (i < tx.nout)
			bless_output_public_inputs(zk.output_public[i], tx.outputs[i], true, badsel);
		else
			bless_output_public_inputs(zk.output_public[i], tx.outputs[i], false, notbad);
	}

	//cerr << "tx.nin " << tx.nin << " tx.nin_with_path " << tx.nin_with_path << " zk.nin " << zk.nin << endl;

	unsigned zkindex = 0;

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		// tx inputs with Merkle paths come first
		unsigned pathnum = tx.inputs[i].pathnum;
		if (!pathnum)
			continue;

		tx.inputs[i].zkindex = zkindex;

		//cerr << "tx input " << i << " pathnum " << pathnum << " maps to zk input " << zkindex << endl;

		bless_input_public_inputs(zk.input_public[zkindex], tx.inputs[i], true, badsel);

		CCASSERT(zkindex < zk.nin_with_path);
		TxInPathZK& zkinpath = zk.inpaths[zkindex];
		bless(zkinpath.enforce_path, 1UL);

		++zkindex;
	}

	CCASSERT(zkindex == tx.nin_with_path);

	for (unsigned i = 0; i < zk.nin; ++i)
	{
		unsigned pathnum = tx.inputs[i].pathnum;
		if (pathnum)
			continue;

		tx.inputs[i].zkindex = zkindex;

		//cerr << "tx input " << i << " nopath maps to zk input " << zkindex << " tx.nin " << tx.nin << endl;

		if (zkindex < tx.nin)
			bless_input_public_inputs(zk.input_public[zkindex], tx.inputs[i], true, badsel);
		else
			bless_input_public_inputs(zk.input_public[zkindex], tx.inputs[i], false, notbad);

		if (zkindex < zk.nin_with_path)
		{
			TxInPathZK& zkinpath = zk.inpaths[zkindex];
			bless(zkinpath.enforce_path, 0UL);
		}

		++zkindex;
	}

	CCASSERT(zkindex == zk.nin);
}

static void bless_private_inputs(TxPayZK& zk, const TxPay& tx, int& badsel)
{
	#if 0
	zk.test_var.resize(100000);
	for (unsigned i = 0; i < zk.test_var.size(); ++i)
		bless(zk.test_var[i], 1);
	#endif

	int notbad = -99;	// used for values that do not always make with proof invalid when they are changed

	bless_tx_private_inputs(zk, tx, badsel);

	for (unsigned i = 0; i < zk.nout; ++i)
	{
		if (i < tx.nout)
			bless_output_private_inputs(zk, zk.output_private[i], tx, tx.outputs[i], true, badsel);
		else
			bless_output_private_inputs(zk, zk.output_private[i], tx, tx.outputs[i], false, notbad);
	}

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		// tx inputs with Merkle paths come first
		unsigned pathnum = tx.inputs[i].pathnum;
		if (!pathnum)
			continue;

		unsigned zkindex = tx.inputs[i].zkindex;

		//cerr << "tx input " << i << " pathnum " << pathnum << " mapped to zk input " << zkindex << endl;

		bless_input_private_inputs(zk, zk.input_public[zkindex], zk.input_private[zkindex], tx, tx.inputs[i], true, true, badsel);

		CCASSERT(zkindex < zk.nin_with_path);
		TxInPathZK& zkinpath = zk.inpaths[zkindex];

		zkinpath.__M_merkle_path.resize(TX_MERKLE_DEPTH);
		for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
			bless_input(0, badsel, zkinpath.__M_merkle_path[j], tx.inpaths[pathnum-1].__M_merkle_path[j], TX_FIELD_BITS, "input __M_merkle_path");							// TX_MERKLE_DEPTH inputs when has_path
	}

	for (unsigned i = 0; i < zk.nin; ++i)
	{
		unsigned pathnum = tx.inputs[i].pathnum;
		if (pathnum)
			continue;

		unsigned zkindex = tx.inputs[i].zkindex;

		//cerr << "tx input " << i << " nopath mapped to zk input " << zkindex << endl;

		if (zkindex < tx.nin)
			bless_input_private_inputs(zk, zk.input_public[zkindex], zk.input_private[zkindex], tx, tx.inputs[i], true, false, badsel);
		else
			bless_input_private_inputs(zk, zk.input_public[zkindex], zk.input_private[zkindex], tx, tx.inputs[i], false, false, notbad);

		if (zkindex < zk.nin_with_path)
		{
			// this input does not include a path, but this proof setup requires these path variables to be blessed, so use dummy values

			TxInPathZK& zkinpath = zk.inpaths[zkindex];

			zkinpath.__M_merkle_path.resize(TX_MERKLE_DEPTH);
			bool anyval = true;
			for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
				bless_input(0, notbad, zkinpath.__M_merkle_path[j], 99UL, TX_FIELD_BITS, "input __M_merkle_path when not enforced (never bad)", 0, anyval);									// TX_MERKLE_DEPTH inputs when not has_path (never bad)
		}
	}
}

static void breakout_bits(TxPayZK& zk)
{
	zk.publics.M_commitment_iv_bits = ZKHasher::extractBits(zk.publics.M_commitment_iv, TX_COMMIT_IV_BITS);

	zk.publics.M_encrypt_iv_bits.resize(TX_ENC_IV_BITS);

	for (unsigned i = 0; i < TX_ENC_IV_BITS; ++i)
		zk.publics.M_encrypt_iv_bits[i] = zk.publics.M_commitment_iv_bits[i];

	for (unsigned i = 0; i < zk.nout; ++i)
	{
		TxOutZKPub& zkoutpub = zk.output_public[i];
		TxOutZKPriv& zkoutpriv = zk.output_private[i];

		zkoutpub.dest_chain_bits = ZKHasher::extractBits(zkoutpub.dest_chain, TX_CHAIN_BITS);
		zkoutpub.M_domain_bits = ZKHasher::extractBits(zkoutpub.M_domain, TX_DOMAIN_BITS);
		zkoutpub.asset_mask_bits = ZKHasher::extractBits(zkoutpub.asset_mask, TX_ASSET_BITS);
		zkoutpub.M_asset_enc_bits = ZKHasher::extractBits(zkoutpub.M_asset_enc, TX_ASSET_BITS);
		zkoutpub.amount_mask_bits = ZKHasher::extractBits(zkoutpub.amount_mask, TX_AMOUNT_BITS);
		zkoutpub.M_amount_enc_bits = ZKHasher::extractBits(zkoutpub.M_amount_enc, TX_AMOUNT_BITS);
		//zkoutpub.M_commitment_index_bits = ZKHasher::nextractBits(zkoutpub.M_commitment_index, TX_COMMIT_INDEX_BITS);

		#if 0 // no longer used
		// compute zkoutpub.M_commitment_iv = i ^ zk.publics.M_commitment_iv
		unsigned xor_bits = i;
		zkoutpub.M_commitment_iv_bits = zk.publics.M_commitment_iv_bits;
		for (unsigned j = 0; xor_bits; ++j)
		{
			if (xor_bits & 1)
				zkoutpub.M_commitment_iv_bits[j] = 1UL - zkoutpub.M_commitment_iv_bits[j];
			xor_bits >>= 1;
		}
		#endif

		for (unsigned j = 0; j < zk.nassets; ++j)
			ZKConstraints::addBooleanity(zkoutpriv.__is_asset[j]);

		zkoutpriv.__dest_bits = ZKHasher::extractBits(zkoutpriv.__dest, TX_FIELD_BITS);
		zkoutpriv.__paynum_bits = ZKHasher::extractBits(zkoutpriv.__paynum, TX_PAYNUM_BITS);
		zkoutpriv.__asset_bits = ZKHasher::extractBits(zkoutpriv.__asset, TX_ASSET_BITS);
		zkoutpriv.__asset_xor_bits = ZKHasher::extractBits(zkoutpriv.__asset_xor, TX_ASSET_BITS);
		zkoutpriv.__amount_fp_bits = ZKHasher::extractBits(zkoutpriv.__amount_fp, TX_AMOUNT_BITS);
		zkoutpriv.__amount_xor_bits = ZKHasher::extractBits(zkoutpriv.__amount_xor, TX_AMOUNT_BITS);
	}

	for (unsigned i = 0; i < zk.nin; ++i)
	{
		TxInZKPub& zkinpub = zk.input_public[i];
		TxInZKPriv& zkinpriv = zk.input_private[i];

		zkinpub.M_domain_bits = ZKHasher::extractBits(zkinpub.M_domain, TX_DOMAIN_BITS);

		for (unsigned j = 0; j < zk.nassets; ++j)
			ZKConstraints::addBooleanity(zkinpriv.__is_asset[j]);

		// these are already just one bit, so we only need to enforce booleanity
		ZKConstraints::addBooleanity(zkinpriv.____enforce_spendspec_with_spend_secrets_bit[0]);
		ZKConstraints::addBooleanity(zkinpriv.____enforce_spendspec_with_trust_secrets_bit[0]);
		ZKConstraints::addBooleanity(zkinpriv.____allow_master_secret_bit[0]);
		ZKConstraints::addBooleanity(zkinpriv.____allow_freeze_bit[0]);
		ZKConstraints::addBooleanity(zkinpriv.____allow_trust_unfreeze_bit[0]);
		ZKConstraints::addBooleanity(zkinpriv.____require_public_hashkey_bit[0]);
		ZKConstraints::addBooleanity(zkinpriv.____restrict_addresses_bit[0]);

		ZKConstraints::addBooleanity(zkinpriv.____master_secret_valid);
		ZKConstraints::addBooleanity(zkinpriv.____spend_secrets_valid);
		ZKConstraints::addBooleanity(zkinpriv.____trust_secrets_valid);

		zkinpriv.__asset_bits = ZKHasher::extractBits(zkinpriv.__asset, TX_ASSET_BITS);
		zkinpriv.__amount_fp_bits = ZKHasher::extractBits(zkinpriv.__amount_fp, TX_AMOUNT_BITS);
		zkinpriv.__M_commitment_iv_bits = ZKHasher::extractBits(zkinpriv.__M_commitment_iv, TX_COMMIT_IV_BITS);
		//zkinpriv.__M_commitment_index_bits = ZKHasher::nextractBits(zkinpriv.__M_commitment_index, TX_COMMIT_INDEX_BITS);

		zkinpriv.__M_commitment_bits = ZKHasher::extractBits(zkinpriv.__M_commitment, TX_FIELD_BITS);
		zkinpriv.__M_commitnum_bits = ZKHasher::extractBits(zkinpriv.__M_commitnum, TX_COMMITNUM_BITS);

		zkinpriv.____master_secret_bits = ZKHasher::extractBits(zkinpriv.____master_secret, TX_INPUT_BITS);
		zkinpriv.____spend_secret_number_bits = ZKHasher::extractBits(zkinpriv.____spend_secret_number, TX_SPEND_SECRETNUM_BITS);
		zkinpriv.____required_spendspec_hash_bits = ZKHasher::extractBits(zkinpriv.____required_spendspec_hash, TX_INPUT_BITS);
		zkinpriv.____spend_locktime_bits = ZKHasher::extractBits(zkinpriv.____spend_locktime, TX_TIME_BITS);
		zkinpriv.____trust_locktime_bits = ZKHasher::extractBits(zkinpriv.____trust_locktime, TX_TIME_BITS);
		zkinpriv.____spend_delaytime_bits = ZKHasher::extractBits(zkinpriv.____spend_delaytime, TX_DELAYTIME_BITS);
		zkinpriv.____trust_delaytime_bits = ZKHasher::extractBits(zkinpriv.____trust_delaytime, TX_DELAYTIME_BITS);

		zkinpriv.____required_spend_secrets_bits = ZKHasher::extractBits(zkinpriv.____required_spend_secrets, TX_MAX_SECRETS_BITS);
		zkinpriv.____required_trust_secrets_bits = ZKHasher::extractBits(zkinpriv.____required_trust_secrets, TX_MAX_SECRETS_BITS);
		zkinpriv.____destnum_bits = ZKHasher::extractBits(zkinpriv.____destnum, TX_DESTNUM_BITS);
		zkinpriv.__paynum_bits = ZKHasher::extractBits(zkinpriv.__paynum, TX_PAYNUM_BITS);

		for (unsigned j = 0; j < zkinpriv.nsecrets; ++j)
		{
			ZKConstraints::addBooleanity(zkinpriv.____secret_valid[j]);

			zkinpriv.____spend_secret_bits[j] = ZKHasher::extractBits(zkinpriv.____spend_secret[j], TX_INPUT_BITS);
			zkinpriv.____trust_secret_bits[j] = ZKHasher::extractBits(zkinpriv.____trust_secret[j], TX_INPUT_BITS);
		}

		for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
		{
			ZKConstraints::addBooleanity(zkinpriv.____use_spend_secret_bits[j]);
			ZKConstraints::addBooleanity(zkinpriv.____use_trust_secret_bits[j]);
		}

		CCASSERTZ(TX_INPUT_BITS & 1);

		for (unsigned j = 0; j < TX_MAX_SECRET_SLOTS; ++j)
		{
			zkinpriv.____monitor_secret_lo_bits[j] = ZKHasher::extractBits(zkinpriv.____monitor_secret[j], TX_INPUT_BITS/2, &zkinpriv.____monitor_secret_hi[j], &zkinpriv.____monitor_secret_lo[j]);
			zkinpriv.____monitor_secret_hi_bits[j] = ZKHasher::extractBits(zkinpriv.____monitor_secret_hi[j], TX_INPUT_BITS/2);
		}

		for (unsigned k = 0; k < zk.nrouts; ++k)
		{
			for (unsigned j = 0; j < zk.nraddrs; ++j)
				ZKConstraints::addBooleanity(zkinpriv.____output_address_matches[k][j]);
		}
	}
}

// ensures a >= b for the field width nbits
// caution: in order for this to work, both a and b must not exceed the field width nbits
//	if either a or b is a hidden (non-public) input, this must be enforced elsewhere by
//	decomposing the hidden input into bits and checking the remainder
static void check_greaterequal(const ZKVAR& a, const ZKVAR& b, unsigned nbits, unsigned index, const ZKVAR *or_zero = NULL)
{
	// compute a-b and then check the carry bits

	ZKVAR diff = a - b;

	#if 0
	cerr << "check_greaterequal a " << hex << ZKValue::value(a) << dec << endl;
	cerr << "check_greaterequal b " << hex << ZKValue::value(b) << dec << endl;
	cerr << "check_greaterequal diff " << hex << ZKValue::value(diff) << dec << endl;
	cerr << "check_greaterequal nbits " << hex << ZKValue::value(nbits) << dec << endl;
	#endif

	ZKVAR rem;
	auto diff_bits = ZKHasher::extractBits(diff, nbits, &rem);

	// remainder or or_zero value must equal zero

	if (or_zero)
		rem = rem * (*or_zero);

	constrain_zero(rem);
}

// ensures all bits in (a & b) are clear (bitwise, for all bits)
#if 0 // not used
static void check_mask(const vector<ZKVAR>& a, const vector<ZKVAR>& b, unsigned index)
{
	//cerr << "check_mask " << a.size() << " " << b.size() << endl;

	CCASSERT(a.size() == b.size());

	CCASSERT(a.size());

	ZKVAR check = 0UL;

	for (unsigned i = 0; i < a.size(); ++i)
		check = check + a[i] * b[i];

	constrain_zero(check);
}
#endif

// ensures val = a & b (bitwise, for all bits)
static void check_and(const vector<ZKVAR>& val, const vector<ZKVAR>& a, const vector<ZKVAR>& b, unsigned index)
{
	// AND = a*b

	CCASSERT(val.size() == a.size());
	CCASSERT(val.size() == b.size());

	for (unsigned i = 0; i < val.size(); ++i)
	{
		ZKVAR check = a[i] * b[i] - val[i];
		constrain_zero(check, i);
	}
}

// ensures val = a ^ b (bitwise, for all bits)
static void check_xor(const vector<ZKVAR>& val, const vector<ZKVAR>& a, const vector<ZKVAR>& b, unsigned index)
{
	// XOR = a+b-2*a*b

	CCASSERT(val.size() == a.size());
	CCASSERT(val.size() == b.size());

	for (unsigned i = 0; i < val.size(); ++i)
	{
		ZKVAR check = a[i] + b[i] - a[i] * b[i] * bigint_t(2UL) - val[i];
		constrain_zero(check, i);
	}
}

// converts floating point amount to an integer
static void compute_integer_amount(const vector<ZKVAR>& amount_fp_bits, ZKVAR& amount_int)
{
	tx_amount_factors_init();

	amount_int = 1UL;

	for (unsigned i = 0; i < TX_AMOUNT_EXPONENT_BITS; ++i)
		amount_int = amount_int * (1UL - amount_fp_bits[i]);

	amount_int = (1UL - amount_int);	// 0 if all exponent bits are 0; 1 if any exponent bit is 1

	for (unsigned i = 0; i < TX_AMOUNT_BITS - TX_AMOUNT_EXPONENT_BITS; ++i)
		amount_int = amount_int + ((uint64_t)1 << i) * amount_fp_bits[i + TX_AMOUNT_EXPONENT_BITS];

	bigint_t base;

	for (unsigned i = 0; i < TX_AMOUNT_EXPONENT_BITS; ++i)
	{
		tx_get_amount_factor(base, 1 << i);

		ZKVAR factor = base * amount_fp_bits[i] + (1UL - amount_fp_bits[i]);

		amount_int = amount_int * factor;
	}
}

static void compute_output(const TxPayZK& zk, const TxOutZKPub& zkoutpub, TxOutZKPriv& zkoutpriv, unsigned index)
{
	vector<ZKHashInput> hashin(6);

	compute_integer_amount(zkoutpriv.__amount_fp_bits, zkoutpriv.__amount_int);

	// RULE tx output: sum(@is_asset[0..N]) = 1

	ZKVAR check = 1UL;
	for (unsigned i = 0; i < zk.nassets; ++i)
		check = check - zkoutpriv.__is_asset[i];
	constrain_zero(check);

	// RULE tx output: for each i = 0..N, if @is_asset[i], then #asset = @tx_asset[i] where @tx_asset[0] = 0

	for (unsigned i = 0; i < zk.nassets; ++i)
	{
		check = zkoutpriv.__asset;
		if (i > 0)
			check = check - zk.privates.__tx_asset[i-1];
		check = check * zkoutpriv.__is_asset[i];
		constrain_zero(check, i);
	}

	// RULE tx output: if #asset = 0, then #amount = 0 or #amount_exponent >= valmin
	// RULE tx output restatement: #amount_exponent >= valmin * enforce_minmax
	// RULE tx output restatement:	where enforce_minmax = (1 - #asset_bit[0]) * (1 - #asset_bit[1]) * ... * (1 - #asset_bit[N])

	ZKVAR enforce_minmax = zkoutpub.enforce;
	for (unsigned i = 0; i < TX_ASSET_BITS; ++i)
		enforce_minmax = enforce_minmax * (1UL - zkoutpriv.__asset_bits[i]);

	ZKVAR exponent = 0UL;
	for (unsigned i = 0; i < TX_AMOUNT_EXPONENT_BITS; ++i)
		exponent = exponent + ((uint64_t)1 << i) * zkoutpriv.__amount_fp_bits[i];

	check_greaterequal(exponent, zk.publics.outvalmin * enforce_minmax, TX_AMOUNT_EXPONENT_BITS, index, &zkoutpriv.__amount_fp);

	// RULE tx output: if #asset = 0, then #amount_exponent <= valmax
	// RULE tx output restatement: valmax >= #amount_exponent * enforce_minmax
	// RULE tx output restatement:	where enforce_minmax = (1 - #asset_bit[0]) * (1 - #asset_bit[1]) * ... * (1 - #asset_bit[N])

	check_greaterequal(zk.publics.outvalmax, exponent * enforce_minmax, TX_AMOUNT_EXPONENT_BITS, index);

	// RULE tx output asset/amount notes:
	//	published and encrypted (default for amounts, with mask = -1):
	//		enforce = 1
	//		mask = -1 (or lesser bit-width)
	//		xor = pad = hash
	//		enc = value ^ hash
	//	published and not encrypted (default for assets, with asset = 0):
	//		enforce = 1
	//		mask = 0
	//		xor = 0
	//		enc = value
	//	not enforced:
	//		enforce = 0
	//		mask = -1
	//		xor = value
	//		enc = value ^ value = 0

	// RULE tx output: if enforce_asset, then M_asset_enc = #asset ^ #asset_xor
	// RULE tx output:	where #asset_xor = asset_mask & (zkhash(M_encrypt_iv, #dest, #paynum))

	hashin.clear();
	hashin.resize(3);
	hashin[0].SetValue(zk.publics.M_encrypt_iv_bits, TX_ENC_IV_BITS);
	hashin[1].SetValue(zkoutpriv.__dest_bits, TX_FIELD_BITS);
	hashin[2].SetValue(zkoutpriv.__paynum_bits, TX_PAYNUM_BITS);
	ZKVAR one_time_pad = ZKHasher::HashBits(hashin, HASH_BASES_AMOUNT_ENC, TX_ASSET_BITS + TX_AMOUNT_BITS);
	ZKVAR hi_pad, low_pad;
	ZKHasher::extractBits(one_time_pad, TX_ASSET_BITS, &hi_pad, &low_pad);

	low_pad = low_pad * zkoutpub.enforce_asset + (1UL - zkoutpub.enforce_asset) * zkoutpriv.__asset;
	auto low_pad_bits = ZKHasher::extractBits(low_pad, TX_ASSET_BITS);
	check_and(zkoutpriv.__asset_xor_bits, zkoutpub.asset_mask_bits, low_pad_bits, index);
	check_xor(zkoutpub.M_asset_enc_bits, zkoutpriv.__asset_bits, zkoutpriv.__asset_xor_bits, index);

	// RULE tx output: if enforce_amount, then M_amount_enc = #amount ^ #amount_xor
	// RULE tx output:	where #amount_xor = amount_mask & (zkhash(M_encrypt_iv, #dest, #paynum) >> TX_ASSET_BITS)

	hi_pad = hi_pad * zkoutpub.enforce_amount + (1UL - zkoutpub.enforce_amount) * zkoutpriv.__amount_fp;
	auto hi_pad_bits = ZKHasher::extractBits(hi_pad, TX_AMOUNT_BITS);
	check_and(zkoutpriv.__amount_xor_bits, zkoutpub.amount_mask_bits, hi_pad_bits, index);
	check_xor(zkoutpub.M_amount_enc_bits, zkoutpriv.__amount_fp_bits, zkoutpriv.__amount_xor_bits, index);

	#if 0
	cerr << "compute_output amount otp dest " << hex << ZKValue::value(zkoutpriv.__dest) << dec << endl;
	hashin[0].Dump();
	cerr << "compute_output amount otp paynum " << hex << ZKValue::value(zkoutpriv.__paynum) << dec << endl;
	hashin[1].Dump();
	cerr << "compute_output amount otp " << hex << ZKValue::value(one_time_pad) << dec << endl;
	cerr << "compute_output enforce_amount " << hex << ZKValue::value(zkoutpub.enforce_amount) << dec << endl;
	cerr << "compute_output amount hi_pad " << hex << ZKValue::value(hi_pad) << dec << endl;
	cerr << "compute_output amount_mask " << hex << ZKValue::value(zkoutpub.amount_mask) << dec << endl;
	cerr << "compute_output amount_xor " << hex << ZKValue::value(zkoutpriv.__amount_xor) << dec << endl;
	cerr << "compute_output amount " << hex << ZKValue::value(zkoutpriv.__amount_fp) << dec << endl;
	cerr << "compute_output amount_enc " << hex << ZKValue::value(zkoutpub.M_amount_enc) << dec << endl;
	#endif

	// RULE tx output: if lowest X bits of the destination value are all 0, then acceptance_required = 1
	// RULE tx output restatement: (1 - dest_bit[0]) * ... * (1 - dest_bit[X]) * (1 - acceptance_required) = 0

	check = 1UL - zkoutpub.acceptance_required;
	unsigned mask = TX_ACCEPT_REQ_DEST_MASK;
	for (unsigned i = 0; mask != 0; ++i)
	{
		if (mask & 1)
			check = check * (1UL - zkoutpriv.__dest_bits[i]);
		mask >>= 1;
	}
	constrain_zero(check);

	// RULE tx output: if middle Y bits of the destination value are all 0, then paynum = 0
	// RULE tx output restatement: (1 - dest_bit[n]) * ... * (1 - dest_bit[n+Y]) * paynum = 0

	check = zkoutpriv.__paynum;
	mask = TX_STATIC_ADDRESS_MASK;
	for (unsigned i = 0; mask != 0; ++i)
	{
		if (mask & 1)
			check = check * (1UL - zkoutpriv.__dest_bits[i]);
		mask >>= 1;
	}
	constrain_zero(check);

	// RULE tx output: if enforce_address, then M_address = zkhash(#dest, dest_chain, #paynum)

	hashin.clear();
	hashin.resize(3);
	hashin[0].SetValue(zkoutpriv.__dest_bits, TX_FIELD_BITS);
	hashin[1].SetValue(zkoutpub.dest_chain_bits, TX_CHAIN_BITS);
	hashin[2].SetValue(zkoutpriv.__paynum_bits, TX_PAYNUM_BITS);
	check = ZKHasher::HashBits(hashin, HASH_BASES_ADDRESS, TX_ADDRESS_BITS);
	check = check - zkoutpub.M_address;
	check = check * zkoutpub.enforce_address;
	constrain_zero(check);

	// RULE tx output: M_commitment = zkhash(M_commitment_iv, #dest, #paynum, M_domain, #asset, #amount)

	hashin.clear();
	hashin.resize(6);
	hashin[0].SetValue(zk.publics.M_commitment_iv_bits, TX_COMMIT_IV_BITS);
	//hashin[1].nSetValue(zkoutpub.M_commitment_index_bits, TX_COMMIT_INDEX_BITS);
	hashin[1].SetValue(zkoutpriv.__dest_bits, TX_FIELD_BITS);
	hashin[2].SetValue(zkoutpriv.__paynum_bits, TX_PAYNUM_BITS);
	hashin[3].SetValue(zkoutpub.M_domain_bits, TX_DOMAIN_BITS);
	hashin[4].SetValue(zkoutpriv.__asset_bits, TX_ASSET_BITS);
	hashin[5].SetValue(zkoutpriv.__amount_fp_bits, TX_AMOUNT_BITS);
	check = ZKHasher::HashBits(hashin, HASH_BASES_COMMITMENT, TX_FIELD_BITS);
	check = check - zkoutpub.M_commitment;
	check = check * zkoutpub.enforce;
	constrain_zero(check);
}

static void compute_input(const TxPayZK& zk, const TxInZKPub& zkinpub, TxInZKPriv& zkinpriv, unsigned index)
{
	vector<ZKHashInput> hashin(2*TX_MAX_SECRET_SLOTS + 4);

	compute_integer_amount(zkinpriv.__amount_fp_bits, zkinpriv.__amount_int);

	// RULE tx input: sum(@is_asset[0..N]) = 1

	ZKVAR check = 1UL;
	for (unsigned i = 0; i < zk.nassets; ++i)
		check = check - zkinpriv.__is_asset[i];
	constrain_zero(check);

	// RULE tx input: for each i = 0..N, if @is_asset[i], then #asset = @tx_asset[i] where @tx_asset[0] = 0

	for (unsigned i = 0; i < zk.nassets; ++i)
	{
		check = zkinpriv.__asset;
		if (i > 0)
			check = check - zk.privates.__tx_asset[i-1];
		check = check * zkinpriv.__is_asset[i];
		constrain_zero(check, i);
	}

	// RULE tx input: if #asset = 0, then #amount_exponent <= valmax
	// RULE tx input restatement: valmax >= #amount_exponent * enforce_minmax
	// RULE tx input restatement:	where enforce_minmax = (1 - #asset_bit[0]) * (1 - #asset_bit[1]) * ... * (1 - #asset_bit[N])

	ZKVAR enforce_minmax = zkinpub.enforce;
	for (unsigned i = 0; i < TX_ASSET_BITS; ++i)
		enforce_minmax = enforce_minmax * (1UL - zkinpriv.__asset_bits[i]);

	ZKVAR exponent = 0UL;
	for (unsigned i = 0; i < TX_AMOUNT_EXPONENT_BITS; ++i)
		exponent = exponent + ((uint64_t)1 << i) * zkinpriv.__amount_fp_bits[i];

	check_greaterequal(zkinpub.invalmax, exponent * enforce_minmax, TX_AMOUNT_EXPONENT_BITS, index);

	// RULE tx input: if enforce_master_secret, then @master_secret_valid = 1

	check = zkinpub.enforce_master_secret * (1UL - zkinpriv.____master_secret_valid);
	constrain_zero(check);

	// RULE tx input: if @master_secret_valid, then @allow_master_secret = 1

	check = zkinpriv.____master_secret_valid * (1UL - zkinpriv.____allow_master_secret_bit[0]);
	constrain_zero(check);

	// RULE tx input: if enforce_freeze, then @allow_freeze = 1

	check = zkinpub.enforce_freeze * (1UL - zkinpriv.____allow_freeze_bit[0]);
	constrain_zero(check);

	// RULE tx input: if enforce_unfreeze, then @master_secret_valid = 1 or @trust_secrets_valid = 1

	check = zkinpub.enforce_unfreeze * (1UL - zkinpriv.____master_secret_valid) * (1UL - zkinpriv.____trust_secrets_valid);
	constrain_zero(check);

	// RULE tx input: if enforce_unfreeze and @trust_secrets_valid, then @allow_trust_unfreeze = 1

	check = zkinpub.enforce_unfreeze * zkinpriv.____trust_secrets_valid * (1UL - zkinpriv.____allow_trust_unfreeze_bit[0]);
	constrain_zero(check);

	// RULE tx input: if enforce_spend_secrets, then @master_secret_valid = 1 or @spend_secrets_valid = 1

	check = zkinpub.enforce_spend_secrets * (1UL - zkinpriv.____master_secret_valid) * (1UL - zkinpriv.____spend_secrets_valid);
	constrain_zero(check);

	// RULE tx input: if enforce_trust_secrets, then @master_secret_valid = 1 or @spend_secrets_valid = 1 or @trust_secrets_valid = 1

	check = zkinpub.enforce_trust_secrets * (1UL - zkinpriv.____master_secret_valid) * (1UL - zkinpriv.____spend_secrets_valid) * (1UL - zkinpriv.____trust_secrets_valid);
	constrain_zero(check);

	// RULE tx input: if @require_public_hashkey and @spend_secrets_valid, then @secret_valid[1] = 1

	check = zkinpriv.____require_public_hashkey_bit[0] * zkinpriv.____spend_secrets_valid * (1UL - zkinpriv.____secret_valid[1]);
	constrain_zero(check);

	// RULE tx input: if @require_public_hashkey and @spend_secrets_valid, then S-hashkey = @spend_secret[1]

	check = zkinpriv.____require_public_hashkey_bit[0] * zkinpriv.____spend_secrets_valid * (zkinpub.S_hashkey - zkinpriv.____spend_secret[1]);
	constrain_zero(check);

	// RULE tx input: if @spend_secrets_valid and @enforce_spendspec_with_spend_secret, then S_spendspec = @required_spendspec_hash

	ZKVAR spendspec_diff = zkinpub.S_spendspec_hashed - zkinpriv.____required_spendspec_hash;
	check = zkinpriv.____spend_secrets_valid * zkinpriv.____enforce_spendspec_with_spend_secrets_bit[0] * spendspec_diff;
	constrain_zero(check);

	// RULE tx input: if @trust_secrets_valid and @enforce_spendspec_with_trust_secret, then S_spendspec = @required_spendspec_hash

	check = zkinpriv.____trust_secrets_valid * zkinpriv.____enforce_spendspec_with_trust_secrets_bit[0] * spendspec_diff;
	constrain_zero(check);

	// RULE tx input: if @spend_secrets_valid, then sum(@secret_valid[i] * @use_spend_secret[i]) >= @required_spend_secrets

	check = 0UL;
	for (unsigned i = 0; i < zkinpriv.nsecrets; ++i)
		check = check + zkinpriv.____secret_valid[i] * zkinpriv.____use_spend_secret_bits[i];
	check_greaterequal(check, zkinpriv.____required_spend_secrets * zkinpriv.____spend_secrets_valid, TX_MAX_SECRETS_BITS, index);

	#if 0
	cerr << "- compute_input:" << endl;
	for (unsigned i = 0; i < zkinpriv.nsecrets; ++i)
	{
		cerr << "compute_input ____secret_valid " << hex << ZKValue::value(zkinpriv.____secret_valid[i]) << dec << endl;
		cerr << "compute_input ____use_spend_secret_bits " << hex << ZKValue::value(zkinpriv.____use_spend_secret_bits[i]) << dec << endl;
		cerr << "compute_input ____use_trust_secret_bits " << hex << ZKValue::value(zkinpriv.____use_trust_secret_bits[i]) << dec << endl;
	}
	cerr << "compute_input spend_secret sum " << hex << ZKValue::value(check) << dec << endl;
	cerr << "compute_input ____required_spend_secrets " << hex << ZKValue::value(zkinpriv.____required_spend_secrets) << dec << endl;
	cerr << "compute_input ____spend_secrets_valid " << hex << ZKValue::value(zkinpriv.____spend_secrets_valid) << dec << endl;
	#endif

	// RULE tx input: if @trust_secrets_valid, then sum(@secret_valid[i] * @use_trust_secret[i]) >= @required_trust_secrets

	check = 0UL;
	for (unsigned i = 0; i < zkinpriv.nsecrets; ++i)
		check = check + zkinpriv.____secret_valid[i] * zkinpriv.____use_trust_secret_bits[i];
	check_greaterequal(check, zkinpriv.____required_trust_secrets * zkinpriv.____trust_secrets_valid, TX_MAX_SECRETS_BITS, index);

	#if 0
	cerr << "compute_input trust_secret sum " << hex << ZKValue::value(check) << dec << endl;
	cerr << "compute_input ____required_trust_secrets " << hex << ZKValue::value(zkinpriv.____required_trust_secrets) << dec << endl;
	cerr << "compute_input ____trust_secrets_valid " << hex << ZKValue::value(zkinpriv.____trust_secrets_valid) << dec << endl;
	#endif

	// RULE tx input: if @spend_secrets_valid, then param_time >= @spend_locktime

	check_greaterequal(zk.publics.param_time, zkinpriv.____spend_locktime * zkinpriv.____spend_secrets_valid, TX_TIME_BITS, index);

	// RULE tx input: if @trust_secrets_valid, then param_time >= @trust_locktime

	check_greaterequal(zk.publics.param_time, zkinpriv.____trust_locktime * zkinpriv.____trust_secrets_valid, TX_TIME_BITS, index);

	// RULE tx input: if @spend_secrets_valid, then delaytime >= @spend_delaytime

	check_greaterequal(zkinpub.delaytime, zkinpriv.____spend_delaytime * zkinpriv.____spend_secrets_valid, TX_DELAYTIME_BITS, index);

	// RULE tx input: if @trust_secrets_valid, then delaytime >= @trust_delaytime

	check_greaterequal(zkinpub.delaytime, zkinpriv.____trust_delaytime * zkinpriv.____trust_secrets_valid, TX_DELAYTIME_BITS, index);

	// RULE tx input: if @master_secret_valid, then @spend_secret[0] = zkhash(@root_secret, @spend_secret_number)
	// RULE tx input:	where @root_secret = zkhash(@master_secret)

	hashin.clear();
	hashin.resize(1);
	hashin[0].SetValue(zkinpriv.____master_secret_bits, TX_INPUT_BITS);
	ZKVAR root_secret = ZKHasher::HashBits(hashin, HASH_BASES_ROOT_SECRET, TX_FIELD_BITS);
	auto root_secret_bits = ZKHasher::extractBits(root_secret, TX_FIELD_BITS);

	hashin.clear();
	hashin.resize(2);
	hashin[0].SetValue(root_secret_bits, TX_FIELD_BITS);
	hashin[1].SetValue(zkinpriv.____spend_secret_number_bits, TX_SPEND_SECRETNUM_BITS);
	check = ZKHasher::HashBits(hashin, HASH_BASES_SPEND_SECRET, TX_FIELD_BITS);
	check = check - zkinpriv.____spend_secret[0];
	check = check * zkinpriv.____master_secret_valid;
	constrain_zero(check);

	for (unsigned i = 0; i < zkinpriv.nsecrets; ++i)
	{
		// RULE tx input: for each i = 0..N, if (i == 0 and @master_secret_valid) or (@spend_secrets_valid and @secret_valid[i]), then @trust_secret[i] = zkhash(@spend_secret[i])
		// RULE tx input restatement: ((i == 0) * @master_secret_valid + @spend_secrets_valid * @secret_valid[i]) * (@trust_secret[i] - zkhash(@spend_secret[i])) = 0

		hashin.clear();
		hashin.resize(1);
		hashin[0].SetValue(zkinpriv.____spend_secret_bits[i], TX_INPUT_BITS);
		check = ZKHasher::HashBits(hashin, HASH_BASES_TRUST_SECRET, TX_FIELD_BITS);
		check = check - zkinpriv.____trust_secret[i];
		if (i == 0)
			check = check * (zkinpriv.____master_secret_valid + zkinpriv.____spend_secrets_valid * zkinpriv.____secret_valid[i]);
		else
			check = check * (zkinpriv.____spend_secrets_valid * zkinpriv.____secret_valid[i]);
		constrain_zero(check, i);

		// RULE tx input: for each i = 0..N, if (i == 0 and @master_secret_valid) or @secret_valid[i], then @monitor_secret[i] = zkhash(@trust_secret[i])
		// RULE tx input restatement: ((i == 0) * @master_secret_valid + @secret_valid[i]) * (@monitor_secret[i] - zkhash(@trust_secret[i])) = 0

		hashin.clear();
		hashin.resize(1);
		hashin[0].SetValue(zkinpriv.____trust_secret_bits[i], TX_INPUT_BITS);
		check = ZKHasher::HashBits(hashin, HASH_BASES_MONITOR_SECRET, TX_FIELD_BITS);
		check = check - zkinpriv.____monitor_secret[i];
		if (i == 0)
			check = check * (zkinpriv.____master_secret_valid + zkinpriv.____secret_valid[i]);
		else
			check = check * zkinpriv.____secret_valid[i];
		constrain_zero(check, i);
	}

	// RULE tx input: if @enforce_restricted_addresses, then allow_restricted_addresses = 1
	// RULE tx input:	where @enforce_restricted_addresses = @restrict_addresses and not @master_secret_valid and not enforce_freeze

	ZKVAR enforce_restricted_addresses = zkinpriv.____restrict_addresses_bit[0] * (1UL - zkinpriv.____master_secret_valid) * (1UL - zkinpub.enforce_freeze);
	check = enforce_restricted_addresses * (1UL - zk.publics.allow_restricted_addresses);
	constrain_zero(check);

	// RULE tx input: for each outi = 0..zk.nrouts, if @enforce_restricted_addresses and @enforce_restricted_address[outi], then there is at least one addri = 0..zk.nraddrs for which @output_address_matches[outi, addri] = 1
	// RULE tx input:	where @enforce_restricted_addresses = @restrict_addresses and not @master_secret_valid and not enforce_freeze
	// RULE tx input:	and @enforce_restricted_address[outi] = @enforce_restricted_addresses * enforce_address[outi]

	for (unsigned i = 0; i < zk.nout; ++i)
	{
		check = enforce_restricted_addresses * zk.output_public[i].enforce_address;

		for (unsigned j = 0; j < zk.nraddrs && i < zk.nrouts; ++j)
			check = check * (1UL - zkinpriv.____output_address_matches[i][j]);

		constrain_zero(check, i);
	}

	// RULE tx input: for each outi = 0..zk.nrouts and addri = 0..zk.nraddrs, if @output_address_matches[outi, addri], then use_spend_secret[secreti] = 0 and use_trust_secret[secreti] = 0 and output address[outi] = input billet monitor_secret[secreti]

	for (unsigned i = 0; i < zk.nrouts; ++i)
	{
		for (unsigned j = 0; j < zk.nraddrs; ++j)
		{
			auto secreti = restricted_address_secret_index(j);

			//cerr << "outi " << i << " addri " << j << " secreti " << secreti << endl;

			if (secreti < TX_MAX_SECRETS)
			{
				// if @output_address_matches[outi, addri], then use_spend_secret[secreti] = 0
				check = zkinpriv.____output_address_matches[i][j] * zkinpriv.____use_spend_secret_bits[secreti];
				constrain_zero(check, i, j);

				// if @output_address_matches[outi, addri], then use_trust_secret[secreti] = 0
				check = zkinpriv.____output_address_matches[i][j] * zkinpriv.____use_trust_secret_bits[secreti];
				constrain_zero(check, i, j);
			}

			// if @output_address_matches[outi, addri], then output address[outi] = input billet monitor_secret[secreti]
			if (j & 1)
				check = zkinpriv.____output_address_matches[i][j] * (zk.output_public[i].M_address - zkinpriv.____monitor_secret_hi[secreti]);
			else
				check = zkinpriv.____output_address_matches[i][j] * (zk.output_public[i].M_address - zkinpriv.____monitor_secret_lo[secreti]);
			constrain_zero(check, i, j);
		}
	}

	// RULE tx input: M_commitment = zkhash(M_commitment_iv, #dest, #paynum, M_domain, #asset, #amount)
	// RULE tx input:	where #dest = zkhash(@receive_secret, @monitor_secret[1..R], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets, @destnum)
	// RULE tx input:	where @receive_secret = zkhash(@monitor_secret[0], @enforce_spendspec_with_spend_secret, @enforce_spendspec_with_trust_secret, @required_spendspec_hash, @allow_master_secret, @allow_freeze, @allow_trust_unfreeze, @require_public_hashkey, @restrict_addresses, @spend_locktime, @trust_locktime, @spend_delaytime, @trust_delaytime)

	hashin.clear();
	hashin.resize(14);
	hashin[0].SetValue(zkinpriv.____monitor_secret_lo_bits[0], TX_INPUT_BITS/2);
	hashin[1].SetValue(zkinpriv.____monitor_secret_hi_bits[0], TX_INPUT_BITS/2);
	hashin[2].SetValue(zkinpriv.____enforce_spendspec_with_spend_secrets_bit, 1);
	hashin[3].SetValue(zkinpriv.____enforce_spendspec_with_trust_secrets_bit, 1);
	hashin[4].SetValue(zkinpriv.____required_spendspec_hash_bits, TX_INPUT_BITS);
	hashin[5].SetValue(zkinpriv.____allow_master_secret_bit, 1);
	hashin[6].SetValue(zkinpriv.____allow_freeze_bit, 1);
	hashin[7].SetValue(zkinpriv.____allow_trust_unfreeze_bit, 1);
	hashin[8].SetValue(zkinpriv.____require_public_hashkey_bit, 1);
	hashin[9].SetValue(zkinpriv.____restrict_addresses_bit, 1);
	hashin[10].SetValue(zkinpriv.____spend_locktime_bits, TX_TIME_BITS);
	hashin[11].SetValue(zkinpriv.____trust_locktime_bits, TX_TIME_BITS);
	hashin[12].SetValue(zkinpriv.____spend_delaytime_bits, TX_DELAYTIME_BITS);
	hashin[13].SetValue(zkinpriv.____trust_delaytime_bits, TX_DELAYTIME_BITS);
	ZKVAR receive_secret = ZKHasher::HashBits(hashin, HASH_BASES_RECEIVE_SECRET, TX_FIELD_BITS);
	auto receive_secret_bits = ZKHasher::extractBits(receive_secret, TX_FIELD_BITS);

	#if 0
	cerr << "compute_input receive_secret" << endl;
	for (unsigned j = 0; j < hashin.size(); ++j)
	{
		cerr << "hash input " << j << endl;
		hashin[j].Dump();
	}
	cerr << "compute_input receive_secret " << hex << ZKValue::value(receive_secret) << dec << endl;
	#endif

	hashin.clear();
	hashin.resize(2*TX_MAX_SECRET_SLOTS + 4);
	unsigned c = 0;
	hashin[c++].SetValue(receive_secret_bits, TX_FIELD_BITS);
	for (unsigned i = 1; i < TX_MAX_SECRET_SLOTS; ++i)
	{
		hashin[c++].SetValue(zkinpriv.____monitor_secret_lo_bits[i], TX_INPUT_BITS/2);
		hashin[c++].SetValue(zkinpriv.____monitor_secret_hi_bits[i], TX_INPUT_BITS/2);
	}
	hashin[c++].SetValue(zkinpriv.____use_spend_secret_bits, TX_MAX_SECRETS);
	hashin[c++].SetValue(zkinpriv.____use_trust_secret_bits, TX_MAX_SECRETS);
	hashin[c++].SetValue(zkinpriv.____required_spend_secrets_bits, TX_MAX_SECRETS_BITS);
	hashin[c++].SetValue(zkinpriv.____required_trust_secrets_bits, TX_MAX_SECRETS_BITS);
	hashin[c++].SetValue(zkinpriv.____destnum_bits, TX_DESTNUM_BITS);
	CCASSERT(c == 2*TX_MAX_SECRET_SLOTS + 4);
	ZKVAR dest = ZKHasher::HashBits(hashin, HASH_BASES_DESTINATION, TX_FIELD_BITS);
	auto dest_bits = ZKHasher::extractBits(dest, TX_FIELD_BITS);

	#if 0
	cerr << "compute_input destination" << endl;
	for (unsigned j = 0; j < hashin.size(); ++j)
	{
		cerr << "hash input " << j << endl;
		hashin[j].Dump();
	}
	cerr << "compute_input destination " << hex << ZKValue::value(dest) << dec << endl;
	#endif

	hashin.clear();
	hashin.resize(6);
	hashin[0].SetValue(zkinpriv.__M_commitment_iv_bits, TX_COMMIT_IV_BITS);
	//hashin[1].nSetValue(zkinpriv.__M_commitment_index_bits, TX_COMMIT_INDEX_BITS);
	hashin[1].SetValue(dest_bits, TX_FIELD_BITS);
	hashin[2].SetValue(zkinpriv.__paynum_bits, TX_PAYNUM_BITS);
	hashin[3].SetValue(zkinpub.M_domain_bits, TX_DOMAIN_BITS);
	hashin[4].SetValue(zkinpriv.__asset_bits, TX_ASSET_BITS);
	hashin[5].SetValue(zkinpriv.__amount_fp_bits, TX_AMOUNT_BITS);
	check = ZKHasher::HashBits(hashin, HASH_BASES_COMMITMENT, TX_FIELD_BITS);

	#if 0
	cerr << "compute_input __M_commitment" << endl;
	for (unsigned j = 0; j < hashin.size(); ++j)
	{
		cerr << "hash input " << j << endl;
		hashin[j].Dump();
	}
	cerr << "compute_input commitment " << hex << ZKValue::value(check) << dec << endl;
	cerr << "compute_input __M_commitment " << hex << ZKValue::value(zkinpriv.__M_commitment) << dec << endl;
	cerr << "compute_input enforce " << hex << ZKValue::value(zkinpub.enforce) << dec << endl;
	#endif

	check = check - zkinpriv.__M_commitment;
	check = check * zkinpub.enforce;
	constrain_zero(check);

	// RULE tx input: if enforce_public_commitment, then public M-commitment = private M-commitment

	check = zkinpub.M_commitment - zkinpriv.__M_commitment;
	check = check * zkinpub.enforce_public_commitment;
	constrain_zero(check);

	// RULE tx input: if enforce_public_commitnum, then public M-commitnum = private M-commitnum

	check = zkinpub.M_commitnum - zkinpriv.__M_commitnum;
	check = check * zkinpub.enforce_public_commitnum;
	constrain_zero(check);

	// RULE tx input: if enforce_serialnum, then S-serialnum = zkhash(@monitor_secret[0], M-commitment, M-commitnum)

	hashin.clear();
	hashin.resize(4);
	hashin[0].SetValue(zkinpriv.____monitor_secret_lo_bits[0], TX_INPUT_BITS/2);
	hashin[1].SetValue(zkinpriv.____monitor_secret_hi_bits[0], TX_INPUT_BITS/2);
	hashin[2].SetValue(zkinpriv.__M_commitment_bits, TX_FIELD_BITS);
	hashin[3].SetValue(zkinpriv.__M_commitnum_bits, TX_COMMITNUM_BITS);
	check = ZKHasher::HashBits(hashin, HASH_BASES_SERIALNUM, TX_SERIALNUM_BITS);
	check = check - zkinpub.S_serialnum;
	check = check * zkinpub.enforce_serialnum;
	constrain_zero(check);
}

static void check_merkle(const TxInZKPub& zkinpub, const TxInZKPriv& zkinpriv, const TxInPathZK& path, unsigned index)
{
	// RULE tx input: if enforce_path, then enforce Merkle path from M-commitment to merkle_root

	vector<ZKHashInput> hashin(2);
	hashin[0].SetValue(zkinpriv.__M_commitment_bits, TX_FIELD_BITS);
	hashin[1].SetValue(zkinpriv.__M_commitnum_bits, TX_COMMITNUM_BITS);
	ZKVAR hash = ZKHasher::HashBits(hashin, HASH_BASES_MERKLE_LEAF, TX_MERKLE_BITS);

	ZKVAR check = ZKHasher::Merkle(hash, TX_MERKLE_BITS, path.__M_merkle_path, TX_MERKLE_BITS);
	check = check - zkinpub.merkle_root;
	check = check * path.enforce_path;
	constrain_zero(check);
}

static void compute_tx(TxPayZK& zk)
{
	#if 0
	ZKVAR test_check = 1UL;
	for (unsigned i = 0; i < zk.test_var.size(); ++i)
		test_check = test_check + zk.test_var[i];
	unsigned index = 0;
	constrain_zero(test_check);
	#endif

	#if 0 // test hash
	unsigned nbits = TX_INPUT_BITS;
	nbits = 16;
	vector<ZKVAR> type_bits;
	ZKVAR rem, bval;
	type_bits = ZKHasher::extractBits(zk.publics.tx_type, nbits, &rem, &bval);

	vector<ZKHashInput> hashin(1);
	hashin[0].SetValue(type_bits, nbits);
	ZKVAR hash = ZKHasher::HashBits(hashin, 1, TX_INPUT_BITS);

	cerr << "test_hash tx_type " << hex << ZKValue::value(zk.publics.tx_type) << dec << endl;
	cerr << "test_hash bval " << hex << ZKValue::value(bval) << dec << endl;
	cerr << "test_hash rem " << hex << ZKValue::value(rem) << dec << endl;
	cerr << "test_hash hash " << hex << ZKValue::value(hash) << dec << endl;
	return;
	#endif

	for (unsigned i = 0; i < zk.nout; ++i)
	{
		compute_output(zk, zk.output_public[i], zk.output_private[i], i);
	}

	for (unsigned i = 0; i < zk.nin; ++i)
	{
		compute_input(zk, zk.input_public[i], zk.input_private[i], i);

		if (i < zk.nin_with_path)
		{
			//cerr << "checking Merkle path for input " << i << endl;
			check_merkle(zk.input_public[i], zk.input_private[i], zk.inpaths[i], i);
		}
	}

	// RULE tx: for each i, sum_inputs(@is_asset[i] * #amount) = sum_outputs(@is_asset[i] * multiplier * #amount) + (i == 0) * donation

	for (unsigned j = 0; j < zk.nassets; ++j)
	{
		ZKVAR check = 0UL;
		if (j == 0) check = zk.publics.donation;

		for (unsigned i = 0; i < zk.nout; ++i)
			check = check + zk.output_private[i].__is_asset[j] * zk.output_private[i].__amount_int * zk.output_public[i].multiplier;

		for (unsigned i = 0; i < zk.nin; ++i)
			check = check - zk.input_private[i].__is_asset[j] * zk.input_private[i].__amount_int;

		unsigned index = 0;
		constrain_zero(check, j);
	}
}

thread_local static TxPayZK *pzk;

CCPROOF_API CCProof_Free()
{
	// clean up thread local storage

	CCPseudoRandomDeInit();

	TL<R1C<ZKPAIRING::Fr>>::singleton(true);		// delete
	TL<PowersOf2<ZKPAIRING::Fr>>::singleton(true);	// delete

	if (pzk)
	{
		delete pzk;

		pzk = NULL;
	}

	return 0;
}

// returns keyindex
unsigned CCProof_Compute(TxPay& tx, unsigned keyindex = -1, bool verify = false, ostringstream *benchmark_text = NULL)
{
	init_rand_seed(tx.random_seed);

	//cerr << "sizeof(TxPay) " << sizeof(TxPay) << endl;
	//cerr << "sizeof(TxPayZK) " << sizeof(TxPayZK) << endl;

	//auto pzk = unique_ptr<TxPayZK>(new TxPayZK);

	if (!pzk)
	{
		pzk = new TxPayZK;
		CCASSERT(pzk);

		//@cerr << "CCProof_Compute new TxPayZK at " << hex << (uintptr_t)pzk << dec << endl;
	}

	TxPayZK& zk(*pzk);

	//@cerr << "tx nout " << tx.nout << " nin " << tx.nin << " nin_with_path " << tx.nin_with_path << endl;

	zk.nout = tx.nout;
	zk.nin = tx.nin;
	zk.nin_with_path = tx.nin_with_path;

	// retrieve best fitting zk key

	if ((int)keyindex == -1)
		keyindex = keystore.GetKeyIndex(zk.nout, zk.nin, zk.nin_with_path, tx.test_uselargerzkkey);
	else
		keystore.SetTxCounts(keyindex, zk.nout, zk.nin, zk.nin_with_path, verify);

	tx.zkkeyid = keystore.GetKeyId(keyindex);

	zk.nassets = (zk.nout < zk.nin ? zk.nout : zk.nin) + (zk.nout != zk.nin);
	if (!zk.nassets) ++zk.nassets;

	zk.nsecrets = TX_MAX_SECRETS;
	zk.nraddrs = TX_MAX_RESTRICTED_ADDRESSES;
	zk.nrouts = zk.nout;

	//cerr << "zk nout " << zk.nout << " nin " << zk.nin << " nin_with_path " << zk.nin_with_path << " nassets " << zk.nassets << " nsecrets " << zk.nsecrets << " nraddrs " << zk.nraddrs << " nrouts " << zk.nrouts << " keyindex " << keyindex << endl;

	if ((int)keyindex == -1)
	{
		//@lock_guard<mutex> lock(g_cerr_lock);
		//@check_cerr_newline(cerr);
		//@cout << "CCProof_Compute error: no key found" << endl;

		return CCPROOF_ERR_NO_KEY;
	}

	if (zk.nout < tx.nout || zk.nin < tx.nin || zk.nin_with_path < tx.nin_with_path)
	{
		//@lock_guard<mutex> lock(g_cerr_lock);
		//@check_cerr_newline(cerr);
		//@cout << "CCProof_Compute error: insufficient key capacity" << endl;

		return CCPROOF_ERR_INSUFFICIENT_KEY;
	}

	if (tx.no_proof)
		return CCPROOF_ERR_NO_PROOF;

	if (benchmark_text)
	{
		*benchmark_text << zk.nout << " output";
		if (zk.nout != 1)
			*benchmark_text << "s";
		*benchmark_text << " and " << zk.nin << " input";
		if (zk.nin != 1)
			*benchmark_text << "s";
		*benchmark_text << " including " << zk.nin_with_path << " input";
		if (zk.nin_with_path != 1)
			*benchmark_text << "s";
		*benchmark_text << " with Merkle path";
		if (zk.nin_with_path != 1)
			*benchmark_text << "s";
	}

	zk.output_public.resize(zk.nout);
	zk.input_public.resize(zk.nin);
	if (!verify)
	{
		zk.output_private.resize(zk.nout);
		zk.input_private.resize(zk.nin);
		//zk.inpaths.resize(zk.nin_with_path);
	}

	if (0) // for testing -- change to 0 for release
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cout << "CCProof_Compute: verify " << verify << " test_make_bad " << tx.test_make_bad << " tx nout " << tx.nout << " nin " << tx.nin << " nin_with_path " << tx.nin_with_path << " zk nout " << zk.nout << " nin " << zk.nin << " nin_with_path " << zk.nin_with_path << " nassets " << zk.nassets << " nsecrets " << zk.nsecrets << " nraddrs " << zk.nraddrs << " nrouts " << zk.nrouts << " keyindex " << keyindex << endl;
	}

	int badsel = -1;

	//if (!verify && (rand() & 1)) tx.test_make_bad = rand(); // for testing

	if (tx.test_make_bad)
	{
		unsigned nvars = 4 + (zk.nassets-1)*(zk.nassets > 0) + (18 + zk.nassets)*tx.nout + (42 + zk.nassets + 3*zk.nsecrets + 2*TX_MAX_SECRETS + TX_MAX_SECRET_SLOTS + zk.nrouts*zk.nraddrs)*tx.nin + (TX_MERKLE_DEPTH)*tx.nin_with_path;

		badsel = tx.test_make_bad % nvars;

		//badsel = nvars - 1;	// for testing

		cerr << "badsel " << badsel << " nvars " << nvars << endl;
		//tx_dump_stream(cerr, tx);
	}

	reset<ZKPAIRING>();

	// public inputs
	bless_public_inputs(zk, tx, badsel);

	// mark end of public inputs
	end_input<ZKPAIRING>();

	if (verify)
		return keyindex;

	// hidden inputs
	bless_private_inputs(zk, tx, badsel);

	if (tx.test_make_bad)
		cerr << "bless done badsel " << badsel << endl;

	if (badsel >= 0)
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cout << "CCProof test makebad omitting proof" << endl;

		return -1;
	}

	breakout_bits(zk);
	compute_tx(zk);

	return keyindex;
}

#if TEST_SUPPORT_ZK_KEYGEN

CCPROOF_API CCProof_GenKeys()
{
	cerr << "CCProof_GenKeys" << endl;

	keystore.Init(true);

	unique_ptr<TxPay> ptx(new TxPay);
	CCASSERT(ptx);

	TxPay& tx = *ptx;

	for (unsigned i = 0; i < keystore.GetNKeys(); ++i)
	{
		tx.Clear();

		keystore.SetTxCounts(i, tx.nout, tx.nin, tx.nin_with_path);

		//if (tx.nout < tx.nin_with_path)
		//	continue;	// skip these keys

		if (tx.nin > 8 && (tx.nin & 1))
			continue;	// skip these keys

		//if (tx.nin != tx.nin_with_path)
		//	continue;	// skip these keys

		for (unsigned j = 0; j < tx.nin_with_path; ++j)
			tx.inputs[j].pathnum = j + 1;

		//cerr << "Generating keypair " << i << " nout " << tx.nout << " nin_with_path " << tx.nin_with_path << " nin " << tx.nin << endl;
		//continue;

		ostringstream benchmark_text;
		uint32_t t0 = ccticks();

		CCProof_Compute(tx, i, false, &benchmark_text);

		{
			auto key = keypair<ZKPAIRING>();

			auto t1 = ccticks();
			auto elapsed = ccticks_elapsed(t0, t1);
			cerr << "Zero knowledge proof key generated " << benchmark_text.str() << "; keyindex " << i << " elapsed time " << elapsed << " ms" << endl;

			keystore.SaveKeyPair(i, key);
		}

		#ifndef _WIN32
		system("./copy_zkkeys.sh");
		#endif
	}

	cerr << "CCProof_GenKeys done" << endl;

	return 0;
}

#endif // TEST_SUPPORT_ZK_KEYGEN

CCPROOF_API CCProof_PreloadVerifyKeys(bool require_all)
{
	//cerr << "CCProof_PreloadVerifyKeys" << endl;

	keystore.Init();

	try
	{
		keystore.PreLoadVerifyKeys(require_all);
	}
	catch (...)
	{
#if TEST_SUPPORT_ZK_KEYGEN
		CCProof_GenKeys();
#else
		throw runtime_error("Error loading zero knowledge proof verification keys");
#endif
	}

	//cerr << "CCProof_PreloadVerifyKeys done" << endl;

	return 0;
}

//#define USE_TEST_CODE		1	// for testing

#ifndef USE_TEST_CODE
#define USE_TEST_CODE		0
#endif

#if USE_TEST_CODE
static Keypair<ZKPAIRING> testkey;
#endif

CCPROOF_API CCProof_GenProof(TxPay& tx)
{
	//cerr << "CCProof_GenProof" << endl;

#if TEST_SKIP_ZKPROOFS
	CCRandom(&tx.zkproof, sizeof(tx.zkproof));
	memset(&tx.zkproof, 0, 2*64);
	*((char*)&tx.zkproof + 128) |= (1 << (rand() & 7));
	return 0;
#endif

#if USE_TEST_CODE
	{
	const unsigned keyindex = 10;

	keystore.Init(true);
	keystore.SaveKeyPair(keyindex, testkey);	// dummy key file so GetKeyIndex() works

	CCProof_Compute(tx, keyindex, false);

	testkey = keypair<ZKPAIRING>();

	keystore.SaveKeyPair(keyindex, testkey);
	keystore.Init(true);
	}
#endif

	bool valid = false;
	unsigned keyindex = -1;

	uint32_t t0;
	ostringstream benchmark_text;

	if (TEST_SHOW_GEN_BENCHMARKS)
		t0 = ccticks();

	try
	{
		reset<ZKPAIRING>();

		keyindex = CCProof_Compute(tx, (tx.tag_type == CC_TYPE_MINT ? TX_MINT_ZKKEY_ID : -1), false, (TEST_SHOW_GEN_BENCHMARKS ? &benchmark_text : NULL));

		if ((int)keyindex >= 0)
		{
			//CCASSERT(0);

			auto key = keystore.GetProofKey(keyindex);

		#if 0 // USE_TEST_CODE
			key = testkey;
		#endif

			if (!key)
				keyindex = CCPROOF_ERR_LOADING_KEY;
			else
			{
				auto zkproof = proof<ZKPAIRING>(*key);

				Proof2Vec(tx.zkproof, zkproof);

				valid = true;
			}
		}

		reset<ZKPAIRING>();	// free memory
	}
	catch (...)
	{
	}

	if ((int)keyindex < 0)
		return keyindex;

	if (!valid)
		return -1;

	if (TEST_SHOW_GEN_BENCHMARKS)
	{
		auto t1 = ccticks();
		auto elapsed = ccticks_elapsed(t0, t1);
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cout << "Zero knowledge proof generated: " << benchmark_text.str() << "; keyindex " << keyindex << " elapsed time " << elapsed << " ms" << endl;
	}

	//for (unsigned i = 0; i < tx.zkproof.size(); ++i)
	//	cerr << "zkproof[" << i << "] " << tx.zkproof[i] << endl;

	return 0;
}

CCPROOF_API CCProof_VerifyProof(TxPay& tx)
{
	if (0) // for testing -- change to 0 for release
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cout << "CCProof_VerifyProof" << endl;
		tx_dump_stream(cout, tx);
	}

#if TEST_SKIP_ZKPROOFS
	// the "proof" is valid if the first 64 bytes appears twice and byte 128 is non-zero
	if (memcmp(&tx.zkproof, (char*)&tx.zkproof + 64, 64))
		return -1;
	if (*((char*)&tx.zkproof + 128))
		return 0;
	return -1;
#endif

	bool valid = false;
	unsigned keyindex = -1;

	uint32_t t0;
	ostringstream benchmark_text;

	if (TEST_SHOW_VERIFY_BENCHMARKS)
		t0 = ccticks();

	try
	{
		reset<ZKPAIRING>();

		keyindex = CCProof_Compute(tx, tx.zkkeyid, true, (TEST_SHOW_VERIFY_BENCHMARKS ? &benchmark_text : NULL));

		if ((int)keyindex >= 0)
		{
			auto witness = input<ZKPAIRING>();

			//auto t0 = ccticks();

			Proof<ZKPAIRING> zkproof;
			Vec2Proof(tx.zkproof, zkproof);

			#if 0 // USE_TEST_CODE
			auto pvk = new ZKKeyStore::VerifyKey(testkey.vk());
			auto key = *pvk;
			#else
			auto key = keystore.GetVerifyKey(tx.zkkeyid);
			#endif

			if (!key)
				keyindex = CCPROOF_ERR_LOADING_KEY;
			else
				valid = snarklib::strongVerify(*key, *witness, zkproof);
		}

		reset<ZKPAIRING>();	// free memory
	}
	catch (...)
	{
	}

	if ((int)keyindex < 0)
		return keyindex;

	if (TEST_SHOW_VERIFY_BENCHMARKS)
	{
		auto t1 = ccticks();
		auto elapsed = ccticks_elapsed(t0, t1);
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cout << "Zero knowledge proof " << (valid ? "verified:  " : "INVALID: ") << benchmark_text.str() << "; keyindex " << keyindex << " elapsed time " << elapsed << " ms" << endl;
	}

	return (valid ? 0 : -1);
}
