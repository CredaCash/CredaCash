/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
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

class BlockChain
{
	SmartBuf m_new_indelible_block;
	SmartBuf m_last_indelible_block;
	uint64_t m_last_indelible_level;
	uint64_t m_startup_prune_level;
	atomic<bool> m_have_fatal_error;

	bool DoConfirmOne(DbConn *dbconn, SmartBuf newobj, struct TxPay &txbuf);

public:

	struct
	{
		uint64_t donation_per_tx;
		uint64_t donation_per_byte;
		uint64_t donation_per_output;
		uint64_t donation_per_input;
		uint64_t outvalmin;
		uint64_t outvalmax;
		uint64_t invalmax;
		uint16_t proof_param_set;

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

	bool SetNewlyIndelibleBlock(DbConn *dbconn, SmartBuf smartobj, struct TxPay &txbuf);

	SmartBuf GetLastIndelibleBlock()
	{
		return m_last_indelible_block;
	}

	uint64_t GetLastIndelibleLevel() const
	{
		return m_last_indelible_level;	// can be momentarily out of sync with GetLastIndelibleBlock(), so if both are needed in sync, use only GetLastIndelibleBlock()
	}

	void SetupGenesisBlock(SmartBuf *retobj);
	void RestoreLastBlocks(DbConn *dbconn, uint64_t level);

	static void CreateGenesisDataFiles();
	bool LoadGenesisDataFiles(class BlockAux* auxp);

	bool DoConfirmations(DbConn *dbconn, SmartBuf newobj, struct TxPay &txbuf);
	bool DoConfirmationLoop(DbConn *dbconn, SmartBuf newobj, struct TxPay &txbuf);

	typedef int (*SerialnumInsertFunction)(DbConn *dbconn, const void *serial, unsigned size, const void* blockp, uint64_t level);

	static bool IndexTxs(DbConn *dbconn, SmartBuf smartobj, struct TxPay &txbuf);
	static void CheckCreatePseudoSerialnum(struct TxPay& txbuf, const void *wire, const uint32_t bufsize);
	static bool IndexTxOutputs(DbConn *dbconn, struct TxOut& tx, uint64_t param_level);

	int CheckSerialnums(DbConn *dbconn, SmartBuf topblock, int type, SmartBuf txobj, void *txwire, unsigned txsize, struct TxPay &txbuf);
	int CheckSerialnum(DbConn *dbconn, SmartBuf topblock, int type, SmartBuf txobj, const void *serial, unsigned size);
	static bool BlockInChain(void *find_block, SmartBuf smartobj, SmartBuf last_indelible_block);
	static bool ChainHasDelibleTxs(SmartBuf smartobj, uint64_t last_indelible_level);
};

extern BlockChain g_blockchain;
