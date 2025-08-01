/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * exchange.hpp
*/

#pragma once

class Transaction;
class Xmatchreq;
class Xmatch;
class DbConn;
struct BlockChainStatus;

class ExchangeRequest
{
public:
	static int BeginAndReadXmatchreq(DbConn *dbconn, Xmatchreq &xreq, Transaction *tx, uint64_t id, bool or_greater = false);
	static int ReadXmatchreq(DbConn *dbconn, Xmatchreq &xreq, Transaction *tx, uint64_t id, bool or_greater = false);

	static int UpdatePollTime(DbConn *dbconn, uint64_t id, bool by_txid, uint64_t poll_time = 0);

	static int PollAddress(DbConn *dbconn, TxQuery& txquery, uint64_t address_id);

	static int PollXmatchreq(DbConn *dbconn, TxQuery& txquery, Xmatchreq &xreq, const Transaction &tx, BlockChainStatus& blockchain_status);
};

class ExchangeMatch
{
public:
	static int UpdateTotalMined(DbConn *dbconn, const Xmatch& xmatch);

	static void UpdatePollTime(Xmatch &xmatch, uint64_t last_matching_completed_time, uint64_t delay = 0);

	static int PollXmatch(DbConn *dbconn, TxQuery& txquery, Xmatch &xmatch, BlockChainStatus& blockchain_status);
};

class Exchange
{
public:
	static int GetTotalMined(DbConn *dbconn, bigint_t& total_mined);
};

