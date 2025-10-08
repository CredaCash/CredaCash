/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * expire.cpp
*/

#include "ccnode.h"
#include "expire.hpp"
#include "seqnum.hpp"
#include "block.hpp"
#include "blockchain.hpp"
#include "witness.hpp"

#include <CCmint.h>
#include <transaction.h>

#define TRACE_EXPIRE	(g_params.trace_expire)

static const int32_t relay_block_expire_age = 10*60*CCTICKS_PER_SEC;
//static const int32_t relay_block_expire_age = 1*60*CCTICKS_PER_SEC;	// for testing
//static const int32_t relay_block_expire_age = 0*CCTICKS_PER_SEC;	// for testing

static const int32_t relay_tx_expire_age = 10*60*CCTICKS_PER_SEC;
//static const int32_t relay_tx_expire_age = 0*CCTICKS_PER_SEC;			// for testing

// note: Relay db does not hold objects, so ValidObj expiration times should be >= RelayObj expiration times

static const int32_t valid_block_expire_age = 12*60*CCTICKS_PER_SEC;
//static const int32_t valid_block_expire_age = 60*60*CCTICKS_PER_SEC;	// for testing
//static const int32_t valid_block_expire_age = 1*60*CCTICKS_PER_SEC;	// for testing
//static const int32_t valid_block_expire_age = 0*60*CCTICKS_PER_SEC;	// for testing

static const int32_t valid_tx_expire_age = 12*60*CCTICKS_PER_SEC;
//static const int32_t valid_tx_expire_age = 0*CCTICKS_PER_SEC;			// for testing
//static const int32_t valid_tx_expire_age = 60*60*CCTICKS_PER_SEC;		// for testing

Expire g_expire;

class RelayObjsExpire : public ExpireObj
{
	bool isValidObj() const
	{
		return false;
	}

	int GetExpires(int64_t& seqnum, SmartBuf *retobj, ccoid_t& oid, uint32_t& next_expires_t0)
	{
		return m_dbconn->RelayObjsGetExpires(m_min_seqnum, m_max_seqnum, seqnum, oid, next_expires_t0);
	}

	int DeleteExpires(int64_t& seqnum, SmartBuf smartobj)
	{
		return m_dbconn->RelayObjsDeleteSeqnum(seqnum);
	}

public:
	RelayObjsExpire(const char *name, int64_t min_seqnum, int64_t max_seqnum, int32_t expire_age)
	 : ExpireObj(name, min_seqnum, max_seqnum, expire_age)
	{ }
};

class ValidObjsExpire : public ExpireObj
{
	bool isValidObj() const
	{
		return true;
	}

	int GetExpires(int64_t& seqnum, SmartBuf *retobj, ccoid_t& oid, uint32_t& next_expires_t0)
	{
		return m_dbconn->ValidObjsGetExpires(m_min_seqnum, m_max_seqnum, seqnum, retobj, next_expires_t0);
	}

	int DeleteExpires(int64_t& seqnum, SmartBuf smartobj)
	{
		auto rc = m_dbconn->ValidObjsDeleteObj(smartobj);
		if (!rc)
			return rc;

		return m_dbconn->ValidObjsDeleteSeqnum(seqnum);	// fallback: attempt to delete obj by seqnum
	}

public:
	ValidObjsExpire(const char *name, int64_t min_seqnum, int64_t max_seqnum, int32_t expire_age)
	 : ExpireObj(name, min_seqnum, max_seqnum, expire_age)
	{ }
};

void ExpireObj::Start()
{
	m_dbconn = new DbConn;
	CCASSERT(m_dbconn);

	m_thread = new thread(&ExpireObj::ThreadProc, this);
	CCASSERT(m_thread);
}

void ExpireObj::Stop()
{
	if (m_thread)
	{
		m_thread->join();

		delete m_thread;

		m_thread = NULL;
	}

	if (m_dbconn)
	{
		delete m_dbconn;

		m_dbconn = NULL;
	}
}

void ExpireObj::ThreadProc()
{
	BOOST_LOG_TRIVIAL(info) << "ExpireObj::ThreadProc " << m_name << " start m_min_seqnum " << m_min_seqnum << " m_max_seqnum " << m_max_seqnum << " m_expire_age " << m_expire_age << " m_dbconn " << (uintptr_t)m_dbconn;

	while (!g_shutdown)
	{
		DoExpires();

		ccsleep(10);
	}

	BOOST_LOG_TRIVIAL(info) << "ExpireObj::ThreadProc " << m_name << " end m_min_seqnum " << m_min_seqnum << " m_max_seqnum " << m_max_seqnum << " m_expire_age " << m_expire_age << " m_dbconn " << (uintptr_t)m_dbconn;
}

void ExpireObj::DoExpires()
{
	int64_t next_expires_seqnum = -1;
	SmartBuf next_expires_smartobj;
	ccoid_t oid;
	uint32_t next_expires_t0;
	int32_t age;

	while (!g_shutdown)
	{
		next_expires_smartobj.ClearRef();
		memset((void*)&oid, 0, sizeof(oid));

		GetExpires(next_expires_seqnum, &next_expires_smartobj, oid, next_expires_t0);

		if (next_expires_seqnum == -1)
		{
			if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << "ExpireObj::DoExpires " << m_name << " seqnum " << next_expires_seqnum;

			return;
		}

		if (!next_expires_smartobj && g_seqnum[BLOCKSEQ][VALIDSEQ].seqmin <= next_expires_seqnum && next_expires_seqnum <= g_seqnum[BLOCKSEQ][VALIDSEQ].seqmax)
		{
			// when expiring a block, we need the obj to get its level

			m_dbconn->ValidObjsGetObj(oid, &next_expires_smartobj);
		}

		auto obj = (CCObject*)next_expires_smartobj.data();
		unsigned objtype = (obj ? obj->ObjType() : 0);

		bool is_ccmint = Implement_CCMint(g_params.blockchain) && objtype == CC_TYPE_MINT;

		while (!is_ccmint)
		{
			age = ccticks_elapsed(next_expires_t0, ccticks());

			if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << "ExpireObj::DoExpires " << m_name << " seqnum " << next_expires_seqnum << " bufp " << (uintptr_t)next_expires_smartobj.BasePtr() << " age " << age << " expire_age " << m_expire_age;

			if (age >= m_expire_age)
				break;

			auto sec = (m_expire_age - age + CCTICKS_PER_SEC/2) / CCTICKS_PER_SEC;

			if (sec <= 0)
				break;

			#if 0
			if (sec > 10)
			{
				ccsleep(10);

				return;		// return to recheck object time in case it's changed by another thread during the sleep
			}
			#endif

			if (ccsleep(sec))
				return;

		}

		while (is_ccmint)
		{
			auto param_level = txpay_param_level_from_wire(obj);

			auto block_level = g_blockchain.GetLastIndelibleLevel();

			if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(debug) << "ExpireObj::DoExpires " << m_name << " is_ccmint " << is_ccmint << " block level " << block_level << " param_level " << param_level;

			if (block_level >= CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
				break;	// expire all mints

			if (!param_level && block_level)
				break;	// expire now

			if (param_level == 1 && block_level && block_level > 2 * CC_MINT_ACCEPT_SPAN)
				break;	// expire now

			if (param_level > 1 && param_level + CC_MINT_ACCEPT_SPAN < block_level)
				break;	// expire now

			if (ccsleep(2))	// wait for new indelible level
				return;
		}

		while (objtype == CC_TYPE_BLOCK)
		{
			auto prune_level = g_blockchain.ComputePruneLevel(0, BLOCK_PRUNE_ROUNDS + 2 + 2 * isValidObj());

			auto block = (Block*)obj;
			auto wire = block->WireData();

			if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(debug) << "ExpireObj::DoExpires " << m_name << " block seqnum " << next_expires_seqnum << " bufp " << (uintptr_t)next_expires_smartobj.BasePtr() << " level " << wire->level.GetValue() << " prune level " << prune_level;

			if (wire->level.GetValue() < prune_level)
			{
				if (isValidObj())
					block->SetPriorBlock(SmartBuf());	// break the link so prior block can be freed

				break;
			}

			if (ccsleep(10))	// wait for blockchain to advance
				return;
		}

		if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << "ExpireObj::DoExpires " << m_name << " expiring seqnum " << next_expires_seqnum << " bufp " << (uintptr_t)next_expires_smartobj.BasePtr() << " type " << objtype << " age " << age << " expire_age " << m_expire_age;

		if (DeleteExpires(next_expires_seqnum, next_expires_smartobj))
			return;	// retry later
	}
}

void Expire::Init()
{
	BOOST_LOG_TRIVIAL(trace) << "Expire::Init";

	m_expireobjs.push_back(new ValidObjsExpire("ValidObjs-Blocks", g_seqnum[BLOCKSEQ][VALIDSEQ].seqmin, g_seqnum[BLOCKSEQ][VALIDSEQ].seqmax, valid_block_expire_age));
	m_expireobjs.push_back(new RelayObjsExpire("RelayObjs-Blocks", g_seqnum[BLOCKSEQ][RELAYSEQ].seqmin, g_seqnum[BLOCKSEQ][RELAYSEQ].seqmax, relay_block_expire_age));

	m_expireobjs.push_back(new ValidObjsExpire("ValidObjs-Txs", g_seqnum[TXSEQ][VALIDSEQ].seqmin, g_seqnum[TXSEQ][VALIDSEQ].seqmax, valid_tx_expire_age));
	m_expireobjs.push_back(new RelayObjsExpire("RelayObjs-Txs", g_seqnum[TXSEQ][RELAYSEQ].seqmin, g_seqnum[TXSEQ][RELAYSEQ].seqmax, relay_tx_expire_age));

	m_expireobjs.push_back(new ValidObjsExpire("ValidObjs-Xreq", g_seqnum[XREQSEQ][VALIDSEQ].seqmin, g_seqnum[XREQSEQ][VALIDSEQ].seqmax, valid_tx_expire_age));
	m_expireobjs.push_back(new RelayObjsExpire("RelayObjs-Xreq", g_seqnum[XREQSEQ][RELAYSEQ].seqmin, g_seqnum[XREQSEQ][RELAYSEQ].seqmax, relay_tx_expire_age));

	for (auto expireobj : m_expireobjs)
	{
		expireobj->Start();
	}
}

void Expire::DeInit()
{
	BOOST_LOG_TRIVIAL(trace) << "Expire::DeInit";

	for (unsigned i = 0; i < m_expireobjs.size(); ++i)
	{
		auto obj = m_expireobjs[i];

		m_expireobjs[i] = NULL;

		obj->Stop();

		delete obj;
	}

	BOOST_LOG_TRIVIAL(trace) << "Expire::DeInit done";
}

int32_t Expire::GetExpireAge(unsigned i)
{
	if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << "Expire::GetExpireAge queue " << i;

	if (i >= m_expireobjs.size())
	{
		BOOST_LOG_TRIVIAL(warning) << "Expire::GetExpireAge queue " << i << " >= " << m_expireobjs.size();

		return -1;
	}

	if (!m_expireobjs[i])
	{
		BOOST_LOG_TRIVIAL(warning) << "Expire::GetExpireAge queue " << i << " is null";

		return -1;
	}

	auto age = m_expireobjs[i]->m_expire_age;

	if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << "Expire::GetExpireAge queue " << i << " returning " << age;

	return age;
}

void Expire::ChangeExpireAge(unsigned i, int32_t age)
{
	if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << "Expire::ChangeExpireAge queue " << i << " new expire age " << age;

	if (i >= m_expireobjs.size())
	{
		BOOST_LOG_TRIVIAL(warning) << "Expire::ChangeExpireAge queue " << i << " >= " << m_expireobjs.size();

		return;
	}

	if (!m_expireobjs[i])
	{
		BOOST_LOG_TRIVIAL(warning) << "Expire::ChangeExpireAge queue " << i << " is null";

		return;
	}

	CCASSERT(m_expireobjs[i]->isValidObj());

	if (age < 0)
		age = m_expireobjs[i]->m_default_expire_age;

	m_expireobjs[i]->m_expire_age = age;

	if (TRACE_EXPIRE) BOOST_LOG_TRIVIAL(trace) << "Expire::ChangeExpireAge queue " << i << " set to expire age " << age;
}
