/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCproof.cpp
*/

#include "CCdef.h"

#include "CChash.hpp"
#include "CCproof.h"
#include "CCproof.hpp"
#include "transaction.hpp"
#include "transaction.h"
#include "zkkeys.hpp"
#include "CCbigint.hpp"
#include "CompressProof.hpp"

#include <CCticks.hpp>
#include <CCutil.h>

//#define TEST_SKIP_ZKPROOFS		1	// for testing

#ifdef CC_DLL_EXPORTS
#define TEST_SHOW_GEN_BENCHMARKS	1	// show benchmarks in DLL build
#define TEST_SHOW_VERIFY_BENCHMARKS	1	// show benchmarks in DLL build
#endif

#ifndef TEST_SKIP_ZKPROOFS
#define TEST_SKIP_ZKPROOFS			0	// don't skip
#endif

#ifndef TEST_SHOW_GEN_BENCHMARKS
#define TEST_SHOW_GEN_BENCHMARKS	0	// don't show
#endif

#ifndef TEST_SHOW_VERIFY_BENCHMARKS
#define TEST_SHOW_VERIFY_BENCHMARKS	0	// don't show
#endif

Hasher::HashBases hashbases;

typedef bigint_x<BN128_FR> ZKVAR;
//typedef field_x<BN128_FR> ZKVAR;
typedef Hasher::zk::HashInput<ZKPAIRING> ZKHashInput;
typedef Hasher::zk::Hasher<ZKPAIRING> ZKHasher;
typedef Hasher::BitConstraints<ZKPAIRING,ZKVAR,ZKVAR> ZKConstraints;
typedef Hasher::ASTValue<ZKVAR, bigint_t> ZKValue;

template <typename PAIRING, typename T>
void constrain_zero(const AST_Var<T>& x) {
    TL<R1C<typename PAIRING::Fr>>::singleton()->setFalse(x->r1Terms()[0]);
}

static ZKKeyStore keystore;

CCPROOF_API CCProof_Init()
{
	static bool binit = false;

	//cerr << "CCProof_Init binit " << binit << endl;

	if (!binit)
	{
		//cerr << "CCProof_Init" << endl;

		srand(time(NULL));
		init_BN128();
		ZKHasher::Init();
		binit = true;

		//for (unsigned i = 0; i < 20; ++i)	// for testing
		//	cerr << hashbases.bigint(i) << " " << hashbases[i] << " " << BN128_FR(hashbases.bigint(i)) << endl;

		//keystore.Init();				// for testing
		//keystore.PreLoadVerifyKeys();	// for testing
		//keystore.PreLoadProofKeys();	// for testing
	}

#if 0 // test the clock
	for (unsigned i = 0; i < 20; ++i)
	{
		auto t0 = ccticks();
		auto t1 = t0;
		while (t1 == t0)
			t1 = ccticks();
		cerr << "ccticks " << t1 - t0 << endl;
	}
#endif

	return 0;
}

struct TxOutZK
{
	unsigned enforce_index;

	struct
	{
		ZKVAR M_address;		// bigint_t
		ZKVAR M_value_enc;		// uint64_t
		ZKVAR M_commitment;		// bigint_t

		vector<ZKVAR> M_address_bits;

	} publics;

	struct
	{
		ZKVAR __dest;			// bigint_t
		ZKVAR __paynum;			// bigint_t
		ZKVAR __value;			// uint64_t

		vector<ZKVAR> __dest_bits;
		vector<ZKVAR> __paynum_bits;
		vector<ZKVAR> __value_bits;

	} privates;

};

struct TxInZK
{
	unsigned enforce_index;

	struct
	{
		ZKVAR M_commitment;			// bigint_t

		ZKVAR S_serialnum;			// bigint_t
		ZKVAR S_spendspec_hashed;	// bigint_t

	} publics;

	struct
	{
		ZKVAR __paynum;				// bigint_t
		ZKVAR __value;				// uint64_t
		ZKVAR M_commitment_iv;		// uint64_t

		ZKVAR M_commitment;								// bigint_t

		ZKVAR ____spend_secret;							// bigint_t
		vector<ZKVAR> ____enforce_spendspec_hash_bit;	// bool

		vector<ZKVAR> __paynum_bits;
		vector<ZKVAR> __value_bits;
		vector<ZKVAR> M_commitment_iv_bits;

		vector<ZKVAR> M_commitment_bits;
		vector<ZKVAR> ____spend_secret_bits;

	} privates;
};

struct TxInPathZK
{
	struct
	{
		ZKVAR ____merkle_leafindex;		// uint64_t
		vector<ZKVAR> ____merkle_path;	// bigint_t

	} privates;
};

struct TxPayZK
{
	uint16_t nout;
	uint16_t nin;
	uint16_t nin_with_path;

	struct
	{
		ZKVAR merkle_root;		// bigint_t
		ZKVAR donation;			// int64_t
		ZKVAR outvalmin;		// uint64_t
		ZKVAR outvalmax;		// uint64_t
		ZKVAR invalmax;			// uint64_t
		ZKVAR encrypt_values;	// bool
		ZKVAR nonfinancial;		// bool
		ZKVAR enforce_flags[TX_MAXOUT + TX_MAXIN + TX_MAXINPATH];

		vector<ZKVAR> M_commitment_iv_bits;

	} publics;

	array<TxOutZK, TX_MAXOUT> output;
	array<TxInZK, TX_MAXIN> input;
	vector<TxInPathZK> inpath;
	array<unsigned, TX_MAXINPATH> inpath_enforce_index;

};

static void bless_input(ZKVAR& var, const bigint_t &val, int& makebad, const char *input, unsigned nbits, const bigint_t *pbadval = NULL)
{
	//cerr << "bless_input makebad " << makebad << endl;

	if (--makebad == 0)
	{
		bigint_t badval = val;
		while (!pbadval)
		{
			pbadval = &badval;
			if (nbits < 256)
				++nbits;						// also try flipping a bit outside the input's field width
			unsigned bit = rand() % nbits;
			//bit = nbits - 1;	// for testing
			BIGWORD(badval, bit/64) ^= (uint64_t)1 << (bit % 64);
		}

		{
			lock_guard<FastSpinLock> lock(g_cout_lock);
			cout << hex << "CCProof test makebad changed tx " << input << " from " << val << " to " << *pbadval << dec << endl;
		}

		if (val == *pbadval)
			*(int*)0 = 0;		// don't use CCASSERT because the exception handler would just cause the proof to fail, mimicing what is supposed to happen

		bless(var, *pbadval);
	}
	else
		bless(var, val);
}

static void bless_tx_public_inputs(TxPayZK& zk, const struct TxPay& tx, int& makebad)
{
	//cerr << "bless_tx_public_inputs makebad " << makebad << endl;

	bless_input(zk.publics.merkle_root, tx.merkle_root, makebad, "merkle_root", TX_INPUT_BITS);								// tx

	bigint_t donation;
	if (tx.donation >= 0)
		donation = bigint_t((uint64_t)tx.donation);
	else
		donation = donation - bigint_t((uint64_t)(-tx.donation));

	bool changebad = false;
	if (makebad == 2)
	{
		changebad = true;
		for (unsigned i = 0; i < tx.nout; ++i)
		{
			if (tx.output[i].__value != (uint64_t)(-1))
				changebad = false;
		}
	}
	if (makebad == 3)
	{
		changebad = true;
		for (unsigned i = 0; i < tx.nout; ++i)
		{
			if (tx.output[i].__value)
				changebad = false;
		}
	}
	if (makebad == 4)
	{
		changebad = true;
		for (unsigned i = 0; i < tx.nin; ++i)
		{
			if (tx.input[i].__value)
				changebad = false;
		}
	}
	if (changebad)
		makebad = 1;	// can't make outvalmin, outvalmax or invalmax bad cause no outputs or inputs, or all outputs or inputs are zero or max'ed out

	bless_input(zk.publics.donation, donation, makebad, "donation", TX_VALUE_BITS);											// tx

	bigint_t badval = (uint64_t)(-1);
	bless_input(zk.publics.outvalmin, tx.outvalmin, makebad, "outvalmin", TX_VALUE_BITS, &badval);							// tx

	badval = 0UL;
	bless_input(zk.publics.outvalmax, tx.outvalmax, makebad, "outvalmax", TX_VALUE_BITS, &badval);							// tx
	bless_input(zk.publics.invalmax, tx.invalmax, makebad, "invalmax", TX_VALUE_BITS, &badval);								// tx

	bigint_t encrypt_values = 1UL - tx.outvals_public;
	bless_input(zk.publics.encrypt_values, encrypt_values, makebad, "outvals_public", 1);									// tx
	bless_input(zk.publics.nonfinancial, tx.nonfinancial, makebad, "nonfinancial", 1);										// tx
}

static void bless_output_public_inputs(TxOutZK& zk, const struct TxOut& tx, int& makebad)
{
	//cerr << "bless_output_public_inputs makebad " << makebad << endl;

	bless_input(zk.publics.M_address, tx.M_address, makebad, "output address", TX_INPUT_BITS);								// output
	bless_input(zk.publics.M_value_enc, tx.M_value_enc, makebad, "output value_enc", TX_VALUE_BITS);						// output
	bless_input(zk.publics.M_commitment, tx.M_commitment, makebad, "output commitment", TX_INPUT_BITS);						// output
}

static void bless_input_public_inputs(TxInZK& zk, const struct TxIn& tx, bool has_path, int& makebad)
{
	//cerr << "bless_input_public_inputs makebad " << makebad << endl;

	if (has_path)
	{
		if (makebad == 1)
			makebad = 2;		// publics.M_commitment won't be enforced, so don't try to make it bad

		bless_input(zk.publics.M_commitment, 0UL, makebad, "ERROR", TX_INPUT_BITS);											// input if has_path
	}
	else
		bless_input(zk.publics.M_commitment, tx.__M_commitment, makebad, "input commitment", TX_INPUT_BITS);				// input if not has_path

	if (makebad == 2 && !tx.____spendsecrets.____enforce_spendspec_hash)
		makebad = 1;	// S_spendspec_hashed not enforced, so make S_serialnum bad instead

	bless_input(zk.publics.S_serialnum, tx.S_serialnum, makebad, "input serialnum", TX_INPUT_BITS);							// input
	bless_input(zk.publics.S_spendspec_hashed, tx.S_spendspec_hashed, makebad, "input spendspec_hashed", TX_INPUT_BITS);	// input
}

// must be called before blessing private inputs
static void bless_public_inputs(TxPayZK& zk, struct TxPay& tx, int& makebad)
{
	bless_tx_public_inputs(zk, tx, makebad);

	unsigned enforce_index = 0;

	for (unsigned i = 0; i < zk.nout; ++i)
	{
		zk.output[i].enforce_index = enforce_index;

		if (i < tx.nout)
		{
			//cerr << "output " << i << " enforce_index " << enforce_index << " enforce 1" << endl;
			bless(zk.publics.enforce_flags[enforce_index++], 1UL);

			bless_output_public_inputs(zk.output[i], tx.output[i], makebad);
		}
		else
		{
			//cerr << "output " << i << " enforce_index " << enforce_index << " enforce 0" << endl;
			bless(zk.publics.enforce_flags[enforce_index++], 0UL);

			int notbad = 0;
			bless_output_public_inputs(zk.output[i], tx.output[i], notbad);
		}
	}

	CCASSERT(enforce_index == zk.nout);

	//cerr << "tx.nin " << tx.nin << " tx.nin_with_path " << tx.nin_with_path << " zk.nin " << zk.nin << endl;

	unsigned zkindex = 0;

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		// tx inputs with merkle paths come first
		unsigned pathnum = tx.input[i].pathnum;
		if (!pathnum)
			continue;

		tx.input[i].zkindex = zkindex;

		//cerr << "tx input " << i << " pathnum " << pathnum << " maps to zk input " << zkindex << endl;

		zk.input[zkindex].enforce_index = enforce_index;
		//cerr << "input " << zkindex << " enforce_index " << enforce_index << " enforce 1" << endl;
		bless(zk.publics.enforce_flags[enforce_index++], 1UL);

		bless_input_public_inputs(zk.input[zkindex], tx.input[i], true, makebad);

		zk.inpath_enforce_index[zkindex] = enforce_index;
		//cerr << "path " << zkindex << " enforce_index " << enforce_index << " enforce 1" << endl;
		bless(zk.publics.enforce_flags[enforce_index++], 1UL);

		++zkindex;
	}

	CCASSERT(zkindex == tx.nin_with_path);
	CCASSERT(enforce_index == zk.nout + tx.nin_with_path*2U);

	for (unsigned i = 0; i < zk.nin; ++i)
	{
		unsigned pathnum = tx.input[i].pathnum;
		if (pathnum)
			continue;

		tx.input[i].zkindex = zkindex;

		//cerr << "tx input " << i << " nopath maps to zk input " << zkindex << " tx.nin " << tx.nin << endl;

		zk.input[zkindex].enforce_index = enforce_index;

		if (zkindex < tx.nin)
		{
			//cerr << "input " << zkindex << " enforce_index " << enforce_index << " enforce 1" << endl;
			bless(zk.publics.enforce_flags[enforce_index++], 1UL);

			bless_input_public_inputs(zk.input[zkindex], tx.input[i], false, makebad);
		}
		else
		{
			//cerr << "input " << zkindex << " enforce_index " << enforce_index << " enforce 0" << endl;
			bless(zk.publics.enforce_flags[enforce_index++], 0UL);

			int notbad = 0;
			bless_input_public_inputs(zk.input[zkindex], tx.input[i], false, notbad);
		}

		if (zkindex < zk.nin_with_path)
		{
			zk.inpath_enforce_index[zkindex] = enforce_index;
			//cerr << "path " << zkindex << " enforce_index " << enforce_index << " enforce 0" << endl;
			bless(zk.publics.enforce_flags[enforce_index++], 0UL);
		}

		++zkindex;
	}

	CCASSERT(zkindex == zk.nin);
	CCASSERT(enforce_index == (unsigned)zk.nout + zk.nin + zk.nin_with_path);
}

static void bless_output_private_inputs(TxOutZK& zk, const struct TxOut& tx, int& makebad)
{
	//cerr << "bless_output_private_inputs makebad " << makebad << endl;

	bless_input(zk.privates.__dest, tx.__dest, makebad, "output dest", TX_INPUT_BITS);										// output
	bless_input(zk.privates.__paynum, tx.__paynum, makebad, "output paynum", TX_PAYNUM_BITS);								// output
	bless_input(zk.privates.__value, tx.__value, makebad, "output value", TX_VALUE_BITS);									// output
}

static void bless_input_private_inputs(TxInZK& zk, const struct TxIn& tx, int& makebad)
{
	//cerr << "bless_input_private_inputs makebad " << makebad << endl;

	bless_input(zk.privates.____spend_secret, tx.____spendsecrets.____spend_secret, makebad, "input spend_secret", TX_INPUT_BITS);	// input

	zk.privates.____enforce_spendspec_hash_bit.resize(1);
	bless_input(zk.privates.____enforce_spendspec_hash_bit[0], tx.____spendsecrets.____enforce_spendspec_hash, makebad, "input enforce_spendspec_hash", 1);	// input

	bless_input(zk.privates.__paynum, tx.__paynum, makebad, "input paynum", TX_PAYNUM_BITS);								// input
	bless_input(zk.privates.__value, tx.__value, makebad, "input value", TX_VALUE_BITS);									// input
	bless_input(zk.privates.M_commitment_iv, tx.__M_commitment_iv, makebad, "input commitment_iv", TX_COMMIT_IV_BITS);		// input

	bless_input(zk.privates.M_commitment, tx.__M_commitment, makebad, "input commitment", TX_INPUT_BITS);					// input
}

static void bless_private_inputs(TxPayZK& zk, const struct TxPay& tx, int& makebad)
{
	for (unsigned i = 0; i < zk.nout; ++i)
	{
		if (i < tx.nout)
			bless_output_private_inputs(zk.output[i], tx.output[i], makebad);
		else
		{
			int notbad = 0;
			bless_output_private_inputs(zk.output[i], tx.output[i], notbad);
		}
	}

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		// tx inputs with merkle paths come first
		unsigned pathnum = tx.input[i].pathnum;
		if (!pathnum)
			continue;

		unsigned zkindex = tx.input[i].zkindex;

		//cerr << "tx input " << i << " pathnum " << pathnum << " mapped to zk input " << zkindex << endl;

		bless_input_private_inputs(zk.input[zkindex], tx.input[i], makebad);

		//cerr << "bless_private_inputs merkle makebad " << makebad << endl;

		bless_input(zk.inpath[zkindex].privates.____merkle_leafindex, tx.inpath[pathnum-1].____merkle_leafindex, makebad, "input commitnum", TX_MERKLE_LEAFINDEX_BITS);	// input if has_path

		zk.inpath[zkindex].privates.____merkle_path.resize(TX_MERKLE_DEPTH);
		for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
			bless_input(zk.inpath[zkindex].privates.____merkle_path[j], tx.inpath[pathnum-1].____merkle_path[j], makebad, "input merkle_path", TX_INPUT_BITS);			// 48 inputs if has_path
	}

	for (unsigned i = 0; i < zk.nin; ++i)
	{
		unsigned pathnum = tx.input[i].pathnum;
		if (pathnum)
			continue;

		unsigned zkindex = tx.input[i].zkindex;

		//cerr << "tx input " << i << " nopath mapped to zk input " << zkindex << endl;

		if (zkindex < tx.nin)
			bless_input_private_inputs(zk.input[zkindex], tx.input[i], makebad);
		else
		{
			int notbad = 0;
			bless_input_private_inputs(zk.input[zkindex], tx.input[i], notbad);
		}

		if (zkindex < zk.nin_with_path)
		{
			// this input does not include a path, but this proof setup requires these path variables to be blessed, so use dummy values

			int notbad = 0;

			bless_input(zk.inpath[zkindex].privates.____merkle_leafindex, tx.inpath[0].____merkle_leafindex, notbad, "ERROR", TX_INPUT_BITS);		// dummy

			zk.inpath[zkindex].privates.____merkle_path.resize(TX_MERKLE_DEPTH);
			for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
				bless_input(zk.inpath[zkindex].privates.____merkle_path[j], tx.inpath[0].____merkle_path[j], notbad, "ERROR", TX_INPUT_BITS);		// dummy
		}
	}
}

static void breakout_bits(TxPayZK& zk)
{
	ZKVAR rem;

	zk.publics.M_commitment_iv_bits = ZKHasher::extractBits(zk.publics.merkle_root, TX_COMMIT_IV_BITS, &rem);

	ZKConstraints::addBooleanity(zk.publics.encrypt_values);		// already just one bit, so we only need to enforce booleanity

	for (unsigned i = 0; i < zk.nout; ++i)
	{
		zk.output[i].publics.M_address_bits = ZKHasher::extractBits(zk.output[i].publics.M_address, TX_FIELD_BITS);

		zk.output[i].privates.__dest_bits = ZKHasher::extractBits(zk.output[i].privates.__dest, TX_FIELD_BITS);
		zk.output[i].privates.__paynum_bits = ZKHasher::extractBits(zk.output[i].privates.__paynum, TX_PAYNUM_BITS);
		zk.output[i].privates.__value_bits = ZKHasher::extractBits(zk.output[i].privates.__value, TX_VALUE_BITS);
	}

	for (unsigned i = 0; i < zk.nin; ++i)
	{
		zk.input[i].privates.__paynum_bits = ZKHasher::extractBits(zk.input[i].privates.__paynum, TX_PAYNUM_BITS);
		zk.input[i].privates.__value_bits = ZKHasher::extractBits(zk.input[i].privates.__value, TX_VALUE_BITS);
		zk.input[i].privates.M_commitment_iv_bits = ZKHasher::extractBits(zk.input[i].privates.M_commitment_iv, TX_COMMIT_IV_BITS);
		zk.input[i].privates.M_commitment_bits = ZKHasher::extractBits(zk.input[i].privates.M_commitment, TX_FIELD_BITS);
		zk.input[i].privates.____spend_secret_bits = ZKHasher::extractBits(zk.input[i].privates.____spend_secret, TX_FIELD_BITS);

		ZKConstraints::addBooleanity(zk.input[i].privates.____enforce_spendspec_hash_bit[0]);	// already just one bit, so we only need to enforce booleanity
	}
}

// ensures a >= b for the field width nbits
// caution: in order for this to work, both a and b must not exceed the field width nbits
//	if either a or b is a hidden (non-public) input, this must be enforced elsewhere by
//	decomposing the hidden input into bits and checking the remainder
static void check_greaterequal(const ZKVAR& a, const ZKVAR& b, unsigned nbits)
{
	// compute a-b and then check the carry bit

	ZKVAR diff = a - b;
	ZKVAR rem;

	auto diff_bits = ZKHasher::extractBits(diff, nbits + 1, &rem);

	constrain_zero<ZKPAIRING>(diff_bits[nbits]);
}

// ensures all bits in (a & b) are clear (bitwise, for all bits)
#if 0 // not used
static void check_mask(const vector<ZKVAR>& a, const vector<ZKVAR>& b)
{
	//cerr << "check_mask " << a.size() << " " << b.size() << endl;

	CCASSERT(a.size() == b.size());

	CCASSERT(a.size());

	ZKVAR x;

	for (unsigned i = 0; i < a.size(); ++i)
	{
		if (i == 0)
			x = a[i] * b[i];
		else
			x = x + a[i] * b[i];
	}

	constrain_zero<ZKPAIRING>(x);
}
#endif

// ensures val = a ^ b (bitwise, for all bits)
static void check_xor(const ZKVAR& val, const vector<ZKVAR>& a, const vector<ZKVAR>& b)
{
	// XOR = a+b-2*a*b

	CCASSERT(a.size() == b.size());

	vector<ZKVAR> x(a.size());

	for (unsigned i = 0; i < a.size(); ++i)
		x[i] = a[i] + b[i] - a[i] * b[i] * bigint_t(2UL);

	ZKConstraints::constrainValue(val, x);
}

static void compute_output(const TxOutZK& zk, const ZKVAR& valmin, const ZKVAR& valmax, const vector<ZKVAR>& commitment_iv_bits, const ZKVAR *enforce, const ZKVAR& encrypt_value)
{
	vector<ZKHashInput> hashin(HASH_MAX_INPUTS);

	// RULE tx output: value >= valmin

	check_greaterequal(zk.privates.__value, valmin * (*enforce), TX_VALUE_BITS);

	// RULE tx output: valmax >= value

	check_greaterequal(valmax, zk.privates.__value * (*enforce), TX_VALUE_BITS);

	// RULE tx output: M-value_enc = #value ^ zkhash(#dest, #paynum)

	hashin.clear();
	hashin.resize(2);
	hashin[0].SetValue(zk.privates.__dest_bits, TX_FIELD_BITS);
	hashin[1].SetValue(zk.privates.__paynum_bits, TX_PAYNUM_BITS);

	ZKVAR one_time_pad = ZKHasher::HashBits(hashin, HASH_BASES_VALUEENC, TX_VALUE_BITS);
	one_time_pad = one_time_pad * encrypt_value;

	ZKVAR rem;
	auto one_time_pad_bits = ZKHasher::extractBits(one_time_pad, TX_VALUE_BITS, &rem);
	// note: if *enforce is 0, then M_value_enc and __value_bits must both be zero:
	check_xor(zk.publics.M_value_enc, zk.privates.__value_bits, one_time_pad_bits);

	#if 0
	cerr << "compute_output dest " << hex << ZKValue::value(zk.privates.__dest) << dec << endl;
	hashin[0].Dump();
	cerr << "compute_output paynum " << hex << ZKValue::value(zk.privates.__paynum) << dec << endl;
	hashin[1].Dump();
	cerr << "compute_output otp " << hex << ZKValue::value(one_time_pad) << dec << endl;
	cerr << "compute_output value " << hex << ZKValue::value(zk.privates.__value) << dec << endl;
	cerr << "compute_output value_enc " << hex << ZKValue::value(zk.publics.M_value_enc) << dec << endl;
	#endif

	// RULE tx output: M-address = zkhash(#dest, #paynum)

	hashin.clear();
	hashin.resize(2);
	hashin[0].SetValue(zk.privates.__dest_bits, TX_FIELD_BITS);
	hashin[1].SetValue(zk.privates.__paynum_bits, TX_PAYNUM_BITS);

	ZKVAR check = ZKHasher::HashBits(hashin, HASH_BASES_ADDRESS, TX_FIELD_BITS);
	check = check - zk.publics.M_address;
	if (enforce)
		check = check * (*enforce);
	constrain_zero<ZKPAIRING>(check);

	// RULE tx output: M-commitment = zkhash(#dest, #paynum, #value, M-commitment_iv)

	hashin.clear();
	hashin.resize(4);
	hashin[0].SetValue(zk.privates.__dest_bits, TX_FIELD_BITS);
	hashin[1].SetValue(zk.privates.__paynum_bits, TX_PAYNUM_BITS);
	hashin[2].SetValue(zk.privates.__value_bits, TX_VALUE_BITS);
	hashin[3].SetValue(commitment_iv_bits, TX_COMMIT_IV_BITS);

	check = ZKHasher::HashBits(hashin, HASH_BASES_COMMITMENT, TX_FIELD_BITS);
	check = check - zk.publics.M_commitment;
	if (enforce)
		check = check * (*enforce);
	constrain_zero<ZKPAIRING>(check);
}

static void compute_input(const TxInZK& zk, const ZKVAR& valmax, const ZKVAR *enforce, const ZKVAR *enforce_path)
{
	vector<ZKHashInput> hashin(HASH_MAX_INPUTS);

	// RULE tx output: valmax >= value

	check_greaterequal(valmax, zk.privates.__value * (*enforce), TX_VALUE_BITS);

	// RULE tx input: M-commitment = zkhash(#dest, #paynum, #value, M-commitment_iv)
	// RULE tx input:   where #dest = zkhash(@enforce_spendspec_hash, @enforce_spendspec_hash*stdhash(S-spendspec), zkhash(@secret))

	hashin.clear();
	hashin.resize(1);
	hashin[0].SetValue(zk.privates.____spend_secret_bits, TX_FIELD_BITS);
	ZKVAR secret_hashed = ZKHasher::HashBits(hashin, HASH_BASES_RECEIVE_SECRET, TX_FIELD_BITS);
	auto secret_hashed_bits = ZKHasher::extractBits(secret_hashed, TX_FIELD_BITS);

	auto spendspec_prod = zk.privates.____enforce_spendspec_hash_bit[0] * zk.publics.S_spendspec_hashed;
	auto spendspec_prod_bits = ZKHasher::extractBits(spendspec_prod, TX_FIELD_BITS);

	hashin.clear();
	hashin.resize(3);
	hashin[0].SetValue(zk.privates.____enforce_spendspec_hash_bit, 1);
	hashin[1].SetValue(spendspec_prod_bits, TX_FIELD_BITS);
	hashin[2].SetValue(secret_hashed_bits, TX_FIELD_BITS);
	ZKVAR dest = ZKHasher::HashBits(hashin, HASH_BASES_DESTINATION, TX_FIELD_BITS);
	auto dest_bits = ZKHasher::extractBits(dest, TX_FIELD_BITS);

	hashin.clear();
	hashin.resize(4);
	hashin[0].SetValue(dest_bits, TX_FIELD_BITS);
	hashin[1].SetValue(zk.privates.__paynum_bits, TX_PAYNUM_BITS);
	hashin[2].SetValue(zk.privates.__value_bits, TX_VALUE_BITS);
	hashin[3].SetValue(zk.privates.M_commitment_iv_bits, TX_COMMIT_IV_BITS);
	ZKVAR check = ZKHasher::HashBits(hashin, HASH_BASES_COMMITMENT, TX_FIELD_BITS);
	check = check - zk.privates.M_commitment;
	if (enforce)
		check = check * (*enforce);
	constrain_zero<ZKPAIRING>(check);

	// RULE tx input: if not checking merkle path (enforce_path == NULL or *enforce_path == 0), then public M-commitment = private M-commitment

	check = zk.publics.M_commitment - zk.privates.M_commitment;
	if (enforce)
		check = check * (*enforce);
	if (enforce_path)
		check = check * (bigint_t(1UL) - (*enforce_path));
	constrain_zero<ZKPAIRING>(check);

	// RULE tx input: S-serialnum = zkhash(M-commitment, @secret)

	hashin.clear();
	hashin.resize(2);
	hashin[0].SetValue(zk.privates.M_commitment_bits, TX_FIELD_BITS);
	hashin[1].SetValue(zk.privates.____spend_secret_bits, TX_FIELD_BITS);
	check = ZKHasher::HashBits(hashin, HASH_BASES_SERIALNUM, TX_FIELD_BITS);
	check = check - zk.publics.S_serialnum;
	if (enforce)
		check = check * (*enforce);
	constrain_zero<ZKPAIRING>(check);
}

static void check_merkle(const TxInZK& zk, const TxInPathZK& path, const ZKVAR& merkle_root, const ZKVAR *enforce)
{
	vector<ZKHashInput> hashin(HASH_MAX_INPUTS);

	// RULE tx input: enforce merkle path

	auto leafindex_bits = ZKHasher::extractBits(path.privates.____merkle_leafindex, TX_MERKLE_LEAFINDEX_BITS);

	hashin.clear();
	hashin.resize(2);
	hashin[0].SetValue(zk.privates.M_commitment_bits, TX_FIELD_BITS);
	hashin[1].SetValue(leafindex_bits, TX_MERKLE_LEAFINDEX_BITS);

	//cerr << "check_merkle M_commitment_bits " << zk.privates.M_commitment_bits.size() << " leafindex_bits " << leafindex_bits.size() << " merkle_path " << path.privates.____merkle_path.size() << endl;

	ZKVAR hash = ZKHasher::HashBits(hashin, HASH_BASES_MERKLE_LEAF, TX_MERKLE_PATH_BITS);

	ZKVAR check = ZKHasher::Merkle(hash, TX_MERKLE_PATH_BITS, path.privates.____merkle_path, TX_MERKLE_PATH_BITS, TX_MERKLE_ROOT_BITS);
	check = check - merkle_root;
	if (enforce)
		check = check * (*enforce);
	constrain_zero<ZKPAIRING>(check);
}

static void compute_tx(const TxPayZK& zk)
{
	// RULE tx: donation + sum(output values) - sum(input values) == 0

	ZKVAR valsum = zk.publics.donation;

	for (unsigned i = 0; i < zk.nout; ++i)
	{
		valsum = valsum + zk.output[i].privates.__value;	// unused output values must be zero

		unsigned enforce_index = zk.output[i].enforce_index;
		//cerr << "output " << i << " enforce_index " << enforce_index << endl;
		const ZKVAR& enforce = zk.publics.enforce_flags[enforce_index];

		ZKVAR encrypt_value = enforce * zk.publics.encrypt_values;

		compute_output(zk.output[i], zk.publics.outvalmin, zk.publics.outvalmax, zk.publics.M_commitment_iv_bits, &enforce, encrypt_value);
	}

	for (unsigned i = 0; i < zk.nin; ++i)
	{
		valsum = valsum - zk.input[i].privates.__value;		// unused input values must be zero

		unsigned enforce_index = zk.input[i].enforce_index;
		//cerr << "input " << i << " enforce_index " << enforce_index << endl;
		const ZKVAR& enforce = zk.publics.enforce_flags[enforce_index];

		const ZKVAR* enforce_path = NULL;
		if (i < zk.nin_with_path)
		{
			unsigned enforce_index = zk.inpath_enforce_index[i];
			//cerr << "path " << i << " enforce_index " << enforce_index << endl;
			enforce_path = &zk.publics.enforce_flags[enforce_index];

			//cerr << "checking merkle for input " << i << endl;
			check_merkle(zk.input[i], zk.inpath[i], zk.publics.merkle_root, enforce_path);
		}

		compute_input(zk.input[i], zk.publics.invalmax, &enforce, enforce_path);
	}

	constrain_zero<ZKPAIRING>(valsum);
}

static thread_local TxPayZK *pzk;	// memory leak

// returns keyindex
unsigned CCProof_Compute(struct TxPay& tx, unsigned keyindex = -1, bool verify = false, ostringstream *benchmark_text = NULL)
{
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

	if (keyindex == (unsigned)(-1))
		keyindex = keystore.GetKeyIndex(zk.nout, zk.nin, zk.nin_with_path, tx.test_uselargerzkkey);
	else
		keystore.SetTxCounts(keyindex, zk.nout, zk.nin, zk.nin_with_path, verify);

	tx.zkkeyid = keystore.GetKeyId(keyindex);

	//cerr << "zk nout " << zk.nout << " nin " << zk.nin << " nin_with_path " << zk.nin_with_path << " keyindex " << keyindex << endl;

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

	if (keyindex == (unsigned)(-1))
	{
		cout << "CCProof_Compute error: no key found" << endl;

		return -1;
	}

	if (zk.nout < tx.nout || zk.nin < tx.nin || zk.nin_with_path < tx.nin_with_path)
	{
		cout << "CCProof_Compute error: insufficient key capacity" << endl;

		return -1;
	}

	//zk.output.resize(zk.nout);
	//zk.input.resize(zk.nin);
	if (!verify)
		zk.inpath.resize(zk.nin_with_path);

	reset<ZKPAIRING>();

	int makebad = 0;
	if (tx.test_make_bad && !verify)
	{
		unsigned nvars = 7 + 6 * tx.nout + 9 * tx.nin + 49 * tx.nin_with_path;
		uint32_t rval;
		CCRandom(&rval, sizeof(rval));

		//rval = -1;
		//nvars += 1;

		makebad = (((uint64_t)rval * nvars) >> 32) + 1;

		//cerr << "makebad " << makebad << " nvars " << nvars << endl;
		//tx_dump_stream(cerr, tx);
	}

	// public inputs
	bless_public_inputs(zk, tx, makebad);

	// marks end of public inputs
	end_input<ZKPAIRING>();

	if (verify)
		return keyindex;

	// hidden inputs
	bless_private_inputs(zk, tx, makebad);

	if (tx.test_make_bad)
	{
		//cerr << "bless done makebad " << makebad << endl;

		if (makebad > 0)
		{
			cerr << "assert failure makebad > 0" << endl;
			*(int*)0 = 0;		// don't use CCASSERT because the exception handler would just cause the proof to fail, mimicing what is supposed to happen
		}
	}

	// do computations

	breakout_bits(zk);

	compute_tx(zk);

	return keyindex;
}

#if SUPPORT_ZK_KEYGEN

CCPROOF_API CCProof_GenKeys()
{
	cerr << "CCProof_GenKeys" << endl;

	keystore.Init(true);

	unique_ptr<TxPay> ptx(new TxPay);
	CCASSERT(ptx);

	TxPay& tx = *ptx;

	for (unsigned i = 0; i < keystore.GetNKeys(); ++i)
	{
		memset(&tx, 0, sizeof(TxPay));

		keystore.SetTxCounts(i, tx.nout, tx.nin, tx.nin_with_path);

		if (tx.nin != tx.nin_with_path)
			continue;	// !!! not currently using commitments without paths

		for (unsigned j = 0; j < tx.nin_with_path; ++j)
			tx.input[j].pathnum = j + 1;

		cerr << "Generating keypair " << i << endl;

		CCProof_Compute(tx, i);

		auto key = keypair<ZKPAIRING>();

		keystore.SaveKeyPair(i, key);
	}

	cerr << "CCProof_GenKeys done" << endl;

	return 0;
}

#endif // SUPPORT_ZK_KEYGEN

CCPROOF_API CCProof_PreloadVerifyKeys()
{
	//cerr << "CCProof_PreloadVerifyKeys" << endl;

	keystore.Init();

	try
	{
		keystore.PreLoadVerifyKeys();
	}
	catch (...)
	{
#if SUPPORT_ZK_KEYGEN
		CCProof_GenKeys();
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

CCPROOF_API CCProof_GenProof(struct TxPay& tx)
{
	//cerr << "CCProof_GenProof" << endl;

#if TEST_SKIP_ZKPROOFS
	CCRandom(&tx.zkproof, sizeof(tx.zkproof));
	return 0;
#endif

#if USE_TEST_CODE
	{
	const unsigned keyindex = 10;

	keystore.Init(true);
	keystore.SaveKeyPair(keyindex, testkey);	// dummy key file so GetKeyIndex() works

	CCProof_Compute(tx, keyindex);

	testkey = keypair<ZKPAIRING>();

	keystore.SaveKeyPair(keyindex, testkey);
	keystore.Init(true);
	}
#endif

	reset<ZKPAIRING>();

	uint32_t t0;
	ostringstream benchmark_text;

	if (TEST_SHOW_GEN_BENCHMARKS)
		t0 = ccticks();

	auto keyindex = CCProof_Compute(tx, -1, false, &benchmark_text);
	if (keyindex == (unsigned)(-1))
	{
		//@cerr << "CCProof_GenProof failed" << endl;
		return -1;
	}

	auto key = keystore.GetProofKey(keyindex);

#if 0 // USE_TEST_CODE
	key = testkey;
#endif

	auto zkproof = proof<ZKPAIRING>(*key);

	Proof2Vec(tx.zkproof, zkproof);

	reset<ZKPAIRING>();	// free memory

	if (TEST_SHOW_GEN_BENCHMARKS)
	{
		auto t1 = ccticks();
		auto elapsed = ccticks_elapsed(t0, t1);
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cout << "Zero knowledge proof generated: " << benchmark_text.str() << "; keyindex " << keyindex << " elapsed time " << elapsed << " ms" << endl;
	}

	//for (unsigned i = 0; i < tx.zkproof.size(); ++i)
	//	cerr << "zkproof[" << i << "] " << tx.zkproof[i] << endl;

	return 0;
}

CCPROOF_API CCProof_VerifyProof(struct TxPay& tx)
{
	//cerr << "CCProof_VerifyProof" << endl;

#if TEST_SKIP_ZKPROOFS
	return 0;
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

		keyindex = CCProof_Compute(tx, tx.zkkeyid, true, &benchmark_text);
		if (keyindex == (unsigned)(-1))
		{
			cout << "CCProof_VerifyProof key not found" << endl;
		}
		else
		{
			auto witness = input<ZKPAIRING>();

			//auto t0 = ccticks();

			Proof<ZKPAIRING> zkproof;
			Vec2Proof(tx.zkproof, zkproof);

			#if 0 // USE_TEST_CODE
			auto pvk = new snarklib::PPZK_PrecompVerificationKey<ZKPAIRING>(testkey.vk());
			auto key = *pvk;
			#else
			auto key = keystore.GetVerifyKey(tx.zkkeyid);
			#endif

			valid = snarklib::strongVerify(key, *witness, zkproof);

			reset<ZKPAIRING>();	// free memory
		}
	}
	catch (...)
	{
	}

	if (TEST_SHOW_VERIFY_BENCHMARKS)
	{
		auto t1 = ccticks();
		auto elapsed = ccticks_elapsed(t0, t1);
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cout << "Zero knowledge proof " << (valid ? "verified:  " : "INVALID: ") << benchmark_text.str() << "; keyindex " << keyindex << " elapsed time " << elapsed << " ms" << endl;
	}

	return valid ? 0 : -1;
}
