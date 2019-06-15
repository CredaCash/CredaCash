#ifndef _SNARKLIB_PPZK_PROOF_HPP_
#define _SNARKLIB_PPZK_PROOF_HPP_

//#define TEST_BENCHMARK_PROOF	1	// for benchmarking

#ifndef TEST_BENCHMARK_PROOF
#define TEST_BENCHMARK_PROOF	0	// don't benchmark
#endif

#include <CCticks.hpp>

#include <cstdint>
#include <istream>
#include <memory>
#include <ostream>

#include <snarklib/AuxSTL.hpp>
#include <snarklib/Pairing.hpp>
#include <snarklib/PPZK_keystruct.hpp>
#include <snarklib/PPZK_randomness.hpp>
#include <snarklib/PPZK_witness.hpp>
#include <snarklib/ProgressCallback.hpp>
#include <snarklib/QAP_witness.hpp>
#include <snarklib/Rank1DSL.hpp>

namespace snarklib {

////////////////////////////////////////////////////////////////////////////////
// Proof generation
//

template <typename PAIRING>
class PPZK_Proof
{
    typedef typename PAIRING::Fr Fr;
    typedef typename PAIRING::G1 G1;
    typedef typename PAIRING::G2 G2;

public:
    PPZK_Proof() = default;

    // used for libsnark proof tests and marshalling
    PPZK_Proof(const Pairing<G1, G1>& A,
               const Pairing<G2, G1>& B,
               const Pairing<G1, G1>& C,
               const G1& H,
               const G1& K)
        : m_A(A),
          m_B(B),
          m_C(C),
          m_H(H),
          m_K(K)
    {}

    template <template <typename> class SYS>
    void PPZK_ProofThread1(const SYS<Fr>& constraintSystem,
               const std::size_t numCircuitInputs,
               const PPZK_ProvingKey<PAIRING>& pk,
               const R1Witness<Fr>& witness,
               const PPZK_ProofRandomness<Fr>& proofRand,
               const std::size_t reserveTune,
               ProgressCallback* callback)
    {
        ProgressCallback_NOP<PAIRING> dummyNOP;
        ProgressCallback* dummy = callback ? callback : std::addressof(dummyNOP);
        dummy->majorSteps(6);

        // randomness
        const auto
            &d1 = proofRand.d1(),
            &d2 = proofRand.d2(),
            &d3 = proofRand.d3();

        const QAP_SystemPoint<SYS, Fr> qap(constraintSystem, numCircuitInputs);

		uint32_t t0;
		if (TEST_BENCHMARK_PROOF)
		{
			t0 = ccticks();
		}

        // step 6 - A
        dummy->majorProgress(true);
        PPZK_WitnessA<PAIRING> Aw(qap, witness, d1);
        Aw.accumQuery(pk.A_query(), reserveTune, callback);
        m_A = Aw.val();

		if (TEST_BENCHMARK_PROOF)
		{
			std::cout << "PPZK_Proof A (independent) " << ccticks_elapsed(t0, ccticks()) << std::endl;
			t0 = ccticks();
		}

        // step 5 - B
        dummy->majorProgress(true);
        PPZK_WitnessB<PAIRING> Bw(qap, witness, d2);
        Bw.accumQuery(pk.B_query(), reserveTune, callback);
        m_B = Bw.val();

		if (TEST_BENCHMARK_PROOF)
		{
			std::cout << "PPZK_Proof B (independent) " << ccticks_elapsed(t0, ccticks()) << std::endl;
			t0 = ccticks();
		}

        // step 4 - C
        dummy->majorProgress(true);
        PPZK_WitnessC<PAIRING> Cw(qap, witness, d3);
        Cw.accumQuery(pk.C_query(), reserveTune, callback);
        m_C = Cw.val();

		if (TEST_BENCHMARK_PROOF)
		{
			std::cout << "PPZK_Proof C (independent) " << ccticks_elapsed(t0, ccticks()) << std::endl;
			t0 = ccticks();
		}

        // step 1 - K
        dummy->majorProgress(true);
        PPZK_WitnessK<PAIRING> Kw(witness, d1, d2, d3);
        Kw.accumQuery(pk.K_query(), reserveTune, callback);
        m_K = Kw.val();

		if (TEST_BENCHMARK_PROOF)
		{
			std::cout << "PPZK_Proof K (independent) " << ccticks_elapsed(t0, ccticks()) << std::endl;
			t0 = ccticks();
		}
	}

    template <template <typename> class SYS>
    void PPZK_ProofThread2(const SYS<Fr>& constraintSystem,
               const std::size_t numCircuitInputs,
               const PPZK_ProvingKey<PAIRING>& pk,
               const R1Witness<Fr>& witness,
               const PPZK_ProofRandomness<Fr>& proofRand,
               const std::size_t reserveTune,
               ProgressCallback* callback)
    {
        ProgressCallback_NOP<PAIRING> dummyNOP;
        ProgressCallback* dummy = callback ? callback : std::addressof(dummyNOP);
        dummy->majorSteps(6);

        // randomness
        const auto
            &d1 = proofRand.d1(),
            &d2 = proofRand.d2(),
            &d3 = proofRand.d3();

        const QAP_SystemPoint<SYS, Fr> qap(constraintSystem, numCircuitInputs);

		uint32_t t0;
		if (TEST_BENCHMARK_PROOF)
		{
			t0 = ccticks();
		}

        // step 3 - ABCH
        dummy->majorProgress(true);
        const QAP_WitnessABCH<SYS, Fr> ABCH(qap, witness, d1, d2, d3, callback);

		if (TEST_BENCHMARK_PROOF)
		{
			std::cout << "PPZK_Proof ABCH (independent) " << ccticks_elapsed(t0, ccticks()) << std::endl;
			t0 = ccticks();
		}

        // step 2 - H
        dummy->majorProgress(true);
        PPZK_WitnessH<PAIRING> Hw;
        Hw.accumQuery(pk.H_query(), ABCH.vec(), callback);
        m_H = Hw.val();

		if (TEST_BENCHMARK_PROOF)
		{
			std::cout << "PPZK_Proof H (needs ABCH) " << ccticks_elapsed(t0, ccticks()) << std::endl;
			t0 = ccticks();
		}
	}

    template <template <typename> class SYS>
    PPZK_Proof(const SYS<Fr>& constraintSystem,
               const std::size_t numCircuitInputs,
               const PPZK_ProvingKey<PAIRING>& pk,
               const R1Witness<Fr>& witness,
               const PPZK_ProofRandomness<Fr>& proofRand,
               const std::size_t reserveTune,
               ProgressCallback* callback)
    {
		std::thread thread1(&PPZK_Proof::PPZK_ProofThread1<SYS>, this, std::ref(constraintSystem),
					   numCircuitInputs,
					   std::ref(pk),
					   std::ref(witness),
					   std::ref(proofRand),
					   reserveTune,
					   callback);

		std::thread thread2(&PPZK_Proof::PPZK_ProofThread2<SYS>, this, std::ref(constraintSystem),
					   numCircuitInputs,
					   std::ref(pk),
					   std::ref(witness),
					   std::ref(proofRand),
					   reserveTune,
					   callback);

		thread1.join();
		thread2.join();
	}

    template <template <typename> class SYS>
    PPZK_Proof(const SYS<Fr>& constraintSystem,
               const std::size_t numCircuitInputs,
               const PPZK_ProvingKey<PAIRING>& pk,
               const R1Witness<Fr>& witness,
               const PPZK_ProofRandomness<Fr>& proofRand,
               ProgressCallback* callback = nullptr)
        : PPZK_Proof{constraintSystem, numCircuitInputs, pk, witness, proofRand, 0, callback}
    {}

    const Pairing<G1, G1>& A() const { return m_A; }
    const Pairing<G2, G1>& B() const { return m_B; }
    const Pairing<G1, G1>& C() const { return m_C; }
    const G1& H() const { return m_H; }
    const G1& K() const { return m_K; }

    bool wellFormed() const {
        return
            m_A.G().wellFormed() && m_A.H().wellFormed() &&
            m_B.G().wellFormed() && m_B.H().wellFormed() &&
            m_C.G().wellFormed() && m_C.H().wellFormed() &&
            m_H.wellFormed() &&
            m_K.wellFormed() &&
			G2::ScalarField::BaseType::modulus() * m_B.G() == G2::zero();	// possibly unecessary, but doesn't hurt: check if m_B.G() is in G2 subgroup
    }

    bool operator== (const PPZK_Proof& other) const {
        return
            A() == other.A() &&
            B() == other.B() &&
            C() == other.C() &&
            H() == other.H() &&
            K() == other.K();
    }

    bool operator!= (const PPZK_Proof& other) const {
        return ! (*this == other);
    }

    void marshal_out(std::ostream& os) const {
        A().marshal_out_raw(os);
        B().marshal_out_raw(os);
        C().marshal_out_raw(os);
        H().marshal_out_raw(os);
        K().marshal_out_raw(os);
    }

    bool marshal_in(std::istream& is) {
        return
            m_A.marshal_in_raw(is) &&
            m_B.marshal_in_raw(is) &&
            m_C.marshal_in_raw(is) &&
            m_H.marshal_in_raw(is) &&
            m_K.marshal_in_raw(is);
    }

    void clear() {
        m_A = Pairing<G1, G1>::zero();
        m_B = Pairing<G2, G1>::zero();
        m_C = Pairing<G1, G1>::zero();
        m_H = G1::zero();
        m_K = G1::zero();
    }

    bool empty() const {
        return
            m_A.isZero() ||
            m_B.isZero() ||
            m_C.isZero() ||
            m_H.isZero() ||
            m_K.isZero();
    }

//private:
    Pairing<G1, G1> m_A;
    Pairing<G2, G1> m_B;
    Pairing<G1, G1> m_C;
    G1 m_H;
    G1 m_K;
};

template <typename PAIRING>
std::ostream& operator<< (std::ostream& os, const PPZK_Proof<PAIRING>& a) {
    a.marshal_out(os);
    return os;
}

template <typename PAIRING>
std::istream& operator>> (std::istream& is, PPZK_Proof<PAIRING>& a) {
    if (! a.marshal_in(is)) a.clear();
    return is;
}

} // namespace snarklib

#endif
