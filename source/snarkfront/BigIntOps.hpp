#ifndef _SNARKFRONT_BIG_INT_OPS_HPP_
#define _SNARKFRONT_BIG_INT_OPS_HPP_

#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <gmp.h>

#include <snarklib/BigInt.hpp>
#include <snarklib/EC_BN128_Modulus.hpp>

#ifndef BIGINT_MOD
#define BIGINT_MOD 1
#endif

namespace snarkfront {

////////////////////////////////////////////////////////////////////////////////
// BigInt arithmetic operations independent of a prime field
//

//
// a + b --> c
//
template <mp_size_t N>
bool addBigInt(const snarklib::BigInt<N>& a,
               const snarklib::BigInt<N>& b,
               snarklib::BigInt<N>& c,
				bool modprime = BIGINT_MOD)
{
    const mp_limb_t carry = mpn_add_n(c.data(), a.data(), b.data(), N);
	(void)carry;	// avoid unused variable warning

	if (modprime)
	{
		CCASSERT(N == snarklib::BN128_Modulus::r_limbs);
		if (mpn_cmp(c.data(), snarklib::BN128_Modulus::modulus_r().data(), N) >= 0)
			mpn_sub_n(c.data(), c.data(), snarklib::BN128_Modulus::modulus_r().data(), N);
	}

    return true;	// ! carry; // failure if overflow
}

template <mp_size_t N>
snarklib::BigInt<N> operator+ (const snarklib::BigInt<N>& a,
                               const snarklib::BigInt<N>& b)
{
    snarklib::BigInt<N> c;
    const bool ok = addBigInt(a, b, c);
#ifdef USE_ASSERT
    CCASSERT(ok);
#endif
    return c;
}

//
// a - b --> c
//
template <mp_size_t N>
bool subBigInt(const snarklib::BigInt<N>& a,
               const snarklib::BigInt<N>& b,
               snarklib::BigInt<N>& c,
				bool modprime = BIGINT_MOD)
{
    //if (mpn_cmp(a.data(), b.data(), N) < 0) return false; // failure
                                                          // if b > a
    const mp_limb_t borrow = mpn_sub_n(c.data(), a.data(), b.data(), N);
	(void)borrow;	// avoid unused variable warning
#ifdef USE_ASSERT
    //CCASSERT(0 == borrow);
#endif

	//std::cerr << "subBigInt borrow " << borrow << " modprime " << modprime << std::endl;

	if (modprime && borrow)
	{
		CCASSERT(N == snarklib::BN128_Modulus::r_limbs);
		mpn_add_n(c.data(), c.data(), snarklib::BN128_Modulus::modulus_r().data(), N);
		//std::cerr << "subBigInt added modulus result " << std::hex << c << std::dec << std::endl;
	}

	if (modprime)
	{
		CCASSERT(N == snarklib::BN128_Modulus::r_limbs);
		if (mpn_cmp(c.data(), snarklib::BN128_Modulus::modulus_r().data(), N) >= 0)
		{
			mpn_sub_n(c.data(), c.data(), snarklib::BN128_Modulus::modulus_r().data(), N);
			//std::cerr << "subBigInt subtracted modulus result " << std::hex << c << std::dec << std::endl;
		}
	}

    return true;
}

template <mp_size_t N>
snarklib::BigInt<N> operator- (const snarklib::BigInt<N>& a,
                               const snarklib::BigInt<N>& b)
{
    snarklib::BigInt<N> c;
    const bool ok = subBigInt(a, b, c);
#ifdef USE_ASSERT
    CCASSERT(ok);
#endif
    return c;
}

//
// a * b --> c
//
template <mp_size_t N>
bool mulBigInt(const snarklib::BigInt<N>& a,
               const snarklib::BigInt<N>& b,
               snarklib::BigInt<N>& c,
				bool modprime = BIGINT_MOD)
{
    std::array<mp_limb_t, 2*N> scratch;
    mpn_mul_n(scratch.data(), a.data(), b.data(), N);

    for (std::size_t i = N; i < 2*N; ++i) {
    //    if (scratch[i]) return false; // failure if overflow
    }

	if (modprime)
	{
		CCASSERT(N == snarklib::BN128_Modulus::r_limbs);
		std::array<mp_limb_t, 2*N> q;
		mpn_tdiv_qr(q.data(), c.data(), 0, scratch.data(), 2*N, snarklib::BN128_Modulus::modulus_r().data(), N);
	}
	else
	    mpn_copyi(c.data(), scratch.data(), N);

    return true;
}

template <mp_size_t N>
snarklib::BigInt<N> operator* (const snarklib::BigInt<N>& a,
                               const snarklib::BigInt<N>& b)
{
    snarklib::BigInt<N> c;
    const bool ok = mulBigInt(a, b, c);
#ifdef USE_ASSERT
    CCASSERT(ok);
#endif
    return c;
}

//
// a / b --> c
//
template <mp_size_t N>
bool divBigInt(const snarklib::BigInt<N>& a,
               const snarklib::BigInt<N>& b,
               snarklib::BigInt<N>& c)
{
    std::array<mp_limb_t, N> scratch;

	c.clear();

	// Divide num by den, put quotient at quot and remainder at rem
	// The most significant limb of the divisor must be non-zero
	// mpn_tdiv_qr(mp_limb_t *quot, mp_limb_t *rem, 0, const mp_limb_t *num, mp_size_t n_num, const mp_limb_t *den, mp_size_t n_den)
	for (int i = b.numberLimbs(); i > 0; --i)
	{
		if (b.data()[i-1])
		{
			mpn_tdiv_qr(c.data(), scratch.data(), 0, a.data(), a.numberLimbs(), b.data(), i);
			return true;
		}
	}

	return false;	// divide by zero error
}

template <mp_size_t N>
snarklib::BigInt<N> operator/ (const snarklib::BigInt<N>& a,
                               const snarklib::BigInt<N>& b)
{
    snarklib::BigInt<N> c;
    const bool ok = divBigInt(a, b, c);
#ifdef USE_ASSERT
    CCASSERT(ok);
#endif
	if (!ok) c = a.data()[0] / b.data()[0];	// throw divide by zero exception
    return c;
}

template <mp_size_t N>
snarklib::BigInt<N> operator/ (const snarklib::BigInt<N>& n, const mp_limb_t& d)
{
    snarklib::BigInt<N> q;
	mp_limb_t rem;
	mpn_tdiv_qr(q.data(), &rem, 0, n.data(), N, &d, 1);
	return q;
}

template <mp_size_t N>
mp_limb_t operator% (const snarklib::BigInt<N>& n, const mp_limb_t& d)
{
    snarklib::BigInt<N> q;
	mp_limb_t rem;
	mpn_tdiv_qr(q.data(), &rem, 0, n.data(), N, &d, 1);
	return rem;
}

template <mp_size_t N>
int cmpBigInt (const snarklib::BigInt<N>& a,
                               const snarklib::BigInt<N>& b)
{
	for (int i = N - 1; i >= 0; --i)
	{
		if (a.data()[i] > b.data()[i])
			return 1;
		if (a.data()[i] < b.data()[i])
			return -1;
	}

	return 0;
}

template <mp_size_t N>
bool operator< (const snarklib::BigInt<N>& a,
                               const snarklib::BigInt<N>& b)
{
	return mpn_cmp(a.data(), b.data(), N) < 0;
	//return cmpBigInt(a,b) < 0;
}

template <mp_size_t N>
bool operator<= (const snarklib::BigInt<N>& a,
                               const snarklib::BigInt<N>& b)
{
	return mpn_cmp(a.data(), b.data(), N) <= 0;
	//return cmpBigInt(a,b) <= 0;
}

template <mp_size_t N>
bool operator> (const snarklib::BigInt<N>& a,
                               const snarklib::BigInt<N>& b)
{
	return mpn_cmp(a.data(), b.data(), N) > 0;
	//return cmpBigInt(a,b) > 0;
}

template <mp_size_t N>
bool operator>= (const snarklib::BigInt<N>& a,
                               const snarklib::BigInt<N>& b)
{
	return mpn_cmp(a.data(), b.data(), N) >= 0;
	//return cmpBigInt(a,b) >= 0;
}

//
// Russian peasant algorithm
//
template <mp_size_t N>
snarklib::BigInt<N> powerBigInt(const std::size_t exponent)
{
    // set lower bits
    auto mask = exponent;
    for (std::size_t i = 1; i <= (sizeof(std::size_t) * CHAR_BIT) / 2; i *= 2) {
        mask |= (mask >> i);
    }

    mask &= ~(mask >> 1); // most significant bit

    const snarklib::BigInt<N> ONE(1);

    auto accum = snarklib::BigInt<N>::zero();

    while (mask) {
        accum = accum + accum;

        if (exponent & mask) {
            accum = accum + ONE;
        }

        mask >>= 1;
    }

    return accum;
}

} // namespace snarkfront

#endif
