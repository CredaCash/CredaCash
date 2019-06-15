#ifndef _SNARKFRONT_ENUM_OPS_HPP_
#define _SNARKFRONT_ENUM_OPS_HPP_

#include <cstdint>

//#include <cryptl/BitwiseINT.hpp>

#include <snarklib/FpModel.hpp>

namespace snarkfront {

// logical and arithmetic
enum class LogicalOps { AND, OR, XOR, SAME, CMPLMNT };
enum class ScalarOps { ADD, SUB, MUL };
enum class FieldOps { ADD, SUB, MUL, INV };
enum class BitwiseOps { AND, OR, XOR, SAME, CMPLMNT,
                        ADDMOD, MULMOD,
                        SHL, SHR, ROTL, ROTR };

// comparison
enum class EqualityCmp { EQ, NEQ };
enum class ScalarCmp { EQ, NEQ, LT, LE, GT, GE };

// number of operator input arguments
template <typename ENUM_OPS> std::size_t opArgc(const ENUM_OPS op);

// returns true for shift and rotate
bool isPermute(const BitwiseOps op);

// EQ --> SAME
// NEQ --> XOR
LogicalOps eqToLogical(const EqualityCmp op);
LogicalOps eqToLogical(const ScalarCmp op);

// evaluate logical operations
template <typename T>
T evalOp(const LogicalOps op, const T& x, const T& y)
{
    switch (op) {
    case (LogicalOps::AND) : return x && y;
    case (LogicalOps::OR) : return x || y;
    case (LogicalOps::XOR) : return x != y;
    case (LogicalOps::SAME) : return x == y;
    case (LogicalOps::CMPLMNT) : return ! x;
    }
	CCASSERT(0);
	return x;
}

// evaluate scalar arithmetic operations
template <typename T>
T evalOp(const ScalarOps op, const T& x, const T& y)
{
    switch (op) {
    case (ScalarOps::ADD) : return x + y;
    case (ScalarOps::SUB) : return x - y;
    case (ScalarOps::MUL) : return x * y;
    }
	CCASSERT(0);
	return x;
}

// evaluate finite scalar field arithmetic operations
template <typename T>
T evalOp(const FieldOps op, const T& x, const T& y)
{
    switch (op) {
    case (FieldOps::ADD) : return x + y;
    case (FieldOps::SUB) : return x - y;
    case (FieldOps::MUL) : return x * y;
    case (FieldOps::INV) : return snarklib::inverse(x);
    }
	CCASSERT(0);
	return x;
}

#if 0 // no cryptl
// evaluate bitwise word operations
template <typename T>
T evalOp(const BitwiseOps op, const T& x, const T& y)
{
    typedef cryptl::BitwiseINT<T> B;

    switch (op) {
    case (BitwiseOps::AND) : return B::AND(x, y);
    case (BitwiseOps::OR) : return B::OR(x, y);
    case (BitwiseOps::XOR) : return B::XOR(x, y);
    case (BitwiseOps::SAME) : return B::CMPLMNT(B::XOR(x, y));
    case (BitwiseOps::CMPLMNT) : return B::CMPLMNT(x);
    case (BitwiseOps::ADDMOD) : return B::ADDMOD(x, y);
    case (BitwiseOps::MULMOD) : return B::MULMOD(x, y);
    case (BitwiseOps::SHL) : return B::SHL(x, y);
    case (BitwiseOps::SHR) : return B::SHR(x, y);
    case (BitwiseOps::ROTL) : return B::ROTL(x, y);
    case (BitwiseOps::ROTR) : return B::ROTR(x, y);
    }
	CCASSERT(0);
	return x;
}
#endif // no cryptl

// evaluate equality comparison operations
template <typename T>
bool evalOp(const EqualityCmp op, const T& x, const T& y)
{
    switch (op) {
    case (EqualityCmp::EQ) : return x == y;
    case (EqualityCmp::NEQ) : return x != y;
    }
	CCASSERT(0);
	return false;
}

// evaluate scalar comparsion operations
template <typename T>
bool evalOp(const ScalarCmp op, const T& x, const T& y)
{
    switch (op) {
    case (ScalarCmp::EQ) : return x == y;
    case (ScalarCmp::NEQ) : return x != y;
    case (ScalarCmp::LT) : return x < y;
    case (ScalarCmp::LE) : return (x < y) || (x == y);
    case (ScalarCmp::GT) : return y < x;
    case (ScalarCmp::GE) : return (y < x) || (x == y);
    }
	CCASSERT(0);
	return false;
}

} // namespace snarkfront

#endif
