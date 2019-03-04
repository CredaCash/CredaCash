/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * block.cpp
*/

#include "ccnode.h"
#include "block.hpp"
#include "blockchain.hpp"
#include "witness.hpp"

#include <blake2/blake2.h>
#include <ed25519/ed25519.h>

#define TRACE_BLOCK			0
#define TRACE_CHECKORDER	0
#define TRACE_CALCSKIPSCORE 0
#define TRACE_CALCOID		0
#define TRACE_SIGNING		0

//#define TEST_SKIP_SIGS		1	// to bypass sigs when they are broken

#ifndef TEST_SKIP_SIGS
#define TEST_SKIP_SIGS		0	// don't skip
#endif

BlockAux* Block::AuxPtr()
{
	auto auxp = preamble.auxp[0];

	CCASSERT(auxp);

	return (BlockAux*)auxp;
}

BlockAux* Block::SetupAuxBuf(SmartBuf smartobj)
{
	auto wire = WireData();

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "Block::SetupAuxBuf level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " prior oid " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

	auto auxp = (BlockAux*)malloc(sizeof(BlockAux));
	if (!auxp)
	{
		BOOST_LOG_TRIVIAL(error) << "Block::SetupAuxBuf malloc failed";

		return NULL;
	}

	memset(auxp, 0, sizeof(BlockAux));

	preamble.auxp[0] = auxp;
	smartobj.SetAuxPtrCount(2);

	auxp->announce_time = ccticks();

	return auxp;
}

#if 0 // use inline version
SmartBuf Block::GetPriorBlock() const
{
	if (TRACE_BLOCK) BOOST_LOG_TRIVIAL(debug) << "Block::GetPriorBlock blockp " << (uintptr_t)this << " prior " << (uintptr_t)preamble.auxp[1];

	return SmartBuf(preamble.auxp[1]);
}
#endif

void Block::SetPriorBlock(SmartBuf priorobj)
{
	if (TRACE_BLOCK) BOOST_LOG_TRIVIAL(debug) << "Block::SetPriorBlock blockp " << (uintptr_t)this << " was " << (uintptr_t)preamble.auxp[1] << " setting to " << (uintptr_t)priorobj.BasePtr();

	SmartBuf(preamble.auxp[1]).DecRef();

	priorobj.IncRef();
	preamble.auxp[1] = priorobj.BasePtr();
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

	auxp->blockchain_params.nwitnesses = prior_auxp->blockchain_params.next_nwitnesses;
	auxp->blockchain_params.maxmal = prior_auxp->blockchain_params.next_maxmal;

	CCASSERT(wire->witness < MAX_NWITNESSES);
	memcpy(&auxp->blockchain_params, &prior_auxp->blockchain_params, sizeof(auxp->blockchain_params));
#if ROTATE_BLOCK_SIGNING_KEYS
	memcpy(&auxp->blockchain_params.signing_keys[wire->witness], &wire->witness_next_signing_public_key, sizeof(wire->witness_next_signing_public_key));
#endif
	memcpy(&auxp->witness_params.next_signing_private_key, &prior_auxp->witness_params.next_signing_private_key, sizeof(auxp->witness_params.next_signing_private_key));

	auxp->SetConfSigs();

	auxp->skip = ComputeSkip(prior_wire->witness, wire->witness, auxp->blockchain_params.nwitnesses);

	if (TRACE_BLOCK) BOOST_LOG_TRIVIAL(trace) << "BlockAux::ChainToPriorBlock level " << wire->level.GetValue() << " nwitnesses " << auxp->blockchain_params.nwitnesses << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " prior witness " << (unsigned)prior_wire->witness << " prior oid " << buf2hex(&prior_auxp->oid, sizeof(ccoid_t));

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

	if (TRACE_CHECKORDER) BOOST_LOG_TRIVIAL(trace) << "Block::CheckBadSigOrder top_witness " << top_witness << " starting at level " << wire->level.GetValue() << " nwitnesses " << nwitnesses << " maxmal " << auxp->blockchain_params.maxmal << " nconfsigs " << nconfsigs << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

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
			const char msg[] = "FATAL ERROR Block::CheckBadSigOrder block level sequence error";

			BOOST_LOG_TRIVIAL(fatal) << msg << " level " << wire->level.GetValue() << " expected level " << expected_level;

			g_blockchain.SetFatalError(msg);

			return true;
		}
	}

	if (skipsum + nconfsigs > nwitnesses)
	{
		if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CheckBadSigOrder block violates dupe/ordering rule; level " << wire->level.GetValue() << " nwitnesses " << nwitnesses << " nsigs " << nsigs << " skipsum " << skipsum << " oid " << buf2hex(&AuxPtr()->oid, sizeof(ccoid_t));

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

	if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore top witness " << top_witness << " test mal " << maltest << " starting at level " << wire->level.GetValue() << " nwitnesses " << auxp->blockchain_params.nwitnesses << " maxmal " << auxp->blockchain_params.maxmal << " nconfsigs " << auxp->blockchain_params.nconfsigs << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " last indelible level " << last_indelible_wire->level.GetValue();

	uint64_t score = 0;
	unsigned scorebits = 0;

	CalcSkipScoreRecursive(last_indelible_wire, genstamp, maltest, score, scorebits);

	if (score && top_witness >= 0)
	{
		if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore before top witness score " << hex << score << dec << " scorebits " << scorebits;

		unsigned nwitnesses = auxp->blockchain_params.nwitnesses;
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
	}

	if (scorebits < MAX_SCORE_BITS)
		score <<= MAX_SCORE_BITS - scorebits;

	if (TRACE_CALCSKIPSCORE) BOOST_LOG_TRIVIAL(trace) << "Block::CalcSkipScore score " << hex << score << dec << " test mal " << maltest << " starting at level " << wire->level.GetValue() << " nwitnesses " << auxp->blockchain_params.nwitnesses << " maxmal " << auxp->blockchain_params.maxmal << " nconfsigs " << auxp->blockchain_params.nconfsigs << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " last indelible level " << last_indelible_wire->level.GetValue();

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
			BOOST_LOG_TRIVIAL(fatal) << "Block::SetOrVerifyOid block oid " << buf2hex(&auxp->oid, sizeof(auxp->oid)) << " hash " << buf2hex(&auxp->block_hash, sizeof(auxp->block_hash)) << " does not match computed hash " << buf2hex(&hash, sizeof(hash));
			CCASSERT(0); raise(SIGTERM);
		}

		if (memcmp(&oid, &auxp->oid, sizeof(auxp->oid)))
		{
			BOOST_LOG_TRIVIAL(fatal) << "Block::SetOrVerifyOid block oid " << buf2hex(&auxp->oid, sizeof(auxp->oid)) << " does not match computed oid " << buf2hex(&oid, sizeof(oid));
			CCASSERT(0); raise(SIGTERM);
		}
	}
}

void BlockAux::SetHash(const block_hash_t& hash)
{
	CCASSERT(sizeof(block_hash) == sizeof(hash));

	memcpy(&block_hash, &hash, sizeof(hash));

	if (TRACE_CALCOID) BOOST_LOG_TRIVIAL(trace) << "BlockAux::SetHash TRACE_CALCOID block with announce time " << announce_time << " set to hash " << buf2hex(&block_hash, sizeof(block_hash));
}

void BlockAux::SetOid(const ccoid_t& soid)
{
	CCASSERT(sizeof(oid) == sizeof(soid));

	memcpy(&oid, &soid, sizeof(oid));

	if (TRACE_CALCOID) BOOST_LOG_TRIVIAL(trace) << "BlockAux::SetOid TRACE_CALCOID block with announce time " << announce_time << " set to oid " << buf2hex(&oid, sizeof(oid));
}

void Block::CalcHash(block_hash_t& block_hash)
{
	auto rc = blake2b(&block_hash, sizeof(block_hash), &header.tag, sizeof(header.tag), BodyPtr() + sizeof(block_signature_t), BodySize() - sizeof(block_signature_t));
	CCASSERTZ(rc);

	if (TRACE_CALCOID) BOOST_LOG_TRIVIAL(trace) << "Block::CalcHash TRACE_CALCOID for block level " << WireData()->level.GetValue() << " size " << ObjSize() << " prior oid " << buf2hex(&(WireData()->prior_oid), sizeof(WireData()->prior_oid)) << " computed block hash " << buf2hex(&block_hash, sizeof(block_hash));
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

	if (TRACE_CALCOID) BOOST_LOG_TRIVIAL(trace) << "Block::CalcOid TRACE_CALCOID for block level " << WireData()->level.GetValue() << " size " << ObjSize() << " prior oid " << buf2hex(&(WireData()->prior_oid), sizeof(WireData()->prior_oid)) << " from block hash " << buf2hex(&block_hash, sizeof(block_hash)) << " computed oid " << buf2hex(&oid, sizeof(oid));
}

bool Block::SignOrVerify(bool verify)
{
	auto wire = WireData();
	auto auxp = AuxPtr();

	auto priorblock = (Block*)GetPriorBlock().data();
	auto prior_auxp = priorblock->AuxPtr();

	if (TEST_SKIP_SIGS)
	{
		if (!verify)
			memset(&wire->signature, 0, sizeof(wire->signature));

		return false;
	}

	BlockSignedData data;
	//cerr << "sizeof(BlockSignedData) = " << sizeof(data) << endl;

	memcpy(&data.prior_block_hash, &prior_auxp->block_hash, sizeof(data.prior_block_hash));
	memcpy(&data.block_hash, &auxp->block_hash, sizeof(data.block_hash));
#if ROTATE_BLOCK_SIGNING_KEYS
	memcpy(&data.witness_next_signing_public_key, &wire->witness_next_signing_public_key, sizeof(data.witness_next_signing_public_key));
#endif
	data.block_size = BodySize();
	data.witness = wire->witness;

	if (wire->witness >= prior_auxp->blockchain_params.nwitnesses)
	{
		if (TRACE_BLOCK) BOOST_LOG_TRIVIAL(info) << "Block::SignOrVerify error witness " << (unsigned)wire->witness << " out of range nwitnesses " << prior_auxp->blockchain_params.nwitnesses;

		return true;
	}

	if (verify)
	{
		//*(uint8_t*)&data ^= 0xff;	// for testing

		if (TRACE_SIGNING)
		{
			BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify verify level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness;
			BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify verify     pubkey " << buf2hex(&prior_auxp->blockchain_params.signing_keys[wire->witness], sizeof(prior_auxp->blockchain_params.signing_keys[wire->witness]));
			BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify signing data " << buf2hex(&data, sizeof(data));
			BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify signature " << buf2hex(&wire->signature, sizeof(wire->signature));
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

		if (TRACE_SIGNING)
		{
			block_signing_public_key_t pubkey;
			ed25519_publickey(&prior_auxp->witness_params.next_signing_private_key[keynum][0], &pubkey[0]);

			BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify sign level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness;
			//@@! don't log the private key in the final release
			BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify sign      privkey " << buf2hex(&prior_auxp->witness_params.next_signing_private_key[keynum], sizeof(prior_auxp->witness_params.next_signing_private_key[keynum]));
			BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify sign       pubkey " << buf2hex(&prior_auxp->blockchain_params.signing_keys[wire->witness], sizeof(prior_auxp->blockchain_params.signing_keys[wire->witness]));
			BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify sign pubkey check " << buf2hex(&pubkey, sizeof(pubkey));
			BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify signing data " << buf2hex(&data, sizeof(data));
			BOOST_LOG_TRIVIAL(debug) << "Block::SignOrVerify signature " << buf2hex(&wire->signature, sizeof(wire->signature));
		}

		return false;
	}
}
