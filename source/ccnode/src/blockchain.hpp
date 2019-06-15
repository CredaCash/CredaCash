/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * blockchain.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <SmartBuf.hpp>
#include <SpinLock.hpp>

#include <transaction.hpp>

#define MAX_NCONFSIGS		(MAX_NWITNESSES + (MAX_NWITNESSES-1)/2)
#define MAX_SCORE_BITS		64

#define GENESIS_NWITNESSES	(g_params.genesis_nwitnesses)
#define GENESIS_MAXMAL		(g_params.genesis_maxmal)

#define BLOCK_PRUNE_ROUNDS	5

struct TxPay;
struct TxOut;

class BlockChain
{
	SmartBuf m_new_indelible_block;
	SmartBuf m_last_indelible_block;
	uint64_t m_last_indelible_level;
	uint64_t m_startup_prune_level;
	atomic<bool> m_have_fatal_error;

	bool DoConfirmOne(DbConn *dbconn, SmartBuf newobj, TxPay& txbuf);

public:

	struct
	{
		bigint_t donation_per_tx;
		bigint_t donation_per_byte;
		bigint_t donation_per_output;
		bigint_t donation_per_input;
		bigint_t minimum_donation;

		uint64_t donation_per_tx_fp;
		uint64_t donation_per_byte_fp;
		uint64_t donation_per_output_fp;
		uint64_t donation_per_input_fp;
		uint64_t minimum_donation_fp;

		uint16_t outvalmin;
		uint16_t outvalmax;
		uint16_t invalmax;

	} proof_params;

	void Init();
	void DeInit();

	BlockChain()
	 :	m_last_indelible_level(0),
		m_startup_prune_level(0),
		m_have_fatal_error(false)
	{ }

	void SetFatalError(const char *msg);

	bool HasFatalError() const
	{
		return m_have_fatal_error.load();
	}

	void SetStartupPruneLevel(uint64_t level)
	{
		m_startup_prune_level = level;
	}

	uint64_t ComputePruneLevel(unsigned min_level, unsigned trailing_rounds) const;

	bool SetNewlyIndelibleBlock(DbConn *dbconn, SmartBuf smartobj, TxPay& txbuf);

	SmartBuf GetLastIndelibleBlock()
	{
		return m_last_indelible_block;
	}

	uint64_t GetLastIndelibleLevel() const
	{
		return m_last_indelible_level;	// can be momentarily out of sync with GetLastIndelibleBlock(), so if both are needed in sync, use only GetLastIndelibleBlock()
	}

	void SetupGenesisBlock(SmartBuf *retobj);
	int SaveGenesisHash(DbConn *dbconn, SmartBuf genesis_block);
	int CheckGenesisHash(DbConn *dbconn, SmartBuf genesis_block);
	void RestoreLastBlocks(DbConn *dbconn, uint64_t level);

	static void CreateGenesisDataFiles();
	bool LoadGenesisDataFiles(class BlockAux* auxp);

	bool DoConfirmations(DbConn *dbconn, SmartBuf newobj, TxPay& txbuf);
	bool DoConfirmationLoop(DbConn *dbconn, SmartBuf newobj, TxPay& txbuf);

	typedef int (*SerialnumInsertFunction)(DbConn *dbconn, const void *serial, unsigned size, const void* blockp, uint64_t level);

	static bool IndexTxs(DbConn *dbconn, SmartBuf smartobj, TxPay& txbuf);
	static void CheckCreatePseudoSerialnum(TxPay& txbuf, const void *wire, const uint32_t bufsize);
	static bool IndexTxOutputs(DbConn *dbconn, const uint64_t level, const TxPay& tx, const TxOut& txout);

	int CheckSerialnums(DbConn *dbconn, SmartBuf topblock, int type, SmartBuf txobj, void *txwire, unsigned txsize, TxPay& txbuf);
	int CheckSerialnum(DbConn *dbconn, SmartBuf topblock, int type, SmartBuf txobj, const void *serial, unsigned size);
	static bool BlockInChain(void *find_block, SmartBuf smartobj, SmartBuf last_indelible_block);
	static bool ChainHasDelibleTxs(SmartBuf smartobj, uint64_t last_indelible_level);
};

extern BlockChain g_blockchain;
