/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CompressProof.hpp
*/

#include "CCbigint.hpp"

//#define TEST_PROOF_COMPRESSION		1

#ifndef TEST_PROOF_COMPRESSION
#define TEST_PROOF_COMPRESSION			0	// don't test
#endif

typedef array<bigint_t, 9> proof_vec_t;

template <typename GROUP>
void G2Int(bigint_t& x, const GROUP& g, const unsigned i = 0)
{
	auto a(g);
	a.affineCoordinates();

	//std::cerr << "G2Int x = " << a.x()[i].asBigInt() << std::endl;
	//std::cerr << "G2Int y = " << a.y()[i].asBigInt() << std::endl;

	//std::cerr << "dimension " << a.x().dimension() << std::endl;
	//std::cerr << "sizeInBits " << a.x().sizeInBits() << std::endl;

	x = a.x()[i].asBigInt();

	CCASSERT((BIGWORD(x, 3) >> 62) == 0);

	if (i == 0)
		BIGWORD(x, 3) |= (uint64_t)(a.y()[0].asUnsignedLong() & 1) << 62;

	//BIGWORD(x, 3) |= a.isZero() << 63;

	//std::cerr << "G2Int x = " << x << std::endl;
}

template <typename GROUP>
void Int2G(bigint_t x, bigint_t x1, GROUP& a)
{
	//std::cerr << "Int2G x = " << x << std::endl;

	bool tampered = (BIGWORD(x, 3) >> 63) || (BIGWORD(x1, 3) >> 62);
	CCASSERT(!tampered);

	unsigned y_lsb = (BIGWORD(x, 3) >> 62) & 1;

	//std::cerr << "Int2G y_lsb = " << a.x()[0].asBigInt() << std::endl;

	BIGWORD(x, 3) &= ~((uint64_t)(3) << 62);
	BIGWORD(x1, 3) &= ~((uint64_t)(3) << 62);

	a.m_X[0] = x;
	if (a.x().sizeInBits() > 256)
		a.m_X[1] = x1;

	// y = +/- sqrt(x^3 + b)
	//std::cerr << "squaring" << std::endl;
	a.y (squared(a.x()));
	//std::cerr << "cubing" << std::endl;
	a.y (a.y() * a.x());
	//std::cerr << "adding coeff b" << std::endl;
	a.y (a.y() + GROUP::Curve::coeff_b(a.x()));
	//std::cerr << "taking sqrt" << std::endl;
	a.y (sqrt(a.y()));
	//std::cerr << "checking sign" << std::endl;
	if ((a.y()[0].asUnsignedLong() & 1) != y_lsb)
		a.y (-a.y());

	//std::cerr << "setting z" << std::endl;
	a.z (a.z().one());

	//std::cerr << "Int2G done" << std::endl;
}

#if 0
std::istream& operator>>(std::istream &in, bn128_G2 &g)
{
	char is_zero;
	in.read((char*)&is_zero, 1); // this reads is_zero;
	is_zero -= '0';
	consume_OUTPUT_SEPARATOR(in);

	bn::Fp2 tX;
	in >> tX.a_;
	consume_OUTPUT_SEPARATOR(in);
	in >> tX.b_;
	consume_OUTPUT_SEPARATOR(in);
	unsigned char Y_lsb;
	in.read((char*)&Y_lsb, 1);
	Y_lsb -= '0';

	// y = +/- sqrt(x^3 + b)
	if (!is_zero)
	{
		g.coord[0] = tX;
		bn::Fp2 tX2, tY2;
		bn::Fp2::square(tX2, tX);
		bn::Fp2::mul(tY2, tX2, tX);
		bn::Fp2::add(tY2, tY2, bn128_twist_coeff_b);

		g.coord[1] = bn128_G2::sqrt(tY2);
		if ((((unsigned char*)&g.coord[1].a_)[0] & 1) != Y_lsb)
		{
			bn::Fp2::neg(g.coord[1], g.coord[1]);
		}
	}

	/* finalize */
	if (!is_zero)
	{
		g.coord[2] = bn::Fp2(bn::Fp(1), bn::Fp(0));
	}
	else
	{
		g = bn128_G2::zero();
	}

	return in;
}
#endif

template <typename PAIRING>
void Proof2Vec(proof_vec_t& vec, const Proof<PAIRING>& proof)
{
	G2Int<typename PAIRING::G1>(vec[0], proof.A().G());
	G2Int<typename PAIRING::G1>(vec[1], proof.A().H());

	G2Int<typename PAIRING::G2>(vec[2], proof.B().G(), 0);
	G2Int<typename PAIRING::G2>(vec[3], proof.B().G(), 1);
	G2Int<typename PAIRING::G1>(vec[4], proof.B().H());

	G2Int<typename PAIRING::G1>(vec[5], proof.C().G());
	G2Int<typename PAIRING::G1>(vec[6], proof.C().H());

	G2Int<typename PAIRING::G1>(vec[7], proof.H());
	G2Int<typename PAIRING::G1>(vec[8], proof.K());
}

template <typename PAIRING>
void Vec2Proof(const proof_vec_t& vec, Proof<PAIRING>& proof)
{
	bigint_t dummy;

	Int2G<typename PAIRING::G1>(vec[0], dummy, proof.m_A.m_G);
	Int2G<typename PAIRING::G1>(vec[1], dummy, proof.m_A.m_H);

	Int2G<typename PAIRING::G2>(vec[2], vec[3], proof.m_B.m_G);
	Int2G<typename PAIRING::G1>(vec[4], dummy, proof.m_B.m_H);

	Int2G<typename PAIRING::G1>(vec[5], dummy, proof.m_C.m_G);
	Int2G<typename PAIRING::G1>(vec[6], dummy, proof.m_C.m_H);

	Int2G<typename PAIRING::G1>(vec[7], dummy, proof.m_H);
	Int2G<typename PAIRING::G1>(vec[8], dummy, proof.m_K);
}

template <typename PAIRING>
void DumpProof(const Proof<PAIRING>& proof)
{
	std::cerr << "proof" << std::endl;
	std::cerr << proof;
	std::cerr << "proof end" << std::endl;
}

template <typename PAIRING>
void StreamOutProof(std::ostream& os, const Proof<PAIRING>& proof)
{
#if TEST_PROOF_COMPRESSION
	DumpProof(proof);

	os << proof;	// for testing
#endif

	proof_vec_t vec;

	Proof2Vec(vec, proof);

	for (auto& v : vec)
		os << v << std::endl;
}

template <typename PAIRING>
void StreamInProof(std::istream& is, Proof<PAIRING>& proof)
{
#if TEST_PROOF_COMPRESSION
	Proof<PAIRING> proof2;
	is >> proof2;	// for testing
#endif

	proof_vec_t vec;

	for (auto& v : vec)
	{
		is >> v;
		//std::cerr << v << std::endl;
	}

	//proof_vec_t vec2;	// for testing
	//std::cerr << "read proof vector" << std::endl;

	Vec2Proof(vec, proof);

#if 0
	Proof2Vec(vec2, proof);	// for testing
	for (auto& v : vec2)
		std::cerr << v << std::endl;

	std::cerr << "A.G well " << proof.m_A.G().wellFormed() << std::endl;
	std::cerr << "A.H well " << proof.m_A.H().wellFormed() << std::endl;
	std::cerr << "B.G well " << proof.m_B.G().wellFormed() << std::endl;
	std::cerr << "B.H well " << proof.m_B.H().wellFormed() << std::endl;
	std::cerr << "C.G well " << proof.m_C.G().wellFormed() << std::endl;
	std::cerr << "C.H well " << proof.m_C.H().wellFormed() << std::endl;
	std::cerr << "H well " << proof.m_H.wellFormed() << std::endl;
	std::cerr << "K well " << proof.m_K.wellFormed() << std::endl;
#endif

#if TEST_PROOF_COMPRESSION
	std::cerr << "A eq " << (proof.m_A == proof2.m_A) << std::endl;
	std::cerr << "B eq " << (proof.m_B == proof2.m_B) << std::endl;
	std::cerr << "C eq " << (proof.m_C == proof2.m_C) << std::endl;
	std::cerr << "H eq " << (proof.m_H == proof2.m_H) << std::endl;
	std::cerr << "K eq " << (proof.m_K == proof2.m_K) << std::endl;

	//proof.m_A = proof2.m_A;	// for testing
	//proof.m_B = proof2.m_B;
	//proof.m_C = proof2.m_C;
	//proof.m_H = proof2.m_H;
	//proof.m_K = proof2.m_K;

	DumpProof(proof);
#endif

}
