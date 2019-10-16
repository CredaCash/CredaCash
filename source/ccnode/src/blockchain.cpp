/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * blockchain.cpp
*/

#include "ccnode.h"
#include "blockchain.hpp"
#include "block.hpp"
#include "mints.hpp"
#include "witness.hpp"
#include "commitments.hpp"
#include "processblock.hpp"
#include "dbparamkeys.h"

#include <CCobjects.hpp>
#include <CCcrypto.hpp>
#include <CCmint.h>
#include <transaction.h>
#include <apputil.h>

#include <blake2/blake2.h>
#include <ed25519/ed25519.h>

#define TRACE_BLOCKCHAIN		(g_params.trace_blockchain)
#define TRACE_SERIALNUM_CHECK	(g_params.trace_serialnum_check)
#define TRACE_DELIBLETX_CHECK	(g_params.trace_delibletx_check)

#define TRACE_SIGNING			0

//#define GEN_WITNESS_SIGNING_KEYS	1	// for setup

#ifndef GEN_WITNESS_SIGNING_KEYS
#define GEN_WITNESS_SIGNING_KEYS	0	// already setup
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define CC_MINT_POW_FACTOR	4		// additional POW difficulty during mint

static const char private_key_file_prefix[] = "private_signing_key_witness_";

static const uint32_t genesis_file_tag = 0x01474343;	// CCG\1 in little endian format

BlockChain g_blockchain;

uint16_t genesis_nwitnesses;
uint16_t genesis_maxmal;

static DbConn *Wal_dbconn = NULL;	// not thread safe

void BlockChain::Init()
{
	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::Init";

	Wal_dbconn = new DbConn;

	auto dbconn = Wal_dbconn;

	#define SET_PROOF_PARAM(var, val) (var##_fp = tx_amount_encode((var = val), true, TX_DONATION_BITS, TX_AMOUNT_EXPONENT_BITS))

	SET_PROOF_PARAM(proof_params.minimum_donation,     "2500000000000000000000000");
	SET_PROOF_PARAM(proof_params.donation_per_tx,      "2500000000000000000000000");
	SET_PROOF_PARAM(proof_params.donation_per_byte,      "20000000000000000000000");
	SET_PROOF_PARAM(proof_params.donation_per_output, "20000000000000000000000000");
	SET_PROOF_PARAM(proof_params.donation_per_input,  "10000000000000000000000000");

	#if 0 // for testing
	SET_PROOF_PARAM(proof_params.minimum_donation,		"0");
	SET_PROOF_PARAM(proof_params.donation_per_tx,		"0");
	SET_PROOF_PARAM(proof_params.donation_per_byte,		"0");
	SET_PROOF_PARAM(proof_params.donation_per_output,	"0");
	SET_PROOF_PARAM(proof_params.donation_per_input,	"0");
	#endif

	proof_params.outvalmin = 23;
	proof_params.outvalmax = 23;
	proof_params.invalmax = 23;

	#if 0 // for testing
	proof_params.outvalmin = 0;
	#endif

	SmartBuf genesis_block;

	SetupGenesisBlock(&genesis_block);

	if (!genesis_block)
	{
		const char *msg = "FATAL ERROR BlockChain::Init error creating genesis block";

		return g_blockchain.SetFatalError(msg);
	}

	uint64_t last_indelible_level;

	auto rc = dbconn->BlockchainSelectMax(last_indelible_level);
	if (rc < 0)
	{
		const char *msg = "FATAL ERROR BlockChain::Init error retrieving last indelible level";

		return g_blockchain.SetFatalError(msg);
	}

	if (rc)
	{
		auto rc = SaveGenesisHash(dbconn, genesis_block);
		if (rc) return;

		TxPay txbuf;

		g_processblock.ValidObjsBlockInsert(dbconn, genesis_block, txbuf, true);

		//return g_blockchain.SetFatalError("test abort after genesis block");

		if (Implement_CCMint(g_params.blockchain) && g_witness.WitnessIndex() == 0)
		{
			SmartBuf smartobj(4096);
			auto obj = (CCObject*)smartobj.data();

			auto f = fopen("first_mint.dat", "rb");
			CCASSERT(f);

			auto n = fread(obj->ObjPtr(), 1, smartobj.size() - sizeof(CCObject::Preamble), f);
			CCASSERT(n);

			fclose(f);

			CCASSERT(obj->IsValid());

			obj->SetObjId();

			auto rc = dbconn->ValidObjsInsert(smartobj);
			CCASSERTZ(rc);
		}
	}
	else
	{
		BOOST_LOG_TRIVIAL(info) << "BlockChain::Init last indelible level " << last_indelible_level;

		rc = CheckGenesisHash(dbconn, genesis_block);
		if (rc) return;

		if (Implement_CCMint(g_params.blockchain) && last_indelible_level >= CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
		{
			BOOST_LOG_TRIVIAL(info) << "BlockChain::Init resetting tx_work_difficulty";

			g_params.tx_work_difficulty *= CC_MINT_POW_FACTOR;
		}

		max_mint_level = last_indelible_level;

		m_startup_prune_level = last_indelible_level;

		RestoreLastBlocks(dbconn, last_indelible_level);

		g_commitments.Init(dbconn);
	}

	Wal_dbconn->DbConnPersistData::PersistentData_StartCheckpointing();	// from this point on, Wal_dbconn can't be used for anything else
}

void BlockChain::DeInit()
{
	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DeInit";

	DbConnPersistData::PersistentData_StopCheckpointing();

	delete Wal_dbconn;
}

void BlockChain::SetFatalError(const char *msg)
{
	m_have_fatal_error.store(true);

	BOOST_LOG_TRIVIAL(fatal) << msg;
	cerr << msg << endl;
}

uint64_t BlockChain::ComputePruneLevel(unsigned min_level, unsigned trailing_rounds) const
{
	if (!m_last_indelible_block)
		return min_level;

	auto block = (Block*)m_last_indelible_block.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	unsigned trailing_levels = trailing_rounds * auxp->blockchain_params.nwitnesses;

	uint64_t prune_level = min_level;

	if (wire->level.GetValue() > trailing_levels)
		prune_level = wire->level.GetValue() - trailing_levels;

	if (prune_level < m_startup_prune_level)
		prune_level = m_startup_prune_level;

	return prune_level;
}

void BlockChain::SetupGenesisBlock(SmartBuf *retobj)
{
	retobj->ClearRef();

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::SetupGenesisBlock";

	unsigned size = sizeof(CCObject::Preamble) + sizeof(CCObject::Header) + sizeof(BlockWireHeader);

	auto smartobj = SmartBuf(size);
	if (!smartobj)
	{
		BOOST_LOG_TRIVIAL(error) << "BlockChain::SetupGenesisBlock smartobj failed";

		return;
	}

	auto block = (Block*)smartobj.data();
	CCASSERT(block);

	block->SetSize(sizeof(CCObject::Header) + sizeof(BlockWireHeader));
	block->SetTag(CC_TAG_BLOCK);

	auto auxp = block->SetupAuxBuf(smartobj);
	if (!auxp)
	{
		BOOST_LOG_TRIVIAL(error) << "BlockChain::SetupGenesisBlock SetupAuxBuf failed";

		return;
	}

	if (LoadGenesisDataFiles(auxp))
	{
		const char *msg = "FATAL ERROR BlockChain::SetupGenesisBlock error loading genesis block data";

		return g_blockchain.SetFatalError(msg);
	}

	genesis_nwitnesses = auxp->blockchain_params.nwitnesses;
	genesis_maxmal = auxp->blockchain_params.maxmal;

	auxp->SetConfSigs();

	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetupGenesisBlock blockchain = " << g_params.blockchain;
	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetupGenesisBlock nwitnesses = " << auxp->blockchain_params.nwitnesses;
	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetupGenesisBlock maxmal = " << auxp->blockchain_params.maxmal;
	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetupGenesisBlock nseqconfsigs = " << auxp->blockchain_params.nseqconfsigs;
	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetupGenesisBlock nskipconfsigs = " << auxp->blockchain_params.nskipconfsigs;

	if (Implement_CCMint(g_params.blockchain))
	{
		auxp->blockchain_params.nwitnesses = auxp->blockchain_params.next_nwitnesses = 1;
		auxp->blockchain_params.maxmal = auxp->blockchain_params.next_maxmal = 0;

		auxp->SetConfSigs();

		g_params.tx_work_difficulty /= CC_MINT_POW_FACTOR;	// make POW more difficult during mint
	}

	*retobj = smartobj;
}

void BlockChain::CreateGenesisDataFiles()
{
	auto fd_pub = open_file(g_params.genesis_data_file, O_BINARY | O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
	if (fd_pub == -1) perror(__FILE__ ":" STRINGIFY(__LINE__));
	CCASSERT(fd_pub != -1);

	block_signing_private_key_t privkey;
	block_signing_public_key_t pubkey;
	uint32_t datum;

	cerr << "Creating genesis block data files" << endl;
	//cerr << "sizeof(pubkey) = " << sizeof(pubkey) << endl;
	//cerr << "sizeof(privkey) = " << sizeof(privkey) << endl;

	auto rc = write(fd_pub, &genesis_file_tag, sizeof(genesis_file_tag));
	if (rc == -1) perror(__FILE__ ":" STRINGIFY(__LINE__));
	CCASSERT(rc == sizeof(genesis_file_tag));

	rc = write(fd_pub, &g_params.blockchain, sizeof(g_params.blockchain));
	if (rc == -1) perror(__FILE__ ":" STRINGIFY(__LINE__));
	CCASSERT(rc == sizeof(g_params.blockchain));

	datum = GENESIS_NWITNESSES;
	rc = write(fd_pub, &datum, sizeof(datum));
	if (rc == -1) perror(__FILE__ ":" STRINGIFY(__LINE__));
	CCASSERT(rc == sizeof(datum));

	datum = GENESIS_MAXMAL;
	rc = write(fd_pub, &datum, sizeof(datum));
	if (rc == -1) perror(__FILE__ ":" STRINGIFY(__LINE__));
	CCASSERT(rc == sizeof(datum));

	for (int i = 0; i < GENESIS_NWITNESSES; ++i)
	{
		CCRandom(&privkey, sizeof(privkey));

		ed25519_publickey(&privkey[0], &pubkey[0]);

		rc = write(fd_pub, &pubkey, sizeof(pubkey));
		if (rc == -1) perror(__FILE__ ":" STRINGIFY(__LINE__));
		CCASSERT(rc == sizeof(pubkey));

		char pname[80];
		sprintf(pname, "%s%i.dat", private_key_file_prefix, i);

		auto fd_priv = open(pname, O_BINARY | O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
		if (fd_priv == -1) perror(__FILE__ ":" STRINGIFY(__LINE__));
		CCASSERT(fd_priv != -1);

		rc = write(fd_priv, &privkey, sizeof(privkey));
		if (rc == -1) perror(__FILE__ ":" STRINGIFY(__LINE__));
		CCASSERT(rc == sizeof(privkey));

		rc = close(fd_priv);
		if (rc == -1) perror(__FILE__ ":" STRINGIFY(__LINE__));

		// don't log the private key in the final release
		//if (TRACE_SIGNING) BOOST_LOG_TRIVIAL(debug) << "BlockChain::CreateGenesisDataFiles generated witness " << i << " signing private key " << buf2hex(&privkey, sizeof(privkey));
		if (TRACE_SIGNING) BOOST_LOG_TRIVIAL(debug) << "BlockChain::CreateGenesisDataFiles generated witness " << i << " signing public key " << buf2hex(&pubkey, sizeof(pubkey));
	}

	rc = close(fd_pub);
	if (rc == -1) perror(__FILE__ ":" STRINGIFY(__LINE__));

	cerr << "Genesis block data files created." << endl;
}

bool BlockChain::LoadGenesisDataFiles(BlockAux* auxp)
{
	auto fd = open_file(g_params.genesis_data_file, O_BINARY | O_RDONLY);
	if (fd == -1)
	{
		BOOST_LOG_TRIVIAL(fatal) << "BlockChain::LoadGenesisDataFiles error opening file \"" << w2s(g_params.genesis_data_file) << "\"; " << strerror(errno);

		return true;
	}

	blake2b_ctx ctx;
	int rc = blake2b_init(&ctx, sizeof(auxp->block_hash), NULL, 0);
	CCASSERTZ(rc);

	uint32_t datum;

	rc = read(fd, &datum, sizeof(datum));
	if (rc != sizeof(datum))
		goto genesis_read_error;

	if (datum != genesis_file_tag)
	{
		BOOST_LOG_TRIVIAL(fatal) << "BlockChain::LoadGenesisDataFiles invalid genesis data file \"" << w2s(g_params.genesis_data_file) << "\"";

		return true;
	}

	rc = read(fd, &g_params.blockchain, sizeof(g_params.blockchain));
	if (rc != sizeof(g_params.blockchain))
		goto genesis_read_error;

	rc = read(fd, &datum, sizeof(datum));
	blake2b_update(&ctx, &datum, sizeof(datum));
	if (rc != sizeof(datum))
		goto genesis_read_error;

	auxp->blockchain_params.nwitnesses		= datum;
	auxp->blockchain_params.next_nwitnesses	= datum;

	rc = read(fd, &datum, sizeof(datum));
	blake2b_update(&ctx, &datum, sizeof(datum));
	if (rc != sizeof(datum))
		goto genesis_read_error;

	auxp->blockchain_params.maxmal			= datum;
	auxp->blockchain_params.next_maxmal		= datum;

	for (int i = 0; i < auxp->blockchain_params.nwitnesses; ++i)
	{
		rc = read(fd, &auxp->blockchain_params.signing_keys[i], sizeof(auxp->blockchain_params.signing_keys[i]));
		blake2b_update(&ctx, &auxp->blockchain_params.signing_keys[i], sizeof(auxp->blockchain_params.signing_keys[i]));
		//cerr << "read pubkey " << i << " rc " << rc << " errno " << errno << endl;
		if (rc != sizeof(auxp->blockchain_params.signing_keys[i]))
			goto genesis_read_error;

		if (TRACE_SIGNING) BOOST_LOG_TRIVIAL(debug) << "BlockChain::LoadGenesisDataFiles witness " << i << " signing public key " << buf2hex(&auxp->blockchain_params.signing_keys[i], sizeof(auxp->blockchain_params.signing_keys[i]));
	}

	char byte;
	rc = read(fd, &byte, sizeof(byte));
	if (rc != 0)
	{
		BOOST_LOG_TRIVIAL(fatal) << "BlockChain::LoadGenesisDataFiles unexpected extra data in genesis file";

		return true;
	}

	close(fd);

	blake2b_final(&ctx, &auxp->block_hash);

	if (!IsWitness())
		return false;

	for (int i = 0; i < auxp->blockchain_params.nwitnesses; ++i)
	{
		if (i == g_witness.WitnessIndex() || TEST_SIM_ALL_WITNESSES)
		{
			char pname[80];
			sprintf(pname, "%s%i.dat", private_key_file_prefix, i);

			fd = open(pname, O_BINARY | O_RDONLY, 0);
			if (fd == -1)
			{
				BOOST_LOG_TRIVIAL(fatal) << "BlockChain::LoadGenesisDataFiles error opening file \"" << pname << "\"; " << strerror(errno);

				return true;
			}

			int keynum = 0;
			if (TEST_SIM_ALL_WITNESSES)
				keynum = i;

			//cerr << "keynum " << keynum << " size " << auxp->witness_params.next_signing_private_key.size() << endl;

			rc = read(fd, &auxp->witness_params.next_signing_private_key[keynum], sizeof(auxp->witness_params.next_signing_private_key[keynum]));
			//cerr << "read privkey " << i << " rc " << rc << " errno " << errno << endl;
			if (rc != sizeof(auxp->witness_params.next_signing_private_key[keynum]))
			{
				BOOST_LOG_TRIVIAL(fatal) << "BlockChain::LoadGenesisDataFiles error reading file \"" << pname << "\"; " << strerror(errno);

				return true;
			}

			close(fd);

			// don't log the private key in the final release
			if (TRACE_SIGNING) BOOST_LOG_TRIVIAL(debug) << "BlockChain::LoadGenesisDataFiles witness " << i << " signing private key " << buf2hex(&auxp->witness_params.next_signing_private_key[keynum], sizeof(auxp->witness_params.next_signing_private_key[keynum]));
		}
	}

	return false;

genesis_read_error:

	BOOST_LOG_TRIVIAL(fatal) << "BlockChain::LoadGenesisDataFiles error reading file \"" << w2s(g_params.genesis_data_file) << "\"; " << strerror(errno);

	return true;
}

int BlockChain::SaveGenesisHash(DbConn *dbconn, SmartBuf genesis_block)
{
	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::SaveGenesisHash";

	auto block = (Block*)genesis_block.data();
	auto auxp = block->AuxPtr();

	auto rc = dbconn->BeginWrite();
	if (rc) goto genesis_save_err;

	rc = dbconn->ParameterInsert(DB_KEY_GENESIS_HASH, 0, &auxp->block_hash, sizeof(auxp->block_hash));
	if (rc) goto genesis_save_err;

	rc = dbconn->EndWrite(true);
	if (rc) goto genesis_save_err;

	dbconn->ReleaseMutex();

	return CheckGenesisHash(dbconn, genesis_block);

genesis_save_err:
	const char *msg = "FATAL ERROR BlockChain::SaveGenesisHash error saving genesis block hash";

	g_blockchain.SetFatalError(msg);

	return -1;
}

int BlockChain::CheckGenesisHash(DbConn *dbconn, SmartBuf genesis_block)
{
	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckGenesisHash";

	block_hash_t check;

	auto rc = dbconn->ParameterSelect(DB_KEY_GENESIS_HASH, 0, &check, sizeof(check));
	if (rc)
	{
		const char *msg = "FATAL ERROR BlockChain::CheckGenesisHash error retrieving genesis block hash";

		g_blockchain.SetFatalError(msg);

		return -1;
	}

	auto block = (Block*)genesis_block.data();
	auto auxp = block->AuxPtr();

	if (memcmp(&check, &auxp->block_hash, sizeof(auxp->block_hash)))
	{
		const char *msg = "FATAL ERROR BlockChain::CheckGenesisHash genesis block hash mismatch";

		g_blockchain.SetFatalError(msg);

		return -1;
	}

	return 0;
}

void BlockChain::RestoreLastBlocks(DbConn *dbconn, uint64_t last_indelible_level)
{
	unsigned nblocks = 1;
	SmartBuf nextobj;

	for (unsigned i = 0; i < nblocks && i <= last_indelible_level; ++i)
	{
		auto level = last_indelible_level - i;

		SmartBuf smartobj;

		dbconn->BlockchainSelect(level, &smartobj);
		if (!smartobj)
		{
			const char *msg = "FATAL ERROR BlockChain::RestoreLastBlocks error retrieving block";

			return g_blockchain.SetFatalError(msg);
		}

		if (nextobj)
		{
			auto block = (Block*)nextobj.data();
			block->SetPriorBlock(smartobj);
		}

		nextobj = smartobj;

		auto block = (Block*)smartobj.data();
		auto wire = block->WireData();

		auto auxp = block->SetupAuxBuf(smartobj);
		if (!auxp)
		{
			const char *msg = "FATAL ERROR BlockChain::RestoreLastBlocks SetupAuxBuf failed";

			return g_blockchain.SetFatalError(msg);
		}

		if (!ROTATE_BLOCK_SIGNING_KEYS && LoadGenesisDataFiles(auxp))
		{
			const char *msg = "FATAL ERROR BlockChain::RestoreLastBlocks error loading genesis block data";

			return g_blockchain.SetFatalError(msg);
		}

		auto rc = dbconn->ParameterSelect(DB_KEY_BLOCK_AUX, wire->level.GetValue() & 63, auxp, (uintptr_t)&auxp->blockchain_params + sizeof(auxp->blockchain_params) - (uintptr_t)auxp);
		if (rc)
		{
			const char *msg = "FATAL ERROR BlockChain::RestoreLastBlocks error in ParameterSelect block aux";

			return g_blockchain.SetFatalError(msg);
		}

		if (i == 0)
		{
			m_last_indelible_block = smartobj;
			m_last_indelible_level = last_indelible_level;

			// read enough blocks to run CheckBadSigOrder
			nblocks = (auxp->blockchain_params.next_nwitnesses - auxp->blockchain_params.next_maxmal) / 2 + auxp->blockchain_params.next_maxmal + 1;

			if (dbconn->ValidObjsInsert(smartobj))
			{
				const char *msg = "FATAL ERROR BlockChain::RestoreLastBlocks error in ValidObjsInsert";

				return g_blockchain.SetFatalError(msg);
			}

			if (IsWitness())
			{
				auto rc = dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_BLOCK, smartobj, &wire->prior_oid, wire->level.GetValue(), PROCESS_Q_STATUS_VALID, 0, false, 0, 0);

				if (rc)
				{
					const char *msg = "FATAL ERROR BlockChain::RestoreLastBlocks error in ProcessQEnqueueValidate";

					return g_blockchain.SetFatalError(msg);
				}
			}
		}
	}
}

/*

There are potentially two threads (processblock and witness) entering blocks onto the blockchain and
causing blocks to become indelible, so we have to watch out for race conditions.  Updates are made
in this order:

- An exclusive write lock is taken
- A WAL write transaction is opened on the PersistentDB
- for each new block:
	- auxp->marked_for_indelible = true
	- IndexTxs
		- tx serialnums are added to the PersistentDB
		- IndexTxOutputs
			- commitment is added to Merkle tree
			- TxOutputsInsert = tx address index is updated
	- the Merkle tree is updated
	- BlockchainInsert = the block is added to the PersistentDB
	- block's aux params stored in PersistentDB
- WAL write transaction is committed
- the global LastIndelibleBlock is updated
- the exclusive write lock is released

Note that only LastIndelibleBlock can be relied on to indicate when the block's values will be
reflected in the PersistentDB.  In particular, the auxp->marked_for_indelible flag cannot be
used for that purpose, since it is updated before the PersistentDB.

*/

bool BlockChain::DoConfirmations(DbConn *dbconn, SmartBuf newobj, TxPay& txbuf)
{
	if (m_have_fatal_error.load())
	{
		BOOST_LOG_TRIVIAL(fatal) << "BlockChain::DoConfirmations unable to proceed due to prior fatal error";

		return true;
	}

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmations";

	auto rc = DoConfirmationLoop(dbconn, newobj, txbuf);

	dbconn->EndWrite();

	return rc;
}

bool BlockChain::DoConfirmationLoop(DbConn *dbconn, SmartBuf newobj, TxPay& txbuf)
{
	auto block = (Block*)newobj.data();
	auto wire = block->WireData();

	bool fullcheckpoint = false;

	if (IsWitness() && wire->witness == (unsigned)g_witness.WitnessIndex())
		fullcheckpoint = true;

	bool have_new = false;

	while (true)
	{
		auto rc = DoConfirmOne(dbconn, newobj, txbuf);	// if true, check for another

		if (g_blockchain.HasFatalError())
			return true;

		if (rc)
			break;

		have_new = true;
	}

	if (!have_new)
		return true;

	CCASSERT(m_new_indelible_block);

	auto rc = dbconn->EndWrite(true);
	if (rc)
	{
		const char *msg = "FATAL ERROR BlockChain::DoConfirmations error committing db write";

		g_blockchain.SetFatalError(msg);

		return true;
	}

	block = (Block*)m_new_indelible_block.data();
	wire = block->WireData();

	// careful when using these: m_last_indelible_block and m_last_indelible_level may appear momentarily out-of-sync
	m_last_indelible_block = m_new_indelible_block;
	m_last_indelible_level = wire->level.GetValue();

	m_new_indelible_block.ClearRef();

	dbconn->ReleaseMutex();		// must release before starting the checkpoint

	// start a checkpoint on a worker thread
	DbConnPersistData::PersistentData_StartCheckpoint(fullcheckpoint);

	//g_witness.NotifyNewlyIndelibleBlocks(dbconn, m_last_indelible_block);

	return false;
}

bool BlockChain::DoConfirmOne(DbConn *dbconn, SmartBuf newobj, TxPay& txbuf)
{
	auto block = (Block*)newobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	if (auxp->marked_for_indelible)	// should only happen when nwitness == 1
		return true;

	auto nseqconfsigs = auxp->blockchain_params.nseqconfsigs;
	auto nskipconfsigs = auxp->blockchain_params.nskipconfsigs;

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmOne starting at level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " nwitnesses " << auxp->blockchain_params.nwitnesses << " maxmal " << auxp->blockchain_params.maxmal << " nseqconfsigs " << nseqconfsigs << " nskipconfsigs " << nskipconfsigs << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

	unsigned nconfsigs = 1;

	SmartBuf lastobj = newobj;

	while (true)
	{
		auto prior_block = block->GetPriorBlock();

		if (!prior_block)
			break;

		// peek back one block

		auto expected_level = wire->level.GetValue() - 1;

		block = (Block*)prior_block.data();
		wire = block->WireData();
		auto scan_auxp = block->AuxPtr();

		if (wire->level.GetValue() != expected_level)
		{
			const char *msg = "FATAL ERROR BlockChain::DoConfirmOne block level sequence error";

			BOOST_LOG_TRIVIAL(fatal) << msg << " level " << wire->level.GetValue() << " expected level " << expected_level;

			g_blockchain.SetFatalError(msg);

			return true;
		}

		// stop if next block is marked for indelible

		if (scan_auxp->marked_for_indelible)
		{
			if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmOne stopping at already marked for indelible block level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " oid " << buf2hex(&scan_auxp->oid, sizeof(ccoid_t));

			break;
		}

		// move back and add this block to nconfsigs

		lastobj = prior_block;
		auxp = scan_auxp;

		++nconfsigs;

		if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmOne now have nconfsigs " << nconfsigs << " after scanning block level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));
	}

	if (m_last_indelible_block && (nconfsigs < nseqconfsigs || (auxp->skip && nconfsigs < nskipconfsigs)))
	{
		if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmOne no new indelible block";

		return true;
	}

	block = (Block*)lastobj.data();
	wire = block->WireData();
	auxp = block->AuxPtr();

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmOne new indelible block level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior oid " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

	if (Implement_CCMint(g_params.blockchain) && wire->level.GetValue() == CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
	{
		BOOST_LOG_TRIVIAL(info) << "BlockChain::DoConfirmOne new indelible block level " << wire->level.GetValue() << " resetting tx_work_difficulty";

		g_params.tx_work_difficulty *= CC_MINT_POW_FACTOR;
	}

	return SetNewlyIndelibleBlock(dbconn, lastobj, txbuf);
}

bool BlockChain::SetNewlyIndelibleBlock(DbConn *dbconn, SmartBuf smartobj, TxPay& txbuf)
{
	auto block = (Block*)smartobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	auto level = wire->level.GetValue();
	auto timestamp = wire->timestamp.GetValue();
	auto prior_oid = &wire->prior_oid;

	// BeginWrite will wait for the checkpoint, so we don't need to do this--and more importantly, it will hang if we already hold the write mutex:
		//DbConnPersistData::PersistentData_WaitForFullCheckpoint();

	auto rc = dbconn->BeginWrite();
	if (rc < 0)
	{
		const char *msg = "FATAL ERROR BlockChain::SetNewlyIndelibleBlock error starting db write";

		g_blockchain.SetFatalError(msg);

		return true;
	}

	if (auxp->marked_for_indelible)
	{
		// another thread set this block indelible before we took the BeginWrite lock

		BOOST_LOG_TRIVIAL(trace) << "BlockChain::SetNewlyIndelibleBlock already indelible level " << level << " witness " << (unsigned)wire->witness << " size " << block->ObjSize() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior oid " << buf2hex(prior_oid, sizeof(ccoid_t));

		return true;
	}

	auxp->marked_for_indelible = true;

	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetNewlyIndelibleBlock announced " << auxp->announce_time << " level " << level << " witness " << (unsigned)wire->witness << " size " << block->ObjSize() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior oid " << buf2hex(prior_oid, sizeof(ccoid_t));

	{
		//lock_guard<FastSpinLock> lock(g_cout_lock);
		//cerr << "INDELIBLE BLOCK LEVEL " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " size " << (block->ObjSize() < 1000 ? " " : "") << block->ObjSize() << " oid " << buf2hex(&auxp->oid, 3, 0) << ".. prior " << buf2hex(&wire->prior_oid, 3, 0) << ".." << endl;
	}

	auto last_indelible_block = m_last_indelible_block;
	if (m_new_indelible_block)
		last_indelible_block = m_new_indelible_block;

	if (!last_indelible_block)
	{
		CCASSERTZ(level);
	}
	else
	{
		auto block = (Block*)last_indelible_block.data();
		auto wire = block->WireData();
		auto auxp = block->AuxPtr();

		auto expected_level = wire->level.GetValue() + 1;

		if (level != expected_level || memcmp(prior_oid, &auxp->oid, sizeof(ccoid_t)))
		{
			const char *msg;

			if (level <= expected_level)
				msg = "FATAL ERROR BlockChain::SetNewlyIndelibleBlock two indelible blocks at same level";
			else
				msg = "FATAL ERROR BlockChain::SetNewlyIndelibleBlock blockchain sequence error";

			BOOST_LOG_TRIVIAL(fatal) << msg << "; level " << level << ", expected level " << expected_level << "; prior oid " << buf2hex(prior_oid, sizeof(ccoid_t)) << ", expected prior oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			g_blockchain.SetFatalError(msg);

			return true;
		}
	}

	rc = IndexTxs(dbconn, smartobj, txbuf);
	if (rc)
		return true;

	rc = g_commitments.UpdateCommitTree(dbconn, smartobj, timestamp);
	if (rc)
	{
		const char *msg = "FATAL ERROR BlockChain::SetNewlyIndelibleBlock error updating CommitTree";

		g_blockchain.SetFatalError(msg);

		return true;
	}

	rc = dbconn->BlockchainInsert(level, smartobj);
	if (rc)
	{
		const char *msg = "FATAL ERROR BlockChain::SetNewlyIndelibleBlock error in BlockchainInsert";

		g_blockchain.SetFatalError(msg);

		return true;
	}

	bigint_t split = auxp->total_donations * bigint_t(2UL) / (3UL * auxp->blockchain_params.nwitnesses);

	#if 0
	if (auxp->total_donations)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "level " << level << " total_donations " << hex << auxp->total_donations << " split " << split << dec << endl;
	}

	bigint_t check = auxp->total_donations * bigint_t(2UL) - split * bigint_t(3UL * auxp->blockchain_params.nwitnesses);
	if (check > bigint_t(3UL * auxp->blockchain_params.nwitnesses))
	{
		cerr << "donation split nwitnesses " << auxp->blockchain_params.nwitnesses << " check " << auxp->total_donations * bigint_t(2UL) - split * bigint_t(3UL * auxp->blockchain_params.nwitnesses) << endl;

		const char *msg = "FATAL ERROR BlockChain::SetNewlyIndelibleBlock donation split check failed";

		g_blockchain.SetFatalError(msg);

		return true;
	}
	#endif

	for (int i = 0; i < auxp->blockchain_params.nwitnesses && auxp->total_donations; ++i)
	{
		bigint_t total;
		rc = dbconn->ParameterSelect(DB_KEY_DONATION_TOTALS, i, &total, sizeof(total));
		if (rc > 0)
			total = 0UL;
		else if (rc)
		{
			const char *msg = "FATAL ERROR BlockChain::SetNewlyIndelibleBlock error in ParameterSelect donation total";

			g_blockchain.SetFatalError(msg);

			return true;
		}

		if (i == wire->witness)
			total = total + auxp->total_donations - split * bigint_t(auxp->blockchain_params.nwitnesses - 1UL);
		else
			total = total + split;

		rc = dbconn->ParameterInsert(DB_KEY_DONATION_TOTALS, i, &total, sizeof(total));
		if (rc)
		{
			const char *msg = "FATAL ERROR BlockChain::SetNewlyIndelibleBlock error in ParameterInsert donation total";

			g_blockchain.SetFatalError(msg);

			return true;
		}
	}

	#if MAX_NCONFSIGS > 64
	#error MAX_NCONFSIGS > 64
	#endif

	rc = dbconn->ParameterInsert(DB_KEY_BLOCK_AUX, level & 63, auxp, (uintptr_t)&auxp->blockchain_params + sizeof(auxp->blockchain_params) - (uintptr_t)auxp);
	if (rc)
	{
		const char *msg = "FATAL ERROR BlockChain::SetNewlyIndelibleBlock error in ParameterInsert block aux";

		g_blockchain.SetFatalError(msg);

		return true;
	}

	m_new_indelible_block = smartobj;

	return false;
}

bool BlockChain::IndexTxs(DbConn *dbconn, SmartBuf smartobj, TxPay& txbuf)
{
	auto bufp = smartobj.BasePtr();
	auto block = (Block*)smartobj.data();
	auto wire = block->WireData();
	auto level = wire->level.GetValue();
	auto pdata = block->TxData();
	auto pend = block->ObjEndPtr();

	if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::IndexTxs block level " << level << " bufp " << (uintptr_t)bufp << " objsize " << block->ObjSize() << " pdata " << (uintptr_t)pdata << " pend " << (uintptr_t)pend;

	while (pdata < pend)
	{
		auto txsize = *(uint32_t*)pdata;

		//if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::IndexTxs ptxdata " << (uintptr_t)pdata << " txsize " << txsize << " data " << buf2hex(pdata, 16);

		auto rc = tx_from_wire(txbuf, (char*)pdata, txsize);
		if (rc)
		{
			const char *msg = "FATAL ERROR BlockChain::IndexTxs error parsing indelible block transaction";

			g_blockchain.SetFatalError(msg);

			tx_dump(txbuf, g_bigbuf, sizeof(g_bigbuf));

			BOOST_LOG_TRIVIAL(fatal) << g_bigbuf;

			return true;
		}

		uint64_t merkle_time, next_commitnum;

		rc = dbconn->CommitRootsSelectLevel(txbuf.param_level, false, merkle_time, next_commitnum, &txbuf.tx_merkle_root, TX_MERKLE_BYTES);
		if (rc)
		{
			const char *msg = "FATAL ERROR BlockChain::IndexTxs error in CommitRootsSelectLevel";

			BOOST_LOG_TRIVIAL(fatal) << msg;

			g_blockchain.SetFatalError(msg);

			return true;
		}

		tx_set_commit_iv(txbuf);

		CheckCreatePseudoSerialnum(txbuf, pdata, txsize);

		pdata += txsize;

		for (unsigned i = 0; i < txbuf.nin; ++i)
		{
			if (!txbuf.inputs[i].no_serialnum)
			{
				auto rc = dbconn->SerialnumInsert(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES, &txbuf.inputs[i].S_hashkey, TX_HASHKEY_WIRE_BYTES);
				if (rc)
				{
					const char *msg = "FATAL ERROR BlockChain::IndexTxs error in SerialnumInsert";

					BOOST_LOG_TRIVIAL(fatal) << msg;

					g_blockchain.SetFatalError(msg);

					return true;
				}
			}
		}

		for (unsigned i = 0; i < txbuf.nout; ++i)
		{
			auto rc = IndexTxOutputs(dbconn, level, txbuf, txbuf.outputs[i]);
			if (rc)
			{
				const char *msg = "FATAL ERROR BlockChain::IndexTxs error in TxOutputsInsert";

				g_blockchain.SetFatalError(msg);

				return true;
			}
		}
	}

	return false;
}

void BlockChain::CheckCreatePseudoSerialnum(TxPay& txbuf, const void *wire, const uint32_t bufsize)
{
	for (unsigned i = 0; i < txbuf.nin && txbuf.tag_type != CC_TYPE_MINT; ++i)
	{
		if (!txbuf.inputs[i].no_serialnum)
			return;
	}

	txbuf.nin = 1;
	txbuf.inputs[0].no_serialnum = 0;

	auto obj = (CCObject*)((char*)wire - sizeof(CCObject::Preamble));
	uint32_t type = obj->ObjType();
	CCASSERT(type == CC_TYPE_TXPAY || type == CC_TYPE_MINT);

	auto rc = blake2b(&txbuf.inputs[0].S_serialnum, TX_SERIALNUM_BYTES, &type, sizeof(type), obj->BodyPtr(), obj->BodySize());
	CCASSERTZ(rc);

	if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckCreatePseudoSerialnum created serialnum " << buf2hex(&txbuf.inputs[0].S_serialnum, sizeof(txbuf.inputs[0].S_serialnum)) << " from tx size " << obj->BodySize() << " param_level " << txbuf.param_level << " address[0] " << buf2hex(&txbuf.outputs[0].M_address, sizeof(txbuf.outputs[0].M_address)) << " commitment[0] " << buf2hex(&txbuf.outputs[0].M_commitment, sizeof(txbuf.outputs[0].M_commitment));
}

bool BlockChain::IndexTxOutputs(DbConn *dbconn, const uint64_t level, const TxPay& tx, const TxOut& txout)
{
	auto commitnum = g_commitments.GetNextCommitnum(true);

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::IndexTxOutputs level " << level << " commitnum " << commitnum;

	auto rc = g_commitments.AddCommitment(dbconn, commitnum, txout.M_commitment);
	if (rc)
		return true;

	if (Implement_CCMint(g_params.blockchain) && level <= 1)
		return false;

	uint32_t pool;
	if (TEST_EXTRA_ON_WIRE)
		pool = txout.M_pool;
	else
		pool = g_params.default_pool;

	bool enc_flag = !txout.asset_mask && !txout.amount_mask;	// 1 if values are not encrypted
	pool = (pool << 1) | enc_flag;								// encode enc_flag with M_pool

	if (!txout.no_address)
		dbconn->TxOutputsInsert(&txout.M_address, TX_ADDRESS_BYTES, pool, txout.M_asset_enc, txout.M_amount_enc, tx.param_level, commitnum);	// if this fails, we can still continue

	if (!Implement_CCMint(g_params.blockchain) || tx.tag_type != CC_TYPE_MINT)
		return false;

	for (unsigned i = 0; i < 2; ++i)
	{
		unsigned index = (level % (MINT_OUTPUTS/2)) + i * (MINT_OUTPUTS/2);

		const bigint_t& dest = mint_outputs[index];
		const bigint_t amount = (i ? bigint_t("9000000000000000000000000000000") : bigint_t("40000000000000000000000000000000"));
		const uint32_t pool = (i ? g_params.default_pool : CC_MINT_FOUNDATION_POOL);
		const uint64_t asset = 0;
		const uint32_t paynum = 0;

		bigint_t addr, commitment;

		auto amount_fp = tx_amount_encode(amount, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS, TX_CC_MINT_EXPONENT, TX_CC_MINT_EXPONENT);

		compute_address(dest, g_params.blockchain, paynum, addr);

		compute_commitment(tx.M_commitment_iv, dest, paynum, pool, asset, amount_fp, commitment);

		auto commitnum = g_commitments.GetNextCommitnum(true);

		//cerr << "mint level " << level << " index " << index << " amount_fp " << amount_fp << hex
		//		<< "\n\t\t\t dest " << dest << " addr " << addr
		//		<< "\n\t\t\t commitment " << commitment << " commitnum " << dec << commitnum << endl;

		auto rc = g_commitments.AddCommitment(dbconn, commitnum, commitment);
		if (rc)
			return true;

		if (g_params.index_mint_donations || level <= (MINT_OUTPUTS/2) + CC_MINT_ACCEPT_SPAN)	// index at least one tx to each mint address
		{
			const bool enc_flag = 1;	// 1 if values are not encrypted

			dbconn->TxOutputsInsert(&addr, TX_ADDRESS_BYTES, (pool << 1) | enc_flag, asset, amount_fp, tx.param_level, commitnum);	// if this fails, we can still continue
		}
	}

	return false;
}

// returns true if found (or there's an error)
// if txobj is provided and tx is found in the persistent serialnum db, then txobj is deleted from the validobjs db
// !!! note: this function is not used?
int BlockChain::CheckSerialnums(DbConn *dbconn, SmartBuf topblock, int type, SmartBuf txobj, void *txwire, unsigned txsize, TxPay& txbuf)
{
	auto rc = tx_from_wire(txbuf, (char*)txwire, txsize);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(warning) << "Witness::CheckSerialnums error parsing tx";

		return -1;
	}

	CheckCreatePseudoSerialnum(txbuf, txwire, txsize);

	for (unsigned i = 0; i < txbuf.nin; ++i)
	{
		rc = CheckSerialnum(dbconn, topblock, type, txobj, &txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES);
		if (rc)
			return rc;
	}

	return 0;
}

int BlockChain::CheckSerialnum(DbConn *dbconn, SmartBuf topblock, int type, SmartBuf txobj, const void *serial, unsigned size)
{
	if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckSerialnum starting at block " << (uintptr_t)topblock.BasePtr() << " type " << type << " tx " << (uintptr_t)txobj.BasePtr() << " serialnum " << buf2hex(serial, size);

	// snapshot m_last_indelible_block before reading so value doesn't get ahead of values read from persistent serialnum db
	auto last_indelible_block = m_last_indelible_block;

	// check serialnum's in persistent db

	auto result = dbconn->SerialnumSelect(serial, size);
	if (result < 0)
	{
		BOOST_LOG_TRIVIAL(error) << "BlockChain::CheckSerialnum error checking persistent serialnums";

		return -1;
	}
	else if (!result)
	{
		if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckSerialnum serialnum in persistent db; deleting from validobjs tx " << (uintptr_t)txobj.BasePtr();

		if (txobj)
			dbconn->ValidObjsDeleteObj(txobj);

		return 4;	// !!! don't want to do this yet--need to check if block is on side chain and if so, it's ok?
	}

	// check serialnum's in temp db
	// note: serialnum is not removed from tempdb until the block is pruned, long after it is indelible or no longer in path to indelible
	//	if that were not so, we would have to check tempdb before persistentdb to avoid race condition (serialnum deleted from tempdb before inserted into persistentdb)

	// note 2: when it comes time to validate blocks, we might be looking at a side chain
	// to detect the potential for conflicting indelible blocks, we'll have to look deeper into the blockchain
	// when the block is found in the persistent db:
	// make sure tx is not in chain from topblock to first indelible block at same or lower level than last_indelible,
	// then scan from last_indelible back to same block as above and if tx is in that subchain, the block is valid

	const int BLOCKARRAYSIZE = TEST_SMALL_BUFS ? 2 : 100;
	void *blockparray[BLOCKARRAYSIZE];
	void *last_blockp = 0;
	bool have_more = true;

	while (have_more)
	{
		auto nblocks = dbconn->TempSerialnumSelect(serial, size, last_blockp, blockparray, BLOCKARRAYSIZE);
		if (nblocks < 0)
		{
			BOOST_LOG_TRIVIAL(error) << "BlockChain::CheckSerialnum error checking temp serialnums";

			return -1;
		}

		if (nblocks > BLOCKARRAYSIZE)
			nblocks = BLOCKARRAYSIZE;
		else
			have_more = false;

		for (int i = 0; i < nblocks; ++i)
		{
			if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckSerialnum serialnum " << buf2hex(serial, size) << " tx " << (uintptr_t)txobj.BasePtr() << " found in block " << (uintptr_t)blockparray[i];

			if (blockparray[i] == (void*)(uintptr_t)type)
				return 2;

			if (BlockInChain(blockparray[i], topblock, last_indelible_block))
				return 3;
		}

		last_blockp = blockparray[BLOCKARRAYSIZE-1];
	}

	if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckSerialnum not found in blockchain serialnum " << buf2hex(serial, size);

	return 0;
}

bool BlockChain::BlockInChain(void *find_block, SmartBuf smartobj, SmartBuf last_indelible)
{
	auto last_indelible_block = (Block*)last_indelible.data();
	auto last_indelible_wire = last_indelible_block->WireData();

	while (smartobj)
	{
		auto bufp = smartobj.BasePtr();
		auto block = (Block*)smartobj.data();
		auto wire = block->WireData();

		if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::BlockInChain searching for block " << (uintptr_t)find_block << " at block bufp " << (uintptr_t)bufp << " level " << wire->level.GetValue();

		if (bufp == find_block)
		{
			if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::BlockInChain found block bufp " << (uintptr_t)find_block << " at level " << wire->level.GetValue();

			return true;
		}

		if (wire->level.GetValue() <= last_indelible_wire->level.GetValue())
		{
			if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::BlockInChain terminating search for block " << (uintptr_t)find_block << " at indelible level " << wire->level.GetValue() << " block bufp " << (uintptr_t)bufp;

			break;
		}

		smartobj = block->GetPriorBlock();
	}

	return false;
}

bool BlockChain::ChainHasDelibleTxs(SmartBuf smartobj, uint64_t last_indelible_level)
{
	while (smartobj)
	{
		auto bufp = smartobj.BasePtr();
		auto block = (Block*)smartobj.data();
		auto wire = block->WireData();

		if (wire->level.GetValue() <= last_indelible_level)
			break;

		if (TRACE_DELIBLETX_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::ChainHasDelibleTxs checking block at bufp " << (uintptr_t)bufp << " level " << wire->level.GetValue();

		if (block->HasTx())
		{
			if (TRACE_DELIBLETX_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::ChainHasDelibleTxs result true";

			return true;
		}

		smartobj = block->GetPriorBlock();
	}

	if (TRACE_DELIBLETX_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::ChainHasDelibleTxs result false";

	return false;
}
