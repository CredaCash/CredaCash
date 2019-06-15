#ifndef _SNARKLIB_PPZK_KEYPAIR_HPP_
#define _SNARKLIB_PPZK_KEYPAIR_HPP_

#include <cstdint>
#include <istream>
#include <memory>
#include <ostream>
#include <thread>

#include <snarklib/AuxSTL.hpp>
#include <snarklib/Group.hpp>
#include <snarklib/Pairing.hpp>
#include <snarklib/PPZK_keystruct.hpp>
#include <snarklib/PPZK_query.hpp>
#include <snarklib/PPZK_randomness.hpp>
#include <snarklib/ProgressCallback.hpp>
#include <snarklib/QAP_query.hpp>
#include <snarklib/Rank1DSL.hpp>
#include <snarklib/WindowExp.hpp>

namespace snarklib {

////////////////////////////////////////////////////////////////////////////////
// Key pair: proving and verification
//

template <typename G>
void SetWindowExp(const std::size_t expCount, WindowExp<G> &exp)
{
	exp = std::move(WindowExp<G>(expCount));
}

template <typename PAIRING>
class PPZK_Keypair
{
    typedef typename PAIRING::Fr Fr;
    typedef typename PAIRING::G1 G1;
    typedef typename PAIRING::G2 G2;

public:
    PPZK_Keypair() = default;

    // only used for roundtrip marshalling tests
    PPZK_Keypair(const PPZK_ProvingKey<PAIRING>& pk,
                 const PPZK_VerificationKey<PAIRING> vk)
        : m_pk(pk),
          m_vk(vk)
    {}

    template <template <typename> class SYS>
    PPZK_Keypair(const SYS<Fr>& constraintSystem,
                 const std::size_t numCircuitInputs,
                 const PPZK_LagrangePoint<Fr>& lagrangeRand,
                 const PPZK_BlindGreeks<Fr, Fr>& blindRand,
                 ProgressCallback* callback = nullptr)
    {
        ProgressCallback_NOP<PAIRING> dummyNOP;
        ProgressCallback* dummy = callback ? callback : std::addressof(dummyNOP);
        dummy->majorSteps(8);

        // randomness
        const auto
            &point = lagrangeRand.point(),
            &rA = blindRand.rA(),
            &rB = blindRand.rB(),
            &rC = blindRand.rC(),
            &alphaA = blindRand.alphaA(),
            &alphaB = blindRand.alphaB(),
            &alphaC = blindRand.alphaC(),
            &gamma = blindRand.gamma(),
            &alphaA_rA = blindRand.alphaA_rA(),
            &alphaB_rB = blindRand.alphaB_rB(),
            &alphaC_rC = blindRand.alphaC_rC(),
            &beta_rA = blindRand.beta_rA(),
            &beta_rB = blindRand.beta_rB(),
            &beta_rC = blindRand.beta_rC(),
            &beta_gamma = blindRand.beta_gamma();

        const QAP_SystemPoint<SYS, Fr> qap(constraintSystem, numCircuitInputs, point);
        if (qap.weakPoint()) {
            // Lagrange evaluation point is root of unity
            return;
        }

        // ABCH
        const QAP_QueryABC<SYS, Fr> ABCt(qap);
        const QAP_QueryH<SYS, Fr> Ht(qap);

        // step 8 - G1 window table
        dummy->majorProgress(true);
        WindowExp<G1> g1_table_temp;
		std::thread thread_G1(SetWindowExp<G1>, g1_exp_count(qap, ABCt, Ht), std::ref(g1_table_temp));

        // step 7 - G2 window table
        dummy->majorProgress(true);
        WindowExp<G2> g2_table_temp;
		std::thread thread_G2(SetWindowExp<G2>, g2_exp_count(ABCt), std::ref(g2_table_temp));

		thread_G1.join();
		thread_G2.join();

        const WindowExp<G1> g1_table(std::move(g1_table_temp));
        const WindowExp<G2> g2_table(std::move(g2_table_temp));

        // step 6 - input consistency
        dummy->majorProgress(true);
        PPZK_QueryIC<PAIRING> ppzkIC(qap_query_IC(qap, ABCt, rA));
        ppzkIC.accumTable(g1_table, dummy);

        // step 5 - A
        dummy->majorProgress(true);
		auto query_A = qap_query_IC(qap, ABCt);
        SparseVector<Pairing<G1, G1>> A;
		std::thread thread_A(ppzk_query_ABC<G1, G1, Fr>, std::ref(query_A), std::ref(rA), std::ref(alphaA_rA),
                                std::ref(g1_table), std::ref(g1_table),
                                std::ref(A), dummy);

        // step 4 - B
        dummy->majorProgress(true);
		auto query_B = ABCt.vecB();
        SparseVector<Pairing<G2, G1>> B;
		std::thread thread_B(ppzk_query_ABC<G2, G1, Fr>, std::ref(query_B), std::ref(rB), std::ref(alphaB_rB),
                                std::ref(g2_table), std::ref(g1_table),
                                std::ref(B), dummy);

        // step 3 - C
        dummy->majorProgress(true);
		auto query_C = ABCt.vecC();
        SparseVector<Pairing<G1, G1>> C;
		std::thread thread_C(ppzk_query_ABC<G1, G1, Fr>, std::ref(query_C), std::ref(rC), std::ref(alphaC_rC),
                                std::ref(g1_table), std::ref(g1_table),
                                std::ref(C), dummy);

        // step 2 - H
        dummy->majorProgress(true);
		auto query_H = Ht.vec();
        std::vector<G1> H;
		std::thread thread_H(ppzk_query_HK<PAIRING>, std::ref(query_H),
                                        std::ref(g1_table),
                                        std::ref(H), dummy);

        // step 1 - K
        dummy->majorProgress(true);
		auto query_K = qap_query_K(qap, ABCt, beta_rA, beta_rB, beta_rC);
        std::vector<G1> K;
		std::thread thread_K(ppzk_query_HK<PAIRING>, std::ref(query_K),
                                        std::ref(g1_table),
                                        std::ref(K), dummy);

		thread_A.join();
		thread_B.join();
		thread_C.join();
		thread_H.join();
		thread_K.join();

        m_pk = PPZK_ProvingKey<PAIRING>(std::move(A),
                                        std::move(B),
                                        std::move(C),
                                        std::move(H),
                                        std::move(K));

        m_vk = PPZK_VerificationKey<PAIRING>(alphaA * G2::one(),
                                             alphaB * G1::one(),
                                             alphaC * G2::one(),
                                             gamma * G2::one(),
                                             beta_gamma * G1::one(),
                                             beta_gamma * G2::one(),
                                             qap.compute_Z() * (rC * G2::one()),
                                             std::move(ppzkIC));
    }

    const PPZK_ProvingKey<PAIRING>& pk() const { return m_pk; }
    const PPZK_VerificationKey<PAIRING>& vk() const { return m_vk; }

    bool operator== (const PPZK_Keypair& other) const {
        return
            pk() == other.pk() &&
            vk() == other.vk();
    }

    bool operator!= (const PPZK_Keypair& other) const {
        return ! (*this == other);
    }

    void marshal_out(std::ostream& os) const {
        pk().marshal_out_rawspecial(os);
        vk().marshal_out_rawspecial(os);
    }

    bool marshal_in(std::istream& is) {
        return
            m_pk.marshal_in_rawspecial(is) &&
            m_vk.marshal_in_rawspecial(is);
    }

    void clear() {
        m_pk.clear();
        m_vk.clear();
    }

    bool empty() const {
        return
            m_pk.empty() ||
            m_vk.empty();
    }

//private:
    PPZK_ProvingKey<PAIRING> m_pk;
    PPZK_VerificationKey<PAIRING> m_vk;
};

template <typename PAIRING>
std::ostream& operator<< (std::ostream& os, const PPZK_Keypair<PAIRING>& a) {
    a.marshal_out(os);
    return os;
}

template <typename PAIRING>
std::istream& operator>> (std::istream& is, PPZK_Keypair<PAIRING>& a) {
    if (! a.marshal_in(is)) a.clear();
    return is;
}

} // namespace snarklib

#endif
