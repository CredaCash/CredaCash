/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * zkhash.hpp
*/

#pragma once

#include <cassert>
#include <array>
#include <vector>
#include <cstdint>
#include <iostream>
#include <gmp.h>
#include <siphash/siphash.h>

#include "HashBases.h"
#include "CCproof.h"

using namespace std;
using namespace snarkfront;

#define DEBUG_ZKHASH			0	// for debugging

namespace CCHasher
{

class HashBases
{
	unsigned size;

	const bigint_t* ptable;

	vector<BN128_FR> FieldBases;

public:
	// this must be called *after* init_BN128()
	void Init()
	{
		if (size)
			return;

		size = sizeof(hash_bases)/(256/8);

		ptable = (const bigint_t*)hash_bases;

		//cerr << "HashBases size " << size << endl;

		FieldBases.reserve(size);

		for (unsigned i = 0; i < size; ++i)
		{
			FieldBases.emplace_back(bigint(i));
			//cerr << i << " " << hex << bigint(i) << endl << BN128_FR(bigint(i)) << endl << FieldBases.at(i) << dec << endl;
		}
	}

	const bigint_t& bigint(const size_t index) const
	{
		CCASSERT(size > 0);
		CCASSERT(index < size);

		return ptable[index];
	}

	const BN128_FR& operator[](const size_t index) const
	{
		CCASSERT(index < size);

		return FieldBases[index];
	}
};

extern "C" HashBases hashbases;


template <typename ZKVAR, typename ZKRESULT>
class Coerce
{
public:
	static ZKRESULT value(const ZKVAR& a)
	{
		return a;
	}
};

template <typename ZKVAR, typename ZKRESULT>
class ASTValue
{
public:
	static ZKRESULT value(const ZKVAR& a)
	{
		return a->value();
	}
};

template <typename ZKVAR, typename ZKRESULT>
class FieldValue
{
public:
	static ZKRESULT value(const ZKVAR& a)
	{
		return a[0].asBigInt();
	}
};

#if 0 // not used
template <typename ZKVAR, typename ZKRESULT>
class Mask
{
public:
	static ZKRESULT value(const ZKVAR& a)
	{
		ZKRESULT rv = a;

		rv.data()[2] &= ((uint64_t)1 << (MERKLE_BITS-128)) - 1;	// mask off the extra bits

		return rv;
	}
};

template <typename ZKVAR, typename ZKRESULT>
class FieldValueMask
{
public:
	static ZKRESULT value(const ZKVAR& a)
	{
		ZKRESULT rv = a[0].asBigInt();

		rv.data()[2] &= ((uint64_t)1 << (MERKLE_BITS-128)) - 1;	// mask off the extra bits

		return rv;
	}
};

template <typename ZKVAR>
class ModPrime
{
public:
	static ZKVAR Condition(const ZKVAR& a)
	{
		return a * ZKVAR(1UL);	// multiply by 1 results in modulo prime
	}
};
#endif

template <typename ZKVAR>
class NoOp
{
public:
	static ZKVAR Condition(const ZKVAR& a)
	{
		return a;
	}
};

template <typename ZKPAIRING, typename ZKVAR, typename ZKBOOL>
class NOPConstraints
{
public:
	static void addBooleanity(const ZKBOOL& a)
	{
		;;;
	}

	static void constrainValue(const ZKVAR& var, const vector<ZKBOOL>& bits, const uint16_t *bases = NULL, ZKVAR const *premainder = NULL, ZKVAR const *pbitval = NULL)
	{
		;;;
	}
};

template <typename ZKPAIRING, typename ZKVAR, typename ZKBOOL>
class BitConstraints
{
public:

	// constrain value to 0 or 1

	static void addBooleanity(const ZKBOOL& a)
	{
		auto& RS = TL<R1C<typename ZKPAIRING::Fr>>::singleton();
		RS->addBooleanity(a->r1Terms()[0]);
	}

	// Constrain linear sum of bits, according to var, premainder and pbitval:
	//	A. if pbitval && premainder:
	//		constrain sum(bit*basis) == *pbitval && *pbitval + *premainder == var
	//	B. if pbitval && !premainder [note: in practice, D would probably be used instead of this]
	//		constrain sum(bit*basis) == *pbitval && *pbitval == var
	//	C. if !pbitval && premainder:
	//		constrain sum(bit*basis) + *premainder == var
	//	D. if !pbitval && !premainder:
	//		constrain sum(bit*basis) == var
	// The basis coefficients are taken from base to (base + bits.size() - 1); premainder (if used) is multiplied by base + bits.size()
	// Note the first 256 bases in the table are the powers of 2, i.e., basis[i] = 1 << i, which are used to decompose values into bits,
	//	and the remaining bases are random values used in the knapsack hash

	static void constrainValue(const ZKVAR& var, const vector<ZKBOOL>& bits, const uint16_t *bases = NULL, ZKVAR const *premainder = NULL, ZKVAR const *pbitval = NULL)
	{
		snarklib::R1Combination<typename ZKPAIRING::Fr> LC;
		LC.reserveTerms(bits.size() + ((premainder && !pbitval) ? 1 : 0));

		auto nbits = bits.size();

		uint16_t basesi = 0;
		auto pbases = bases;
		if (!pbases)
			pbases = &basesi;

		for (unsigned i = 0; i < nbits; ++i)
		{
			LC.addTerm(bits[i]->r1Terms()[0] * hashbases[*pbases]);

			if (DEBUG_ZKHASH) cerr << "constrainValue: bit " << i << " value " << hex << ASTValue<ZKBOOL,bigint_t>::value(bits[i]) << " scale " << FieldValue<typename ZKPAIRING::Fr,bigint_t>::value(hashbases[*pbases]) << dec << endl;

			if (bases)
				++pbases;
			else
				++basesi;
		}

		auto& RS = TL<R1C<typename ZKPAIRING::Fr>>::singleton();

		if (pbitval)
		{
			if (DEBUG_ZKHASH) cerr << "constrainValue: use bitval " << hex << ASTValue<ZKVAR,bigint_t>::value(*pbitval) << dec << endl;

			RS->m_constraintSystem.addConstraint(LC == (*pbitval)->r1Terms()[0]);

			LC.m_terms.clear();
			LC.reserveTerms(2);
			LC.addTerm((*pbitval)->r1Terms()[0]);
		}

		if (premainder)
		{
			if (DEBUG_ZKHASH) cerr << "constrainValue: use remainder " << hex << ASTValue<ZKVAR,bigint_t>::value(*premainder) << " scale " << FieldValue<typename ZKPAIRING::Fr,bigint_t>::value(hashbases[*pbases]) << dec << endl;

			LC.addTerm((*premainder)->r1Terms()[0] * hashbases[*pbases]);
		}

		if (DEBUG_ZKHASH) cerr << "constrainValue: var " << hex << ASTValue<ZKVAR,bigint_t>::value(var) << dec << endl;

		RS->m_constraintSystem.addConstraint(LC == var->r1Terms()[0]);
	}
};

class HashInput
{
public:
	unsigned nbits;
	bool mask_higher_bits;

	void DumpBase() const
	{
		cerr << "HashInput nbits " << nbits << endl;
		//cerr << "HashInput mask_higher_bits " << mask_higher_bits << endl;
	}
};

template <typename ZKIPARAM, typename ZKBOOL, template <typename ZKIPARAM2> class ZKIFIX, template<typename ZKVAR, typename ZKRESULT> class ZKVAL>
class EvalHashInput : public HashInput
{
	ZKIPARAM value;

public:
	void SetValue(const ZKIPARAM& val, unsigned bits)
	{
		value = ZKIFIX<ZKIPARAM>::Condition(val);
		nbits = bits;

		#if LIMIT_ZKPROOF_INPUTS_TO_PRIME_FIELD
		if (bits > TX_FIELD_BITS)
		{
			value = value * bigint_t(1UL);
			nbits = TX_FIELD_BITS;
		}
		#endif

		//cerr << "EvalHashInput val " << hex << val << " value " << value << dec << endl;
	}

	const ZKIPARAM& GetValue() const
	{
		return value;
	}

	const vector<ZKBOOL>& GetValueAsVector() const
	{
		CCASSERT(0);
		static vector<ZKBOOL> null_vector;

		return null_vector;
	}

	void Dump() const
	{
		cerr << "HashInput value " << hex << value << dec << endl;
		this->DumpBase();
	}
};

template <typename ZKIPARAM, typename ZKBOOL, template <typename ZKIPARAM2> class ZKIFIX, template<typename ZKVAR, typename ZKRESULT> class ZKVAL>
class ZKHashInput : public HashInput
{
	const void *pvalue;
	bool isvector;
public:
	void SetValue(const ZKIPARAM& val, unsigned bits)
	{
		isvector = false;
		pvalue = &val;
		nbits = bits;
		#if LIMIT_ZKPROOF_INPUTS_TO_PRIME_FIELD
		if (nbits > TX_FIELD_BITS) nbits = TX_FIELD_BITS;
		#endif
	}

	void SetValue(const vector<ZKBOOL>& val, unsigned bits)
	{
		isvector = true;
		pvalue = &val;
		nbits = bits;
		#if LIMIT_ZKPROOF_INPUTS_TO_PRIME_FIELD
		if (nbits > TX_FIELD_BITS) nbits = TX_FIELD_BITS;
		#endif

		CCASSERT(val.size() == nbits);
	}

	const ZKIPARAM& GetValue() const
	{
		CCASSERT(!isvector);

		return *(ZKIPARAM *)pvalue;
	}

	const vector<ZKBOOL>& GetValueAsVector() const
	{
		CCASSERT(isvector);

		return *(vector<ZKBOOL> *)pvalue;
	}

	void Dump() const
	{
		if (pvalue && isvector)
		{
			bigint_t val = 0UL;
			auto a = GetValueAsVector();
			for (int i = a.size() - 1; i >= 0; --i)
			{
				val = val * bigint_t(2UL);
				if (ZKVAL<ZKBOOL,bigint_t>::value(a[i]))
					val = val + bigint_t(1UL);
			}
			cerr << "HashInput value " << hex << val << dec << endl;
		}
		else if (pvalue)
			cerr << "HashInput value " << hex << ZKVAL<ZKIPARAM,bigint_t>::value(GetValue()) << dec << endl;
		else
			cerr << "HashInput value = NULL" << endl;

		this->DumpBase();
	}
};

// typename		zk namespace type		eval namespace type		usage
// --------		-----------------		-------------------		-----
// ZKPPARAM		integer					integer					Integers
// ZKIPARAM		zk variable				integer					Function inputs and outputs
// ZKCONST		zk constant				modulo integer			Algorithm constants
// ZKVAR		zk variable				modulo integer			Algorithm computations
// ZKBOOL		zk variable				uint16_t				bit values

template <typename ZKPAIRING, typename ZKPPARAM, typename ZKIPARAM, typename ZKCONST, typename ZKVAR, typename ZKBOOL,
template <typename ZKIPARAM2, typename ZKBOOL2, template <typename ZKIPARAM9> class ZKIFIX2, template <typename ZKVAR9, typename ZKRESULT9> class ZKVAL2> class ZKHASHINPUT,
template <typename ZKIPARAM3> class ZKIFIX,
template <typename ZKVAR4, typename ZKRESULT4> class ZKVAL,
template <typename ZKBOOL5, typename ZKRESULT5> class ZKBVAL,
template <typename ZKVAR6, typename ZKRESULT6> class ZKIVAL,
template <typename ZKPAIRING7, typename ZKVAR7, typename ZKBOOL7> class ZKCONSTRAINTS>
class ZKHasher
{
public:
	typedef const ZKCONST Const;

	static void Init()
	{
		hashbases.Init();
	}

	// Decompose the lower nbits of var into individual bits, enforce booleanity on the bits, and return the bits in a vector<ZKBOOL>
	// The function has two options to deal with values that might have upper bits set (beyond the lower nbits):
	//	if premainder, (var >> nbits) is returned in *premainder
	//	if pbitval, the lower nbits of var, i.e., (var & ((1 << nbits) - 1)), is returned in *pbitval
	// If enforce_value is set, the sum(bit[i]*2^i) + *premainder (if not NULL) is contrained to equal var
	//	and if pbitval is not NULL, sum(bit[i]*2^i) is constrained to equal *pbitval

	static vector<ZKBOOL> extractBits(const ZKVAR& var, int nbits, ZKVAR *premainder = NULL, ZKVAR *pbitval = NULL)
	{
		bigint_t val = ZKVAL<ZKVAR, bigint_t>::value(var);

		if (DEBUG_ZKHASH) cerr << "extractBits: nbits " << nbits << " val " << hex << val << dec << endl;

		#if LIMIT_ZKPROOF_INPUTS_TO_PRIME_FIELD
		if (nbits > TX_FIELD_BITS)
		{
			nbits = TX_FIELD_BITS;
		}

		if (premainder && nbits >= TX_FIELD_BITS)
		{
			(int*)0 = 0;	// make sure this code path isn't used

			*premainder = 0UL;
			premainder = NULL;	// there can be no reminder, so ignore this

			if (DEBUG_ZKHASH) cerr << "extractBits ignoring remainder because nbits = " << nbits << " >= " << TX_FIELD_BITS << endl;
		}
		#endif

		vector<ZKBOOL> bits;
		bits.reserve(nbits);

		bigint_t bval = 0UL;

		for (int i = 0; i < nbits; ++i)
		{
			unsigned bit = BIG64(val) & 1;
			mpn_rshift(BIGDATA(val), BIGDATA(val), BIGINT(val).numberLimbs(), 1);

			if (pbitval && bit)
				addBigInt(bval, hashbases.bigint(i), bval, false);

			if (DEBUG_ZKHASH) cerr << "extractBits: index " << i << " bit " << bit << " bitval " << hex << bval << " remainder " << val << dec << endl;

			bits.emplace_back(bit);

			ZKCONSTRAINTS<ZKPAIRING,ZKVAR,ZKBOOL>::addBooleanity(bits.back());
		}

		if (premainder)
		{
			ZKVAR _remainder = val;	// necessary to run the setup in this constructor

			*premainder = _remainder;

			if (DEBUG_ZKHASH) cerr << "extractBits: set remainder = " << hex << ZKVAL<ZKVAR,bigint_t>::value(*premainder) << dec << endl;
		}
		else if (val)
		{
			if (DEBUG_ZKHASH) cerr << "extractBits warning: val = " << hex << ZKVAL<ZKVAR, bigint_t>::value(var) << " remainder = " << val << dec << endl;
			//CCASSERTZ(val && !premainder);
			//CCASSERT(0);
		}

		if (pbitval)
		{
			ZKVAR _bitval = bval;	// necessary to run the setup in this constructor

			*pbitval = _bitval;

			if (DEBUG_ZKHASH) cerr << "extractBits: set bitval = " << hex << ZKVAL<ZKVAR,bigint_t>::value(*pbitval) << dec << endl;
		}

		ZKCONSTRAINTS<ZKPAIRING,ZKVAR,ZKBOOL>::constrainValue(var, bits, NULL, premainder, pbitval);

		return bits;
	}

	static ZKVAR Knapsack1(const vector<ZKBOOL>& bits, const void *prfkey, uint32_t& basisi, bool sequential)
	{
		uint16_t bases[256];

		unsigned nbits = bits.size();

		CCASSERT(nbits <= sizeof(bases)/sizeof(uint16_t));
		CCASSERTZ(HASHBASES_NRANDOM & (HASHBASES_NRANDOM - 1));	// must be a power of 2

		//auto basisi0 = basisi;

		for (unsigned i = 0; i < nbits; ++i)
		{
			if (!prfkey)
				bases[i] = i + HASHBASES_RANDOM_START;
			else
			{
				if (sequential)
					bases[i] = *(uint16_t*)prfkey + basisi;
				else
					bases[i] = siphash_keyed((uint8_t*)prfkey, (uint8_t*)&basisi, sizeof(basisi));

				bases[i] &= (HASHBASES_NRANDOM - 1);
				bases[i] += HASHBASES_RANDOM_START;
				++basisi;
			}

			//if (prfkey && !sequential)
			//	cerr << "prfkey " << (uintptr_t)prfkey << " for basis " << basisi0 << " sequential " << sequential << " bases[" << i << "] = " << bases[i] << endl;

			//for (unsigned j = 0; j < i; ++j)
			//{
			//	if (bases[i] == bases[j])
			//		cerr << "prfkey " << (uintptr_t)prfkey << " for basis " << basisi0 << " sequential " << sequential << " bases " << i << " = " << j << " key " << *(uint64_t*)prfkey << endl;
			//}
		}

		bigint_t sum = 0UL;

		for (unsigned i = 0; i < nbits; ++i)
		{
			if (BIG64((ZKVAL<ZKBOOL,bigint_t>::value(bits[i]))))
			{
				sum = sum + hashbases.bigint(bases[i]);

				if (DEBUG_ZKHASH) cerr << "Knapsack: bit " << i << " adding basis " << bases[i] << endl << " value " << hex << hashbases.bigint(bases[i]) << endl << " sum " << sum << dec << endl;
			}
		}

		ZKVAR var = sum;	// needed to run the setup in this constructor

		ZKCONSTRAINTS<ZKPAIRING,ZKVAR,ZKBOOL>::constrainValue(var, bits, bases);

		return var;
	}

	static array<ZKVAR,2> Knapsack2(const vector<ZKBOOL>& bits, const void *prfkey, uint32_t& basisi)
	{
		array<ZKVAR,2> ks;

		ks[0] = Knapsack1(bits, prfkey, basisi, true);
		ks[1] = Knapsack1(bits, prfkey, basisi, false);

		return ks;
	}

	static ZKIPARAM HashFinish(ZKVAR& acc, ZKVAR& ks0, ZKVAR& ks1, const void *prfkey, uint32_t& basisi, const unsigned outbits, bool skip_final_knapsack)
	{
		if (DEBUG_ZKHASH) cerr << "Hash: Knapsack output:" << endl;
		if (DEBUG_ZKHASH) cerr << hex << ZKVAL<ZKIPARAM,bigint_t>::value(acc) << dec << endl;
		if (DEBUG_ZKHASH) cerr << hex << ZKVAL<ZKIPARAM,bigint_t>::value(ks0) << dec << endl;
		if (DEBUG_ZKHASH) cerr << hex << ZKVAL<ZKIPARAM,bigint_t>::value(ks1) << dec << endl;

		for (unsigned i = 0; i < 8; ++i)
		{
			static Const c = 1UL;

			ks0 = ks0*ks0 + ks0 + c;
			ks1 = ks1*ks1 - ks1 + c;
		}

		acc = acc + ks0 + ks1;

		if (DEBUG_ZKHASH) cerr << "Hash: Diophantine output:" << endl;
		if (DEBUG_ZKHASH) cerr << hex << ZKVAL<ZKIPARAM,bigint_t>::value(acc) << dec << endl;
		if (DEBUG_ZKHASH) cerr << hex << ZKVAL<ZKIPARAM,bigint_t>::value(ks0) << dec << endl;
		if (DEBUG_ZKHASH) cerr << hex << ZKVAL<ZKIPARAM,bigint_t>::value(ks1) << dec << endl;

		if (!skip_final_knapsack)
		{
			// do a final knapsack

			auto inbits = outbits * 2;
			if (inbits > TX_FIELD_BITS)
				inbits = TX_FIELD_BITS;

			ZKVAR rem;
			auto accbits = extractBits(acc, inbits, (inbits >= TX_FIELD_BITS ? NULL : &rem));
			acc = Knapsack1(accbits, prfkey, basisi, true);
		}

		if (outbits >= TX_FIELD_BITS)
			return ZKIVAL<ZKVAR,ZKIPARAM>::value(acc);
		else
		{
			ZKVAR result, rem;
			auto accbits = extractBits(acc, outbits, &rem, &result);
			return ZKIVAL<ZKVAR,ZKIPARAM>::value(result);
		}
	}

	static ZKIPARAM Hash(vector<ZKHASHINPUT<ZKIPARAM, ZKBOOL, ZKIFIX, ZKVAL>>& a, int basis, const unsigned outbits, bool skip_final_knapsack = false, bool bit_inputs = false)
	{
		ZKVAR acc, ks0, ks1;

		if (DEBUG_ZKHASH) cerr << "Hash nin " << a.size() << " basis " << basis << " outbits " << outbits << " skip_final_knapsack " << skip_final_knapsack << " bit_inputs " << bit_inputs << endl;

		//acc = 0UL;	// for testing

		CCASSERT(basis < (int)sizeof(hash_bases_prfkeys)/(128/8));
		CCASSERT(HASH_BASES_MERKLE_NODE < 0);

		const void *prfkey = NULL;
		if (basis >= 0)
			prfkey = &hash_bases_prfkeys[basis*2];

		uint32_t basisi = 0;

		for (unsigned i = 0; i < a.size(); ++i)
		{
			//acc = acc + a[i].GetValue();	// for testing
			//continue;						// for testing

			vector<ZKBOOL> abits;
			ZKVAR ar;

			if (bit_inputs)
				abits = a[i].GetValueAsVector();
			else
				abits = extractBits(a[i].GetValue(), a[i].nbits, (a[i].mask_higher_bits ? &ar : NULL));

			auto ks = Knapsack2(abits, prfkey, basisi);

			//if (a.size() == 2 && (ZKVAL<ZKVAR,bigint_t>::value(a[0].GetValue()) == 0 && ZKVAL<ZKVAR,bigint_t>::value(a[1].GetValue()) == 0))
			//	cerr << "Knapsack2 " << i << " " << a[i].basis << " " << hex << ZKVAL<ZKVAR,bigint_t>::value(a[i].GetValue()) << " " << ZKVAL<ZKVAR,bigint_t>::value(ks[0]) << " " << ZKVAL<ZKVAR,bigint_t>::value(ks[1]) << dec << endl;

			if (i == 0)
			{
				ks0 = ks[0];
				ks1 = ks[1];
				acc = ks0 + ks1;
			}
			else
			{
				ks0 = ks0 + ks[0];
				ks1 = ks1 + ks[1];
				acc = acc + ks[0] + ks[1];
			}
		}

		return HashFinish(acc, ks0, ks1, prfkey, basisi, outbits, skip_final_knapsack);
	}

	static ZKIPARAM HashBits(vector<ZKHASHINPUT<ZKIPARAM, ZKBOOL, ZKIFIX, ZKVAL>>& a, unsigned basis, const unsigned outbits, bool skip_final_knapsack = false)
	{
		ZKVAR acc, ks0, ks1;

		return Hash(a, basis, outbits, skip_final_knapsack, true);
	}

	static ZKIPARAM Merkle(const ZKIPARAM& leafval, const unsigned leafbits, const vector<ZKIPARAM>& inputs, const unsigned pathbits)
	{
		CCASSERT(inputs.size() >= 1);

		vector<ZKHASHINPUT<ZKIPARAM, ZKBOOL, ZKIFIX, ZKVAL>> a(2);

		// start: a[0] = leafval, a[1] = first path input

		a[0].SetValue(leafval, leafbits);

		a[0].mask_higher_bits = (leafbits < TX_FIELD_BITS);

		if (DEBUG_ZKHASH) cerr << "Merkle: leaf" << endl;
		if (DEBUG_ZKHASH) a[0].Dump();

		ZKVAR hash;	// need to declare this here since SetValue stores a pointer to it

		for (unsigned i = 0; i < inputs.size(); ++i)
		{
			a[1].SetValue(inputs[i], pathbits);

			if (DEBUG_ZKHASH) cerr << "Merkle: input " << i << endl;
			if (DEBUG_ZKHASH) a[1].Dump();

			hash = Hash(a, HASH_BASES_MERKLE_NODE, pathbits, i < inputs.size() - 1);

			// iteration: a[0] = hash output, a[1] = next path input (at top of loop)

			a[0].SetValue(hash, pathbits);

			if (DEBUG_ZKHASH) cerr << "Merkle: output " << i << endl;
			if (DEBUG_ZKHASH) a[0].Dump();
		}

		return ZKIVAL<ZKVAR,ZKIPARAM>::value(a[0].GetValue());
	}
};

// Namespaces to construct zk proofs.  Uses slow datatypes.
// If BIGINT_MOD is defined (see below), the proof it computes is correct but the values are not.  In that case, the eval or inteval namespaces must be used to compute the values.

#define zk_VAR		bigint_x<typename ZKPAIRING::Fr>
#define zk_CONST	c_bigint<typename ZKPAIRING::Fr>

namespace zk
{
	template <typename ZKPAIRING> using
	HashInput = ZKHashInput<zk_VAR, zk_VAR, NoOp, ASTValue>;

	template <typename ZKPAIRING> using
	Hasher = ZKHasher<ZKPAIRING, bigint_t, zk_VAR, zk_CONST, zk_VAR, zk_VAR,
		ZKHashInput, NoOp, ASTValue, ASTValue, Coerce, BitConstraints>;
}

// Namespace to compute values.  Uses faster datatypes than zk namespace.
namespace eval
{
	typedef EvalHashInput<bigint_t, uint16_t, NoOp, Coerce> HashInput;

	template <typename ZKPAIRING> using
	Hasher = ZKHasher<ZKPAIRING, bigint_t, bigint_t, typename ZKPAIRING::Fr, typename ZKPAIRING::Fr, uint16_t,
		EvalHashInput, NoOp, FieldValue, Coerce, FieldValue, NOPConstraints>;

}

// Namespace to compute values using BigInts hacked to do modulo arithmetic, which is the fastest method.
// This method only works only if BIGINT_MOD is defined.

namespace inteval
{
	template <typename T> using
	HashInput = EvalHashInput<T, uint16_t, NoOp, Coerce>;

	template <typename T> using
	Hasher = ZKHasher<void, T, T, T, T, uint16_t,
		EvalHashInput, NoOp, Coerce, Coerce, Coerce, NOPConstraints>;
}

} // namespace CCHasher
