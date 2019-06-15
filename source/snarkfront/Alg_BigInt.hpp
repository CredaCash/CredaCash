#ifndef _SNARKFRONT_ALG_BIGINT_HPP_
#define _SNARKFRONT_ALG_BIGINT_HPP_

#include <gmp.h>

#include <snarkfront/Alg.hpp>
#include <snarkfront/Alg_internal.hpp>
#include <snarkfront/BigIntOps.hpp>

namespace snarkfront {

////////////////////////////////////////////////////////////////////////////////
// Alg_BigInt
//

template <typename FR>
void evalStackOp(std::stack<Alg_BigInt<FR>>& S, const ScalarOps op) {
    evalStackOp_Scalar<Alg_BigInt<FR>>(S, op);
}

template <typename ALG>
void evalStackCmp_Scalar(std::stack<ALG>& S, const ScalarCmp op)
{
    typedef typename ALG::ValueType Value;
    typedef typename ALG::FrType Fr;
    typedef typename ALG::R1T R1T;
    auto& RS = TL<R1C<Fr>>::singleton();
    auto& POW2 = TL<PowersOf2<Fr>>::singleton();

    // y is right argument
    const auto R = S.top();
    S.pop();
    const Value yvalue = R.value();

    // x is left argument
    const auto L = S.top();
    S.pop();
    const Value xvalue = L.value();

    // z is result
    const bool result = evalOp(op, xvalue, yvalue);
    const Value zvalue = powerBigInt<Value::numberLimbs()>(result);
    const Fr zwitness = boolTo<Fr>(result);
    R1T z;

    if (ScalarCmp::EQ == op || ScalarCmp::NEQ == op) {
        // equalities need to compare bits
        const std::vector<int>
            ybits = R.splitBits(),
            xbits = L.splitBits();
        const std::vector<R1T>
            y = RS->argBits(R),
            x = RS->argBits(L);

#ifdef USE_ASSERT
        CCASSERT(y.size() == sizeBits(yvalue));
        CCASSERT(x.size() == sizeBits(xvalue));
#endif

        // intermediate constraint variables for each bit
        std::vector<R1T> zvec;
        std::vector<int> zwitness;
        zvec.reserve(sizeBits(yvalue));
        zwitness.reserve(sizeBits(yvalue));
        for (std::size_t i = 0; i < sizeBits(yvalue); ++i) {
            const bool b = evalOp(op, xbits[i], ybits[i]);
            zwitness.push_back(b);
            zvec.emplace_back(
                RS->createResult(eqToLogical(op), x[i], y[i], boolTo<Fr>(b)));
        }

        z = ScalarCmp::EQ == op
            ? RS->declarative_AND(zvec) // all must be same
            : RS->imperative_OR(zvec, zwitness); // one must be different

    } else {
        // inequalities need to compare scalar values
        const Fr
            ywitness = R.witness(),
            xwitness = L.witness();
        const R1T
            y = RS->argScalar(R),
            x = RS->argScalar(L);

        // offset is half of maximum value
        const std::size_t N_msb = sizeBits(yvalue) - 1;
        static const Value ovalue = powerBigInt<Value::numberLimbs()>(N_msb);
        const Fr owitness = POW2->getNumber(N_msb);
        const R1T o = RS->createConstant(owitness);

        // simpler to handle LT(x, y) as GT(y, x)
        //               and LE(x, y) as GE(y, x)
        // which means offset + y - x
        //  instead of offset + x - y
        const bool interchangeXY = (ScalarCmp::LT == op || ScalarCmp::LE == op);

        // offset + x (or offset + y)
        const Value oxvalue = ovalue + (interchangeXY ? yvalue : xvalue);
        const Fr oxwitness = owitness + (interchangeXY ? ywitness : xwitness);
        const R1T ox = RS->createResult(ScalarOps::ADD,
                                        o,
                                        interchangeXY ? y : x,
                                        oxwitness);

        // offset + x - y (or offset + y - x)
        const Value oxyvalue = oxvalue - (interchangeXY ? xvalue : yvalue);
        const Fr oxywitness = oxwitness - (interchangeXY ? xwitness : ywitness);
        const R1T oxy = RS->createResult(ScalarOps::SUB,
                                         ox,
                                         interchangeXY ? x : y,
                                         oxywitness);

        // constraint variable bit representation of offset + x - y (or offset + y - x)
        const std::vector<int> oxy_splitBits = valueBits(oxyvalue);
        const std::vector<R1T> oxybits = RS->witnessToBits(oxy, oxy_splitBits);
        const bool high_witness = oxy_splitBits[N_msb];
        const R1T& high_bit = oxybits[N_msb];

        switch (op) {
        case (ScalarCmp::LT) : // interchanged X and Y so same as GT
            // if x < y, then offset + x - y < offset == high_bit
            // so high bit should be clear and some low bit set
        case (ScalarCmp::GT) :
            // if x > y, then offset + x - y > offset == high_bit
            // so high bit should be set and some low bit should also be set
            {
                std::vector<int> low_witness;
                std::vector<R1T> low_bits;
                low_witness.reserve(sizeBits(yvalue));
                low_bits.reserve(sizeBits(yvalue));

                // low bits
                for (std::size_t i = 0; i < N_msb; ++i) {
                    low_witness.push_back(oxy_splitBits[i]);
                    low_bits.emplace_back(oxybits[i]);
                }

                // last bit is duplicate to make vector even power of 2
                low_witness.push_back(low_witness[0]);
                low_bits.emplace_back(low_bits[0]);

                const R1T low_bit_set = RS->imperative_OR(low_bits, low_witness);
                z = RS->createResult(LogicalOps::AND, high_bit, low_bit_set, zwitness);
            }
            break;

        case (ScalarCmp::LE) : // interchanged X and Y so same as GE
            // if x <= y, then offset + x - y <= offset == high_bit
            // so there are two cases:
            // some low bit is set and high bit clear (less than)
            // low bits are clear and high bit is set (equal)
        case (ScalarCmp::GE) :
            // if x >= y, then offset + x - y >= offset == high_bit
            // so high bit should be set (ignore low bits)
            z = high_bit;
            break;
        }
    }

    S.push(
        ALG(zvalue, zwitness, valueBits(zvalue), {z}));
}

template <typename FR>
void evalStackCmp(std::stack<Alg_BigInt<FR>>& S, const ScalarCmp op) {
    evalStackCmp_Scalar(S, op);
}

} // namespace snarkfront

#endif
