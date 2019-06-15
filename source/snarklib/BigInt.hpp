#ifndef _SNARKLIB_BIG_INT_HPP_
#define _SNARKLIB_BIG_INT_HPP_

#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cctype>
#include <functional>
#include <gmp.h>
#include <iostream>
#include <istream>
#include <memory>
#include <ostream>
#include <random>
#include <string>
#include <vector>

#include <snarklib/AsmMacros.hpp>
#include <snarklib/Util.hpp>

#include <CCcrypto.hpp>

namespace snarklib {

////////////////////////////////////////////////////////////////////////////////
// BigInt
//
// Wrapper around GMP big integers
//

template <mp_size_t N>
class BigInt
{
public:
    static constexpr mp_size_t numberLimbs() {
        return N;
    }

    // default is zero
    BigInt() {
        clear(); // GMP data must be zeroed before use
    }

    // long
    explicit BigInt(const long a)
        : BigInt{}
    {
        m_data[0] = a;

		CCASSERT(a >= 0);	// the algorithm below won't work right with BIGINT_MOD

		if (a < 0)
		{
	        m_data[0] = -(mp_limb_t)(-a);

			for (unsigned i = 1; i < N; ++i)
				m_data[i] = (mp_limb_t)(-1);
		};
    }

    // uint16_t
    BigInt(const uint16_t a)
        : BigInt{}
    {
        m_data[0] = a;
    }

    // unsigned
    BigInt(const unsigned a)
        : BigInt{}
    {
        m_data[0] = a;
    }

    // unsigned long
    BigInt(const unsigned long a)
        : BigInt{}
    {
        m_data[0] = a;
    }

#if ((ULONG_MAX >> 63) == 0)
    BigInt(const uint64_t a)
        : BigInt{}
    {
        m_data[0] = a;
    }
#endif

#if 1 //defined(__int128_t)
    BigInt(const __int128_t a)
        : BigInt{}
    {
		CCASSERT(sizeof(m_data[0]) * CHAR_BIT == 64);

        m_data[0] = a;
        m_data[1] = a >> 64;
    }
#endif

    // string (decimal number)
    explicit BigInt(const std::string& base10)
        : BigInt{}
    {
        std::vector<unsigned char> v;
        v.reserve(base10.size());

        for (const auto& c : base10) {
#ifdef USE_ASSERT
            CCASSERT(isdigit(c));
#endif
            v.push_back(c - '0');
        }

        const mp_size_t limbsWritten = mpn_set_str(data(),
                                                   std::addressof(v[0]),
                                                   v.size(),
                                                   10);

		(void)limbsWritten;	// avoid unused variable warning
#ifdef USE_ASSERT
        CCASSERT(limbsWritten <= N);
#endif
    }

    // C-string
    explicit BigInt(const char* base10)
        : BigInt{std::string(base10)}
    {}

#if 0 // this function won't work under mingw because mpz_get_ui() returns a 32 bit value instead of 64 bits; rather than fixing it, it is disabled
    // GMP integer
    explicit BigInt(const mpz_t a) {
        mpz_t k;
        mpz_init_set(k, a);

        for (auto& r : m_data) {
            r = mpz_get_ui(k);

            mpz_fdiv_q_2exp(k,
                            k,
                            GMP_NUMB_BITS);
        }

#ifdef USE_ASSERT
        CCASSERT(0 == mpz_sgn(k));
#endif
        mpz_clear(k);
    }
#endif

    BigInt<N>& operator= (const BigInt<N>& other) = default;

    BigInt<N>& operator= (const unsigned long a) {
        return *this = BigInt<N>(a);
    }

#if ((ULONG_MAX >> 63) == 0)
    BigInt<N>& operator= (const uint64_t a) {
        return *this = BigInt<N>(a);
    }
#endif

    BigInt<N>& operator= (const std::string& s) {
        return *this = BigInt<N>(s);
    }

    BigInt<N>& operator= (const char* s) {
        return *this = BigInt<N>(s);
    }

    bool operator== (const BigInt<N>& other) const {
        return 0 == mpn_cmp(data(),
                            other.data(),
                            N);
    }

    bool operator!= (const BigInt<N>& other) const {
        return ! operator== (other);
    }

    // used by multiExp() for a max-heap
    bool multiExp_lessthan(const BigInt<N>& other) const {
// this asm code doesn't work in the general case
#if defined(__x86_64__) && defined(USE_ASM)
        if (3 == N)
        {
            long res;
            __asm__
                ("// check for overflow           \n\t"
                 "mov $0, %[res]                  \n\t"
                 ADD_CMP(16)
                 ADD_CMP(8)
                 ADD_CMP(0)
                 "jmp done%=                      \n\t"
                 "subtract%=:                     \n\t"
                 "mov $1, %[res]                  \n\t"
                 "done%=:                         \n\t"
                 : [res] "=&r" (res)
                 : [A] "r" (other.data()), [mod] "r" (data())
                 : "cc", "%rax");
            return res;
        }
        else if (4 == N)
        {
            long res;
            __asm__
                ("// check for overflow           \n\t"
                 "mov $0, %[res]                  \n\t"
                 ADD_CMP(24)
                 ADD_CMP(16)
                 ADD_CMP(8)
                 ADD_CMP(0)
                 "jmp done%=                      \n\t"
                 "subtract%=:                     \n\t"
                 "mov $1, %[res]                  \n\t"
                 "done%=:                         \n\t"
                 : [res] "=&r" (res)
                 : [A] "r" (other.data()), [mod] "r" (data())
                 : "cc", "%rax");
            return res;
        }
        else if (5 == N)
        {
            long res;
            __asm__
                ("// check for overflow           \n\t"
                 "mov $0, %[res]                  \n\t"
                 ADD_CMP(32)
                 ADD_CMP(24)
                 ADD_CMP(16)
                 ADD_CMP(8)
                 ADD_CMP(0)
                 "jmp done%=                      \n\t"
                 "subtract%=:                     \n\t"
                 "mov $1, %[res]                  \n\t"
                 "done%=:                         \n\t"
                 : [res] "=&r" (res)
                 : [A] "r" (other.data()), [mod] "r" (data())
                 : "cc", "%rax");
            return res;
        }
        else
#endif
        {
            return *this < other;
        }
    }

#if 0 // use comparison operators in BigIntOps.hpp instead
    bool operator< (const BigInt<N>& other) const {
            return 0 > mpn_cmp(data(),
                               other.data(),
                               N);
    }
#endif

    void clear() {
        mpn_zero(data(), N);
    }

    bool isZero() const {
        for (const auto& r : m_data) {
            if (0 != r)
                return false;
        }

        return true;
    }

    explicit operator bool() const {
        return ! isZero();
    }

    static constexpr std::size_t maxBits() {
        return N * GMP_NUMB_BITS;
    }

    std::size_t numBits() const {
        for (int i = N - 1; i >= 0; --i) {
            const mp_limb_t x = m_data[i];

            if (0 != x)
                return ((i + 1) * GMP_NUMB_BITS) - __builtin_clzll(x);
        }

        return 0;
    }

    // convert to unsigned long
    unsigned long asUnsignedLong() const {
        return m_data[0];
    }

    // convert to GMP integer
    void toMPZ(mpz_t a) const {
        mpz_set_ui(a, 0);

        for (int i = N - 1; i >= 0; --i) {
			if (sizeof(long)*8 == GMP_NUMB_BITS/2)
			{
				mpz_mul_2exp(a,
							 a,
							 GMP_NUMB_BITS/2);

				mpz_add_ui(a,
						   a,
						   m_data[i] >> GMP_NUMB_BITS/2);

				mpz_mul_2exp(a,
							 a,
							 GMP_NUMB_BITS/2);

				mpz_add_ui(a,
						   a,
						   m_data[i] - ((m_data[i] >> GMP_NUMB_BITS/2) << GMP_NUMB_BITS/2) );
			}
			else
			{
				CCASSERT(sizeof(long)*8 >= GMP_NUMB_BITS);

            mpz_mul_2exp(a,
                         a,
                         GMP_NUMB_BITS);

            mpz_add_ui(a,
                       a,
                       m_data[i]);
        }

			//std::cerr << std::hex << std::endl << "toMPZ " << GMP_NUMB_BITS << " " << mp_bits_per_limb << " " << i << " " << m_data[i] << " " << mpz_size(a) << " " << a << " " << mpz_getlimbn(a,0) << std::dec;
        }

		//std::cerr << std::endl;
    }

    bool testBit(const std::size_t i) const {
        if (i >= N * GMP_NUMB_BITS) {
            return false;

        } else {
            const std::size_t part = i / GMP_NUMB_BITS;
            const std::size_t bit = i - (GMP_NUMB_BITS * part);

            return m_data[part] & (((uint64_t)1) << bit);
        }
    }

    void clearBit(const std::size_t i) {
        const std::size_t part = i / GMP_NUMB_BITS;
        const std::size_t bit = i - (GMP_NUMB_BITS * part);

        m_data[part] &= ~(((uint64_t)1) << bit);
    }

    BigInt<N>& randomize() {
#if 0	// for testing
	static unsigned count = 0;
	for (unsigned i = 0; i < N*sizeof(mp_limb_t); ++i)
		*((unsigned char *)m_data.data() + i) = ++count;
	return *this;
#elif 1
	CCRandom(m_data.data(), N*sizeof(mp_limb_t));
	return *this;
#else
#error using std::random_device
        std::random_device rd; // uses /dev/urandom

        return randomize<unsigned int>(
            [&rd] () {
                return rd();
            });
#endif
    }

    template <typename UINT>
    BigInt<N>& randomize(std::vector<UINT>& v) {
#ifdef USE_ASSERT
        CCASSERT(sizeof(UINT) <= sizeof(mp_limb_t));
#endif

        return randomize<UINT>(
            [&v] () {
                if (v.empty()) {
                    return UINT(0);

                } else {
                    const UINT r = v.back();
                    v.resize(v.size() - 1);
                    return r;
                }
            });
    }

    static BigInt<N> zero() {
        return BigInt<N>(0ul);
    }

    static BigInt<N> one() {
        return BigInt<N>(1ul);
    }

    static BigInt<N> random() {
        BigInt<N> a;
        return a.randomize();
    }

    template <typename T>
    static BigInt<N> random(std::vector<T>& v) {
        BigInt<N> a;
        return a.randomize(v);
    }

    mp_limb_t* data() {
        return m_data.data();
    }

    const mp_limb_t* data() const {
        return m_data.data();
    }

    void marshal_out(std::ostream& os, const bool use_endl = true) const {
        mpz_t t;
        mpz_init(t);
        toMPZ(t);

#if 0 // this causes libgmpxx.dll to get linked in with stdc++ dll dependencies--so let's avoid it
        os << t;
#else
		static const int buflen = 80;
		char buf[buflen];
		buf[buflen-1] = 0;
		if (os.flags() & std::ios::hex)
			gmp_sprintf(buf, "%Zx", t);
		else
			gmp_sprintf(buf, "%Zd", t);
		CCASSERTZ(buf[buflen-1]);
		os << buf;
#endif

        if (use_endl) os << std::endl;

        mpz_clear(t);
    }

    bool marshal_in(std::istream& is) {
        std::string s;
        is >> s;
        if (!is) return false;

        *this = s;

        return true; // ok
    }

    // raw format is little-endian
    void marshal_out_raw(std::ostream& os) const {
        const char *ptr = reinterpret_cast<const char*>(m_data.data());

        if (is_big_endian<int>()) {

            // big-endian
            for (std::size_t i = 0; i < N; ++i) {
                for (int j = sizeof(mp_limb_t) - 1; j >= 0; --j) {
                    os.put(ptr[i * sizeof(mp_limb_t) + j]);
                }
            }

        } else {
            // little-endian
            os.write(ptr, sizeof(m_data));
        }
    }

    // raw format is little-endian
    bool marshal_in_raw(std::istream& is) {
        char *ptr = reinterpret_cast<char*>(m_data.data());

        // endianness test
        if (is_big_endian<int>()) {

            // big-endian
            for (std::size_t i = 0; i < N; ++i) {
                for (int j = sizeof(mp_limb_t) - 1; j >= 0; --j) {
                    if (! is.get(ptr[i * sizeof(mp_limb_t) + j]))
                        return false;
                }
            }

            return true; // ok

        } else {
            // little-endian
            return !!is.read(ptr, sizeof(m_data));
        }
    }

private:
    template <typename T>
    BigInt<N>& randomize(std::function<T ()> func) {
#ifdef USE_ASSERT
        CCASSERT(GMP_NUMB_BITS == sizeof(mp_limb_t) * CHAR_BIT);
#endif

        const std::size_t n = sizeof(mp_limb_t) / sizeof(T);

        for (auto& r : m_data) {
            for (std::size_t i = 0; i < n; ++i) {
                r <<= n * CHAR_BIT;
                r |= func();
            }
        }

        return *this;
    }

    std::array<mp_limb_t, N> m_data;
};

////////////////////////////////////////////////////////////////////////////////
// Operator functions
//

// print to stream
template <mp_size_t N>
std::ostream& operator<< (std::ostream& os, const BigInt<N>& a) {
    a.marshal_out(os, false);
    return os;
}

// extract from stream
template <mp_size_t N>
std::istream& operator>> (std::istream& is, BigInt<N>& a) {
    a.marshal_in(is);
    return is;
}

// Russian peasant algorithm (field exponentiation)
// for fields, exponent follows base
template <typename T, mp_size_t N>
T power(const T& base, const BigInt<N>& exponent) {
    T result = T::one(); // multiplicative identity
    bool foundOne = false;

    for (long i = exponent.maxBits() - 1; i >= 0; --i) {
        if (foundOne) {
            result = result * result;
        }

        if (exponent.testBit(i)) {
            foundOne = true;
            result = result * base;
        }
    }

    return result;
}

// Russian peasant algorithm (group multiplication)
// for groups: base follows exponent
template <typename T, mp_size_t N>
T power(const BigInt<N>& exponent, const T& base) {
    T result = T::zero(); // additive identity
    bool foundOne = false;

    for (long i = exponent.maxBits() - 1; i >= 0; --i) {
        if (foundOne) {
            result = result.dbl();
        }

        if (exponent.testBit(i)) {
            foundOne = true;
            result = result + base;
        }
    }

    return result;
}

// Russian peasant algorithm (for fields)
template <typename T>
T power(const T& base, const unsigned long exponent) {
    return power(base, BigInt<1>(exponent));
}

////////////////////////////////////////////////////////////////////////////////
// wNAF - windowed Non-Adjacent Form (elliptic curve point multiplication)
//

// used for F[(p^3)^2] cyclotomic exponentiation
// called by wnafExp() for group exponentiation
template <mp_size_t N>
std::array<int64_t, N * GMP_NUMB_BITS + 1> // BigInt<B>::maxBits()
find_wNAF(const std::size_t w, const BigInt<N>& exponent)
{
    std::array<int64_t, N * GMP_NUMB_BITS + 1> res = {0};
    auto c = exponent;
    long j = 0;

    while (! c.isZero()) {
        int64_t u;

        if (1 == (c.data()[0] & 1)) {
            u = c.data()[0] % ((uint64_t)1u << (w + 1));

            if (u > ((int64_t)1 << w)) {
                u = u - ((int64_t)1 << (w + 1));
            }

            if (u > 0) {
                mpn_sub_1(c.data(),
                          c.data(),
                          N,
                          u);

            } else {
                mpn_add_1(c.data(),
                          c.data(),
                          N,
                          -u);
            }

        } else {
            u = 0;
        }

        res[j++] = u;

        // c = c/2
        mpn_rshift(c.data(),
                   c.data(),
                   N,
                   1);
    }

    return res;
}

} // namespace snarklib

#endif
