#ifndef _SNARKFRONT_RANK_1_OPS_HPP_
#define _SNARKFRONT_RANK_1_OPS_HPP_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

#include <snarklib/Rank1DSL.hpp>

#include <snarkfront/PowersOf2.hpp>
#include <snarkfront/TLsingleton.hpp>

namespace snarkfront {

////////////////////////////////////////////////////////////////////////////////
// variable consistency, enforce valid values
//

// constrain rank-1 variable to 0 and 1 values
template <template <typename> class SYS, typename FR>
void rank1_booleanity(SYS<FR>& S,
                      const snarklib::R1Variable<FR>& x)
{
#ifdef USE_ASSERT
    CCASSERT(! x.zeroIndex());
#endif
    S.addConstraint(x * (FR::one() - x) == FR::zero()); // only roots are 0 and 1
}

template <template <typename> class SYS, typename FR>
void rank1_booleanity(SYS<FR>& S,
                      const snarklib::R1Term<FR>& x)
{
#ifdef USE_ASSERT
    CCASSERT(x.isVariable());
#endif
    rank1_booleanity(S, x.var());
}

template <template <typename> class SYS, typename FR>
void rank1_booleanity(SYS<FR>& S,
                      const std::vector<snarklib::R1Term<FR>>& x)
{
    for (const auto& a : x)
        rank1_booleanity(S, a);
}

// constrain between a scalar in [0, 2^k) and representation as k bits
template <template <typename> class SYS, typename FR>
void rank1_split(SYS<FR>& S,
                 const snarklib::R1Term<FR>& x,
                 const std::vector<snarklib::R1Term<FR>>& b)
{
#ifdef USE_ASSERT
    CCASSERT(x.isVariable());
#endif

    snarklib::R1Combination<FR> LC;
    LC.reserveTerms(b.size());

    for (std::size_t i = 0; i < b.size(); ++i) {
        if (! b[i].zeroTerm())
            LC.addTerm(
                TL<PowersOf2<FR>>::singleton()->lookUp(i) * b[i]);
    }

    S.addConstraint(LC == x);
}

////////////////////////////////////////////////////////////////////////////////
// operators
//

#define DEFN_R1OP(NAME, XYZ)                            \
template <typename FR>                                  \
class R1_ ## NAME                                       \
{                                                       \
public:                                                 \
    typedef FR FieldType;                               \
    static snarklib::R1Constraint<FR> constraint(       \
        const snarklib::R1Term<FR>& x,                  \
        const snarklib::R1Term<FR>& y,                  \
        const snarklib::R1Term<FR>& z) {                \
        return XYZ ;                                    \
    }                                                   \
};

// AND, OR, XOR, SAME, CMPLMNT
DEFN_R1OP(AND, x * y == z)
DEFN_R1OP(OR, x + y - z == x * y)
DEFN_R1OP(XOR, x + y - z == ((FR::one() + FR::one()) * x) * y)
DEFN_R1OP(SAME, x + y + z - FR::one() == ((FR::one() + FR::one()) * x) * y)
DEFN_R1OP(CMPLMNT, x + z == FR::one())

// ADD, SUB, MUL
DEFN_R1OP(ADD, x + y == z)
DEFN_R1OP(SUB, x - y == z)
DEFN_R1OP(MUL, x * y == z)

// INV
DEFN_R1OP(INV, x * z == FR::one())

#undef DEFN_R1OP

////////////////////////////////////////////////////////////////////////////////
// function to apply operators
//

template <template <typename> class SYS, typename R1OP>
void rank1_op(
    SYS<typename R1OP::FieldType>& S,
    const snarklib::R1Term<typename R1OP::FieldType>& x,
    const snarklib::R1Term<typename R1OP::FieldType>& y,
    const snarklib::R1Term<typename R1OP::FieldType>& z)
{
    S.addConstraint(R1OP::constraint(x, y, z));
}

////////////////////////////////////////////////////////////////////////////////
// bit shift and rotate
//

template <typename FR>
void rank1_shiftleft(std::vector<snarklib::R1Term<FR>>& x,
                     const std::size_t n)
{
#ifdef USE_ASSERT
    CCASSERT(! x.empty());
#endif
    if (0 == n) return; // do nothing

    const auto N = n % x.size();

    for (std::size_t i = x.size() - 1; i >= N; --i) {
        x[i] = x[i - N];
    }

    for (std::size_t i = 0; i < N; ++i) {
        x[i] = snarklib::R1Term<FR>();
    }
}

template <typename FR>
void rank1_shiftright(std::vector<snarklib::R1Term<FR>>& x,
                      const std::size_t n)
{
#ifdef USE_ASSERT
    CCASSERT(! x.empty());
#endif
    if (0 == n) return; // do nothing

    const auto N = n % x.size();

    for (std::size_t i = 0; i < x.size() - N; ++i) {
        x[i] = x[i + N];
    }

    for (std::size_t i = x.size() - N; i < x.size(); ++i) {
        x[i] = snarklib::R1Term<FR>();
    }
}

template <typename FR>
void rank1_rotateleft(std::vector<snarklib::R1Term<FR>>& x,
                      const std::size_t n)
{
#ifdef USE_ASSERT
    CCASSERT(! x.empty());
#endif
    if (0 == n) return; // do nothing

    const auto N = n % x.size();

    std::vector<snarklib::R1Term<FR>> v(x.size());

    for (std::size_t i = 0; i < x.size(); ++i) {
        v[(i + N) % x.size()] = x[i];
    }

    x = v;
}

template <typename FR>
void rank1_rotateright(std::vector<snarklib::R1Term<FR>>& x,
                       const std::size_t n)
{
#ifdef USE_ASSERT
    CCASSERT(! x.empty());
#endif
    if (0 == n) return; // do nothing

    const auto N = n % x.size();

    std::vector<snarklib::R1Term<FR>> v(x.size());

    for (std::size_t i = 0; i < x.size(); ++i) {
        v[i] = x[(i + N) % x.size()];
    }

    x = v;
}

////////////////////////////////////////////////////////////////////////////////
// bitwise conversion between unsigned integers and bool
//

template <typename FR>
std::vector<snarklib::R1Term<FR>>
rank1_xword(
    const std::vector<snarklib::R1Term<FR>>& x,
    const std::size_t returnSize)
{
    std::vector<snarklib::R1Term<FR>> v(returnSize, FR::zero());

    if (1 == x.size()) {
        // source is bool

        if (returnSize <= 64) {
            // convert bool to 8-bit, 32-bit, or 64-bit word as bitmask
            for (std::size_t i = 0; i < returnSize; ++i)
                v[i] = x[0];

        } else if (128 == returnSize) {
            // convert bool to 128-bit as integer value
            v[0] = x[0];

        } else {
            // convert bool to FR::zero() or FR::one()
            const auto ONE = valueBits(FR::one());
            for (std::size_t i = 0; i < ONE.size(); ++i) {
                if (ONE[i]) v[i] = x[0];
            }
        }

    } else {
#ifdef USE_ASSERT
        // source and destination are not finite scalar field
        CCASSERT(sizeBits(FR::zero()) != x.size());
        CCASSERT(sizeBits(FR::zero()) != returnSize);
#endif

        // source is unsigned integer: 8-bit, 32-bit, 64-bit, 128-bit

        // if destination type is bool, returnSize is 1 so takes 0th bit
        // otherwise, bitwise slices between unsigned integer types
        const auto N = std::min(returnSize, x.size());
        for (std::size_t i = 0; i < N; ++i)
            v[i] = x[i];
    }

    return v;
}

} // namespace snarkfront

#endif
