/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * block.cpp
*/

#include "ccnode.h"
#include "block.hpp"
#include "blockchain.hpp"
#include "witness.hpp"

#include <CCmint.h>

#include <blake2/blake2.h>

#define TRACE_BLOCK			0
#define TRACE_CHECKORDER	0
#define TRACE_CALCSKIPSCORE 0
#define TRACE_CALCOID		0
#define TRACE_SIGNING		0

//!#define RTEST_CUZZ			(64-1)
//#define TEST_SKIP_SIGS		1	// to bypass sigs when they are broken

#ifndef RTEST_CUZZ
#define RTEST_CUZZ			0	// don't test
#endif

#ifndef TEST_SKIP_SIGS
#define TEST_SKIP_SIGS		0	// don't skip
#endif

#define MAX_SCORE_BITS		64

BlockAux* Block::AuxPtr()
{
	auto auxp = preamble.auxp[0];

	CCASSERT(auxp);

	return (BlockAux*)auxp;
}

BlockAux* Block::SetupAuxBuf(SmartBuf smartobj, bool from_tx_net)
{
	auto wire = WireData();

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "Block::SetupAuxBuf from_tx_net " << from_tx_net << " level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " prior oid " << buf2hex(&wire->prior_oid, CC_OID_TRACE_SIZE);

	auto auxp = (BlockAux*)malloc(sizeof(BlockAux));
	if (!auxp)
	{
		BOOST_LOG_TRIVIAL(error) << "Block::SetupAuxBuf malloc failed";

		return NULL;
	}

	memset((void*)auxp, 0, sizeof(BlockAux));

	preamble.auxp[0] = auxp;
	smartobj.SetAuxPtrCount(2);

	auxp->from_tx_net = from_tx_net;
	auxp->announce_ticks = ccticks();

	return auxp;
}

static FastSpinLock prior_block_lock(__FILE__, __LINE__);

SmartBuf Block::GetPriorBlock() const
{
	lock_guard<FastSpinLock> lock(prior_block_lock);

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);

	auto prior = SmartBuf(preamble.auxp[1]);

	//if (TRACE_BLOCK) BOOST_LOG_TRIVIAL(debug) << "Block::GetPriorBlock blockp " << (uintptr_t)this << " prior " << (uintptr_t)preamble.auxp[1];

	return prior;
}

void Block::SetPriorBlock(SmartBuf priorobj)
{
	lock_guard<FastSpinLock> lock(prior_block_lock);

	auto curprior = preamble.auxp[1];

	if (TRACE_BLOCK) BOOST_LOG_TRIVIAL(debug) << "Block::SetPriorBlock blockp " << (uintptr_t)this << " was " << (uintptr_t)curprior << " setting to " << (uintptr_t)priorobj.BasePtr();

	preamble.auxp[1] = priorobj.BasePtr();
	priorobj.IncRef();

	SmartBuf(curprior).DecRef(true);

	if (RTEST_CUZZ) usleep(rand() & RTEST_CUZZ);
}

void Block::ChainToPriorBlock(SmartBuf priorobj)
{
	auto wire = WireData();
	auto auxp = AuxPtr();

	auto priorblock = (Block*)priorobj.data();
	CCASSERT(priorblock);

	auto prior_wire = priorblock->WireData();
	auto prior_auxp = priorblock->AuxPtr();

	SetPriorBlock(priorobj);

	CCASSERT(wire->witness < MAX_NWITNESSES);
	memcpy(&auxp->blockchain_params, &prior_auxp->blockchain_params, sizeof(auxp->blockchain_params));

	auxp->blockchain_params.nwitnesses = prior_auxp->blockchain_params.next_nwitnesses;
	auxp->blockchain_params.maxmal = prior_auxp->blockchain_params.next_maxmal;

	if (Implement_CCMint(g_params.blockchain) && wire->level.GetValue() == CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
	{
		BOOST_LOG_TRIVIAL(debug) << "Block::ChainToPriorBlock block level " << wire->level.GetValue() << " setting next_nwitnesses " << genesis_nwitnesses << " next_maxmal " << genesis_maxmal;

		auxp->blockchain_params.next_nwitnesses = genesis_nwitnesses;
		auxp->blockchain_params.next_maxmal = genesis_maxmal;
	}

#if ROTATE_BLOCK_SIGNING_KEYS
	memcpy(&auxp->blockchain_params.signing_keys[wire->witness], &wire->witness_next_signing_public_key, sizeof(wire->witness_next_signing_public_key));
#endif
	memcpy(&auxp->witness_params.next_signing_private_key, &prior_auxp->witness_params.next_signing_private_key, sizeof(auxp->witness_params.next_signing_private_key));

	auxp->SetConfSigs();

	auxp->skip = ComputeSkip(prior_wire->witness, wire->witness, auxp->blockchain_params.nwitnesses);

	if (TRACE_BLOCK) BOOST_LOG_TRIVIAL(trace) << "Block::ChainToPriorBlock level " << wire->level.GetValue() << " nwitnesses " << auxp->blockchain_params.nwitnesses << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " prior witness " << (unsigned)prior_wire->witness << " prior oid " << buf2hex(&prior_auxp->oid, CC_OID_TRACE_SIZE);

	CCASSERT(auxp->blockchain_params.nwitnesses);
	CCASSERT(auxp->skip < auxp->blockchain_params.nwitnesses);
}

void BlockAux::SetConfSigs()
{
	if (blockchain_params.nwitnesses <= 0)
	{
		BOOST_LOG_TRIVIAL(warning) << "BlockAux::SetConfSigs nwitnesses " << blockchain_params.nwitnesses << " <= 0";

		blockchain_params.nwitnesses = 1;
	}

	if (blockchain_params.maxmal >= (blockchain_params.nwitnesses + 1) / 2)
	{
		BOOST_LOG_TRIVIAL(warning) << "BlockAux::SetConfSigs maxmal " << blockchain_params.maxmal << " >= (nwitnesses + 1) / 2 = " << (blockchain_params.nwitnesses + 1) / 2;

		blockchain_params.maxmal = (blockchain_params.nwitnesses - 1) / 2;
	}

	CCASSERT(blockchain_params.nwitnesses > 0);
	CCASSERT(blockchain_params.maxmal < (blockchain_params.nwitnesses + 1) / 2);

	blockchain_params.nconfsigs = (blockchain_params.nwitnesses - blockchain_params.maxmal) / 2 + blockchain_params.maxmal + 1;

	blockchain_params.nskipconfsigs = blockchain_params.nwitnesses + blockchain_params.maxmal;

	if (blockchain_params.maxmal)
		blockchain_params.nseqconfsigs = blockchain_params.nskipconfsigs;
	else
		blockchain_params.nseqconfsigs = blockchain_params.nconfsigs;

	if (blockchain_params.nconfsigs > MAX_NCONFSIGS)
	{
		BOOST_LOG_TRIVIAL(warning) << "BlockAux::SetConfSigs nconfsigs " << blockchain_params.nconfsigs << " > " << MAX_NCONFSIGS;

		blockchain_params.nconfsigs = MAX_NCONFSIGS;
	}

	if (blockchain_params.nseqconfsigs > MAX_NCONFSIGS)
	{
		BOOST_LOG_TRIVIAL(warning) << "BlockAux::SetConfSigs nseqconfsigs " << blockchain_params.nseqconfsigs << " > " << MAX_NCONFSIGS;

		blockchain_params.nseqconfsigs = MAX_NCONFSIGS;
	}

	if (blockchain_params.nskipconfsigs > MAX_NCONFSIGS)
	{
		BOOST_LOG_TRIVIAL(warning) << "BlockAux::SetConfSigs nskipconfsigs " << blockchain_params.nskipconfsigs << " > " << MAX_NCONFSIGS;

		blockchain_params.nskipconfsigs = MAX_NCONFSIGS;
	}
}

unsigned Block::ComputeSkip(unsigned prev_witness, unsigned next_witness, unsigned nwitnesses)
{
	return (next_witness - ((prev_witness + 1) % nwitnesses) + nwitnesses) % nwitnesses;
}

bool Block::CheckBadSigOrder(int top_witness) const
{
	auto block = this;
	auto wire = WireData();
	auto auxp = AuxPtr();

	unsigned nwitnesses = auxp->blockchain_params.nwitnesses;
	unsigned nconfsigs = auxp->blockchain_params.nconfsigs;

	if (top_witness >= 0)
	{
		nwitnesses = auxp->blockchain_params.next_nwitnesses;
		nconfsigs = (nwitnesses - auxp->blockchain_params.next_maxmal) / 2 + auxp->blockchain_params.next_maxmal + 1;
	}

	if (TRACE_CHECKORDER) BOOST_LOG_TRIVIAL(trace) << "Block::CheckBadSigOrder top_witness " << top_witness << " starting at level " << wire->level.GetValue() << " nwitnesses " << nwitnesses << " maxmal " << auxp->blockchain_params.maxmal << " nconfsigs " << nconfsigs << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE);

	unsigned nsigs = 0;
	unsigned skipsum = 0;

	if (top_witness >= 0)
	{
		nsigs++;
		skipsum += ComputeSkip(wire->witness, top_witness, nwitnesses);

		if (TRACE_CHECKORDER) BOOST_LOG_TRIVIAL(trace) << "Block::CheckBadSigOrder added top witness skip " << skipsum << " nsigs " << nsigs << " skipsum " << skipsum;
	}

	while (nsigs < nconfsigs)
	{
		nsigs++;
		skipsum += auxp->skip;

		if (TRACE_CHECKORDER) BOOST_LOG_TRIVIAL(trace) << "Block::CheckBadSigOrder added skip " << auxp->skip << " nsigs " << nsigs << " skipsum " << skipsum;

		auto prior_block = block->GetPriorBlock();

		//if (TRACE_BLOCK) BOOST_LOG_TRIVIAL(debug) << "Block::CheckBadSigOrder prior blockp " << (uintptr_t)prior_block.BasePtr();

		if (!prior_block)
		{
			if (wire->level.GetValue())
			{
				BOOST_LOG_TRIVIAL(error) << "Block::CheckBadSigOrder no prior block at level " << wire->level.GetValue() << " skip " << auxp->skip << " nsigs " << nsigs << " skipsum " << skipsum;

				return true;
			}

			break;
		}

		// move back one block

		auto expected_level = wire->level.GetValue() - 1;

		auto smartobj = prior_block;
		block = (Block*)smartobj.data();
		wire = block->WireData();
		auxp = block->AuxPtr();

		//if (TRACE_BLOCK) BOOST_LOG_TRIVIAL(debug) << "Block::CheckBadSigOrder prior prior blockp " << (uintptr_t)block->GetPriorBlock().BasePtr();

		if (wire->level.GetValue() != expected_level)
		{
			const char msg[] = "Block::CheckBadSigOrder block level sequence error";

			BOOST_LOG_TRIVIAL(fatal) << msg << " level " << wire->level.GetValue() << " expected level " << expected_level;

			return g_blockchain.SetFatalError(msg);
		}
	}

	if (skipsum + nconfsigs > nwitnesses)
	{
		if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CheckBadSigOrder block violates dupe/ordering rule; level " << wire->level.GetValue() << " nwitnesses " << nwitnesses << " nsigs " << nsigs << " skipsum " << skipsum << " oid " << buf2hex(&AuxPtr()->oid, CC_OID_TRACE_SIZE);

		return true;
	}

	return false;
}

// returns 0 if block does not chain back to last indelible block
uint64_t Block::CalcSkipScore(int top_witness, SmartBuf last_indelible_block, uint16_t genstamp, bool maltest)
{
	CCASSERT(last_indelible_block);
	auto block = (Block*)last_indelible_block.data();
	auto last_indelible_wire = block->WireData();

	auto wire = WireData();
	auto auxp = AuxPtr();

	if (maltest)
	{
		auto target_level = last_indelible_wire->level.GetValue();
		auto offset = auxp->blockchain_params.nskipconfsigs;

		if (target_level <= offset)
			target_level = 0;
		else
			target_level -= offset;

		if (wire->level.GetValue() <= target_level)
		{
			if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore top witness " << top_witness << " test mal " << maltest << " starting at level " << wire->level.GetValue() << " <= target level " << target_level << " returning score 0";

			return 0;
		}
	}

	if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore top witness " << top_witness << " genstamp " << genstamp << " test mal " << maltest << " starting at level " << wire->level.GetValue() << " nwitnesses " << auxp->blockchain_params.nwitnesses << " maxmal " << auxp->blockchain_params.maxmal << " nconfsigs " << auxp->blockchain_params.nconfsigs << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " last indelible level " << last_indelible_wire->level.GetValue();

	uint64_t score = 0;
	unsigned scorebits = 0;

	CalcSkipScoreRecursive(last_indelible_wire, genstamp, maltest, score, scorebits);

	if (score && top_witness >= 0)
	{
		if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore before top witness score " << hex << score << dec << " scorebits " << scorebits;

		unsigned nwitnesses = auxp->blockchain_params.next_nwitnesses;
		auto skip = ComputeSkip(wire->witness, top_witness, nwitnesses);

		score <<= skip + 1;
		score |= 1;
		scorebits += skip + 1;

		if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore added top witness " << top_witness << " skip " << skip << " score " << hex << score << dec << " scorebits " << scorebits;
	}

	if (scorebits > MAX_SCORE_BITS)
	{
		static unsigned highestscorebits = 0;

		if (scorebits > highestscorebits)
			highestscorebits = scorebits;

		BOOST_LOG_TRIVIAL(error) << "Block::CalcSkipScore top witness " << top_witness << " test mal " << maltest << " score " << hex << score << dec << " scorebits " << scorebits << " exceeds max scorebits " << MAX_SCORE_BITS << "; highest scorebits seen " << highestscorebits;

		if (maltest)
			scorebits = MAX_SCORE_BITS;
		else
			score = 0;

		//start_shutdown();	// for debugging
	}

	if (scorebits < MAX_SCORE_BITS)
		score <<= MAX_SCORE_BITS - scorebits;

	if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore score " << hex << score << dec << " test mal " << maltest << " starting at level " << wire->level.GetValue() << " nwitnesses " << auxp->blockchain_params.nwitnesses << " maxmal " << auxp->blockchain_params.maxmal << " nconfsigs " << auxp->blockchain_params.nconfsigs << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " last indelible level " << last_indelible_wire->level.GetValue();

	return score;
}

void Block::CalcSkipScoreRecursive(const BlockWireHeader* last_indelible_wire, uint16_t genstamp, bool maltest, uint64_t& score, unsigned& scorebits)
{
	auto wire = WireData();
	auto auxp = AuxPtr();

	if (genstamp && genstamp == auxp->witness_params.score_genstamp)
	{
		score = auxp->witness_params.score;
		scorebits = auxp->witness_params.score_bits;

		return;
	}

	if (wire == last_indelible_wire && !maltest)
	{
		score = 1;
		scorebits = 1;

		if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore at last indelible block returning score " << hex << score << dec << " scorebits " << scorebits;

		return;
	}

	if (wire->level.GetValue() <= last_indelible_wire->level.GetValue() && !maltest)
	{
		score = 0;
		scorebits = 0;

		if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore passed last indelible block returning score " << hex << score << dec << " scorebits " << scorebits;

		return;
	}

	if (maltest)
	{
		auto target_level = last_indelible_wire->level.GetValue();
		auto offset = auxp->blockchain_params.nskipconfsigs;

		if (target_level <= offset)
			target_level = 0;
		else
			target_level -= offset;

		if (wire->level.GetValue() <= target_level)
		{
			score = 1;
			scorebits = 1;

			if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore test mal " << maltest << " reached level " << wire->level.GetValue() << " returning score " << hex << score << dec << " scorebits " << scorebits;

			return;
		}
	}

	auto prior_block = GetPriorBlock();

	if (!prior_block)
	{
		if (!maltest)
		{
			score = 0;
			scorebits = 0;
		}
		else
		{
			score = 1;
			scorebits = 1;
		}

		if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore test mal " << maltest << " no prior block at level " << wire->level.GetValue() << " returning score " << hex << score << dec << " scorebits " << scorebits;

		return;
	}

	auto smartobj = prior_block;
	auto block = (Block*)smartobj.data();

	block->CalcSkipScoreRecursive(last_indelible_wire, genstamp, maltest, score, scorebits);

	if (!score)
		return;

	score <<= auxp->skip + 1;
	score |= 1;
	scorebits += auxp->skip + 1;

	if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore level " << wire->level.GetValue() << " added skip " << auxp->skip << " score " << hex << score << dec << " scorebits " << scorebits;

	if (genstamp)
	{
		auxp->witness_params.score = score;
		auxp->witness_params.score_bits = scorebits;
		auxp->witness_params.score_genstamp = genstamp;
	}
}

void Block::SetOrVerifyOid(bool bset)
{
	block_hash_t hash;
	ccoid_t oid;

	CalcHash(hash);
	CalcOid(hash, oid);

	auto auxp = AuxPtr();

	if (bset)
	{
		auxp->SetHash(hash);
		auxp->SetOid(oid);
	}
	else
	{
		if (memcmp(&hash, &auxp->block_hash, sizeof(auxp->block_hash)))
		{
			BOOST_LOG_TRIVIAL(fatal) << "Block::SetOrVerifyOid block oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " hash " << buf2hex(&auxp->block_hash, sizeof(auxp->block_hash)) << " does not match computed hash " << buf2hex(&hash, sizeof(hash));
			CCASSERT(0); raise(SIGTERM);
		}

		if (memcmp(&oid, &auxp->oid, sizeof(ccoid_t)))
		{
			BOOST_LOG_TRIVIAL(fatal) << "Block::SetOrVerifyOid block oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " does not match computed oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);
			CCASSERT(0); raise(SIGTERM);
		}
	}
}

void BlockAux::SetHash(const block_hash_t& hash)
{
	CCASSERT(sizeof(block_hash) == sizeof(hash));

	memcpy(&block_hash, &hash, sizeof(hash));

	if (TRACE_CALCOID) BOOST_LOG_TRIVIAL(info) << "BlockAux::SetHash TRACE_CALCOID block with announced " << announce_ticks << " set to hash " << buf2hex(&block_hash, sizeof(block_hash));
}

void BlockAux::SetOid(const ccoid_t& soid)
{
	CCASSERT(sizeof(oid) == sizeof(soid));

	memcpy(&oid, &soid, sizeof(ccoid_t));

	if (TRACE_CALCOID) BOOST_LOG_TRIVIAL(info) << "BlockAux::SetOid TRACE_CALCOID block with announced " << announce_ticks << " set to oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);
}

void Block::CalcHash(block_hash_t& block_hash)
{
	auto rc = blake2b(&block_hash, sizeof(block_hash), &header.tag, sizeof(header.tag), BodyPtr() + sizeof(block_signature_t), BodySize() - sizeof(block_signature_t));
	CCASSERTZ(rc);

	if (TRACE_CALCOID) BOOST_LOG_TRIVIAL(info) << "Block::CalcHash TRACE_CALCOID for block level " << WireData()->level.GetValue() << " size " << ObjSize() << " prior oid " << buf2hex(&(WireData()->prior_oid), CC_OID_TRACE_SIZE) << " computed block hash " << buf2hex(&block_hash, sizeof(block_hash));
}

void Block::CalcOid(const block_hash_t& block_hash, ccoid_t& oid)
{
	auto wire = WireData();

	blake2s_ctx ctx;

	auto rc =
	blake2s_init(&ctx, sizeof(ccoid_t), &header.tag, sizeof(header.tag));
	blake2s_update(&ctx, &block_hash, sizeof(block_hash));
	blake2s_update(&ctx, &wire->signature, sizeof(wire->signature));
	blake2s_final(&ctx, &oid);
	CCASSERTZ(rc);

#if TEST_SEQ_BLOCK_OID
	static std::atomic<uint32_t> seq(1);
	*(uint32_t*)&oid = seq.fetch_add(1);
#endif

	if (TRACE_CALCOID) BOOST_LOG_TRIVIAL(info) << "Block::CalcOid TRACE_CALCOID for block level " << WireData()->level.GetValue() << " size " << ObjSize() << " prior oid " << buf2hex(&(WireData()->prior_oid), CC_OID_TRACE_SIZE) << " from block hash " << buf2hex(&block_hash, sizeof(block_hash)) << " computed oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);
}

static void CummulativeHash(uint64_t level, block_hash_t& hash, const void *data, unsigned bytes)
{
	if (level && TRACE_SIGNING) BOOST_LOG_TRIVIAL(info) << "Block::CummulativeHash level " << level << "   in " << buf2hex(&hash, sizeof(hash));
	if (level && TRACE_SIGNING) BOOST_LOG_TRIVIAL(info) << "Block::CummulativeHash level " << level << " data " << buf2hex(data, bytes);

	blake2b_ctx ctx;
	auto rc =
	blake2b_init(&ctx, sizeof(hash), NULL, 0);
	blake2b_update(&ctx, &hash, sizeof(hash));
	blake2b_update(&ctx, data, bytes);
	blake2b_final(&ctx, &hash);
	CCASSERTZ(rc);

	if (level && TRACE_SIGNING) BOOST_LOG_TRIVIAL(info) << "Block::CummulativeHash level " << level << "  out " << buf2hex(&hash, sizeof(hash));
}

bool Block::SignOrVerify(bool verify)
{
	auto wire = WireData();
	auto auxp = AuxPtr();

	auto level = wire->level.GetValue();

	auto priorblock = (Block*)GetPriorBlock().data();
	auto prior_auxp = priorblock->AuxPtr();

	if (TEST_SKIP_SIGS)
	{
		if (!verify)
			memset(&wire->signature, 0, sizeof(wire->signature));

		return false;
	}

	block_hash_t data;

	CCASSERT(sizeof(data) == sizeof(prior_auxp->block_hash));

	memcpy(&data, &prior_auxp->block_hash, sizeof(prior_auxp->block_hash));

	CummulativeHash(level*verify, data, &auxp->block_hash, sizeof(auxp->block_hash));

#if ROTATE_BLOCK_SIGNING_KEYS
	CummulativeHash(level*verify, data, &wire->witness_next_signing_public_key, sizeof(wire->witness_next_signing_public_key));
#endif

	if (wire->witness >= prior_auxp->blockchain_params.next_nwitnesses)
	{
		if (TRACE_BLOCK) BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify error witness " << (unsigned)wire->witness << " out of range next_nwitnesses " << prior_auxp->blockchain_params.next_nwitnesses;

		return true;
	}

	if (verify)
	{
		//*(uint8_t*)&data ^= 0xff;	// for testing

		if (TRACE_SIGNING)
		{
			BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify verify level " << level << " witness " << (unsigned)wire->witness;
			BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify verify     pubkey " << buf2hex(&prior_auxp->blockchain_params.signing_keys[wire->witness], sizeof(prior_auxp->blockchain_params.signing_keys[wire->witness]));
			BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify signing data " << buf2hex(&data, sizeof(data));
			BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify signature " << buf2hex(&wire->signature, sizeof(wire->signature));
		}

		auto rc = ed25519_sign_open((const unsigned char*)&data, sizeof(data), &prior_auxp->blockchain_params.signing_keys[wire->witness][0], &wire->signature[0]);

		if (TRACE_SIGNING || TRACE_BLOCK) BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify signature verify result " << rc;

		return rc;
	}
	else
	{
		CCASSERT(sizeof(wire->signature) == sizeof(ed25519_signature));

		int keynum = 0;
		if (TEST_SIM_ALL_WITNESSES)
			keynum = wire->witness;

		ed25519_sign((const unsigned char*)&data, sizeof(data), &prior_auxp->witness_params.next_signing_private_key[keynum][0], &prior_auxp->blockchain_params.signing_keys[wire->witness][0], &wire->signature[0]);

		if (0 && TRACE_SIGNING)
		{
			block_signing_public_key_t pubkey;
			ed25519_publickey(&prior_auxp->witness_params.next_signing_private_key[keynum][0], &pubkey[0]);

			BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify sign level " << level << " witness " << (unsigned)wire->witness;
			// don't log the private key in the final release
			//BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify sign      privkey " << buf2hex(&prior_auxp->witness_params.next_signing_private_key[keynum], sizeof(prior_auxp->witness_params.next_signing_private_key[keynum]));
			BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify sign       pubkey " << buf2hex(&prior_auxp->blockchain_params.signing_keys[wire->witness], sizeof(prior_auxp->blockchain_params.signing_keys[wire->witness]));
			BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify sign pubkey check " << buf2hex(&pubkey, sizeof(pubkey));
			BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify signing data " << buf2hex(&data, sizeof(data));
			BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify signature " << buf2hex(&wire->signature, sizeof(wire->signature));
		}

		return false;
	}
}

void Block::ConsoleAnnounce(const char *verb, const BlockWireHeader *wire, const BlockAux *auxp, const char *note1, const char *note2) const
{
	struct tm tms;
	char ts[40];

	memset(&tms, 0, sizeof(tms));
	memset(ts, 0, sizeof(ts));

	time_t timestamp = wire->timestamp.GetValue();
	int64_t age = unixtime() - timestamp;

	#ifdef _WIN32
	gmtime_s(&tms, &timestamp);
	#else
	gmtime_r(&timestamp, &tms);
	#endif
	strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tms);

	auto size = ObjSize();

	lock_guard<mutex> lock(g_cerr_lock);
	check_cerr_newline();

	cerr << verb;
	cerr << " block timestamp " << ts;
	cerr << " level " << wire->level.GetValue();
	cerr << " witness " << (unsigned)wire->witness;
	cerr << " skip " << auxp->skip;
	cerr << " size " << (size < 10000 ? " " : "") << (size < 1000 ? " " : "") << (size < 100 ? " " : "") << size;
	cerr << " hash " << buf2hex(&auxp->oid, 3) << "..";
	cerr << " prior " << buf2hex(&wire->prior_oid, 3) << "..";
	cerr << " age " << age;
	cerr << note1;
	cerr << note2;
	cerr << endl;
}

