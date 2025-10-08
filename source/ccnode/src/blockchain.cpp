/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * blockchain.cpp
*/

#include "ccnode.h"
#include "blockchain.hpp"
#include "block.hpp"
#include "mints.hpp"
#include "witness.hpp"
#include "commitments.hpp"
#include "exchange.hpp"
#include "exchange_mining.hpp"
#include "processblock.hpp"
#include "processtx.hpp"
#include "process-xreq.hpp"
#include "dbparamkeys.h"

#include <CCobjects.hpp>
#include <CCcrypto.hpp>
#include <CCmint.h>
#include <transaction.h>
#include <unifloat.hpp>
#include <xtransaction-xpay.hpp>
#include <xmatch.hpp>
#include <encode.h>
#include <apputil.h>
#include <BlockChainStatus.hpp>

#include <blake2/blake2.h>

//#define TEST_NO_DONATION	1

//#define TEST_DEBUG_STOP	1

#ifndef TEST_NO_DONATION
#define TEST_NO_DONATION	0	// don't test
#endif

#ifndef TEST_DEBUG_STOP
#define TEST_DEBUG_STOP	0	// don't test
#endif

#define TRACE_BLOCKCHAIN		(g_params.trace_blockchain)
#define TRACE_SERIALNUM_CHECK	(g_params.trace_serialnum_check)
#define TRACE_DELIBLETX_CHECK	(g_params.trace_delibletx_check)
#define TRACE_TRANSACT			(g_params.trace_tx_server)

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

static const uint32_t genesis_file_tag = 0x02474343;	// CCG\2 in little endian format

BlockChain g_blockchain;

uint16_t genesis_nwitnesses;
uint16_t genesis_maxmal;

static DbConn *Wal_dbconn = NULL;	// not thread safe

static bigint_t total_donations = 0UL;	// for testing

static void ShutdownTestThreadProc()
{
	while (1 && (uint64_t)unixtime() > 40 + g_blockchain.GetLastIndelibleTimestamp())
	{
		if (g_blockchain.HasFatalError() || g_shutdown)
			return;

		{
			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
			cerr << unixtime() - g_blockchain.GetLastIndelibleTimestamp() << endl;
		}

		sleep(10);
	}

	//ccsleep(180);

	ccsleep(rand() % 50);
	usleep(rand() & (1024*1024-1));

	start_shutdown();

	lock_guard<mutex> lock(g_cerr_lock);
	check_cerr_newline();
	cerr << "Random shutdown..." << endl;
}

void BlockChain::Init()
{
	BOOST_LOG_TRIVIAL(trace) << "BlockChain::Init";

	CCASSERTZ(TX_TIME_OFFSET % TX_TIME_DIVISOR);

	#define SET_PROOF_PARAM(var, val) (var##_fp = tx_amount_encode((var = val), true, TX_DONATION_BITS, TX_AMOUNT_EXPONENT_BITS))

	SET_PROOF_PARAM(proof_params.minimum_donation,			  "2500000000000000000000000");
	SET_PROOF_PARAM(proof_params.donation_per_tx,			  "2500000000000000000000000");
	SET_PROOF_PARAM(proof_params.donation_per_byte,			    "20000000000000000000000");
	SET_PROOF_PARAM(proof_params.donation_per_output,		 "20000000000000000000000000");
	SET_PROOF_PARAM(proof_params.donation_per_input,		 "10000000000000000000000000");
	SET_PROOF_PARAM(proof_params.donation_per_xcx_req,		"200000000000000000000000000");
	SET_PROOF_PARAM(proof_params.donation_per_xcx_pay,		 "10000000000000000000000000");

	#if TEST_NO_DONATION
	SET_PROOF_PARAM(proof_params.minimum_donation,			"0");
	SET_PROOF_PARAM(proof_params.donation_per_tx,			"0");
	SET_PROOF_PARAM(proof_params.donation_per_byte,			"0");
	SET_PROOF_PARAM(proof_params.donation_per_output,		"0");
	SET_PROOF_PARAM(proof_params.donation_per_input,		"0");
	SET_PROOF_PARAM(proof_params.donation_per_xcx_req,		"0");
	SET_PROOF_PARAM(proof_params.donation_per_xcx_pay,		"0");
	#endif

	proof_params.outvalmin = 23;
	proof_params.outvalmax = 23;
	proof_params.invalmax = 23;

	#if 0 // for testing
	proof_params.outvalmin = 0;
	#endif

	SmartBuf genesis_block;

	SetGenesisBlock(&genesis_block);

	if (!genesis_block)
		return (void)g_blockchain.SetFatalError("BlockChain::Init error creating genesis block");

	Wal_dbconn = new DbConn;

	auto dbconn = Wal_dbconn;

	g_commitments.Init(dbconn);

	g_exchange.Init(dbconn);

	g_exchange_mining.Init();
	g_exchange_mining.Test();

	uint64_t last_indelible_level;

	auto rc = dbconn->BlockchainSelectMax(last_indelible_level);
	if (rc < 0)
		return (void)g_blockchain.SetFatalError("BlockChain::Init error retrieving last indelible level");

	if (rc)
	{
		auto rc = dbconn->BeginWrite();
		if (rc)
			return (void)g_blockchain.SetFatalError("BlockChain::Init BeginWrite error");

		rc = SaveGenesisHash(dbconn, genesis_block);
		if (rc) return;

		rc = g_exchange_mining.SaveMining(dbconn);
		if (rc)
			return (void)g_blockchain.SetFatalError("BlockChain::Init error saving exchanging mining parameters");

		TxPay txbuf;

		if (g_params.blockchain == MAINNET_BLOCKCHAIN)
		{
			cerr << "Loading blockchain history file..." << endl;

			if (LoadHistoryDataFile(dbconn, txbuf))
				return (void)g_blockchain.SetFatalError("BlockChain::Init error loading blockchain history data");

			cerr << "Blockchain history file loaded.\n" << endl;
		}

		g_processblock.ValidObjsBlockInsert(dbconn, genesis_block, txbuf, true); // calls dbconn->EndWrite()

		//return (void)g_blockchain.SetFatalError("test abort after genesis block");

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

		rc = g_exchange_mining.RestoreMining(dbconn);
		if (rc)
			return (void)g_blockchain.SetFatalError("BlockChain::Init error restoring exchanging mining parameters");

		if (Implement_CCMint(g_params.blockchain) && last_indelible_level >= CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
		{
			BOOST_LOG_TRIVIAL(info) << "BlockChain::Init resetting tx_work_difficulty";

			g_params.tx_work_difficulty *= CC_MINT_POW_FACTOR;
		}

		max_mint_level = last_indelible_level;

		m_startup_prune_level = last_indelible_level;

		RestoreLastBlocks(dbconn, last_indelible_level);
	}

	g_exchange.Restore(dbconn);

	rc = g_process_xreqs.Init(dbconn, last_indelible_level, m_last_indelible_timestamp);
	if (rc)
		return (void)g_blockchain.SetFatalError("BlockChain::Init error starting exchange matching");

	if (g_witness.witness_index)
	{
		//new thread(ShutdownTestThreadProc); // for testing
		//if (last_indelible_level) SetFatalError("Post-restore test stop"); // for testing
		//if (last_indelible_level) {bigint_t a=1UL,b; TxPay c; dbconn->BeginWrite(); CreateTxOutputs(dbconn, 1, a, b, 0, c); dbconn->EndWrite(true); dbconn->ReleaseMutex();} // for testing
	}

	(void)ShutdownTestThreadProc;	// surpress unused function warning

	Wal_dbconn->DbConnPersistData::PersistentData_StartCheckpointing();	// from this point on, Wal_dbconn can't be used for anything else
}

void BlockChain::DeInit()
{
	BOOST_LOG_TRIVIAL(trace) << "BlockChain::DeInit";

	DbConnPersistData::PersistentData_StopCheckpointing();

	delete Wal_dbconn;
}

bool BlockChain::SetFatalError(const char *msg)
{
	m_have_fatal_error.store(true);

	BOOST_LOG_TRIVIAL(fatal) << msg;

	lock_guard<mutex> lock(g_cerr_lock);
	check_cerr_newline();
	cerr << "FATAL ERROR: " << msg << endl;

	DebugStop(NULL);

	//start_shutdown(); // for testing

	return true;	// always returns true
}

#define DEBUG_STOP_FILE "ccnode.kill"

void BlockChain::DebugStop(const char *msg)
{
#if TEST_DEBUG_STOP

	if (g_shutdown)
		return;

	auto f = fopen(DEBUG_STOP_FILE, "wb");

	fclose(f);

	if (msg)
	{
		BOOST_LOG_TRIVIAL(fatal) << msg;

		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "DEBUG STOP: " << msg << endl;
	}

#endif
}

static void CheckDebugStop()
{
#if TEST_DEBUG_STOP

	struct stat statbuf;

	auto rc = stat(DEBUG_STOP_FILE, &statbuf);

	if (!rc)
	{
		g_blockchain.SetFatalError("BlockChain debug stop");

		if (!g_shutdown)
			start_shutdown();
	}

#endif
}

uint64_t BlockChain::ComputePruneLevel(unsigned min_level, unsigned trailing_rounds) const
{
	uint64_t last_level = 0;
	unsigned nwitnesses = 0;
	unsigned trailing_levels = 0;

	if (m_last_indelible_block)
	{
		auto block = (Block*)m_last_indelible_block.data();
		auto wire = block->WireData();
		auto auxp = block->AuxPtr();

		last_level = wire->level.GetValue();
		nwitnesses = auxp->blockchain_params.nwitnesses;
		trailing_levels = trailing_rounds * nwitnesses;
	}

	uint64_t prune_level = 0;

	if (last_level > trailing_levels)
		prune_level = last_level - trailing_levels;

	if (prune_level < min_level)
		prune_level = min_level;

	if (prune_level < m_startup_prune_level)
		prune_level = m_startup_prune_level;

	//if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::ComputePruneLevel last_level " << last_level << " trailing_rounds " << trailing_rounds << " nwitnesses " << nwitnesses << " min_level " << min_level << " startup_prune_level " << m_startup_prune_level << " prune_level " << prune_level;

	return prune_level;
}

void BlockChain::SetGenesisBlock(SmartBuf *retobj)
{
	retobj->ClearRef();

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::SetGenesisBlock";

	unsigned size = sizeof(CCObject::Preamble) + sizeof(CCObject::Header) + sizeof(BlockWireHeader);

	auto smartobj = SmartBuf(size);
	if (!smartobj)
	{
		BOOST_LOG_TRIVIAL(error) << "BlockChain::SetGenesisBlock smartobj failed";

		return;
	}

	auto block = (Block*)smartobj.data();
	CCASSERT(block);

	block->SetSize(sizeof(CCObject::Header) + sizeof(BlockWireHeader));
	block->SetTag(CC_TAG_BLOCK);

	auto auxp = block->SetupAuxBuf(smartobj);
	if (!auxp)
	{
		BOOST_LOG_TRIVIAL(error) << "BlockChain::SetGenesisBlock SetupAuxBuf failed";

		return;
	}

	memcpy(&auxp->oid, &g_params.blockchain, sizeof(g_params.blockchain));

	if (LoadGenesisDataFiles(auxp))
		return (void)g_blockchain.SetFatalError("BlockChain::SetGenesisBlock error loading genesis block data");

	genesis_nwitnesses = auxp->blockchain_params.nwitnesses;
	genesis_maxmal = auxp->blockchain_params.maxmal;

	auxp->SetConfSigs();

	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetGenesisBlock blockchain = " << g_params.blockchain;
	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetGenesisBlock nwitnesses = " << auxp->blockchain_params.nwitnesses;
	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetGenesisBlock maxmal = " << auxp->blockchain_params.maxmal;
	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetGenesisBlock nconfsigs = " << auxp->blockchain_params.nconfsigs;
	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetGenesisBlock maxskip = " << auxp->blockchain_params.nwitnesses - auxp->blockchain_params.nconfsigs;
	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetGenesisBlock nseqconfsigs = " << auxp->blockchain_params.nseqconfsigs;
	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetGenesisBlock nskipconfsigs = " << auxp->blockchain_params.nskipconfsigs;

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
		//if (TRACE_SIGNING) BOOST_LOG_TRIVIAL(info) << "BlockChain::CreateGenesisDataFiles generated witness " << i << " signing private key " << buf2hex(&privkey, sizeof(privkey));
		if (TRACE_SIGNING) BOOST_LOG_TRIVIAL(info) << "BlockChain::CreateGenesisDataFiles generated witness " << i << " signing public key " << buf2hex(&pubkey, sizeof(pubkey));
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

		if (TRACE_SIGNING) BOOST_LOG_TRIVIAL(info) << "BlockChain::LoadGenesisDataFiles witness " << i << " signing public key " << buf2hex(&auxp->blockchain_params.signing_keys[i], sizeof(auxp->blockchain_params.signing_keys[i]));
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
			//if (TRACE_SIGNING) BOOST_LOG_TRIVIAL(info) << "BlockChain::LoadGenesisDataFiles witness " << i << " signing private key " << buf2hex(&auxp->witness_params.next_signing_private_key[keynum], sizeof(auxp->witness_params.next_signing_private_key[keynum]));
		}
	}

	return false;

genesis_read_error:

	BOOST_LOG_TRIVIAL(fatal) << "BlockChain::LoadGenesisDataFiles error reading file \"" << w2s(g_params.genesis_data_file) << "\"; " << strerror(errno);

	return true;
}

static int read_and_update_hash(blake2s_ctx *ctx, int fd, void *data, int nbytes)
{
	auto rc = read(fd, data, nbytes);

	if (rc == nbytes)
	{
		blake2s_update(ctx, data, nbytes);

		return 0;
	}

	if (!rc)
		return 1;
	else
		return -1;
}

bool BlockChain::LoadHistoryDataFile(DbConn *dbconn, TxPay& txbuf)
{
	const char *filehash = "\x24\xbc\xe3\x65\x79\x75\x6b\x7b\xa9\x3c\x96\x1a\xe7\x60\x15\xb9\x18\x57\x0d\xee\xcb\x6f\xdc\x26\x7f\xd0\xfe\xe2\x5d\x97\x1c\x93";

	auto fd = open_file(g_params.history_data_file, O_BINARY | O_RDONLY);
	if (fd == -1)
	{
		BOOST_LOG_TRIVIAL(fatal) << "BlockChain::LoadHistoryDataFile error opening file \"" << g_params.history_data_file << "\"; " << strerror(errno);

		return true;
	}

	blake2s_ctx ctx;
	char hash[32];

	auto rc = blake2s_init(&ctx, sizeof(hash), NULL, 0);
	CCASSERTZ(rc);

	uint64_t level = -1;
	bigint_t last_root = 0UL;

	for (unsigned i = 0; !g_shutdown; ++i)
	{
		bigint_t donation;

		auto rc = read_and_update_hash(&ctx, fd, &donation, sizeof(donation));
		if (rc) goto history_read_error;

		if (!donation)
			break;

		BOOST_LOG_TRIVIAL(debug) << "BlockChain::LoadHistoryDataFile witness " << i << " donation total " << donation;

		packed_unsigned_amount_t packed_amount;

		CCASSERTZ(pack_unsigned_amount(donation, packed_amount));

		rc = dbconn->ParameterInsert(DB_KEY_DONATION_TOTALS, i, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES);
		if (rc) return true;
	}

	while (!g_shutdown)
	{
		bigint_t serialnum = 0UL;

		auto rc = read_and_update_hash(&ctx, fd, &serialnum, TX_SERIALNUM_BYTES);
		if (rc) goto history_read_error;

		if (!serialnum)
			break;

		rc = dbconn->SerialnumInsert(&serialnum, TX_SERIALNUM_BYTES, NULL, 0, 0);
		if (rc) return true;
	}

	CCASSERT(1 == g_commitments.GetNextCommitnum(false));

	while (!g_shutdown)
	{
		bigint_t address = 0UL, root = 0UL, commitment = 0UL;
		uint64_t asset = 0, amount = 0;

		auto rc = read_and_update_hash(&ctx, fd, &address, TX_ADDRESS_BYTES);
		if (rc < 0) goto history_read_error;
		if (rc) break;

		rc = read_and_update_hash(&ctx, fd, &asset, TX_ASSET_WIRE_BYTES);
		if (rc) goto history_read_error;

		rc = read_and_update_hash(&ctx, fd, &amount, TX_AMOUNT_BYTES);
		if (rc) goto history_read_error;

		rc = read_and_update_hash(&ctx, fd, &root, TX_COMMIT_IV_BYTES);
		if (rc) goto history_read_error;

		rc = read_and_update_hash(&ctx, fd, &commitment, TX_COMMITMENT_BYTES);
		if (rc) goto history_read_error;

		uint64_t domain = (asset ? 2 : 3);

		auto commitnum = g_commitments.GetNextCommitnum(true);

		//if (commitnum > 100) return true; // for testing

		rc = g_commitments.AddCommitment(dbconn, commitnum, commitment);
		if (rc) return true;

		if (last_root != root)
		{
			if (!last_root)
			{
				// insert one root immediately, because CreateTxOutputs (called below) needs a root at level <= m_last_indelible_level

				last_root = root;
			}

			rc = dbconn->CommitRootsInsert(level, 0, commitnum, &last_root, TX_COMMIT_IV_BYTES);
			if (rc) return true;

			--level;
			last_root = root;
		}

		rc = dbconn->TxOutputInsert(&address, TX_ADDRESS_BYTES, domain, asset, amount, level, commitnum);
		if (rc) return true;

		for (unsigned i = 0; i < 2 && commitnum < 600000; ++i)
		{
			unsigned index = ((commitnum/3) % (MINT_OUTPUTS/2)) + i * (MINT_OUTPUTS/2);

			//auto dest = bigint_t("7444695303708942777242326334479445182769755803439347948974496095141180830954"); // for testing
			const bigint_t& dest = mint_outputs[index];
			bigint_t amount = (i ? bigint_t("9000000000000000000000000000000") : bigint_t("40000000000000000000000000000000"));
			uint32_t domain = (i ? g_params.default_domain : CC_MINT_FOUNDATION_DOMAIN);
			bool bindex = (g_params.index_mint_donations || (commitnum/3) < (MINT_OUTPUTS/2));	// index at least one tx to each mint address

			//cout << "mint commitnum " << commitnum << " index " << index << " domain " << domain << " bindex " << bindex << " amount " << amount << endl;

			auto rc = CreateTxOutputs(dbconn, 0, amount, dest, domain, txbuf, bindex, true, 0, true, true);
			if (rc)
				return true;
		}
	}

	rc = dbconn->CommitRootsInsert(level, 0, 0, &last_root, TX_COMMIT_IV_BYTES);
	if (rc)
		return true;

	blake2s_final(&ctx, &hash);

	if (memcmp(filehash, hash, sizeof(hash)))
	{
		BOOST_LOG_TRIVIAL(fatal) << "BlockChain::LoadHistoryDataFile file hash mismatch \\" << buf2hex(hash, sizeof(hash), '\\');

		return true;
	}

	return false;

history_read_error:

	BOOST_LOG_TRIVIAL(fatal) << "BlockChain::LoadHistoryDataFile error reading file \"" << g_params.history_data_file << "\"; " << strerror(errno);

	return true;

}

int BlockChain::SaveGenesisHash(DbConn *dbconn, SmartBuf genesis_block)
{
	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::SaveGenesisHash";

	auto block = (Block*)genesis_block.data();
	auto auxp = block->AuxPtr();

	bigint_t commitment = 0UL;
	auto commitnum = g_commitments.GetNextCommitnum(true);
	CCASSERTZ(commitnum);

	auto rc = dbconn->ParameterInsert(DB_KEY_GENESIS_HASH, 0, &auxp->block_hash, sizeof(auxp->block_hash));
	if (rc) goto genesis_save_err;

	rc = g_commitments.AddCommitment(dbconn, commitnum, commitment);
	if (rc) goto genesis_save_err;

	return CheckGenesisHash(dbconn, genesis_block);

genesis_save_err:

	g_blockchain.SetFatalError("BlockChain::SaveGenesisHash error saving genesis block hash");

	return -1;
}

int BlockChain::CheckGenesisHash(DbConn *dbconn, SmartBuf genesis_block)
{
	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckGenesisHash";

	block_hash_t check;

	auto rc = dbconn->ParameterSelect(DB_KEY_GENESIS_HASH, 0, &check, sizeof(check));
	if (rc)
	{
		g_blockchain.SetFatalError("BlockChain::CheckGenesisHash error retrieving genesis block hash");

		return -1;
	}

	auto block = (Block*)genesis_block.data();
	auto auxp = block->AuxPtr();

	if (memcmp(&check, &auxp->block_hash, sizeof(auxp->block_hash)))
	{
		g_blockchain.SetFatalError("BlockChain::CheckGenesisHash genesis block hash mismatch");

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
			return (void)g_blockchain.SetFatalError("BlockChain::RestoreLastBlocks error retrieving block");

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
			return (void)g_blockchain.SetFatalError("BlockChain::RestoreLastBlocks SetupAuxBuf failed");

		if (!ROTATE_BLOCK_SIGNING_KEYS && LoadGenesisDataFiles(auxp))
			return (void)g_blockchain.SetFatalError("BlockChain::RestoreLastBlocks error loading genesis block data");

		auto rc = dbconn->ParameterSelect(DB_KEY_BLOCK_AUX, level & 63, auxp, (uintptr_t)&auxp->blockchain_params + sizeof(auxp->blockchain_params) - (uintptr_t)auxp);
		if (rc)
			return (void)g_blockchain.SetFatalError("BlockChain::RestoreLastBlocks error in ParameterSelect block aux");

		if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::RestoreLastBlocks level " << level << " nwitnesses " << auxp->blockchain_params.nwitnesses << " witness " << (unsigned)wire->witness << " skip " << auxp->skip;

		if (i == 0)
		{
			SetLastIndelible(smartobj);

			m_last_indelible_ticks -= (unixtime() - m_last_indelible_timestamp) * CCTICKS_PER_SEC;

			// read enough blocks to run CheckBadSigOrder
			nblocks = (auxp->blockchain_params.next_nwitnesses - auxp->blockchain_params.next_maxmal) / 2 + auxp->blockchain_params.next_maxmal + 1;

			if (dbconn->ValidObjsInsert(smartobj))
				return (void)g_blockchain.SetFatalError("BlockChain::RestoreLastBlocks error in ValidObjsInsert");

			if (IsWitness())
			{
				auto rc = dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_BLOCK, smartobj, &wire->prior_oid, wire->level.GetValue(), PROCESS_Q_STATUS_VALID, PROCESS_Q_PRIORITY_BLOCK_HI, false, 0, 0);

				if (rc)
					return (void)g_blockchain.SetFatalError("BlockChain::RestoreLastBlocks error in ProcessQEnqueueValidate");
			}
		}
	}
}

static FastSpinLock last_indelible_lock(__FILE__, __LINE__);

void BlockChain::SetLastIndelible(SmartBuf smartobj)
{
	CCASSERT(smartobj);

	auto block = (Block*)smartobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	lock_guard<FastSpinLock> lock(last_indelible_lock);

	m_last_indelible_block = smartobj;

	auto level = wire->level.GetValue();
	if (m_last_indelible_level)
		CCASSERT(level <= m_last_indelible_level + auxp->blockchain_params.nskipconfsigs);
	m_last_indelible_level = level;
	
	m_last_indelible_timestamp = wire->timestamp.GetValue();

	m_last_matching_completed_block_time = g_process_xreqs.m_last_matched_block_time;
	m_last_matching_start_block_time = g_process_xreqs.m_matching_block_time.load();

	m_last_indelible_ticks = ccticks();

	cc_alpha_set_default_decode_tables(m_last_indelible_timestamp);

	if (TRACE_BLOCKCHAIN || TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << "BlockChain::SetLastIndelible last_indelible_level " << m_last_indelible_level << " last_matching_start_block_time " << m_last_matching_start_block_time;
}

void BlockChain::GetLastIndelibleValues(BlockChainStatus& blockchain_status)
{
	lock_guard<FastSpinLock> lock(last_indelible_lock);

	blockchain_status.last_indelible_level = m_last_indelible_level;
	blockchain_status.last_indelible_timestamp = m_last_indelible_timestamp;
	blockchain_status.last_matching_completed_block_time = m_last_matching_completed_block_time;
	blockchain_status.last_matching_start_block_time = m_last_matching_start_block_time;

	if (TRACE_TRANSACT) BOOST_LOG_TRIVIAL(debug) << "BlockChain::GetLastIndelibleValues last_indelible_level " << m_last_indelible_level << " last_matching_start_block_time " << m_last_matching_start_block_time;
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
			- TxOutputInsert = tx address index is updated
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

// returns true on error
bool BlockChain::DoConfirmations(DbConn *dbconn, SmartBuf newobj, TxPay& txbuf)
{
	if (HasFatalError())
	{
		BOOST_LOG_TRIVIAL(fatal) << "BlockChain::DoConfirmations unable to proceed due to prior fatal error";

		return true;
	}

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmations";

	if (!m_last_indelible_level)
		m_last_indelible_ticks = ccticks();

	auto rc = DoConfirmationLoop(dbconn, newobj, txbuf);

	dbconn->EndWrite();

	CheckDebugStop();

	return rc;
}

// returns true on error
// on g_shutdown, returns immediately without committing changes
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

		if (g_blockchain.HasFatalError() || g_shutdown)
			return true;

		if (rc)
			break;

		have_new = true;
	}

	if (!have_new)
		return true;

	CCASSERT(m_new_indelible_block);

	auto rc = g_exchange_mining.SaveMining(dbconn);
	if (rc)
		return g_blockchain.SetFatalError("BlockChain::DoConfirmations error saving exchanging mining parameters");

	rc = dbconn->EndWrite(true);
	if (rc)
		return g_blockchain.SetFatalError("BlockChain::DoConfirmations error committing db write");

	SetLastIndelible(m_new_indelible_block);	// for consistency, call after EndWrite

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

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmOne starting at level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " nwitnesses " << auxp->blockchain_params.nwitnesses << " maxmal " << auxp->blockchain_params.maxmal << " nseqconfsigs " << nseqconfsigs << " nskipconfsigs " << nskipconfsigs << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE);

	unsigned nconfsigs = 1;

	SmartBuf lastobj = newobj;

	while (true)
	{
		if (g_shutdown)
			return true;

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
			const char *msg = "BlockChain::DoConfirmOne block level sequence error";

			BOOST_LOG_TRIVIAL(fatal) << msg << " level " << wire->level.GetValue() << " expected level " << expected_level;

			return g_blockchain.SetFatalError(msg);
		}

		// stop if next block is marked indelible

		if (scan_auxp->marked_for_indelible)
		{
			if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmOne stopping at already marked indelible block level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " oid " << buf2hex(&scan_auxp->oid, CC_OID_TRACE_SIZE);

			break;
		}

		// move back and add this block to nconfsigs

		lastobj = prior_block;
		auxp = scan_auxp;

		++nconfsigs;

		if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmOne now have nconfsigs " << nconfsigs << " after scanning block level " << wire->level.GetValue() << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE);
	}

	if (m_last_indelible_block && (nconfsigs < nseqconfsigs || (auxp->skip && nconfsigs < nskipconfsigs)))
	{
		if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmOne no new indelible block";

		return true;
	}

	block = (Block*)lastobj.data();
	wire = block->WireData();
	auxp = block->AuxPtr();

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::DoConfirmOne new indelible block level " << wire->level.GetValue() << " timestamp " << wire->timestamp.GetValue() << " witness " << (unsigned)wire->witness << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " prior oid " << buf2hex(&wire->prior_oid, CC_OID_TRACE_SIZE);

	if (Implement_CCMint(g_params.blockchain) && wire->level.GetValue() == CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
	{
		BOOST_LOG_TRIVIAL(info) << "BlockChain::DoConfirmOne new indelible block level " << wire->level.GetValue() << " resetting tx_work_difficulty";

		g_params.tx_work_difficulty *= CC_MINT_POW_FACTOR;
	}

	return SetNewlyIndelibleBlock(dbconn, lastobj, txbuf);
}

bool BlockChain::SetNewlyIndelibleBlock(DbConn *dbconn, SmartBuf smartobj, TxPay& txbuf)
{
	auto t0 = ccticks();

	auto block = (Block*)smartobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	auto level = wire->level.GetValue();
	auto timestamp = wire->timestamp.GetValue();
	auto prior_oid = &wire->prior_oid;

	if (0 && level > 5000)
	{
		DebugStop("Reached test end level");
		CCASSERT(0);
	}

	// BeginWrite will wait for the checkpoint, so we don't need to call PersistentData_WaitForFullCheckpoint.
	// More importantly, it will hang if we already hold the write mutex:
	//DbConnPersistData::PersistentData_WaitForFullCheckpoint();

	auto rc = dbconn->BeginWrite();
	if (rc < 0)
		return g_blockchain.SetFatalError("BlockChain::SetNewlyIndelibleBlock error starting db write");

	if (auxp->marked_for_indelible)
	{
		// another thread set this block indelible before we took the BeginWrite lock

		BOOST_LOG_TRIVIAL(trace) << "BlockChain::SetNewlyIndelibleBlock already indelible level " << level << " timestamp " << timestamp << " witness " << (unsigned)wire->witness << " size " << block->ObjSize() << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " prior oid " << buf2hex(prior_oid, CC_OID_TRACE_SIZE);

		return true;
	}

	auxp->marked_for_indelible = true;

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
				msg = "BlockChain::SetNewlyIndelibleBlock two indelible blocks at same level";
			else
				msg = "BlockChain::SetNewlyIndelibleBlock blockchain sequence error";

			BOOST_LOG_TRIVIAL(fatal) << msg << "; level " << level << ", expected level " << expected_level << "; prior oid " << buf2hex(prior_oid, CC_OID_TRACE_SIZE) << ", expected prior oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE);

			return g_blockchain.SetFatalError(msg);
		}
	}

	// snapshot next_xreqnum to use when pruning xreq's (newly persistent xreqs will have xreqnum's >= new_xreqnum)

	auto new_xreqnum = g_exchange.GetNextXreqnum();

	// process all tx's and msgs in this block [may create TxOutputs]

	rc = IndexTxs(dbconn, timestamp, smartobj, txbuf);
	if (rc)
		return true;

	// find matching xreq's

	rc = g_process_xreqs.SynchronizeMatching(dbconn, level, timestamp, new_xreqnum, txbuf);
	if (rc)
		return g_blockchain.SetFatalError("BlockChain::SetNewlyIndelibleBlock error updating exchange matches");

	rc = g_exchange.SaveNextNums(dbconn, level, timestamp);
	if (rc)
		return g_blockchain.SetFatalError("BlockChain::SetNewlyIndelibleBlock error saving exchange sequence numbers");

	// settle matches that are past the payment deadline (blocktime > payment deadline) [may create TxOutputs]

	rc = ExpireMatches(dbconn, timestamp, txbuf);
	if (rc)
		return g_blockchain.SetFatalError("BlockChain::SetNewlyIndelibleBlock error in ExpireMatches");

	// delete MatchingReqs that are no longer needed (delete_time >= blocktime)

	PruneMatchingReqs(dbconn, timestamp);

	// finish

	rc = g_commitments.UpdateCommitTree(dbconn, smartobj, timestamp);
	if (rc)
		return g_blockchain.SetFatalError("BlockChain::SetNewlyIndelibleBlock error updating CommitTree");

	rc = dbconn->BlockchainInsert(level, smartobj);
	if (rc)
		return g_blockchain.SetFatalError("BlockChain::SetNewlyIndelibleBlock error in BlockchainInsert");

	total_donations = total_donations + auxp->total_donations;

	auto nwitnesses = auxp->blockchain_params.nwitnesses;
	CCASSERT(nwitnesses);

	bigint_t big_split = auxp->total_donations;
	bigint_t little_split = big_split * bigint_t(2UL) / (3UL * nwitnesses);
	bigint_t little_sum = bigint_t(nwitnesses - 1UL) * little_split;
	if (big_split > little_sum)
		big_split = big_split - little_sum;
	else
		little_split = 0UL;

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::SetNewlyIndelibleBlock nwitnesses " << nwitnesses << " total_donations " << auxp->total_donations << " big_split " << big_split << " little_split " << little_split;

	for (int i = 0; i < nwitnesses && auxp->total_donations; ++i)
	{
		bigint_t total;
		packed_unsigned_amount_t packed_amount;

		rc = dbconn->ParameterSelect(DB_KEY_DONATION_TOTALS, i, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES);
		if (!rc)
			unpack_unsigned_amount(packed_amount, total);
		else if (rc > 0)
			total = 0UL;
		else
			return g_blockchain.SetFatalError("BlockChain::SetNewlyIndelibleBlock error in ParameterSelect donation total");

		if (i == wire->witness)
			total = total + big_split;
		else
			total = total + little_split;

		CCASSERTZ(pack_unsigned_amount(total, packed_amount));

		rc = dbconn->ParameterInsert(DB_KEY_DONATION_TOTALS, i, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES);
		if (rc)
			return g_blockchain.SetFatalError("BlockChain::SetNewlyIndelibleBlock error in ParameterInsert donation total");
	}

	#if MAX_NCONFSIGS > 64
	#error MAX_NCONFSIGS > 64
	#endif

	rc = dbconn->ParameterInsert(DB_KEY_BLOCK_AUX, level & 63, auxp, (uintptr_t)&auxp->blockchain_params + sizeof(auxp->blockchain_params) - (uintptr_t)auxp);
	if (rc)
		return g_blockchain.SetFatalError("BlockChain::SetNewlyIndelibleBlock error in ParameterInsert block aux");


	BOOST_LOG_TRIVIAL(info) << "BlockChain::SetNewlyIndelibleBlock announced " << auxp->announce_ticks << " level " << level << " timestamp " << timestamp << " witness " << (unsigned)wire->witness << " size " << block->ObjSize() << " oid " << buf2hex(&auxp->oid, CC_OID_TRACE_SIZE) << " prior oid " << buf2hex(prior_oid, CC_OID_TRACE_SIZE) << " donations " << auxp->total_donations << " running total " << total_donations << " processing elapsed ticks " << ccticks_elapsed(t0, ccticks()) << " xreq_count " << dbconn->XreqCountPersistent();

	//block->ConsoleAnnounce("INDELIBLE", wire, auxp);

	m_new_indelible_block = smartobj;

	return false;
}

bool BlockChain::IndexTxs(DbConn *dbconn, uint64_t blocktime, SmartBuf smartobj, TxPay& txbuf)
{
	auto bufp = smartobj.BasePtr();
	auto block = (Block*)smartobj.data();
	auto auxp = block->AuxPtr();
	auto wire = block->WireData();
	auto level = wire->level.GetValue();
	auto pdata = block->TxData();
	auto pend = block->ObjEndPtr();

	if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::IndexTxs blocktime " << blocktime << " blocklevel " << level << " bufp " << (uintptr_t)bufp << " objsize " << block->ObjSize() << " pdata " << (uintptr_t)pdata << " pend " << (uintptr_t)pend;

	while (pdata < pend && !g_shutdown)
	{
		auto txsize = *(uint32_t*)pdata;

		//if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::IndexTxs ptxdata " << (uintptr_t)pdata << " txsize " << txsize << " data " << buf2hex(pdata, 16);

		if (IsWitness())
		{
			// delete obj from ValidObjs so witness won't spend time trying to add it to another block

			ccoid_t oid;
			SmartBuf obj;

			CCObject::ComputeMessageObjId(pdata, &oid);

			auto rc = dbconn->ValidObjsGetObj(oid, &obj);

			//BOOST_LOG_TRIVIAL(info) << "BlockChain::IndexTxs ValidObjsGetObj " << buf2hex(&oid, CC_OID_TRACE_SIZE) << " returned " << rc;

			if (!rc)
			{
				auto rc = dbconn->ValidObjsDeleteObj(obj);
				(void)rc;

				//BOOST_LOG_TRIVIAL(info) << "BlockChain::IndexTxs ValidObjsDeleteObj " << buf2hex(&oid, CC_OID_TRACE_SIZE) << " returned " << rc;
			}
		}

		auto rc = tx_from_wire(txbuf, (char*)pdata, txsize);
		if (rc)
		{
			g_blockchain.SetFatalError("BlockChain::IndexTxs error parsing indelible block transaction");

			tx_dump(txbuf, g_bigbuf, sizeof(g_bigbuf));

			BOOST_LOG_TRIVIAL(fatal) << g_bigbuf;

			return true;
		}

		uint64_t merkle_time, next_commitnum;

		rc = dbconn->CommitRootsSelectLevel(txbuf.param_level, false, merkle_time, next_commitnum, &txbuf.tx_merkle_root, TX_MERKLE_BYTES); // place tx_merkle_root into txbuf
		if (rc)
			return g_blockchain.SetFatalError("BlockChain::IndexTxs error in CommitRootsSelectLevel");

		tx_set_commit_iv(txbuf);

		auto xtx = ProcessTx::ExtractXtx(dbconn, txbuf);
		if (!xtx && ProcessTx::ExtractXtxFailed(txbuf))
			return true;

		CheckCreatePseudoSerialnum(txbuf, xtx, pdata, true);

		if (Xtx::TypeIsXreq(txbuf.tag_type))
		{
			auto rc = AddXreq(dbconn, blocktime, *Xreq::Cast(xtx), (char*)pdata);

			if (rc)
				return g_blockchain.SetFatalError("BlockChain::IndexTxs error in AddXreq");
		}

		pdata += txsize;

		auto tx_commitnum = g_commitments.GetNextCommitnum();

		bool have_serialnum = false;

		for (unsigned i = 0; i < txbuf.nin; ++i)
		{
			if (!txbuf.inputs[i].no_serialnum)
			{
				have_serialnum = true;

				auto rc = dbconn->SerialnumInsert(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES, &txbuf.inputs[i].S_hashkey, TX_HASHKEY_WIRE_BYTES, tx_commitnum);
				if (rc)
					return g_blockchain.SetFatalError("BlockChain::IndexTxs error in SerialnumInsert");
			}
		}

		CCASSERT(have_serialnum);

		for (unsigned i = 0; i < txbuf.nout; ++i)
		{
			auto rc = IndexTxOutputs(dbconn, level, txbuf, txbuf.outputs[i]);
			if (rc)
				return g_blockchain.SetFatalError("BlockChain::IndexTxs error in IndexTxOutputs");
		}

		bigint_t donation = 0UL;

		if (Xtx::TypeIsXpay(txbuf.tag_type))
		{
			auto rc = ProcessXpayment(dbconn, blocktime, *Xpay::Cast(xtx), donation, txbuf);
			if (rc)
				return g_blockchain.SetFatalError("BlockChain::IndexTxs error in ProcessXpayment");
		}
		else if (txbuf.tag_type != CC_TYPE_MINT)
		{
			tx_amount_decode(txbuf.donation_fp, donation, true, TX_DONATION_BITS, TX_AMOUNT_EXPONENT_BITS);
		}

		auxp->total_donations = auxp->total_donations + donation;
	}

	return false;
}

void BlockChain::CheckCreatePseudoSerialnum(TxPay& txbuf, shared_ptr<Xtx>& xtx, const void *wire, bool bperistent)
{
	auto type = txbuf.tag_type;

	bool xchain_sell = (!bperistent && Xtx::TypeIsXreq(type) && Xtx::TypeIsCrosschain(type) && Xtx::TypeIsSeller(type));

	bool need_pseudo = (xchain_sell || type == CC_TYPE_MINT);

	for (unsigned i = 0; i < txbuf.nin && !need_pseudo; ++i)
	{
		if (!txbuf.inputs[i].no_serialnum)
			return;							// have a serialnum, so don't need a pseudo serialnum
	}

	if (type == CC_TYPE_MINT)
		--txbuf.nin;

	unsigned i = txbuf.nin++;
	CCASSERT(i < TX_MAXIN);					// also checked in ValidateXreq

	txbuf.inputs[i].no_serialnum = 0;

	auto obj = (CCObject*)((char*)wire - sizeof(CCObject::Preamble));
	uint64_t objtype = obj->ObjType();

	CCASSERT(objtype);

	if (xchain_sell)
	{
		// for a crosschain sell request, the serialnum is set to the hash of the foreign_address
		//	 to ensure all active foreign_address's are unique so that no buyer can claim another buyer's foreign payment

		auto xreq = Xreq::Cast(xtx);

		//if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckCreatePseudoSerialnum for " << xreq->DebugString();

		CCASSERT(xreq->foreign_address.length());

		auto rc = blake2b(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES, NULL, 0, xreq->foreign_address.c_str(), xreq->foreign_address.length());
		CCASSERTZ(rc);

		//CCRandom(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES);	// for testing

		if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckCreatePseudoSerialnum created serialnum " << buf2hex(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES) << " from " << xreq->foreign_address << " ; " << xreq->DebugString();
	}
	else if (Xtx::TypeIsXpay(type))
	{
		// for an Xpay, the serialnum is set to the payment id hash, to prevent any other tx from claiming the same payment on the foreign blockchain
		//		the hashkey is set to the tx hash, which allows the wallet to be informed if the foreign payment was sent by a different tx, or this same tx

		auto xpay = Xpay::Cast(xtx);

		//if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckCreatePseudoSerialnum for " << xpay->DebugString();

		xpay->ComputePaymentIdHash(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES);

		auto rc = blake2b(&txbuf.inputs[i].S_hashkey, TX_HASHKEY_WIRE_BYTES, &objtype, sizeof(objtype), obj->BodyPtr(), obj->BodySize());
		CCASSERTZ(rc);

		//CCRandom(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES);	// for testing

		if                (TRACE_XPAYS)  BOOST_LOG_TRIVIAL(info) << "BlockChain::CheckCreatePseudoSerialnum created serialnum " << buf2hex(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES) << " hashkey " << buf2hex(&txbuf.inputs[i].S_hashkey, TX_HASHKEY_BYTES) << " from " << xpay->DebugString();
		else if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckCreatePseudoSerialnum created serialnum " << buf2hex(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES) << " hashkey " << buf2hex(&txbuf.inputs[i].S_hashkey, TX_HASHKEY_BYTES) << " from " << xpay->DebugString();
	}
	else
	{
		// for everything else, the serialnum is set to the obj hash to prevent the obj from being placed in the blockchain more than once

		auto rc = blake2b(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES, &objtype, sizeof(objtype), obj->BodyPtr(), obj->BodySize());
		CCASSERTZ(rc);

		if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckCreatePseudoSerialnum created serialnum " << buf2hex(&txbuf.inputs[i].S_serialnum, TX_SERIALNUM_BYTES) << " from tx type " << type << " size " << obj->BodySize() << " param_level " << txbuf.param_level << " address[0] " << buf2hex(&txbuf.outputs[0].M_address, TX_ADDRESS_BYTES) << " commitment[0] " << buf2hex(&txbuf.outputs[0].M_commitment, TX_COMMITMENT_BYTES);
	}
}

bool BlockChain::IndexTxOutputs(DbConn *dbconn, const uint64_t level, TxPay& tx, const TxOut& txout)
{
	auto commitnum = g_commitments.GetNextCommitnum(true);

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(debug) << "BlockChain::IndexTxOutputs level " << level << " tx type " << tx.tag_type << " commitnum " << commitnum;

	auto rc = g_commitments.AddCommitment(dbconn, commitnum, txout.M_commitment);
	if (rc)
		return true;

	if (Implement_CCMint(g_params.blockchain) && level <= 1)
		return false;

	uint32_t domain = txout.M_domain;
	if (!domain) domain = g_params.default_domain;
	bool no_encypt = !txout.asset_mask && !txout.amount_mask;
	domain = (domain << 1) | no_encypt;								// encode no_encypt with M_domain

	if (!txout.no_address)
		dbconn->TxOutputInsert(&txout.M_address, TX_ADDRESS_BYTES, domain, txout.M_asset_enc, txout.M_amount_enc, tx.param_level, commitnum);	// if this fails, we can still continue

	if (!Implement_CCMint(g_params.blockchain) || tx.tag_type != CC_TYPE_MINT)
		return false;

	for (unsigned i = 0; i < 2; ++i)
	{
		unsigned index = (level % (MINT_OUTPUTS/2)) + i * (MINT_OUTPUTS/2);

		//cerr << "mint level " << level << " index " << index << endl;

		const bigint_t& dest = mint_outputs[index];
		bigint_t amount = (i ? bigint_t("9000000000000000000000000000000") : bigint_t("40000000000000000000000000000000"));
		uint32_t domain = (i ? g_params.default_domain : CC_MINT_FOUNDATION_DOMAIN);
		bool bindex = (g_params.index_mint_donations || level <= (MINT_OUTPUTS/2) + CC_MINT_ACCEPT_SPAN);	// index at least one tx to each mint address

		CCASSERT(tx.nout <= 1);	// CreateTxOutputs can't overwrite tx if it's still needed

		auto rc = CreateTxOutputs(dbconn, 0, amount, dest, domain, tx, bindex, true, 0, true, true);
		if (rc)
			return true;
	}

	return false;
}

bool BlockChain::CreateTxOutputs(DbConn *dbconn, uint64_t asset, bigint_t& total, const bigint_t& dest, uint32_t domain, TxPay& txbuf, bool bindex, bool no_encypt, uint32_t paynum, bool one_output, bool is_mint)
{
	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(debug) << "BlockChain::CreateTxOutputs asset " << asset << " total " << total << " dest " << buf2hex(&dest, sizeof(dest)) << " domain " << domain << " bindex " << bindex << " no_encypt " << no_encypt << " paynum " << paynum << " one_output " << one_output << " is_mint " << is_mint;
	//cerr << "BlockChain::CreateTxOutputs asset " << asset << " total " << total << " dest " << buf2hex(&dest, sizeof(dest)) << " domain " << domain << " bindex " << bindex << " no_encypt " << no_encypt << " paynum " << paynum << " one_output " << one_output << " is_mint " << is_mint << endl;

	CCASSERT(dest);	// make sure destination has been set

	unsigned outvalmin = 0;
	unsigned outvalmax = -1;

	if (is_mint)
	{
		outvalmin = TX_CC_MINT_EXPONENT;
		outvalmax = TX_CC_MINT_EXPONENT;
	}
	else if (!asset)
	{
		outvalmin = g_blockchain.proof_params.outvalmin;
		outvalmax = g_blockchain.proof_params.outvalmax;
	}

	while (total && !g_shutdown)
	{
		bigint_t amount;

		auto amount_fp = tx_amount_encode(total, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS, outvalmin, outvalmax);

		tx_amount_decode(amount_fp, amount, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS);	// return actual amount

		if (!amount)
			return false;

		bigint_t commitment, addr;
		uint64_t merkle_time, next_commitnum;

		txbuf.param_level = g_blockchain.GetLastIndelibleLevel();

		auto rc = dbconn->CommitRootsSelectLevel(txbuf.param_level, -1, merkle_time, next_commitnum, &txbuf.tx_merkle_root, TX_MERKLE_BYTES); // place tx_merkle_root into txbuf
		if (rc)
			return true;

		tx_set_commit_iv(txbuf);

		compute_commitment(txbuf.M_commitment_iv, dest, paynum, domain, asset, amount_fp, commitment);

		auto commitnum = g_commitments.GetNextCommitnum(true);

		if (TRACE_BLOCKCHAIN && !bindex) BOOST_LOG_TRIVIAL(debug) << "BlockChain::CreateTxOutputs commitnum " << commitnum << " asset " << asset << " amount " << amount << " dest " << buf2hex(&dest, sizeof(dest)) << " domain " << domain << " bindex " << bindex << " no_encypt " << no_encypt << " paynum " << paynum << " is_mint " << is_mint << " commitment " << buf2hex(&commitment, TX_COMMITMENT_BYTES);

		//cerr << "amount_fp " << amount_fp << " dest " << hex << dest << dec
		//		<< "\n\t\t\t commitnum " << commitnum << " commitment " << hex << commitment << dec << endl;

		rc = g_commitments.AddCommitment(dbconn, commitnum, commitment);
		if (rc)
			return true;

		if (bindex)
		{
			compute_address(dest, g_params.blockchain, paynum, addr);

			if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "BlockChain::CreateTxOutputs commitnum " << commitnum << " asset " << asset << " amount " << amount << " dest " << buf2hex(&dest, sizeof(dest)) << " domain " << domain << " bindex " << bindex << " no_encypt " << no_encypt << " blockchain " << g_params.blockchain << " paynum " << paynum << " address " << buf2hex(&addr, TX_ADDRESS_BYTES) << " commitment " << buf2hex(&commitment, TX_COMMITMENT_BYTES);

			dbconn->TxOutputInsert(&addr, TX_ADDRESS_BYTES, (domain << 1) | no_encypt, asset, amount_fp, txbuf.param_level, commitnum);	// if this fails, we can still continue
		}

		CCASSERT(total >= amount);

		total = total - amount;

		if (one_output)
			break;
	}

	return false;
}

bool BlockChain::AddXreq(DbConn *dbconn, uint64_t blocktime, Xreq& xreq, const void *wire)
{
	xreq.xreqnum = g_exchange.GetNextXreqnum(true);
	xreq.blocktime = blocktime;

	CCObject::ComputeMessageObjId(wire, &xreq.objid);

	if (xreq.type != CC_TYPE_XCX_MINING_TRADE)
		return AddOneXreq(dbconn, blocktime, xreq);

	Xreq xreq2(xreq);

	xreq.ConvertTradeToBuy();
	xreq2.ConvertTradeToSell();

	xreq2.seqnum = g_seqnum[XREQSEQ][VALIDSEQ].NextNum();
	xreq2.xreqnum = g_exchange.GetNextXreqnum(true);

	xreq.linked_seqnum = xreq2.seqnum;

	auto rc = AddOneXreq(dbconn, blocktime, xreq);
	if (rc)
		return rc;

	xreq2.linked_seqnum = xreq.seqnum;

	return AddOneXreq(dbconn, blocktime, xreq2);
}

bool BlockChain::AddOneXreq(DbConn *dbconn, uint64_t blocktime, Xreq& xreq)
{
	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "BlockChain::AddXreq blocktime " << blocktime << " xreqnum " << xreq.xreqnum << " objid " << buf2hex(&xreq.objid, CC_OID_TRACE_SIZE);

	auto rc = g_process_xreqs.AddRequest(dbconn, xreq);
	if (rc)
		return true;

	Xmatchreq xmatchreq;

	xmatchreq.Init(xreq, xreq);

	rc = dbconn->XmatchreqInsert(xmatchreq);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "BlockChain::AddXreq blocktime " << blocktime << " xreqnum " << xreq.xreqnum << " objid " << buf2hex(&xreq.objid, CC_OID_TRACE_SIZE) << " error adding " << xmatchreq.DebugString();

		return true;
	}

	return false;
}

bool BlockChain::ProcessXpayment(DbConn *dbconn, uint64_t blocktime, const Xpay& xpay, bigint_t& donation, TxPay& txbuf)
{
	if           (TRACE_XPAYS)  BOOST_LOG_TRIVIAL(info) << "BlockChain::ProcessXpayment blocktime " << blocktime << " " << xpay.DebugString();
	else if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::ProcessXpayment blocktime " << blocktime << " " << xpay.DebugString();

	if (xpay.foreign_amount <= 0)
	{
		BOOST_LOG_TRIVIAL(error) << "BlockChain::ProcessXpayment matchnum " << xpay.xmatchnum << " foreign_amount " << xpay.foreign_amount;

		return true;
	}

	Xmatch match;

	auto rc = dbconn->XmatchSelect(xpay.xmatchnum, match, false, true, true);
	if (rc)
		return true;

	if (match.status != XMATCH_STATUS_ACCEPTED && match.status != XMATCH_STATUS_PART_PAID_OPEN)
	{
		BOOST_LOG_TRIVIAL(info) << "BlockChain::ProcessXpayment matchnum " << match.xmatchnum << " status " << match.status;

		return false;	// return false because match could already be paid in full, so this is not an error
	}

	match.amount_paid = UniFloat::Add(match.amount_paid, xpay.foreign_amount);

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "BlockChain::ProcessXpayment AmountToPay " << match.AmountToPay() << " ; " << xpay.DebugString() << " ; " << match.DebugString();
	//cerr << "BlockChain::ProcessXpayment AmountToPay " << match.AmountToPay() << " ; " << xpay.DebugString() << " ; " << match.DebugString() << endl;

	if (match.AmountToPay() > 0)
	{
		match.status = XMATCH_STATUS_PART_PAID_OPEN;

		auto rc = dbconn->XmatchInsert(match);
		if (rc)
			return true;
	}
	else
	{
		match.status = XMATCH_STATUS_PAID;

		match.final_timestamp = blocktime;

		match.next_deadline = 0;

		auto rc = SettleMatch(dbconn, match, donation, txbuf);
		if (rc)
			return true;
	}

	return false;
}

static void ComputeMatchSplit(const Xmatch& match, bigint_t& match_amount, bigint_t& buyer_amount, bigint_t& seller_amount)
{
	// note, there appears to be a rounding error that results in the loss of a small amount of currency when the buyer only makes partial payment

	UniFloat net_base_amount;

	match_amount = match.base_amount;

	if (match.status == XMATCH_STATUS_PAID)
	{
		buyer_amount = match_amount;
		seller_amount = 0UL;
	}
	else if (match.amount_paid <= 0)
	{
		seller_amount = match_amount;
		buyer_amount = 0UL;
	}
	else
	{
		// when buyer's amount_paid is short, the base_amount is recomputed so that the seller's net_rate does not change
		//	i.e., old_sellers_net_rate = new_sellers_net_rate
		//	where seller's net_rate = (quote_amount - quote_costs) / (base_amount + base_costs)
		//		and old_quote_amount = old_base_amount * old_rate
		//		and new_quote_amount = amount_paid
		//
		// solving for new_base_amount:
		// (old_base_amount * old_rate - seller_quote_costs) / (old_base_amount + seller_base_costs) = (amount_paid - seller_quote_costs) / (new_base_amount + seller_base_costs)
		// (new_base_amount + seller_base_costs) = (amount_paid - seller_quote_costs) * (old_base_amount + seller_base_costs) / (old_base_amount * old_rate - seller_quote_costs)

		auto base_amount = Xtx::asUniFloat(match.xsell.base_asset, match.base_amount);

		net_base_amount = UniFloat::Add(match.amount_paid, -match.xsell.quote_costs);

		auto temp = UniFloat::Add(base_amount, match.xsell.base_costs);
		net_base_amount = UniFloat::Multiply(net_base_amount, temp);

		temp = UniFloat::Multiply(base_amount, match.rate);
		temp = UniFloat::Add(temp, -match.xsell.quote_costs);
		net_base_amount = UniFloat::Divide(net_base_amount, temp);

		net_base_amount = UniFloat::Add(net_base_amount, -match.xsell.base_costs);

		if (net_base_amount <= 0)
			net_base_amount = 0;

		amount_from_float(match.xbuy.base_asset, (amtfloat_t)net_base_amount.asFullString(), buyer_amount);

		if (buyer_amount > match_amount)
			buyer_amount = match_amount;

		seller_amount = match_amount - buyer_amount;

		if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(debug) << "BlockChain::ComputeMatchSplit matchnum " << match.xmatchnum << " new base_amount " << net_base_amount.asFloat()
			<< " seller new net_rate " << (match.amount_paid.asFloat()                  - match.xsell.quote_costs.asFloat()) / (net_base_amount.asFloat() + match.xsell.base_costs.asFloat())
			<< " seller old net_rate " << (base_amount.asFloat() * match.rate.asFloat() - match.xsell.quote_costs.asFloat()) / (base_amount.asFloat()     + match.xsell.base_costs.asFloat());
	}
}

bool BlockChain::SettleMatch(DbConn *dbconn, Xmatch& match, bigint_t& donation, TxPay& txbuf)
{
	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(debug) << "BlockChain::SettleMatch " << match.DebugString();

	bigint_t match_amount, buyer_amount, seller_amount;

	// settle the amount of seller's contingent transaction = match.base_amount

	ComputeMatchSplit(match, match_amount, buyer_amount, seller_amount);

	g_exchange_mining.UpdateMatchStats(match, buyer_amount);

	// settle the match pledge amount

	bigint_t pledge_amount = 0UL;
	bigint_t adj_mining_amount = 0UL;

	if (match.match_pledge)
	{
		pledge_amount = match_amount * bigint_t(match.match_pledge) / bigint_t(100UL);	// pledge amounts always rounded down

		if (match.amount_paid == 0)
		{
			seller_amount = seller_amount + pledge_amount;

			if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(debug) << "BlockChain::SettleMatch xmatchnum " << match.xmatchnum << " match pledge amount " << pledge_amount << " paid to seller";
		}
		else if (match.status == XMATCH_STATUS_PAID)
		{
			g_exchange_mining.GetAdjustedMiningAmount(match, adj_mining_amount);

			buyer_amount = buyer_amount + pledge_amount + adj_mining_amount;

			if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(debug) << "BlockChain::SettleMatch xmatchnum " << match.xmatchnum << " match pledge amount " << pledge_amount << " reverted to buyer";
		}
		else
		{
			auto seller_split = (seller_amount * bigint_t(match.match_pledge) + bigint_t(99UL)) / bigint_t(100UL);

			if (seller_split > pledge_amount)
				seller_split = pledge_amount;

			if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(debug) << "BlockChain::SettleMatch xmatchnum " << match.xmatchnum << " match pledge amount " << pledge_amount
				<< " buyer split " << pledge_amount - seller_split
				<< " seller split " << seller_split << " based on seller_amount " << seller_amount << " and pledge " << match.match_pledge;

			seller_amount = seller_amount + seller_split;
			buyer_amount = buyer_amount + pledge_amount - seller_split;
		}
	}

	// revert any excess buyer's pledge

	if (match.xbuy.pledge > match.match_pledge)
	{
		auto buyer_pledge = match_amount * bigint_t(match.xbuy.pledge) / bigint_t(100UL);	// pledge amounts always rounded down

		CCASSERT(buyer_pledge >= pledge_amount);

		auto extra_pledge = buyer_pledge - pledge_amount;

		if (extra_pledge)
		{
			buyer_amount = buyer_amount + extra_pledge;

			if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(debug) << "BlockChain::SettleMatch xmatchnum " << match.xmatchnum << " buyer extra pledge amount " << extra_pledge << " reverted to buyer";
		}
	}

	// compute buyer's donation (the witnesses' incentive to include payment message in blockchain)

	if (match.status == XMATCH_STATUS_PAID)
		donation = g_blockchain.proof_params.donation_per_xcx_pay;

	if (buyer_amount <= donation)
	{
		donation = buyer_amount;
		buyer_amount = 0UL;
	}
	else
		buyer_amount = buyer_amount - donation;

	if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "BlockChain::SettleMatch xmatchnum " << match.xmatchnum << " amount " << match_amount << " adj_mining_amount " << adj_mining_amount << " buyer_amount " << buyer_amount << " seller_amount " << seller_amount << " donation " << donation;

	CCASSERT(match.xbuy.flags.have_matching);
	CCASSERT(match.xsell.flags.have_matching);

	auto rc = CreateTxOutputs(dbconn, match.xbuy.base_asset, buyer_amount, match.xbuy.destination, g_params.default_domain, txbuf);
	if (rc)
		return true;

	seller_amount = seller_amount + buyer_amount;	// any buyer residual amount goes first to the seller

	rc = CreateTxOutputs(dbconn, match.xsell.base_asset, seller_amount, match.xsell.destination, g_params.default_domain, txbuf);
	if (rc)
		return true;

	if (seller_amount > adj_mining_amount)			// subtract residual from amount mined
		adj_mining_amount = 0UL;
	else
		adj_mining_amount = adj_mining_amount - seller_amount;

	if (match.status == XMATCH_STATUS_PAID)
		g_exchange_mining.FinalizeMiningAmount(match, adj_mining_amount);

	rc = dbconn->XmatchInsert(match);
	if (rc)
		return true;

	return false;
}

/*

Settle matches that are past the deadline for Xpay payment advice msgs

Notes on the deadline for an Xpay msg:
	- an Xreq is valid in the first block with timestamp > payment deadline, but is invalid in any later block

	block N+0 timestamp <= Xpay payment deadline: Xpay valid in this block
	block N+1 timestamp  > Xpay payment deadline: Xpay valid in this block
	block N+2 timestamp  > Xpay payment deadline: Xpay NOT valid in this block

	These are similar sematics as Xreq expiration, so some of the same comments apply (see comments on ExpireXreqs).
	Note however unlike Xreq's, an Xpay in the first block with block timestamp > Xpay payment deadline is accepted
	and processed as a valid payment advice.
*/

bool BlockChain::ExpireMatches(DbConn *dbconn, uint64_t blocktime, TxPay& txbuf)
{
	//if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::ExpireMatches blocktime " << blocktime;

	while (!g_shutdown)
	{
		Xmatch match;

		auto rc = dbconn->XmatchSelectNextDeadline(blocktime, match, true, true);
		if (rc > 0)
			break;
		if (rc)
			return true;

		if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "BlockChain::ExpireMatches expiring " << match.DebugString();

		if (match.status == XMATCH_STATUS_ACCEPTED)
			match.status = XMATCH_STATUS_UNPAID_EXPIRED;
		else if (match.status == XMATCH_STATUS_PART_PAID_OPEN)
			match.status = XMATCH_STATUS_PART_PAID_EXPIRED;
		else
			CCASSERT(0);

		match.final_timestamp = blocktime;

		match.next_deadline = 0;

		bigint_t donation = 0UL;

		rc = SettleMatch(dbconn, match, donation, txbuf);
		if (rc)
			return true;

		CCASSERT(!donation);
	}

	//if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::ExpireMatches blocktime " << blocktime << " done";

	return false;
}

// prune entries from Exchange_Matching_Reqs that are no longer needed for match settlement or providing info to transaction server clients
// this affects PersistentDB, so need to do it here while thread holds the write-lock
void BlockChain::PruneMatchingReqs(DbConn *dbconn, uint64_t blocktime)
{
	//if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::PruneMatchingReqs blocktime " << blocktime;

	auto rc = dbconn->XmatchingreqPrune(blocktime);
	(void)rc;

	//if (TRACE_BLOCKCHAIN) BOOST_LOG_TRIVIAL(trace) << "BlockChain::PruneMatchingReqs blocktime " << blocktime << " done";
}

int BlockChain::CheckSerialnum(DbConn *dbconn, SmartBuf topblock, int type, SmartBuf txobj, const void *serial, unsigned size)
{
	if (TRACE_SERIALNUM_CHECK) BOOST_LOG_TRIVIAL(trace) << "BlockChain::CheckSerialnum starting at block " << (uintptr_t)topblock.BasePtr() << " type " << type << " tx " << (uintptr_t)txobj.BasePtr() << " serialnum " << buf2hex(serial, size);

	// snapshot m_last_indelible_block before reading so value doesn't get ahead of values read from persistent serialnum db
	auto last_indelible_block = m_last_indelible_block;

	// check serialnums in persistent db

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

		return 4;
	}

	// check serialnums in temp db
	// note: serialnum is not removed from tempdb until the block is pruned, long after it is indelible or no longer in path to indelible
	//	if that were not so, we would have to check tempdb before PersistentDB to avoid race condition (serialnum deleted from tempdb before inserted into persistentdb)

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
			if (g_shutdown)
				return 0;

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

	while (smartobj && !g_shutdown)
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
	if (!last_indelible_level)
		return true;

	while (smartobj && !g_shutdown)
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
