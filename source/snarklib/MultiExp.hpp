#ifndef _SNARKLIB_MULTI_EXP_HPP_
#define _SNARKLIB_MULTI_EXP_HPP_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <gmp.h>
#include <vector>
#include <sched.h>

#include <snarklib/AuxSTL.hpp>
#include <snarklib/BigInt.hpp>
#include <snarklib/ProgressCallback.hpp>

extern int g_multiExp_nthreads;	// global to set number of multiExp threads
extern int g_multiExp_nice;		// global to set multiExp thread priority
void set_nice(int nice);

namespace snarklib {

////////////////////////////////////////////////////////////////////////////////
// multi-exponentiation
//

// wNAF exponentiation (windowed non-adjacent form)
template <mp_size_t N, typename T>
T wnafExp(const BigInt<N>& scalar,
          const T& base)
{
/* FIXME - this wNAF code requires intractably large amounts of memory
 *
 *                             BN128          Edwards
 *
 * How often used              50%            60%
 * minimum scalarBits          11             9
 * maximum scalarBits          198            177
 * (from 300 trials for each elliptic curve)
 *
 * The vector table of group elements will often be so large as to
 * exceed addressable memory. This causes the heap allocator to fail
 * and the process to abort. More intelligent algorithm memoization
 * is required.
 *
 * The code is left here (instead of removed) in case there are more
 * ideas which can fix its problems at a future time.
 *

    const std::size_t scalarBits = scalar.numBits();

    for (long i = T::params.wnaf_window_table().size() - 1; i >= 0; --i) {
        if (scalarBits >= T::params.wnaf_window_table()[i])
        {
            const auto NAF = find_wNAF(i + 1, scalar);

            // this table can be huge
            std::vector<T> table(1u << (scalarBits - 1));

            auto tmp = base;
            const auto dbl = base.dbl();
            for (std::size_t i = 0; i < table.size(); ++i) {
                table[i] = tmp;
                tmp = tmp + dbl;
            }

            auto res = T::zero();

            bool found_nonzero = false;
            for (long i = NAF.size() - 1; i >= 0; --i) {
                if (found_nonzero) {
                    res = res.dbl();
                }

                if (NAF[i] != 0) {
                    found_nonzero = true;
                    if (NAF[i] > 0) {
                        res = res + table[NAF[i] / 2];
                    } else {
                        res = res - table[(-NAF[i]) / 2];
                    }
                }
            }

            return res;
        }
    }
*/

    return scalar * base;
}

template <typename T, typename F>
void multiExpThread(std::vector<T>& baseVec,
           const std::vector<OrdPair<BigInt<F::BaseType::numberLimbs()>, std::size_t>>& scalarQ,
	       unsigned offset,
	       unsigned nelem,
	       T& res,
	       int nice,
           ProgressCallback* callback = nullptr)
{
#ifdef USE_ASSERT
    CCASSERT(offset < scalarQ.size());
    CCASSERT(offset + nelem <= scalarQ.size());
#endif

	set_nice(nice);

    const mp_size_t N = F::BaseType::numberLimbs();
    typedef OrdPair<BigInt<N>, std::size_t> ScalarIndex;

    PriorityQueue<ScalarIndex> scalarPQ(nelem);

    for (std::size_t i = offset; i < offset + nelem; ++i)
        scalarPQ.push(scalarQ[i]);

    res = T::zero();

    while (! scalarPQ.empty() &&
           ! scalarPQ.top().key.isZero())
    {
        auto a = scalarPQ.top();
        scalarPQ.pop();

        const auto& b = scalarPQ.top();

        bool reweight = false;

        if (! scalarPQ.empty()) {
            const std::size_t
                abits = a.key.numBits(),
                bbits = b.key.numBits();

            reweight = (bbits >= (((uint64_t)1) << std::min((std::size_t)(20), abits - bbits)));
			//std::cerr << "multiExp sizeof(int) " <<  sizeof(int) << " sizeof(bbits) " << sizeof(bbits) << " bbits " << bbits << " abits " << abits << " min(20,abits-bbits) " << std::min((std::size_t)(20), abits - bbits) << " 1<< " << (((uint64_t)1) << std::min((std::size_t)(20), abits - bbits)) << " reweight " << reweight << std::endl;
        }

        // reweighting is both optimization and avoids overflow, the
        // reduction is likely to fail without it
        if (reweight) {
            // xA + yB = xA - yA + yB + yA = (x - y)A + y(B + A)
            mpn_sub_n(a.key.data(), a.key.data(), b.key.data(), N);
            baseVec[b.value] = baseVec[b.value] + baseVec[a.value];

            scalarPQ.push(a);
                //ScalarIndex(a.key, a.value));
        } else {
			//std::cout << "wnafExp index " << a.value << " value " << a.key << std::endl;
            res = res + wnafExp(a.key, baseVec[a.value]);
        }
    }

    return;
}

// calculates sum(scalar[i] * base[i])
template <typename T, typename F>
T multiExp(const std::vector<T>& base,
           const std::vector<F>& scalar,
           ProgressCallback* callback = nullptr,
	       int nthreads = 0,
	       int nice = 0)
{
    const std::size_t M = callback ? callback->minorSteps() : 0;
    std::size_t callbackCount = 0;

	//std::cout << "multiExp base.size() " << base.size() << " scalar.size() " << scalar.size() << std::endl;

#ifdef USE_ASSERT
    CCASSERT(base.size() == scalar.size());
#endif

    if (base.empty()) {
        // final callbacks
        for (std::size_t i = callbackCount; i < M; ++i)
            callback->minorProgress();

        return T::zero();
    }

    if (1 == base.size()) {
        // final callbacks
        for (std::size_t i = callbackCount; i < M; ++i)
            callback->minorProgress();

        return scalar[0][0] * base[0];
    }

	const mp_size_t N = F::BaseType::numberLimbs();
	typedef OrdPair<BigInt<N>, std::size_t> ScalarIndex;

    std::vector<T> baseVec(base);
	std::vector<ScalarIndex> scalarQ;
	auto qsize = scalar.size();
	scalarQ.reserve(qsize);

	// for now, override these parameter values with a globals--easier than modifying the code to pass down the params
	nthreads = g_multiExp_nthreads;
	nice = g_multiExp_nice;

#define MAX_EXP_NTHREADS	16

	int n = std::thread::hardware_concurrency();
	if (n < 1)
		n = MAX_EXP_NTHREADS;

	if (nthreads < 0)
		n -= nthreads;
	else if (nthreads > 0 && n > nthreads)
		n = nthreads;

//#define TEST_RAND_EXP_NTHREADS	1	// for testing

#ifndef TEST_RAND_EXP_NTHREADS
#define TEST_RAND_EXP_NTHREADS	0	// don'test
#endif

	if (TEST_RAND_EXP_NTHREADS)
		n = rand() % MAX_EXP_NTHREADS;

	//n = 4;	// for testing

	const int min_size = 100;	// guesstimate value to maximize speed
	int max_n = (qsize + min_size - 1) / min_size;
	if (n > max_n)
		n = max_n;

	if (n < 1)
		n = 1;
	if (n > MAX_EXP_NTHREADS)
		n = MAX_EXP_NTHREADS;

	if (n == 1)
	{
		for (std::size_t i = 0; i < qsize; ++i) {
			scalarQ.push_back(
				ScalarIndex(scalar[i][0].asBigInt(), i));
		}
	}
	else
	{
		PriorityQueue<ScalarIndex> scalarPQ(qsize);

		for (std::size_t i = 0; i < qsize; ++i) {
			scalarPQ.push(
				ScalarIndex(scalar[i][0].asBigInt(), i));
		}

		while (! scalarPQ.empty() &&
			   ! scalarPQ.top().key.isZero())
		{
			auto a = scalarPQ.top();
			scalarPQ.pop();
			scalarQ.push_back(a);
		}

		qsize = scalarQ.size();

		int max_n = (qsize + min_size - 1) / min_size;
		if (n > max_n)
			n = max_n;

		if (n < 1)
			n = 1;
		if (n > MAX_EXP_NTHREADS)
			n = MAX_EXP_NTHREADS;
	}

	//std::cout << "multiExp nthreads " << n << std::endl;

	auto res = T::zero();

	if (n == 1)
	{
		multiExpThread<T, F>(baseVec, scalarQ, 0, qsize, res, 0, callback);	// doesn't create a new thread, so use nice = zero
	}
	else
	{
		std::array<std::thread, MAX_EXP_NTHREADS> threads;
		std::array<T, MAX_EXP_NTHREADS> parts;

		unsigned nt = 0;
		unsigned offset = 0;
		unsigned nelem = (qsize + n - 1) / n;

		for (unsigned i = 0; i < (unsigned)n && offset < qsize; ++i)
		{
			//std::cout << "multiExp nthreads " << n << " iteration " << i << " offset " << offset << " nelem " << nelem << " offset+nelem " << offset+nelem << " qsize " << qsize << std::endl;

			std::thread	t(multiExpThread<T, F>, std::ref(baseVec), std::ref(scalarQ), offset, nelem, std::ref(parts[i]), nice, callback);
			threads[i] = std::move(t);
			++nt;

			offset += nelem;
			if (offset + nelem > qsize)
				nelem = qsize - offset;
		}

		CCASSERT(offset == qsize);

		for (unsigned i = 0; i < nt; ++i)
			threads[i].join();

		for (unsigned i = 0; i < nt; ++i)
			res = res + parts[i];
	}

    // final callbacks
    for (std::size_t i = callbackCount; i < M; ++i)
        callback->minorProgress();

    return res;
}

// sum of multi-exponentiation when scalar vector has many zeros and ones
template <template <typename> class VEC, typename T, typename F>
T multiExp01(const VEC<T>& base,
             const std::size_t startOffset,
             const std::size_t indexShift,
             const std::vector<F>& scalar,
             const std::size_t reserveCount, // for performance tuning
             ProgressCallback* callback)
{
    const auto
        ZERO = F::zero(),
        ONE = F::one();

    std::vector<T> base2;
    std::vector<F> scalar2;
    if (reserveCount) {
        base2.reserve(reserveCount);
        scalar2.reserve(reserveCount);
    }

    auto accum = T::zero();

    for (std::size_t i = vector_start(base) + startOffset; i < vector_stop(base); ++i) {
        const auto& a = scalar[i - indexShift];

        if (ZERO == a) {
            continue;

        } else if (ONE == a) {
#ifdef USE_ADD_SPECIAL
            accum = fastAddSpecial(accum, base[i]);
#else
            accum = accum + base[i];
#endif

        } else {
            base2.emplace_back(base[i]);
            scalar2.emplace_back(a);
        }
    }

    return accum + multiExp(base2, scalar2, callback);
}

// sum of multi-exponentiation when scalar vector has many zeros and ones
// (g++ 4.8.3 template argument deduction/substitution fails for VEC)
template <typename T, typename F>
T multiExp01(const std::vector<T>& base,     // VEC is std::vector
             const std::size_t startOffset,
             const std::size_t indexShift,
             const std::vector<F>& scalar,
             const std::size_t reserveCount, // for performance tuning
             ProgressCallback* callback)
{
    const auto
        ZERO = F::zero(),
        ONE = F::one();

    std::vector<T> base2;
    std::vector<F> scalar2;
    if (reserveCount) {
        base2.reserve(reserveCount);
        scalar2.reserve(reserveCount);
    }

    auto accum = T::zero();

    for (std::size_t i = vector_start(base) + startOffset; i < vector_stop(base); ++i) {
        const auto& a = scalar[i - indexShift];

        if (ZERO == a) {
            continue;

        } else if (ONE == a) {
#ifdef USE_ADD_SPECIAL
            accum = fastAddSpecial(accum, base[i]);
#else
            accum = accum + base[i];
#endif

        } else {
            base2.emplace_back(base[i]);
            scalar2.emplace_back(a);
        }
    }

    return accum + multiExp(base2, scalar2, callback);
}

} // namespace snarklib

#endif
