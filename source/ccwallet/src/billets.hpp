/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * billets.hpp
*/

#pragma once

#include <CCbigint.hpp>

// A bill's status and flags determine which balance it is allocated to; therefore,
// when a bill's status or flags are changed, the bill's balance may need to be reallocated.

#define BILL_STATUS_VOID			0
#define BILL_STATUS_ERROR			1	// permanent; output bills only: tx creation encountered an error
#define BILL_STATUS_ABANDONED		2	// transitory; output bills only: may later clear
#define BILL_STATUS_PENDING			3	// transitory; output bills only: may later clear or become abandoned
#define BILL_STATUS_PREALLOCATED	4	// transitory; output bills only: indicates a pre-allocated pending output bill
#define BILL_STATUS_SENT			5	// semi-permanent; unspendable output bills only: bill is an output in tx that cleared; future: status may change if spend secrets are added to wallet and bill becomes spendable
#define BILL_STATUS_CLEARED			6	// transitory; spendable output bills only: bill is an output in tx that cleared; status will change when bill used as a tx input
#define BILL_STATUS_ALLOCATED		7	// transitory: cleared bill is allocated for use in a future or pending tx
#define BILL_STATUS_SPENT			8	// permanent: cleared or spendable bill was used as in input in a tx that has cleared
#define BILL_STATUS_INVALID			9

#define BILL_RECV_MASK_WATCH		(1 << 0)
#define BILL_RECV_MASK_TRACK		(1 << 1)
#define BILL_RECV_MASK				(BILL_RECV_MASK_TRACK | BILL_RECV_MASK_WATCH)

#define BILL_FLAG_TRUSTED			(1 << 2)	// amount is trusted
#define BILL_IS_CHANGE				(1 << 3)
#define BILL_FLAG_NO_TXID			(1 << 4)

#define TXID_COMMITMENT_BYTES		16

struct TxPay;
struct TxOut;

class TxQuery;
class DbConn;

class Billet
{
public:
	uint64_t id;
	unsigned status;
	unsigned flags;
	uint64_t create_tx;
	uint64_t dest_id;
	uint64_t blockchain;
	uint32_t pool;
	uint64_t asset;
	uint64_t amount_fp;
	unsigned delaytime;
	uint64_t commitnum;
	uint64_t spend_tx_commitnum;		// property of the spend tx, not the Billet
	snarkfront::bigint_t amount;
	snarkfront::bigint_t address;		// part of txid; M_address = zkhash(#dest, dest_chain, #paynum)
	snarkfront::bigint_t commit_iv;
	snarkfront::bigint_t commitment;	// part of txid; M_commitment = zkhash(M_commitment_iv, #dest, #paynum, M_pool, #asset, #amount)
	snarkfront::bigint_t serialnum;		// holds monitor_secret[0] until billet clears; S-serialnum = zkhash(@monitor_secret[0], M-commitment, M-commitnum)
	snarkfront::bigint_t spend_hashkey;	// property of the spend tx, not the Billet

	Billet();

	void Clear();
	void Copy(const Billet& other);
	string DebugString() const;

	static bool StatusIsValid(unsigned status)
	{
		return status > BILL_STATUS_VOID && status < BILL_STATUS_INVALID;
	}

	bool IsValid() const;

	bool BillIsChange() const
	{
		return flags & BILL_IS_CHANGE;
	}

	bool BillIsPending() const
	{
		return status >= BILL_STATUS_ERROR && status <= BILL_STATUS_PREALLOCATED;
	}

	static unsigned FlagsFromDestinationType(unsigned type);

	static bool HasSerialnum(unsigned status, unsigned flags);

	bool HasSerialnum() const
	{
		return HasSerialnum(status, flags);
	}

	void SetFromTxOut(const TxPay& ts, const TxOut& txout);

	static int PollUnspent(DbConn *dbconn, TxQuery& txquery);
	static int ResetAllocated(DbConn *dbconn, bool reset_balance);

	int SetStatusCleared(DbConn *dbconn, uint64_t _commitnum);
	int SetStatusSpent(DbConn *dbconn, const snarkfront::bigint_t& hashkey, uint64_t tx_commitnum = 0);

	static int CheckIfBilletsSpent(DbConn *dbconn, TxQuery& txquery, Billet *billets, unsigned nbills, bool or_pending = false);

	static uint64_t GetBilletAvailableCount();
	static void NotifyNewBillet(bool increment);
	static int WaitNewBillet(uint64_t last_count, uint32_t seconds);
	static void Shutdown();
};
