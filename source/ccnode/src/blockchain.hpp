/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * blockchain.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <SmartBuf.hpp>
#include <SpinLock.hpp>

#include <transaction.hpp>
#include <xtransaction-xreq.hpp>

#define MAX_NCONFSIGS		(MAX_NWITNESSES + (MAX_NWITNESSES-1)/2)

#define GENESIS_NWITNESSES	(g_params.genesis_nwitnesses)
#define GENESIS_MAXMAL		(g_params.genesis_maxmal)

#define BLOCK_PRUNE_ROUNDS	5

struct TxPay;
struct TxOut;
class Xpay;
struct BlockChainStatus;

class BlockChain
{
	friend class ProcessXreqs;
	friend class ExchangeMining;

	SmartBuf m_new_indelible_block;
	SmartBuf m_last_indelible_block;
	atomic<uint64_t> m_last_indelible_level;
	atomic<uint64_t> m_last_indelible_timestamp;
	atomic<uint64_t> m_last_matching_completed_block_time;
	atomic<uint64_t> m_last_matching_start_block_time;
	atomic<uint32_t> m_last_indelible_ticks;

	uint64_t m_startup_prune_level;

	atomic<bool> m_have_fatal_error;

	bool SetNewlyIndelibleBlock(DbConn *dbconn, SmartBuf smartobj, TxPay& txbuf);

	void SetLastIndelible(SmartBuf smartobj);

	bool DoConfirmationLoop(DbConn *dbconn, SmartBuf newobj, TxPay& txbuf);
	bool DoConfirmOne(DbConn *dbconn, SmartBuf newobj, TxPay& txbuf);

	static bool IndexTxs(DbConn *dbconn, uint64_t blocktime, SmartBuf smartobj, TxPay& txbuf);
	static bool IndexTxOutputs(DbConn *dbconn, const uint64_t level, TxPay& tx, const TxOut& txout);
	static bool CreateTxOutputs(DbConn *dbconn, uint64_t asset, bigint_t& total, const bigint_t& dest, uint32_t domain, TxPay& txbuf, bool bindex = true, bool no_encypt = true, uint32_t paynum = 0, bool one_output = true, bool is_mint = false);

	static bool AddXreq(DbConn *dbconn, uint64_t blocktime, Xreq& xreq, const void *wire);
	static bool AddOneXreq(DbConn *dbconn, uint64_t blocktime, Xreq& xreq);

	static bool ProcessXpayment(DbConn *dbconn, uint64_t blocktime, const Xpay& xpay, bigint_t& donation, TxPay& txbuf);
	static bool SettleMatch(DbConn *dbconn, Xmatch& match, bigint_t& donation, TxPay& txbuf);

	static bool ExpireMatches(DbConn *dbconn, uint64_t blocktime, TxPay& txbuf);
	static void PruneMatchingReqs(DbConn *dbconn, uint64_t blocktime);

public:

	struct
	{
		bigint_t minimum_donation;
		bigint_t donation_per_tx;
		bigint_t donation_per_byte;
		bigint_t donation_per_output;
		bigint_t donation_per_input;
		bigint_t donation_per_xcx_req;
		bigint_t donation_per_xcx_pay;

		uint64_t minimum_donation_fp;
		uint64_t donation_per_tx_fp;
		uint64_t donation_per_byte_fp;
		uint64_t donation_per_output_fp;
		uint64_t donation_per_input_fp;
		uint64_t donation_per_xcx_req_fp;
		uint64_t donation_per_xcx_pay_fp;

		uint16_t outvalmin;
		uint16_t outvalmax;
		uint16_t invalmax;

	} proof_params;

	void Init();
	void DeInit();

	BlockChain()
	 :	m_last_indelible_level(0),
		m_last_indelible_timestamp(0),
		m_last_matching_completed_block_time(0),
		m_last_matching_start_block_time(0),
		m_last_indelible_ticks(0),
		m_startup_prune_level(0),
		m_have_fatal_error(false)
	{ }

	void DebugStop(const char *msg);

	bool SetFatalError(const char *msg);

	bool HasFatalError() const
	{
		return m_have_fatal_error.load();
	}

	uint64_t ComputePruneLevel(unsigned min_level, unsigned trailing_rounds) const;

	void GetLastIndelibleValues(BlockChainStatus& blockchain_status);

	SmartBuf GetLastIndelibleBlock()
	{
		return m_last_indelible_block;
	}

	uint64_t GetLastIndelibleLevel() const
	{
		return m_last_indelible_level.load();	// can be momentarily out of sync with GetLastIndelibleBlock(), so if both are needed in sync, use GetLastIndelibleValues()
	}

	uint64_t GetLastIndelibleTimestamp() const
	{
		return m_last_indelible_timestamp.load();	// can be momentarily out of sync with GetLastIndelibleBlock(), so if both are needed in sync, use GetLastIndelibleValues()
	}

	uint32_t GetLastIndelibleTicks() const
	{
		return m_last_indelible_ticks.load();
	}

	void SetGenesisBlock(SmartBuf *retobj);
	int SaveGenesisHash(DbConn *dbconn, SmartBuf genesis_block);
	int CheckGenesisHash(DbConn *dbconn, SmartBuf genesis_block);
	void RestoreLastBlocks(DbConn *dbconn, uint64_t level);

	static void CreateGenesisDataFiles();
	bool LoadGenesisDataFiles(class BlockAux* auxp);
	bool LoadHistoryDataFile(DbConn *dbconn, TxPay& txbuf);

	bool DoConfirmations(DbConn *dbconn, SmartBuf newobj, TxPay& txbuf);

	static void CheckCreatePseudoSerialnum(TxPay& txbuf, shared_ptr<Xtx>& xtx, const void *wire, bool bperistent = false);
	int CheckSerialnum(DbConn *dbconn, SmartBuf topblock, int type, SmartBuf txobj, const void *serial, unsigned size);
	static bool BlockInChain(void *find_block, SmartBuf smartobj, SmartBuf last_indelible_block);
	static bool ChainHasDelibleTxs(SmartBuf smartobj, uint64_t last_indelible_level);
};

extern BlockChain g_blockchain;
