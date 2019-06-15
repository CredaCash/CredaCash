#include <CCassert.h>

#include "snarkfront/InitPairing.hpp"

namespace snarkfront {

////////////////////////////////////////////////////////////////////////////////
// Barreto-Naehrig 128 bits
//

const snarklib::BigInt<snarklib::BN128_Modulus::r_limbs>
BN128_MODULUS_R = snarklib::BN128::modulus_r();

const snarklib::BigInt<snarklib::BN128_Modulus::q_limbs>
BN128_MODULUS_Q = snarklib::BN128::modulus_q();

// initialize elliptic curve parameters
void init_BN128() {
    typedef snarklib::BN128 CURVE;

    // the R and Q modulus should be about the same size for GMP
#ifdef USE_ASSERT
    CCASSERT(CURVE::r_limbs == CURVE::q_limbs);
#endif

    // critically important to initialize finite field and group parameters
    CURVE::Fields<BN128_NRQ, BN128_MODULUS_R>::initParams();
    CURVE::Fields<BN128_NRQ, BN128_MODULUS_Q>::initParams();
    CURVE::Groups<BN128_NRQ, BN128_MODULUS_R, BN128_MODULUS_Q>::initParams();
}

} // namespace snarkfront
