#ifndef _SNARKLIB_PAIRING_HPP_
#define _SNARKLIB_PAIRING_HPP_

#include <cstdint>
#include <gmp.h>
#include <istream>
#include <ostream>
#include <vector>

#include <snarklib/AuxSTL.hpp>
#include <snarklib/BigInt.hpp>
#include <snarklib/Group.hpp>
#include <snarklib/ProgressCallback.hpp>
#include <snarklib/WindowExp.hpp>

namespace snarklib {

////////////////////////////////////////////////////////////////////////////////
// Paired group knowledge commitment
//

template <typename GA, typename GB>
class Pairing
{
public:
    Pairing() = default;

    Pairing(const GA& a, const GB& b)
        : m_G(a), m_H(b)
    {}

    // breaks encapsulation
    const GA& G() const { return m_G; }
    const GB& H() const { return m_H; }

    bool operator== (const Pairing& other) const {
        return m_G == other.m_G && m_H == other.m_H;
    }

    bool operator!= (const Pairing& other) const {
        return ! (*this == other);
    }

    Pairing operator- () const {
        return Pairing<GA, GB>(G(), -H());
    }

    void toSpecial() {
        m_G.toSpecial();
        m_H.toSpecial();
    }

    bool isSpecial() const {
        return G().isSpecial() && H().isSpecial();
    }

    bool isZero() const {
        return G().isZero() && H().isZero();
    }

    static Pairing zero() {
        return Pairing<GA, GB>(GA::zero(), GB::zero());
    }

    static Pairing one() {
        return Pairing<GA, GB>(GA::one(), GB::one());
    }

    void marshal_out(std::ostream& os) const {
        G().marshal_out(os);
        H().marshal_out(os);
    }

    bool marshal_in(std::istream& is) {
        return
            m_G.marshal_in(is) &&
            m_H.marshal_in(is);
    }

    void marshal_out_raw(std::ostream& os) const {
        G().marshal_out_raw(os);
        H().marshal_out_raw(os);
    }

    bool marshal_in_raw(std::istream& is) {
        return
            m_G.marshal_in_raw(is) &&
            m_H.marshal_in_raw(is);
    }

    // affine representation
    void marshal_out_special(std::ostream& os) const {
        G().marshal_out_special(os);
        H().marshal_out_special(os);
    }

    // affine representation
    bool marshal_in_special(std::istream& is) {
        return
            m_G.marshal_in_special(is) &&
            m_H.marshal_in_special(is);
    }

    // affine representation with raw data
    void marshal_out_rawspecial(std::ostream& os) const {
        G().marshal_out_rawspecial(os);
        H().marshal_out_rawspecial(os);
    }

    // affine representation with raw data
    bool marshal_in_rawspecial(std::istream& is) {
        return
            m_G.marshal_in_rawspecial(is) &&
            m_H.marshal_in_rawspecial(is);
    }

//private:
    GA m_G;
    GB m_H;
};

////////////////////////////////////////////////////////////////////////////////
// Operator functions
//

template <typename GA, typename GB>
Pairing<GA, GB> operator+ (const Pairing<GA, GB>& a, const Pairing<GA, GB>& b) {
    return Pairing<GA, GB>(a.G() + b.G(),
                           a.H() + b.H());
}

template <typename GA, typename GB>
Pairing<GA, GB> operator- (const Pairing<GA, GB>& a, const Pairing<GA, GB>& b) {
    return Pairing<GA, GB>(a.G() - b.G(),
                           a.H() - b.H());
}

template <typename T, typename GA, typename GB>
Pairing<GA, GB> operator* (const T& a, const Pairing<GA, GB>& b) {
    return Pairing<GA, GB>(a * b.G(),
                           a * b.H());
}

template <typename GA, typename GB>
Pairing<GA, GB> fastAddSpecial(const Pairing<GA, GB>& a,
                               const Pairing<GA, GB>& b) {
    return Pairing<GA, GB>(fastAddSpecial(a.G(), b.G()),
                           fastAddSpecial(a.H(), b.H()));
}

template <typename GA, typename GB>
SparseVector<Pairing<GA, GB>>& batchSpecial(SparseVector<Pairing<GA, GB>>& vec)
{
    std::vector<GA> G_vec(vec.size(), GA::zero());
    std::vector<GB> H_vec(vec.size(), GB::zero());

    for (std::size_t i = 0; i < vec.size(); ++i) {
        G_vec[i] = vec.getElement(i).G();
        H_vec[i] = vec.getElement(i).H();
    }

    batchSpecial(G_vec);
    batchSpecial(H_vec);

    for (std::size_t i = 0; i < vec.size(); ++i) {
        vec.setElement(i,
                       Pairing<GA, GB>(G_vec[i], H_vec[i]));
    }

    return vec;
}

template <mp_size_t N, typename GA, typename GB>
Pairing<GA, GB> wnafExp(const BigInt<N>& scalar,
                        const Pairing<GA, GB>& base)
{
    return Pairing<GA, GB>(wnafExp(scalar, base.G()),
                           wnafExp(scalar, base.H()));
}

template <typename GA, typename GB, typename FR, typename VEC>
SparseVector<Pairing<GA, GB>> batchExp_internal(
    const WindowExp<GA>& tableA,
    const WindowExp<GB>& tableB,
    const FR& coeffA,
    const FR& coeffB,
    const VEC& vec,
    const std::size_t startIdx,
    const std::size_t stopIdx,
    ProgressCallback* callback)
{
    const std::size_t
        M = callback ? callback->minorSteps() : 0,
        N = stopIdx - startIdx;

    SparseVector<Pairing<GA, GB>> res(N, Pairing<GA, GB>::zero());

    std::size_t index = startIdx, idx = 0;

    // full blocks
    for (std::size_t j = 0; j < M; ++j) {
        for (std::size_t k = 0; k < N / M; ++k) {
            if (! vec[index].isZero()) {
                res.setIndexElement(
                    idx++,
                    index,
                    Pairing<GA, GB>(tableA.exp(coeffA * vec[index]),
                                    tableB.exp(coeffB * vec[index])));
            }

            ++index;
        }

        callback->minorProgress();
    }

    // remaining steps smaller than one block
    while (index < stopIdx) {
        if (! vec[index].isZero()) {
            res.setIndexElement(
                idx++,
                index,
                Pairing<GA, GB>(tableA.exp(coeffA * vec[index]),
                                tableB.exp(coeffB * vec[index])));
        }

        ++index;
    }

    res.resize(idx);

    return res;
}

// standard vector, works with map-reduce or monolithic window tables
template <typename GA, typename GB, typename FR>
SparseVector<Pairing<GA, GB>> batchExp(const WindowExp<GA>& tableA,
                                       const WindowExp<GB>& tableB,
                                       const FR& coeffA,
                                       const FR& coeffB,
                                       const std::vector<FR>& vec,
                                       ProgressCallback* callback = nullptr)
{
    return batchExp_internal(tableA,
                             tableB,
                             coeffA,
                             coeffB,
                             vec,
                             0,
                             vec.size(),
                             callback);
}

// block partitioned vector, works with map-reduce or monolithic window table
template <typename GA, typename GB, typename FR>
SparseVector<Pairing<GA, GB>> batchExp(const WindowExp<GA>& tableA,
                                       const WindowExp<GB>& tableB,
                                       const FR& coeffA,
                                       const FR& coeffB,
                                       const BlockVector<FR>& vec,
                                       ProgressCallback* callback = nullptr)
{
    return batchExp_internal(tableA,
                             tableB,
                             coeffA,
                             coeffB,
                             vec,
                             vec.startIndex(),
                             vec.stopIndex(),
                             callback);
}

template <typename GA, typename GB, typename FR, typename VEC>
void batchExp_internal(
    SparseVector<Pairing<GA, GB>>& res, // returned from batchExp()
    const WindowExp<GA>& tableA,
    const WindowExp<GB>& tableB,
    const FR& coeffA,
    const FR& coeffB,
    const VEC& vec,
    ProgressCallback* callback)
{
    const std::size_t M = callback ? callback->minorSteps() : 0;
    const std::size_t N = res.size(); // iterate over sparse vector directly

    std::size_t idx = 0;

    // full blocks
    for (std::size_t j = 0; j < M; ++j) {
        for (std::size_t k = 0; k < N / M; ++k) {
            const auto index = res.getIndex(idx);
            const auto& ga = res.getElement(idx).G();
            const auto& gb = res.getElement(idx).H();

            res.setIndexElement(
                idx++,
                index,
                Pairing<GA, GB>(ga + tableA.exp(coeffA * vec[index]),
                                gb + tableB.exp(coeffB * vec[index])));
        }

        callback->minorProgress();
    }

    // remaining steps smaller than one block
    while (idx < N) {
        const auto index = res.getIndex(idx);
        const auto& ga = res.getElement(idx).G();
        const auto& gb = res.getElement(idx).H();

        res.setIndexElement(
            idx++,
            index,
            Pairing<GA, GB>(ga + tableA.exp(coeffA * vec[index]),
                            gb + tableB.exp(coeffB * vec[index])));
    }
}

// used with map-reduce
template <typename GA, typename GB, typename FR>
void batchExp(SparseVector<Pairing<GA, GB>>& res, // returned from batchExp()
              const WindowExp<GA>& tableA,
              const WindowExp<GB>& tableB,
              const FR& coeffA,
              const FR& coeffB,
              const std::vector<FR>& vec,
              ProgressCallback* callback = nullptr)
{
    batchExp_internal(res,
                      tableA,
                      tableB,
                      coeffA,
                      coeffB,
                      vec,
                      callback);
}

// block partitioned vector, used with map-reduce
template <typename GA, typename GB, typename FR>
void batchExp(SparseVector<Pairing<GA, GB>>& res, // returned from batchExp()
              const WindowExp<GA>& tableA,
              const WindowExp<GB>& tableB,
              const FR& coeffA,
              const FR& coeffB,
              const BlockVector<FR>& vec,
              ProgressCallback* callback = nullptr)
{
    batchExp_internal(res,
                      tableA,
                      tableB,
                      coeffA,
                      coeffB,
                      vec,
                      callback);
}

template <typename GA, typename GB, typename FR>
Pairing<GA, GB> multiExp01(const SparseVector<Pairing<GA, GB>>& base,
                           const std::vector<FR>& scalar,
                           const std::size_t minIndex,
                           const std::size_t maxIndex,
                           const std::size_t reserveCount, // for performance tuning
                           ProgressCallback* callback)
{
    const auto
        ZERO = FR::zero(),
        ONE = FR::one();

    std::vector<Pairing<GA, GB>> base2;
    std::vector<FR> scalar2;
    if (reserveCount) {
        base2.reserve(reserveCount);
        scalar2.reserve(reserveCount);
    }

    auto accum = Pairing<GA, GB>::zero();

    for (std::size_t i = 0; i < base.size(); ++i) {
        const auto idx = base.getIndex(i);

        if (idx >= maxIndex) {
            break;

        } else if (idx >= minIndex) {
            const auto& a = scalar[idx - minIndex];

            if (ZERO == a) {
                continue;

            } else if (ONE == a) {
#ifdef USE_ADD_SPECIAL
                accum = fastAddSpecial(accum, base.getElement(i));
#else
                accum = accum + base.getElement(i);
#endif

            } else {
                base2.emplace_back(base.getElement(i));
                scalar2.emplace_back(a);
            }
        }
    }

    return accum + multiExp(base2, scalar2, callback);
}

template <typename GA, typename GB, typename FR>
Pairing<GA, GB> multiExp01(const SparseVector<Pairing<GA, GB>>& base,
                           const std::vector<FR>& scalar,
                           const std::size_t minIndex,
                           const std::size_t maxIndex,
                           ProgressCallback* callback = nullptr)
{
    return multiExp01(base, scalar, minIndex, maxIndex, 0, callback);
}

} // namespace snarklib

#endif
