/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * dbconn-xreqs.cpp
*/

#include "ccnode.h"
#include "dbconn.hpp"
#include "seqnum.hpp"
#include "processtx.hpp"

#include <dblog.h>
#include <CCobjects.hpp>
#include <transaction.h>
#include <transaction.hpp>
#include <xtransaction-xreq.hpp>

#define TRACE_DBCONN	(g_params.trace_exchange_db)

//static boost::shared_mutex Xreqs_db_mutex;	// to avoid inconsistency problems with shared cache
static mutex Xreqs_db_mutex;				// to avoid inconsistency problems with shared cache

atomic<unsigned> DbConnXreqs::xreq_count_pending(0);
atomic<unsigned> DbConnXreqs::xreq_count_persistent(0);

static const int xreq_cols = 69;	// number of xreq columns
static const int nwc = 14;			// number of witness columns

DbConnXreqs::DbConnXreqs()
{
	ClearDbPointers();

	//lock_guard<boost::shared_mutex> lock(Xreqs_db_mutex);
	lock_guard<mutex> lock(Xreqs_db_mutex);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::DbConnXreqs dbconn " << (uintptr_t)this;

	OpenDb();

	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "insert into Xreqs values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, ?31, ?32, ?33, ?34, ?35, ?36, ?37, ?38, ?39, ?40, ?41, ?42, ?43, ?44, ?45, ?46, ?47, ?48, ?49, ?50, ?51, ?52, ?53, ?54, ?55, ?56, ?57, ?58, ?59, ?60, ?61, ?62, ?63, ?64, ?65, ?66, ?67, ?68, ?69);", -1, &Xreqs_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "delete from Xreqs where Seqnum = ?1;", -1, &Xreqs_delete, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select true from Xreqs where QuoteAsset = ?1 and ForeignAddress = ?2 and ForeignAddressUnique and ForeignAddress is not null limit 1;", -1, &Xreqs_select_unique_foreign_address, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs where Seqnum >= ?1 order by Seqnum limit 1;", -1, &Xreqs_select_seqnum, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs where Xreqnum >= ?1 and (not ?2 or Type = ?2) order by Xreqnum limit 1;", -1, &Xreqs_select_xreqnum, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs where ObjId = ?1 order by Seqnum limit 1;", -1, &Xreqs_select_objid, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs where ExpireTime <= ?1 order by ExpireTime, Xreqnum limit 1;", -1, &Xreqs_select_expire, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs indexed by Xreqs_OpenRateRequired_Index "
		"where IsBuyer = ?1 and OpenAmount is not null " // required to use Xreqs_OpenRateRequired_Index
		"and BaseAsset = ?2 and QuoteAsset = ?3 and ForeignAsset = ?4 "
		"and Type >= ?5 and (Type <= ?6 or not ?6) "
		"and ConsiderationRequired <= ?7 and ConsiderationOffered >= ?8 "
		"and case when IsBuyer then Pledge >= ?9 else Pledge <= ?9 end "
		"and AcceptTimeRequired <= ?10 and AcceptTimeOffered >= ?11 "
		"and case when IsBuyer then PaymentTime >= ?12 else PaymentTime <= ?12 end "
		"and case when IsBuyer then Confirmations >= ?13 else Confirmations <= ?13 end "
		"and MinAmount <= ?14 and OpenAmount >= ?15 "
		"and OpenRateRequired >= ?16 and PendingMatchRate <= ?17 "
		"order by BaseAsset, QuoteAsset, ForeignAsset, IsBuyer, OpenRateRequired, Xreqnum, Seqnum;", -1, &Xreqs_select_open_rate_required, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs indexed by Xreqs_PendingMatchRate_Index "
		"where IsBuyer = ?1 and PendingMatchRate > 0 and PendingMatchHoldTime > 0 " // required to use Xreqs_PendingMatchRate_Index
		"and BaseAsset = ?2 and QuoteAsset = ?3 and ForeignAsset = ?4 "
		"and Type >= ?5 and (Type <= ?6 or not ?6) "
		"and ConsiderationRequired <= ?7 and ConsiderationOffered >= ?8 "
		"and case when IsBuyer then Pledge >= ?9 else Pledge <= ?9 end "
		"and AcceptTimeRequired <= ?10 and AcceptTimeOffered >= ?11 "
		"and case when IsBuyer then PaymentTime >= ?12 else PaymentTime <= ?12 end "
		"and case when IsBuyer then Confirmations >= ?13 else Confirmations <= ?13 end "
		"and MinAmount <= ?14 and OpenAmount >= ?15 "
		"and PendingMatchRate >= ?16 "
		"order by BaseAsset, QuoteAsset, ForeignAsset, IsBuyer, PendingMatchRate, Xreqnum, Seqnum;", -1, &Xreqs_select_pending_match_rate_ascending, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs indexed by Xreqs_PendingMatchRate_Index "
		"where IsBuyer = ?1 and PendingMatchRate > 0 and PendingMatchHoldTime > 0 " // required to use Xreqs_PendingMatchRate_Index
		"and BaseAsset = ?2 and QuoteAsset = ?3 and ForeignAsset = ?4 "
		"and Type >= ?5 and (Type <= ?6 or not ?6) "
		"and ConsiderationRequired <= ?7 and ConsiderationOffered >= ?8 "
		"and case when IsBuyer then Pledge >= ?9 else Pledge <= ?9 end "
		"and AcceptTimeRequired <= ?10 and AcceptTimeOffered >= ?11 "
		"and case when IsBuyer then PaymentTime >= ?12 else PaymentTime <= ?12 end "
		"and case when IsBuyer then Confirmations >= ?13 else Confirmations <= ?13 end "
		"and MinAmount <= ?14 and OpenAmount >= ?15 "
		"and PendingMatchRate <= ?16 "
		"order by BaseAsset desc, QuoteAsset desc, ForeignAsset desc, IsBuyer desc, PendingMatchRate desc, Xreqnum desc, Seqnum desc;", -1, &Xreqs_select_pending_match_rate_descending, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs as X "
		"join Xreqs as Match on Match.Seqnum = X.BestOtherSeqnum "
		"where X.PendingMatchOrder > 0 order by X.PendingMatchOrder limit 1;", -1, &Xreqs_select_pending_match, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "update Xreqs set PendingMatchRate = 0 "
		"where PendingMatchRate > 0 and PendingMatchEpoch != ?1 and Xreqnum <= ?2;", -1, &Xreqs_clear_pending_matches, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "update Xreqs set "
		// ?1 = ForWitness, ?2 = FirstPass, ?3 = LastMatchedNum, ?4 = BlockTime, ?5 = max_xreqnum
		"Recalc						= case when     ?1 then Recalc  else (?2 and RecalcTime  >= " STRINGIFY(XREQ_RECALC_NEXT) " and RecalcTime  <= ?4) or (LastMatched  = ?3) or (BestAmount  is not null and ifnull((select (LastMatched  = ?3) from Xreqs as X where X.Seqnum = Xreqs.BestOtherSeqnum ), true)) end, "
		"RecalcW					= case when not ?1 then RecalcW else (?2 and RecalcTimeW >= " STRINGIFY(XREQ_RECALC_NEXT) " and RecalcTimeW <= ?4) or (LastMatchedW = ?3) or (BestAmountW is not null and ifnull((select (LastMatchedW = ?3) from Xreqs as X where X.Seqnum = Xreqs.BestOtherSeqnumW), true)) end "
		"where Xreqnum <= ?5;", -1, &Xreqs_set_recalc, NULL)));	// "SCAN Xreqs" with two CORRELATED SCALAR SUBQUERY's
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "update Xreqs set "
		// ?1 = ForWitness, ?2 = FirstPass, ?3 = max_xreqnum, ?4 = DBL_MAX
		"RecalcTime					= case when     ?1 or not ?2 or not Recalc		then RecalcTime  else " STRINGIFY(XREQ_RECALC_NOT) " end, "
		"RecalcTimeW				= case when not ?1 or not ?2 or not RecalcW		then RecalcTimeW else " STRINGIFY(XREQ_RECALC_NOT) " end, "
		"XreqnumW					= case when not ?1 or not ?2		then XreqnumW else Xreqnum end, "
		"BlockTimeW					= case when not ?1 or not ?2		then BlockTimeW else BlockTime end, "
		"MatchingAmount				= case when     ?1 or not ?2		then MatchingAmount  else OpenAmount end, "
		"MatchingAmountW			= case when not ?1 or not ?2		then MatchingAmountW else OpenAmount end, "
		"MatchingRateRequired		= case when     ?1 or not ?2		then MatchingRateRequired  else case when IsBuyer then -OpenRateRequired else OpenRateRequired end end, "
		"MatchingRateRequiredW		= case when not ?1 or not ?2		then MatchingRateRequiredW else case when IsBuyer then -OpenRateRequired else OpenRateRequired end end, "
		// don't clear these because they are currently used to retrieve final matches:
		//"BestOtherSeqnum			= case when     ?1 or not Recalc	then BestOtherSeqnum  else 0 end, "
		//"BestOtherSeqnumW			= case when not ?1 or not RecalcW	then BestOtherSeqnumW else 0 end, "
		//"BestOtherXreqnum			= case when     ?1 or not Recalc	then BestOtherXreqnum  else 0 end, "
		//"BestOtherXreqnumW			= case when not ?1 or not RecalcW	then BestOtherXreqnumW else 0 end, "
		"BestAmount					= case when     ?1 or not Recalc	then BestAmount  else null end, "
		"BestAmountW				= case when not ?1 or not RecalcW	then BestAmountW else null end, "
		"BestOtherMatchingAmount	= case when     ?1 or not Recalc	then BestOtherMatchingAmount  else null end, "
		"BestOtherMatchingAmountW	= case when not ?1 or not RecalcW	then BestOtherMatchingAmountW else null end, "
		"BestRate					= case when     ?1 or not Recalc	then BestRate  else case when IsBuyer then ?4 else -?4 end end, "
		"BestRateW					= case when not ?1 or not RecalcW	then BestRateW else case when IsBuyer then ?4 else -?4 end end, "
		"BestNetRate				= case when     ?1 or not Recalc	then BestNetRate  else case when IsBuyer then ?4 else -?4 end end, "
		"BestNetRateW				= case when not ?1 or not RecalcW	then BestNetRateW else case when IsBuyer then ?4 else -?4 end end, "
		"BestOtherNetRate			= case when     ?1 or not Recalc	then BestOtherNetRate  else case when IsBuyer then -?4 else ?4 end end, "
		"BestOtherNetRateW			= case when not ?1 or not RecalcW	then BestOtherNetRateW else case when IsBuyer then -?4 else ?4 end end "
		"where Xreqnum <= ?3;", -1, &Xreqs_init_matching, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs indexed by Xreqs_OpenRateRequired_Index "
		"where OpenAmount is not null " // required to use Xreqs_OpenRateRequired_Index
		"and BaseAsset > ?1 "
		"and Xreqnum >= ?2 and Xreqnum <= ?3 "
		"order by BaseAsset, QuoteAsset, ForeignAsset, IsBuyer, OpenRateRequired, Xreqnum, Seqnum limit 1;", -1, &Xreqs_select_pair_base, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs indexed by Xreqs_OpenRateRequired_Index "
		"where OpenAmount is not null " // required to use Xreqs_OpenRateRequired_Index
		"and BaseAsset = ?1 and QuoteAsset >= ?2 and (QuoteAsset > ?2 or ForeignAsset > ?3) "
		"and Xreqnum >= ?4 and Xreqnum <= ?5 "
		"order by BaseAsset, QuoteAsset, ForeignAsset, IsBuyer, OpenRateRequired, Xreqnum, Seqnum limit 1;", -1, &Xreqs_select_pair_quote, NULL)));
	// note: the following queries include OpenRateRequired > ?2 and order by BaseAsset...Seqnum so they can use Xreqs_OpenRateRequired_Index
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs indexed by Xreqs_OpenRateRequired_Index "
		// ?1 = ForWitness
		"where IsBuyer = 1 and OpenAmount is not null " // required to use Xreqs_OpenRateRequired_Index
		"and BaseAsset = ?2 and QuoteAsset = ?3 and ForeignAsset = ?4 "
		"and (OpenRateRequired, Xreqnum, Seqnum) > (?5, ?6, ?7) "
		"and Xreqnum <= ?8 "
		"and case when ?1 then true else Xreqnum != 0 end "
		"and case when ?1 then MatchingAmountW is not null else MatchingAmount is not null end "
		"and case when ?1 then MatchingRateRequiredW >= 0 else MatchingRateRequired >= 0 end "
		"order by BaseAsset, QuoteAsset, ForeignAsset, IsBuyer, OpenRateRequired, Xreqnum, Seqnum limit 1;", -1, &Xreqs_select_major, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "select * from Xreqs indexed by Xreqs_OpenRateRequired_Index "
		// ?1 = ForWitness
		"where IsBuyer = 0 and OpenAmount is not null " // required to use Xreqs_OpenRateRequired_Index
		"and BaseAsset = ?2 and QuoteAsset = ?3 and ForeignAsset = ?4 "
		"and (OpenRateRequired, Xreqnum, Seqnum) > (?5, ?6, ?7) "
		"and Xreqnum <= ?8 "
		"and Type >= ?9 and (Type <= ?10 or not ?10) "
		"and ConsiderationRequired <= ?11 and ConsiderationOffered >= ?12 and Pledge <= ?13 "
		"and AcceptTimeRequired <= ?14 and AcceptTimeOffered >= ?15 "
		"and PaymentTime <= ?16 and Confirmations <= ?17 "
		"and MinAmount <= ?18 "
		"and case when ?1 then true else Xreqnum != 0 end "
		"and case when ?1 then MatchingAmountW >= ?19 else MatchingAmount >= ?19 end "
		"and case when ?1 then MatchingRateRequiredW <= ?20 else MatchingRateRequired <= ?20 end "
		"and case when ?1 then RecalcW >= ?21 else Recalc >= ?21 end "
		"order by BaseAsset, QuoteAsset, ForeignAsset, IsBuyer, OpenRateRequired, Xreqnum, Seqnum limit 1;", -1, &Xreqs_select_minor, NULL)));
    const string search_match_select_sql = "select * from Xreqs as X, Xreqs as Match "
		"where X.IsBuyer and ";
	const string search_match_order_sql = "order by X.Xreqnum, X.Seqnum limit 1;";
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, (search_match_select_sql
		+ "X.Xreqnum >= ?1 "
		"and X.BestAmount  not null "
		"and Match.Seqnum = X.BestOtherSeqnum "
		"and Match.BestOtherSeqnum  = X.Seqnum "
		+ search_match_order_sql).c_str(), -1, &Xreqs_select_nonwitness_match, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, (search_match_select_sql
		+ "X.Xreqnum >= ?1 "
		"and X.BestAmountW not null "
		"and Match.Seqnum = X.BestOtherSeqnumW "
		"and Match.BestOtherSeqnumW = X.Seqnum "
		+ search_match_order_sql).c_str(), -1, &Xreqs_select_witness_match_xreqnum, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, (search_match_select_sql
		+ "X.Xreqnum = 0 and X.Seqnum >= ?1 "
		"and X.BestAmountW not null "
		"and Match.Seqnum = X.BestOtherSeqnumW "
		"and Match.BestOtherSeqnumW = X.Seqnum "
		+ search_match_order_sql).c_str(), -1, &Xreqs_select_witness_match_seqnum, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Xreqs_db, "update Xreqs set "
		"LinkedSeqnum = ?2, "
		// ?3 = ForWitness
		"OpenAmount				= case when     ?3 then OpenAmount else ?4 end, "
		"OpenRateRequired		= case when     ?3 then OpenRateRequired else ?5 end, "
		"PendingMatchEpoch		= case when     ?3 then PendingMatchEpoch else ?6 end, "
		"PendingMatchOrder		= case when     ?3 then PendingMatchOrder else ?7 end, "
		"PendingMatchAmount		= case when     ?3 then PendingMatchAmount else ?8 end, "
		"PendingMatchRate		= case when     ?3 then PendingMatchRate else ?9 end, "
		"PendingMatchHoldTime	= case when     ?3 then PendingMatchHoldTime else ?10 end, "
		"XreqnumW				= case when not ?3 then XreqnumW else ?11 end, "				// note: Xreqnum   only set by Xreqs_insert
		"BlockTimeW				= case when not ?3 then BlockTimeW else ?12 end, "				// note: BlockTime only set by Xreqs_insert
		"MatchingAmount			= case when     ?3 then MatchingAmount  else ?13 end, "
		"MatchingAmountW		= case when not ?3 then MatchingAmountW else ?13 end, "
		"MatchingRateRequired	= case when     ?3 then MatchingRateRequired  else ?14 end, "
		"MatchingRateRequiredW	= case when not ?3 then MatchingRateRequiredW else ?14 end, "
		"RecalcTime				= case when     ?3 then RecalcTime  else ?15 end, "
		"RecalcTimeW			= case when not ?3 then RecalcTimeW else ?15 end, "
		"LastMatched			= case when     ?3 then LastMatched  else ?16 end, "
		"LastMatchedW			= case when not ?3 then LastMatchedW else ?16 end, "
		"BestAmount				= case when     ?3 then BestAmount  else ?17 end, "
		"BestAmountW			= case when not ?3 then BestAmountW else ?17 end, "
		"BestRate				= case when     ?3 then BestRate  else ?18 end, "
		"BestRateW				= case when not ?3 then BestRateW else ?18 end, "
		"BestNetRate			= case when     ?3 then BestNetRate  else ?19 end, "
		"BestNetRateW			= case when not ?3 then BestNetRateW else ?19 end, "
		"BestOtherSeqnum		= case when     ?3 then BestOtherSeqnum  else ?20 end, "
		"BestOtherSeqnumW		= case when not ?3 then BestOtherSeqnumW else ?20 end, "
		"BestOtherXreqnum		= case when     ?3 then BestOtherXreqnum  else ?21 end, "
		"BestOtherXreqnumW		= case when not ?3 then BestOtherXreqnumW else ?21 end, "
		"BestOtherMatchingAmount = case when     ?3 then BestOtherMatchingAmount  else ?22 end, "
		"BestOtherMatchingAmountW = case when not ?3 then BestOtherMatchingAmountW else ?22 end, "
		"BestOtherNetRate		= case when     ?3 then BestOtherNetRate  else ?23 end, "
		"BestOtherNetRateW		= case when not ?3 then BestOtherNetRateW else ?23 end  "
		"where Seqnum = ?1;", -1, &Xreqs_update, NULL)));

	//if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::DbConnXreqs dbconn done " << (uintptr_t)this;
}

DbConnXreqs::~DbConnXreqs()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::~DbConnXreqs dbconn " << (uintptr_t)this;

	static bool explain = TEST_EXPLAIN_DB_QUERIES;

#if TEST_EXPLAIN_DB_QUERIES
	unique_lock<mutex> elock(g_db_explain_lock);

	if (!explain)
		elock.unlock();

	//lock_guard<boost::shared_mutex> lock(Xreqs_db_mutex);
	lock_guard<mutex> lock(Xreqs_db_mutex);
#endif

	//if (explain)
	//	CCASSERTZ(dbexec(Xreqs_db, "analyze;"));

	DbFinalize(Xreqs_insert, explain);
	DbFinalize(Xreqs_delete, explain);
	DbFinalize(Xreqs_select_seqnum, explain);
	DbFinalize(Xreqs_select_xreqnum, explain);
	DbFinalize(Xreqs_select_objid, explain);
	DbFinalize(Xreqs_select_expire, explain);
	DbFinalize(Xreqs_select_unique_foreign_address, explain);
	DbFinalize(Xreqs_select_open_rate_required, explain);
	DbFinalize(Xreqs_select_pending_match_rate_ascending, explain);
	DbFinalize(Xreqs_select_pending_match_rate_descending, explain);
	DbFinalize(Xreqs_select_pending_match, explain);
	DbFinalize(Xreqs_clear_pending_matches, explain);
	DbFinalize(Xreqs_set_recalc, explain);
	DbFinalize(Xreqs_init_matching, explain);
	DbFinalize(Xreqs_select_pair_base, explain);
	DbFinalize(Xreqs_select_pair_quote, explain);
	DbFinalize(Xreqs_select_major, explain);
	DbFinalize(Xreqs_select_minor, explain);
	DbFinalize(Xreqs_select_nonwitness_match, explain);
	DbFinalize(Xreqs_select_witness_match_seqnum, explain);
	DbFinalize(Xreqs_select_witness_match_xreqnum, explain);
	DbFinalize(Xreqs_update, explain);

	explain = false;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::~DbConnXreqs done dbconn " << (uintptr_t)this;
}

void DbConnXreqs::DoXreqsFinish()
{
	if (RandTest(RTEST_DELAY_DB_RESET)) sleep(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::DoXreqsFinish dbconn " << uintptr_t(this);

	sqlite3_reset(Xreqs_insert);
	sqlite3_reset(Xreqs_delete);
	sqlite3_reset(Xreqs_select_seqnum);
	sqlite3_reset(Xreqs_select_xreqnum);
	sqlite3_reset(Xreqs_select_objid);
	sqlite3_reset(Xreqs_select_expire);
	sqlite3_reset(Xreqs_select_unique_foreign_address);
	sqlite3_reset(Xreqs_select_open_rate_required);
	sqlite3_reset(Xreqs_select_pending_match_rate_ascending);
	sqlite3_reset(Xreqs_select_pending_match_rate_descending);
	sqlite3_reset(Xreqs_select_pending_match);
	sqlite3_reset(Xreqs_clear_pending_matches);
	sqlite3_reset(Xreqs_set_recalc);
	sqlite3_reset(Xreqs_init_matching);
	sqlite3_reset(Xreqs_select_pair_base);
	sqlite3_reset(Xreqs_select_pair_quote);
	sqlite3_reset(Xreqs_select_major);
	sqlite3_reset(Xreqs_select_minor);
	sqlite3_reset(Xreqs_select_nonwitness_match);
	sqlite3_reset(Xreqs_select_witness_match_seqnum);
	sqlite3_reset(Xreqs_select_witness_match_xreqnum);
	sqlite3_reset(Xreqs_update);
}

int DbConnXreqs::XreqsInsert(Xreq& xreq)
{
	//lock_guard<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);				// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	Xreq xreq2;

	auto rc = XreqsSelectObjIdWithLock(xreq.objid, false, xreq2);
	if (rc < 0) return rc;
	if (!rc)
	{
		if (!xreq2.xreqnum)
		{
			// replace the existing non-presistent Xreq that has the same ObjId

			if (!xreq.seqnum)
			{
				xreq.seqnum = xreq2.seqnum;

				if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsInsert re-using seqnum " << xreq.seqnum;
			}

			rc = XreqsDeleteWithLock(xreq2);
			if (rc < 0) return rc;
		}
	}

	if (!xreq.seqnum)
		xreq.seqnum = g_seqnum[XREQSEQ][VALIDSEQ].NextNum();

	return XreqsInsertWithLock(xreq);
}

int DbConnXreqs::XreqsInsertWithLock(const Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsInsert dbconn " << uintptr_t(this) << " " << xreq.DebugString();

	// MinAmount, OpenAmount and MatchingAmount are stored big endian, in a fixed 128 bit field
	packed_unsigned_amount_t min_amount, open_amount, matching_amount;
	CCASSERTZ(pack_unsigned_amount(xreq.min_amount, min_amount));
	CCASSERTZ(pack_unsigned_amount(xreq.open_amount, open_amount));
	CCASSERTZ(pack_unsigned_amount(xreq.matching_amount, matching_amount));

	//Xreqs:
	// 1: Seqnum, LinkedSeqnum, ObjId, Type, IsBuyer, ExpireTime, BaseAsset, QuoteAsset, ForeignAsset, MinAmount, MaxAmount
	// 12: NetRateRequired, WaitDiscount, BaseCosts, QuoteCosts
	// 16: AddImmediatelyToBlockchain, AutoAcceptMatches, NoMinimumAfterFirstMatch, MustLiquidateCrossingMinimum, MustLiquidateBelowMinimum
	// 21: ConsiderationRequired, ConsiderationOffered, Pledge
	// 24: HoldTime, HoldTimeRequired, MinWaitTime
	// 27: AcceptTimeRequired, AcceptTimeOffered
	// 29: PaymentTime, Confirmations
	// 31: ForeignAddressUnique, ForeignAddress, Destination, PubSigningKey
	// 35: OpenAmount blob, OpenRateRequired float
	// 37: PendingMatchEpoch integer, PendingMatchOrder integer, PendingMatchAmount blob, PendingMatchRate numeric, PendingMatchHoldTime integer
	// 42: Xreqnum , BlockTime , MatchingAmount , MatchingRateRequired , RecalcTime , Recalc , LastMatched , BestAmount , BestRate , BestNetRate , BestOtherSeqnum , BestOtherXreqnum , BestOtherMatchingAmount , BestOtherNetRate
	// 56: XreqnumW, BlockTimeW, MatchingAmountW, MatchingRateRequiredW, RecalcTimeW, RecalcW, LastMatchedW, BestAmountW, BestRateW, BestNetRateW, BestOtherSeqnumW, BestOtherXreqnumW, BestOtherMatchingAmountW, BestOtherNetRateW
	if (dblog(sqlite3_bind_int64(Xreqs_insert, 1, xreq.seqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_insert, 2, xreq.linked_seqnum))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_insert, 3, &xreq.objid, sizeof(xreq.objid), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 4, xreq.type))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 5, xreq.IsBuyer()))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_insert, 6, xreq.expire_time))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_insert, 7, xreq.base_asset))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_insert, 8, xreq.quote_asset))) return -1;
	if (dblog(sqlite3_bind_text(Xreqs_insert, 9, xreq.foreign_asset.c_str(), -1, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_insert, 10, &min_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_insert, 11, &xreq.max_amount, sizeof(xreq.max_amount), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_insert, 12, xreq.net_rate_required.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_insert, 13, xreq.wait_discount.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_insert, 14, xreq.base_costs.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_insert, 15, xreq.quote_costs.asFloat()))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 16, xreq.flags.add_immediately_to_blockchain))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 17, xreq.flags.auto_accept_matches))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 18, xreq.flags.no_minimum_after_first_match))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 19, xreq.flags.must_liquidate_crossing_minimum))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 20, xreq.flags.must_liquidate_below_minimum))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 21, xreq.consideration_required))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 22, xreq.consideration_offered))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 23, xreq.pledge))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 24, xreq.hold_time))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 25, xreq.hold_time_required))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 26, xreq.min_wait_time))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 27, xreq.accept_time_required))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 28, xreq.accept_time_offered))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 29, xreq.payment_time))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 30, xreq.confirmations))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 31, ProcessTx::CheckTransientDuplicateForeignAddresses(xreq.quote_asset)))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_insert, 32, xreq.foreign_address.c_str(), xreq.foreign_address.length(), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_insert, 33, &xreq.destination, sizeof(xreq.destination), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_insert, 34, &xreq.signing_public_key, sizeof(xreq.signing_public_key), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_insert, 35, &open_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_insert, 36, xreq.SignedRate(xreq.open_rate_required).asFloat()))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_insert, 37, xreq.pending_match_epoch))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_insert, 38, xreq.pending_match_order))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_insert, 39, &xreq.pending_match_amount, sizeof(xreq.pending_match_amount), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_insert, 40, xreq.pending_match_rate.asFloat()))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_insert, 41, xreq.pending_match_hold_time))) return -1;

	for (unsigned i = 0; i < 2; ++i)
	{
		if (dblog(sqlite3_bind_int64(Xreqs_insert, 42 + nwc*i, xreq.xreqnum))) return -1;
		if (dblog(sqlite3_bind_int64(Xreqs_insert, 43 + nwc*i, xreq.blocktime))) return -1;
		if (dblog(sqlite3_bind_blob(Xreqs_insert, 44 + nwc*i, &matching_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
		if (dblog(sqlite3_bind_double(Xreqs_insert, 45 + nwc*i, xreq.matching_rate_required.asFloat()))) return -1;
		if (dblog(sqlite3_bind_int64(Xreqs_insert, 46 + nwc*i, xreq.recalc_time))) return -1;
		if (dblog(sqlite3_bind_int(Xreqs_insert, 47 + nwc*i, xreq.recalc))) return -1;
		if (dblog(sqlite3_bind_int64(Xreqs_insert, 48 + nwc*i, xreq.last_matched))) return -1;
		if (dblog(sqlite3_bind_blob(Xreqs_insert, 49 + nwc*i, &xreq.best_amount, sizeof(xreq.best_amount), SQLITE_STATIC))) return -1;
		if (dblog(sqlite3_bind_double(Xreqs_insert, 50 + nwc*i, xreq.best_rate.asFloat()))) return -1;
		if (dblog(sqlite3_bind_double(Xreqs_insert, 51 + nwc*i, xreq.best_net_rate.asFloat()))) return -1;
		if (dblog(sqlite3_bind_int64(Xreqs_insert, 52 + nwc*i, xreq.best_other_seqnum))) return -1;
		if (dblog(sqlite3_bind_int64(Xreqs_insert, 53 + nwc*i, xreq.best_other_xreqnum))) return -1;
		if (dblog(sqlite3_bind_blob(Xreqs_insert, 54 + nwc*i, &xreq.best_other_matching_amount, sizeof(xreq.best_other_matching_amount), SQLITE_STATIC))) return -1;
		if (dblog(sqlite3_bind_double(Xreqs_insert, 55 + nwc*i, xreq.best_other_net_rate.asFloat()))) return -1;
	}

	if (!xreq.flags.has_signing_key)
	{
		if (dblog(sqlite3_bind_null(Xreqs_insert, 34))) return -1;
	}

	if (!xreq.open_amount)
	{
		if (dblog(sqlite3_bind_null(Xreqs_insert, 35))) return -1;
	}

	if (!xreq.matching_amount)
	{
		if (dblog(sqlite3_bind_null(Xreqs_insert, 44))) return -1;
		if (dblog(sqlite3_bind_null(Xreqs_insert, 44 + nwc))) return -1;
	}

	if (!xreq.best_amount)
	{
		if (dblog(sqlite3_bind_null(Xreqs_insert, 49))) return -1;
		if (dblog(sqlite3_bind_null(Xreqs_insert, 49 + nwc))) return -1;
	}

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::XreqsInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Xreqs_insert);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnXreqs::XreqsInsert constraint violation " << xreq.DebugString();

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Xreqs_db);

	if (changes != 1)
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsInsert sqlite3_changes " << changes << " after insert " << xreq.DebugString();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnXreqs::XreqsInsert inserted " << xreq.DebugString();

	if (xreq.xreqnum)
		++xreq_count_persistent;
	else
		++xreq_count_pending;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnXreqs::XreqsInsert xreq_count_pending " << xreq_count_pending.load() << " xreq_count_persistent " << xreq_count_persistent.load();

	return 0;
}

int DbConnXreqs::XreqsDelete(const Xreq& xreq)
{
	//lock_guard<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);				// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	return XreqsDeleteWithLock(xreq);
}

int DbConnXreqs::XreqsDeleteWithLock(const Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsDelete " << xreq.DebugString();

	// 1: Seqnum
	if (dblog(sqlite3_bind_int64(Xreqs_delete, 1, xreq.seqnum))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::XreqsDelete simulating database error pre-delete";

		return -1;
	}

	auto rc = sqlite3_step(Xreqs_delete);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Xreqs_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnXreqs::XreqsDelete changes " << changes << " after delete " << xreq.DebugString();

	if (changes && xreq.xreqnum)
	{
		CCASSERT(xreq_count_persistent.load());
		--xreq_count_persistent;
	}

	if (changes && !xreq.xreqnum)
	{
		CCASSERT(xreq_count_pending.load());
		--xreq_count_pending;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnXreqs::XreqsDelete xreq_count_pending " << xreq_count_pending.load() << " xreq_count_persistent " << xreq_count_persistent.load();

	return 0;
}

int DbConnXreqs::XreqsSelectUniqueForeignAddress(uint64_t blockchain, const string& foreign_address)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelectUniqueForeignAddress blockchain " << blockchain << " foreign_address " << foreign_address;

	// 1: Blockchain, 2:ForeignAddress
	if (dblog(sqlite3_bind_int64(Xreqs_select_unique_foreign_address, 1, blockchain))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_select_unique_foreign_address, 2, foreign_address.c_str(), foreign_address.length(), SQLITE_STATIC))) return -1;

	int rc;

	if (dblog(rc = sqlite3_step(Xreqs_select_unique_foreign_address), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::XreqsSelectUniqueForeignAddress simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelectUniqueForeignAddress returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelectUniqueForeignAddress returned " << rc;

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnXreqs::XreqsSelectUniqueForeignAddress returning found";

	return 0;
}

int DbConnXreqs::XreqsUpdate(const Xreq& xreq)
{
	//lock_guard<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);				// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsUpdate dbconn " << uintptr_t(this) << " " << xreq.DebugString();

	// OpenAmount and MatchingAmount are stored big endian, in a fixed 128 bit field
	packed_unsigned_amount_t open_amount, matching_amount;
	CCASSERTZ(pack_unsigned_amount(xreq.open_amount, open_amount));
	CCASSERTZ(pack_unsigned_amount(xreq.matching_amount, matching_amount));

	//Xreqs:
	// 1: Seqnum, LinkedSeqnum, ForWitness
	// 3: OpenAmount blob, OpenRateRequired float
	// 5: PendingMatchEpoch integer, PendingMatchOrder integer, PendingMatchAmount blob, PendingMatchRate numeric, PendingMatchHoldTime integer
	// 10: Xreqnum, BlockTime, MatchingAmount, MatchingRateRequired, RecalcTime, LastMatched, BestAmount, BestRate, BestNetRate, BestOtherSeqnum, BestOtherXreqnum, BestOtherMatchingAmount, BestOtherNetRate
	if (dblog(sqlite3_bind_int64(Xreqs_update, 1, xreq.seqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_update, 2, xreq.linked_seqnum))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_update, 3, xreq.for_witness))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_update, 4, &open_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_update, 5, xreq.SignedRate(xreq.open_rate_required).asFloat()))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_update, 6, xreq.pending_match_epoch))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_update, 7, xreq.pending_match_order))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_update, 8, &xreq.pending_match_amount, sizeof(xreq.pending_match_amount), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_update, 9, xreq.pending_match_rate.asFloat()))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_update, 10, xreq.pending_match_hold_time))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_update, 11, xreq.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_update, 12, xreq.blocktime))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_update, 13, &matching_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_update, 14, xreq.matching_rate_required.asFloat()))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_update, 15, xreq.recalc_time))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_update, 16, xreq.last_matched))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_update, 17, &xreq.best_amount, sizeof(xreq.best_amount), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_update, 18, xreq.best_rate.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_update, 19, xreq.best_net_rate.asFloat()))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_update, 20, xreq.best_other_seqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_update, 21, xreq.best_other_xreqnum))) return -1;
	if (dblog(sqlite3_bind_blob(Xreqs_update, 22, &xreq.best_other_matching_amount, sizeof(xreq.best_other_matching_amount), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_update, 23, xreq.best_other_net_rate.asFloat()))) return -1;

	if (!xreq.open_amount)
	{
		if (dblog(sqlite3_bind_null(Xreqs_update, 4))) return -1;
	}

	if (!xreq.matching_amount)
	{
		if (dblog(sqlite3_bind_null(Xreqs_update, 13))) return -1;
	}

	if (!xreq.best_amount)
	{
		if (dblog(sqlite3_bind_null(Xreqs_update, 17))) return -1;
	}

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::XreqsUpdate simulating database error pre-update";

		return -1;
	}

	auto rc = sqlite3_step(Xreqs_update);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Xreqs_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnXreqs::XreqsUpdate changes " << changes << " after update " << xreq.DebugString();

	return 0;
}

int DbConnXreqs::XreqsClearOldPendingMatches(const uint64_t match_epoch, const uint64_t max_xreqnum)
{
	//lock_guard<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);				// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsClearOldPendingMatches dbconn " << uintptr_t(this) << " match_epoch " << match_epoch << " max_xreqnum " << max_xreqnum;

	// PendingMatchEpoch != ?1 and Xreqnum <= ?2
	if (dblog(sqlite3_bind_int64(Xreqs_clear_pending_matches, 1, match_epoch))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_clear_pending_matches, 2, max_xreqnum))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::XreqsClearOldPendingMatches simulating database error pre-update";

		return -1;
	}

	auto rc = sqlite3_step(Xreqs_clear_pending_matches);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Xreqs_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnXreqs::XreqsClearOldPendingMatches changes " << changes << " after update match_epoch " << match_epoch << " max_xreqnum " << max_xreqnum;

	return 0;
}

// returns 0=found, 1=not found, -1=server error
int DbConnXreqs::XreqsSelect(sqlite3_stmt *select, const bool for_witness, Xreq& xreq)
{
	int rc;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::XreqsSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelect returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != xreq_cols)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	return XreqsSelectInternal(select, for_witness, xreq);
}

int DbConnXreqs::XreqsSelectInternal(sqlite3_stmt *select, const bool for_witness, Xreq& xreq, unsigned cs)
{
	auto objid_blob = sqlite3_column_blob(select, cs + 2);
	auto objid_size = sqlite3_column_bytes(select, cs + 2);
	auto min_amount_blob = sqlite3_column_blob(select, cs + 9);
	auto min_amount_size = sqlite3_column_bytes(select, cs + 9);
	auto max_amount_blob = sqlite3_column_blob(select, cs + 10);
	auto max_amount_size = sqlite3_column_bytes(select, cs + 10);
	auto destination_blob = sqlite3_column_blob(select, cs + 32);
	auto destination_size = sqlite3_column_bytes(select, cs + 32);
	auto signing_public_key_blob = sqlite3_column_blob(select, cs + 33);
	auto signing_public_key_size = sqlite3_column_bytes(select, cs + 33);
	auto open_amount_blob = sqlite3_column_blob(select, cs + 34);
	auto open_amount_size = sqlite3_column_bytes(select, cs + 34);
	auto pending_match_amount_blob = sqlite3_column_blob(select, cs + 38);
	auto pending_match_amount_size = sqlite3_column_bytes(select, cs + 38);
	auto matching_amount_blob = sqlite3_column_blob(select, cs + 43 + nwc*for_witness);
	auto matching_amount_size = sqlite3_column_bytes(select, cs + 43 + nwc*for_witness);
	auto best_amount_blob = sqlite3_column_blob(select, cs + 48 + nwc*for_witness);
	auto best_amount_size = sqlite3_column_bytes(select, cs + 48 + nwc*for_witness);
	auto best_other_matching_amount_blob = sqlite3_column_blob(select, cs + 53 + nwc*for_witness);
	auto best_other_matching_amount_size = sqlite3_column_bytes(select, cs + 53 + nwc*for_witness);

	if (!objid_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect objid_blob is null";

		return -1;
	}
	else if (objid_size != sizeof(xreq.objid))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect objid_size " << objid_size << " != " << sizeof(xreq.objid);

		return -1;
	}

	if (!min_amount_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect min_amount_blob is null";

		return -1;
	}
	else if (min_amount_size != AMOUNT_UNSIGNED_PACKED_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect packed min_amount_size " << min_amount_size << " != " << AMOUNT_UNSIGNED_PACKED_BYTES;

		return -1;
	}

	if (!max_amount_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect max_amount_blob is null";

		return -1;
	}
	else if (max_amount_size != sizeof(xreq.max_amount))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect max_amount_size " << max_amount_size << " != " << sizeof(xreq.max_amount);

		return -1;
	}

	if (!destination_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect destination_blob is null";

		return -1;
	}
	else if (destination_size != sizeof(xreq.destination))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect destination_size " << destination_size << " != " << sizeof(xreq.destination);

		return -1;
	}

	if (signing_public_key_blob && signing_public_key_size != sizeof(xreq.signing_public_key))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect signing_public_key_size " << signing_public_key_size << " != " << sizeof(xreq.signing_public_key);

		return -1;
	}

	if (open_amount_blob && open_amount_size != AMOUNT_UNSIGNED_PACKED_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect packed open_amount_size " << open_amount_size << " != " << AMOUNT_UNSIGNED_PACKED_BYTES;

		return -1;
	}

	if (!pending_match_amount_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect amount is null";

		return -1;
	}

	if (pending_match_amount_size != sizeof(xreq.pending_match_amount))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect returned pending_match_amount_size " << pending_match_amount_size << " != " << sizeof(xreq.pending_match_amount);

		return -1;
	}

	if (matching_amount_blob && matching_amount_size != AMOUNT_UNSIGNED_PACKED_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect packed matching_amount_size " << matching_amount_size << " != " << AMOUNT_UNSIGNED_PACKED_BYTES;

		return -1;
	}

	if (best_amount_blob && best_amount_size != sizeof(xreq.best_amount))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect best_amount_size " << best_amount_size << " != " << sizeof(xreq.best_amount);

		return -1;
	}

	if (best_other_matching_amount_blob && best_other_matching_amount_size != sizeof(xreq.best_other_matching_amount))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect best_other_matching_amount_size " << best_other_matching_amount_size << " != " << sizeof(xreq.best_other_matching_amount);

		return -1;
	}

	//Xreqs:
	// 0: Seqnum, LinkedSeqnum, ObjId blob, Type, IsBuyer, ExpireTime, BaseAsset, QuoteAsset, ForeignAsset string, MinAmount blob, MaxAmount blob
	// 11: NetRateRequired float, WaitDiscount float, BaseCosts float, QuoteCosts float
	// 15: AddImmediatelyToBlockchain, AutoAcceptMatches, NoMinimumAfterFirstMatch, MustLiquidateCrossingMinimum, MustLiquidateBelowMinimum
	// 20: ConsiderationRequired, ConsiderationOffered, Pledge
	// 23: HoldTime, HoldTimeRequired, MinWaitTime
	// 26: AcceptTimeRequired, AcceptTimeOffered
	// 28: PaymentTime, Confirmations
	// 30: ForeignAddressUnique boolean, ForeignAddress blob, Destination blob, PubSigningKey blob
	// 34: OpenAmount blob, OpenRateRequired float
	// 36: PendingMatchEpoch integer, PendingMatchOrder integer, PendingMatchAmount blob, PendingMatchRate numeric, PendingMatchHoldTime integer
	// 41: Xreqnum, BlockTime, MatchingAmount blob, MatchingRateRequired float, IsNew, RecalcTime, Recalc, Reset, LastMatched, BestAmount blob, BestRate float, BestNetRate float, BestOtherSeqnum, BestOtherXreqnum, BestOtherMatchingAmount blob, BestOtherNetRate float

	xreq.seqnum = sqlite3_column_int64(select, cs + 0);
	xreq.linked_seqnum = sqlite3_column_int64(select, cs + 1);
	//auto objid_blob = sqlite3_column_blob(select, cs + 2);
	xreq.type = sqlite3_column_int(select, cs + 3);
	//xreq.isbuyer = sqlite3_column_int64(select, cs + 4);
	xreq.expire_time = sqlite3_column_int64(select, cs + 5);
	xreq.base_asset = sqlite3_column_int64(select, cs + 6);
	xreq.quote_asset = sqlite3_column_int64(select, cs + 7);
	auto foreign_asset_text = sqlite3_column_text(select, cs + 8);
	//auto min_amount_blob = sqlite3_column_blob(select, cs + 9);
	//auto max_amount_blob = sqlite3_column_blob(select, cs + 10);
	xreq.net_rate_required = sqlite3_column_double(select, cs + 11);
	xreq.wait_discount = sqlite3_column_double(select, cs + 12);
	xreq.base_costs = sqlite3_column_double(select, cs + 13);
	xreq.quote_costs = sqlite3_column_double(select, cs + 14);
	xreq.flags.add_immediately_to_blockchain = sqlite3_column_int(select, cs + 15);
	xreq.flags.auto_accept_matches = sqlite3_column_int(select, cs + 16);
	xreq.flags.no_minimum_after_first_match = sqlite3_column_int(select, cs + 17);
	xreq.flags.must_liquidate_crossing_minimum = sqlite3_column_int(select, cs + 18);
	xreq.flags.must_liquidate_below_minimum = sqlite3_column_int(select, cs + 19);
	xreq.consideration_required = sqlite3_column_int(select, cs + 20);
	xreq.consideration_offered = sqlite3_column_int(select, cs + 21);
	xreq.pledge = sqlite3_column_int(select, cs + 22);
	xreq.hold_time = sqlite3_column_int(select, cs + 23);
	xreq.hold_time_required = sqlite3_column_int(select, cs + 24);
	xreq.min_wait_time = sqlite3_column_int(select, cs + 25);
	xreq.accept_time_required = sqlite3_column_int(select, cs + 26);
	xreq.accept_time_offered = sqlite3_column_int(select, cs + 27);
	xreq.payment_time = sqlite3_column_int(select, cs + 28);
	xreq.confirmations = sqlite3_column_int(select, cs + 29);
	//ForeignAddressUnique = sqlite3_column_int(select, cs + 30);
	auto foreign_address_text = sqlite3_column_text(select, cs + 31);
	//auto destination_blob = sqlite3_column_blob(select, cs + 32);
	//auto signing_public_key_blob = sqlite3_column_blob(select, cs + 33);
	//auto open_amount_blob = sqlite3_column_blob(select, cs + 34);
	xreq.open_rate_required = xreq.SignedRate(sqlite3_column_double(select, cs + 35));
	xreq.pending_match_epoch = sqlite3_column_int64(select, cs + 36);
	xreq.pending_match_order = sqlite3_column_int64(select, cs + 37);
	//xreq.pending_match_amount = sqlite3_column_blob(select, cs + 38);
	xreq.pending_match_rate = sqlite3_column_double(select, cs + 39);
	xreq.pending_match_hold_time = sqlite3_column_int(select, cs + 40);
	xreq.xreqnum = sqlite3_column_int64(select, cs + 41 + nwc*for_witness);
	xreq.blocktime = sqlite3_column_int64(select, cs + 42 + nwc*for_witness);
	//auto matching_amount_blob = sqlite3_column_blob(select, cs + 43 + nwc*for_witness);
	xreq.matching_rate_required = sqlite3_column_double(select, cs + 44 + nwc*for_witness);
	xreq.recalc_time = sqlite3_column_int64(select, cs + 45 + nwc*for_witness);
	xreq.recalc = sqlite3_column_int(select, cs + 46 + nwc*for_witness);
	xreq.last_matched = sqlite3_column_int64(select, cs + 47 + nwc*for_witness);
	//auto best_amount_blob = sqlite3_column_blob(select, cs + 48 + nwc*for_witness);
	xreq.best_rate = sqlite3_column_double(select, cs + 49 + nwc*for_witness);
	xreq.best_net_rate = sqlite3_column_double(select, cs + 50 + nwc*for_witness);
	xreq.best_other_seqnum = sqlite3_column_int64(select, cs + 51 + nwc*for_witness);
	xreq.best_other_xreqnum = sqlite3_column_int64(select, cs + 52 + nwc*for_witness);
	//auto best_other_matching_amount_blob = sqlite3_column_blob(select, cs + 53 + nwc*for_witness);
	xreq.best_other_net_rate = sqlite3_column_double(select, cs + 54 + nwc*for_witness);

	if (dblog(sqlite3_extended_errcode(Xreqs_db), DB_STMT_SELECT))
	{
		xreq.Clear();

		return -1;	// check if error retrieving results
	}

	if (!xreq.seqnum)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XreqsSelect seqnum is zero";

		xreq.Clear();

		return -1;
	}

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::XreqsSelect simulating database error post-select";

		xreq.Clear();

		return -1;
	}

	xreq.for_witness = for_witness;
	xreq.foreign_asset = (char*)foreign_asset_text;
	xreq.foreign_address = (char*)foreign_address_text;
	memcpy((void*)&xreq.objid, objid_blob, sizeof(xreq.objid));
	unpack_unsigned_amount(min_amount_blob, xreq.min_amount);
	memcpy((void*)&xreq.max_amount, max_amount_blob, sizeof(xreq.max_amount));
	memcpy((void*)&xreq.pending_match_amount, pending_match_amount_blob, sizeof(xreq.pending_match_amount));
	memcpy((void*)&xreq.destination, destination_blob, sizeof(xreq.destination));

	if (signing_public_key_blob)
	{
		xreq.flags.has_signing_key = true;
		memcpy((void*)&xreq.signing_public_key, signing_public_key_blob, sizeof(xreq.signing_public_key));
	}
	else
	{
		xreq.flags.has_signing_key = false;
		memset((void*)&xreq.signing_public_key, 0, sizeof(xreq.signing_public_key));
	}

	if (open_amount_blob)
		unpack_unsigned_amount(open_amount_blob, xreq.open_amount);
	else
		memset((void*)&xreq.open_amount, 0, sizeof(xreq.open_amount));

	if (matching_amount_blob)
		unpack_unsigned_amount(matching_amount_blob, xreq.matching_amount);
	else
		memset((void*)&xreq.matching_amount, 0, sizeof(xreq.matching_amount));

	if (best_amount_blob)
		memcpy((void*)&xreq.best_amount, best_amount_blob, sizeof(xreq.best_amount));
	else
		memset((void*)&xreq.best_amount, 0, sizeof(xreq.best_amount));

	if (best_other_matching_amount_blob)
		memcpy((void*)&xreq.best_other_matching_amount, best_other_matching_amount_blob, sizeof(xreq.best_other_matching_amount));
	else
		memset((void*)&xreq.best_other_matching_amount, 0, sizeof(xreq.best_other_matching_amount));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnXreqs::XreqsSelect returning " << xreq.DebugString();

	return 0;
}

// returns 0=found, 1=not found, -1=server error
int DbConnXreqs::XreqsSelectSeqnum(const int64_t seqnum, bool for_witness, Xreq& xreq, bool or_greater)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelectSeqnum " << seqnum << " or_greater " << or_greater;

	xreq.Clear();

	// Seqnum >=
	if (dblog(sqlite3_bind_int64(Xreqs_select_seqnum, 1, seqnum))) return -1;

	auto rc = XreqsSelect(Xreqs_select_seqnum, for_witness, xreq);
	if (rc) return rc;

	CCASSERT(xreq.seqnum >= seqnum);

	if (xreq.seqnum != seqnum && !or_greater)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnXreqs::XreqsSelectSeqnum " << seqnum << " or_greater " << or_greater << " found " << xreq.seqnum << " returning SQLITE_DONE";

		xreq.Clear();

		return 1;
	}

	return 0;
}

// returns 0=found, 1=not found, -1=server error
int DbConnXreqs::XreqsSelectObjId(const ccoid_t& objid, bool for_witness, Xreq& xreq)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	return XreqsSelectObjIdWithLock(objid, for_witness, xreq);
}

int DbConnXreqs::XreqsSelectObjIdWithLock(const ccoid_t& objid, bool for_witness, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelectObjId dbconn " << uintptr_t(this) << " for_witness " << for_witness << " objid " << buf2hex(&objid, CC_OID_TRACE_SIZE);

	xreq.Clear();

	// ObjId
	if (dblog(sqlite3_bind_blob(Xreqs_select_objid, 1, &objid, sizeof(objid), SQLITE_STATIC))) return -1;

	auto rc = XreqsSelect(Xreqs_select_objid, for_witness, xreq);
	if (rc) return rc;

	CCASSERT(xreq.objid == objid);

	return 0;
}

// returns 0=found, 1=not found, -1=server error
int DbConnXreqs::XreqsSelectExpire(const uint64_t expire_time, Xreq& xreq)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelectExpire " << expire_time;

	xreq.Clear();

	// ExpireTime <=
	if (dblog(sqlite3_bind_int64(Xreqs_select_expire, 1, expire_time))) return -1;

	return XreqsSelect(Xreqs_select_expire, false, xreq);
}

// returns 0=found, 1=not found, -1=server error
int DbConnXreqs::XreqsSelectXreqnum(uint64_t xreqnum, Xreq& xreq, unsigned type)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelectXreqnum xreqnum " << xreqnum << " type " << type;

	xreq.Clear();

	// Xreqnum >=, Type
	if (dblog(sqlite3_bind_int64(Xreqs_select_xreqnum, 1, xreqnum))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_select_xreqnum, 2, type))) return -1;

	auto rc = XreqsSelect(Xreqs_select_xreqnum, false, xreq);
	if (rc) return rc;

	CCASSERT(xreq.xreqnum >= xreqnum);

	return 0;
}

// return # found
int DbConnXreqs::XreqsSelectOpenRateRequired(const Xreq& xreq, unsigned matching_type, unsigned maxret, unsigned offset, bool include_pending_matched, Xreq *xreqs, bool *have_more)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelectOpenRateRequired matching_type " << matching_type << " maxret " << maxret << " offset " << offset << " include_pending_matched " << include_pending_matched << " " << xreq.DebugString();

	auto select = Xreqs_select_open_rate_required;

	// OpenRateRequired >= ?16 and PendingMatchRate <= ?17
	if (dblog(sqlite3_bind_double(select, 16, Xreq::SignedRate(!Xtx::TypeIsBuyer(matching_type), xreq.open_rate_required).asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(select, 17, (include_pending_matched ? DBL_MAX : 0)))) return -1;

	return XreqsSelectRateInternal(select, xreq, matching_type, maxret, offset, xreqs, have_more);
}

// return # found
int DbConnXreqs::XreqsSelectPendingMatchRate(const Xreq& xreq, unsigned matching_type, unsigned maxret, unsigned offset, Xreq *xreqs, bool *have_more)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelectPendingMatchRate matching_type " << matching_type << " maxret " << maxret << " offset " << offset << " " << xreq.DebugString();

	auto select = (Xtx::TypeIsBuyer(matching_type) ? Xreqs_select_pending_match_rate_ascending : Xreqs_select_pending_match_rate_descending);

	if (dblog(sqlite3_bind_double(select, 16, xreq.pending_match_rate.asFloat()))) return -1;

	return XreqsSelectRateInternal(select, xreq, matching_type, maxret, offset, xreqs, have_more);
}

// return # found
int DbConnXreqs::XreqsSelectRateInternal(sqlite3_stmt* select, const Xreq& xreq, unsigned matching_type, unsigned maxret, unsigned offset, Xreq *xreqs, bool *have_more)
{
	if (have_more)
		*have_more = false;

	unsigned nfound = 0;

	packed_unsigned_amount_t min_amount, max_amount;
	CCASSERTZ(pack_unsigned_amount(xreq.min_amount, min_amount));
	CCASSERTZ(pack_unsigned_amount(xreq.max_amount, max_amount));

	if (!xreq.max_amount)
		memset(&max_amount, -1, AMOUNT_UNSIGNED_PACKED_BYTES);

	auto isbuyer = !Xtx::TypeIsBuyer(matching_type);

	// IsBuyer = ?1 and OpenAmount is not null " // required to use Xreqs_OpenRateRequired_Index
	// BaseAsset = ?2 and QuoteAsset = ?3 and ForeignAsset = ?4 "
	// Type >= ?5 and (Type <= ?6 or not ?6) "
	// ConsiderationRequired <= ?7 and ConsiderationOffered >= ?8 "
	// case when IsByer then Pledge >= ?9 else Pledge <= ?9 end "
	// AcceptTimeRequired <= ?10 and AcceptTimeOffered >= ?11 "
	// case when IsByer then PaymentTime >= ?12 else PaymentTime <= ?12 end "
	// case when IsByer then Confirmations >= ?13 else Confirmations <= ?13 end "
	// MinAmount <= ?14 and OpenAmount >= ?15 and OpenRateRequired/PendingMatchRate >= ?16 "
	if (dblog(sqlite3_bind_int(select, 1, isbuyer))) return -1;
	if (dblog(sqlite3_bind_int64(select, 2, xreq.base_asset))) return -1;
	if (dblog(sqlite3_bind_int64(select, 3, xreq.quote_asset))) return -1;
	if (dblog(sqlite3_bind_text(select, 4, xreq.foreign_asset.c_str(), -1, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(select, 5, xreq.type))) return -1;
	if (dblog(sqlite3_bind_int(select, 6, xreq.db_search_max))) return -1;
	if (dblog(sqlite3_bind_int(select, 7, xreq.consideration_required))) return -1;
	if (dblog(sqlite3_bind_int(select, 8, xreq.consideration_offered))) return -1;
	if (dblog(sqlite3_bind_int(select, 9, xreq.pledge))) return -1;
	if (dblog(sqlite3_bind_int(select, 10, xreq.accept_time_required))) return -1;
	if (dblog(sqlite3_bind_int(select, 11, xreq.accept_time_offered))) return -1;
	if (dblog(sqlite3_bind_int(select, 12, xreq.payment_time))) return -1;
	if (dblog(sqlite3_bind_int(select, 13, xreq.confirmations))) return -1;
	if (dblog(sqlite3_bind_blob(select, 14, &max_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(select, 15, &min_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;

	while (nfound < maxret && !g_shutdown)
	{
		Xreq& xreq_out = xreqs[nfound];

		xreq_out.Clear();

		auto rc = XreqsSelect(select, false, xreq_out);
		if (rc < 0) return rc;
		if (rc) return nfound;

		CCASSERT(xreq_out.IsBuyer() == isbuyer);
		CCASSERT(xreq_out.base_asset == xreq.base_asset);
		CCASSERT(xreq_out.quote_asset == xreq.quote_asset);
		CCASSERT(xreq_out.foreign_asset == xreq.foreign_asset);
		CCASSERT(xreq_out.type >= xreq.type);
		CCASSERT(xreq_out.type <= xreq.db_search_max || !xreq.db_search_max);
		CCASSERT(xreq_out.foreign_asset == xreq.foreign_asset);

		CCASSERT(xreq_out.min_amount <= xreq.max_amount || !xreq.max_amount);
		CCASSERT(xreq_out.open_amount >= xreq.min_amount);

		// to ensure integrity of mining, only match CC_TYPE_XCX_SIMPLE_BUY and CC_TYPE_XCX_MINING_BUY with CC_TYPE_XCX_SIMPLE_SELL or CC_TYPE_XCX_MINING_SELL
		if ((matching_type == CC_TYPE_XCX_SIMPLE_BUY || matching_type == CC_TYPE_XCX_MINING_BUY) && xreq_out.type != CC_TYPE_XCX_SIMPLE_SELL && xreq_out.type != CC_TYPE_XCX_MINING_SELL)
			continue;
		if ((xreq_out.type == CC_TYPE_XCX_SIMPLE_BUY || xreq_out.type == CC_TYPE_XCX_MINING_BUY) && matching_type != CC_TYPE_XCX_SIMPLE_SELL && matching_type != CC_TYPE_XCX_MINING_SELL)
			continue;

		if (offset)
		{
			if ((select == Xreqs_select_open_rate_required				&& xreq_out.SignedRate(xreq_out.open_rate_required) < Xreq::SignedRate(isbuyer, xreq.matching_rate_required))
			 || (select == Xreqs_select_pending_match_rate_ascending	&& xreq_out.pending_match_rate < xreq.matching_rate_required)
			 || (select == Xreqs_select_pending_match_rate_descending	&& xreq_out.pending_match_rate > xreq.matching_rate_required))
			{
				--offset;
				continue;
			}

			offset = 0;
		}

		++nfound;
	}

	// step one more time to set have_more

	if (have_more)
	{
		auto rc = sqlite3_step(select);

		if (dbresult(rc) != SQLITE_DONE)
			*have_more = true;
	}

	return nfound;
}

// returns 0=found, 1=not found, -1=server error
int DbConnXreqs::XreqsSelectPairBase(const Xreq& xreq, Xreq& xreq_out)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelectPairBase " << xreq.DebugString();

	xreq_out.Clear();

	auto select = Xreqs_select_pair_base;

	// BaseAsset = ?1
	// Xreqnum >= ?2 and Xreqnum <= ?3
	if (dblog(sqlite3_bind_int64(select, 1, xreq.base_asset))) return -1;
	if (dblog(sqlite3_bind_int64(select, 2, xreq.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(select, 3, xreq.db_search_max_xreqnum))) return -1;

	auto rc = XreqsSelect(select, xreq.for_witness, xreq_out);
	if (rc) return rc;

	CCASSERT((int64_t)xreq_out.base_asset > (int64_t)xreq.base_asset);
	CCASSERT(xreq_out.xreqnum >= xreq.xreqnum);
	CCASSERT(xreq_out.xreqnum <= xreq.db_search_max_xreqnum);

	xreq_out.db_search_max_xreqnum = xreq.db_search_max_xreqnum;

	return 0;
}

// returns 0=found, 1=not found, -1=server error
int DbConnXreqs::XreqsSelectPairQuote(const Xreq& xreq, Xreq& xreq_out)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::XreqsSelectPairQuote " << xreq.DebugString();

	xreq_out.Clear();

	auto select = Xreqs_select_pair_quote;

	// BaseAsset = ?1, QuoteAsset >= ?2, ForeignAsset > ?3
	// Xreqnum >= ?4 and Xreqnum <= ?5
	if (dblog(sqlite3_bind_int64(select, 1, xreq.base_asset))) return -1;
	if (dblog(sqlite3_bind_int64(select, 2, xreq.quote_asset))) return -1;
	if (dblog(sqlite3_bind_text(select, 3, xreq.foreign_asset.c_str(), -1, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(select, 4, xreq.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(select, 5, xreq.db_search_max_xreqnum))) return -1;

	auto rc = XreqsSelect(select, xreq.for_witness, xreq_out);
	if (rc) return rc;

	// BaseAsset = ?1 and QuoteAsset >= ?2 and (QuoteAsset > ?2 or ForeignAsset > ?3)
	CCASSERT(xreq_out.base_asset == xreq.base_asset);
	CCASSERT((int64_t)xreq_out.quote_asset >= (int64_t)xreq.quote_asset);
	CCASSERT((int64_t)xreq_out.quote_asset >  (int64_t)xreq.quote_asset || xreq_out.foreign_asset > xreq.foreign_asset);

	CCASSERT(xreq_out.xreqnum >= xreq.xreqnum);
	CCASSERT(xreq_out.xreqnum <= xreq.db_search_max_xreqnum);

	xreq_out.db_search_max_xreqnum = xreq.db_search_max_xreqnum;

	return 0;
}

// returns 0=found, 1=not found, -1=server error
int DbConnXreqs::MatchingSelectMajor(const Xreq& xreq, Xreq& xreq_out)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingSelectMajor " << xreq.DebugString();

	xreq_out.Clear();

	auto select = Xreqs_select_major;

	// ?1 = ForWitness
	// BaseAsset = ?2 and QuoteAsset = ?3 and ForeignAsset = ?4 "
	// (OpenRateRequired, Xreqnum, Seqnum) > (?5, ?6, ?7) "
	// Xreqnum <= ?8 "
	if (dblog(sqlite3_bind_int(select, 1, xreq.for_witness))) return -1;
	if (dblog(sqlite3_bind_int64(select, 2, xreq.base_asset))) return -1;
	if (dblog(sqlite3_bind_int64(select, 3, xreq.quote_asset))) return -1;
	if (dblog(sqlite3_bind_text(select, 4, xreq.foreign_asset.c_str(), -1, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(select, 5, xreq.open_rate_required.asFloat()))) return -1; // major's xreq.open_rate_required is SignedRate
	if (dblog(sqlite3_bind_int64(select, 6, xreq.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(select, 7, xreq.seqnum))) return -1;
	if (dblog(sqlite3_bind_int64(select, 8, xreq.db_search_max_xreqnum))) return -1;

	auto rc = XreqsSelect(select, xreq.for_witness, xreq_out);
	if (rc) return rc;

	CCASSERT(xreq_out.IsBuyer());

	CCASSERT(xreq_out.base_asset == xreq.base_asset);
	CCASSERT(xreq_out.quote_asset == xreq.quote_asset);
	CCASSERT(xreq_out.foreign_asset == xreq.foreign_asset);

	CCASSERT(xreq_out.SignedRate(xreq_out.open_rate_required) >= xreq.open_rate_required);
	CCASSERT(xreq_out.SignedRate(xreq_out.open_rate_required) >  xreq.open_rate_required || xreq_out.xreqnum >= xreq.xreqnum);
	CCASSERT(xreq_out.SignedRate(xreq_out.open_rate_required) >  xreq.open_rate_required || xreq_out.xreqnum >  xreq.xreqnum || xreq_out.seqnum > xreq.seqnum);
	CCASSERT(xreq_out.SignedRate(xreq_out.open_rate_required) >  xreq.open_rate_required
		 || (xreq_out.SignedRate(xreq_out.open_rate_required) == xreq.open_rate_required && xreq_out.xreqnum >  xreq.xreqnum)
		 || (xreq_out.SignedRate(xreq_out.open_rate_required) == xreq.open_rate_required && xreq_out.xreqnum >  xreq.xreqnum && xreq_out.seqnum > xreq.seqnum));

	CCASSERT(xreq.for_witness || xreq_out.xreqnum);
	CCASSERT(xreq_out.xreqnum <= xreq.db_search_max_xreqnum);

	CCASSERT(xreq_out.matching_amount);
	CCASSERT(xreq_out.matching_amount <= xreq_out.open_amount);
	CCASSERT(xreq_out.open_rate_required >= 0);
	CCASSERT(xreq_out.matching_rate_required >= 0);

	xreq_out.db_search_max_xreqnum = xreq.db_search_max_xreqnum;

	return 0;
}

// returns 0=found, 1=not found, -1=server error
int DbConnXreqs::MatchingSelectMinor(const Xreq& xreq, Xreq& xreq_out)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingSelectMinor " << xreq.DebugString();

	xreq_out.Clear();

	// MinAmount and MatchingAmount are compared big endian, in a fixed 128 bit field
	packed_unsigned_amount_t min_amount, matching_amount;
	CCASSERTZ(pack_unsigned_amount(xreq.min_amount, min_amount));
	CCASSERTZ(pack_unsigned_amount(xreq.matching_amount, matching_amount));

	auto select = Xreqs_select_minor;

	// ?1 = ForWitness
	// BaseAsset = ?2 and QuoteAsset = ?3 and ForeignAsset = ?4 "
	// (OpenRateRequired, Xreqnum, Seqnum) > (?5, ?6, ?7) "
	// Xreqnum <= ?8 "
	// Type >= ?9 and (Type <= ?10 or not ?10) "
	// ConsiderationRequired <= ?11 and ConsiderationOffered >= ?12 and Pledge <= ?13 "
	// AcceptTimeRequired <= ?14 and AcceptTimeOffered >= ?15 "
	// PaymentTime <= ?16 and Confirmations <= ?17 "
	// MinAmount <= ?18 "
	// case when ?1 then true else Xreqnum != 0 end "
	// case when ?1 then MatchingAmountW >= ?19 else MatchingAmount >= ?19 end "
	// case when ?1 then MatchingRateRequiredW <= ?20 else MatchingRateRequired <= ?20 end "
	// case when ?1 then RecalcW >= ?21 else Recalc >= ?21 end "
	if (dblog(sqlite3_bind_int(select, 1, xreq.for_witness))) return -1;
	if (dblog(sqlite3_bind_int64(select, 2, xreq.base_asset))) return -1;
	if (dblog(sqlite3_bind_int64(select, 3, xreq.quote_asset))) return -1;
	if (dblog(sqlite3_bind_text(select, 4, xreq.foreign_asset.c_str(), -1, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(select, 5, xreq.open_rate_required.asFloat()))) return -1; // minor's xreq.open_rate_required is SignedRate
	if (dblog(sqlite3_bind_int64(select, 6, xreq.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(select, 7, xreq.seqnum))) return -1;
	if (dblog(sqlite3_bind_int64(select, 8, xreq.db_search_max_xreqnum))) return -1;
	if (dblog(sqlite3_bind_int(select, 9, xreq.type))) return -1;
	if (dblog(sqlite3_bind_int(select, 10, xreq.db_search_max))) return -1;
	if (dblog(sqlite3_bind_int(select, 11, xreq.consideration_required))) return -1;
	if (dblog(sqlite3_bind_int(select, 12, xreq.consideration_offered))) return -1;
	if (dblog(sqlite3_bind_int(select, 13, xreq.pledge))) return -1;
	if (dblog(sqlite3_bind_int(select, 14, xreq.accept_time_required))) return -1;
	if (dblog(sqlite3_bind_int(select, 15, xreq.accept_time_offered))) return -1;
	if (dblog(sqlite3_bind_int(select, 16, xreq.payment_time))) return -1;
	if (dblog(sqlite3_bind_int(select, 17, xreq.confirmations))) return -1;
	if (dblog(sqlite3_bind_blob(select, 18, &min_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(select, 19, &matching_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(select, 20, xreq.matching_rate_required.asFloat()))) return -1;
	if (dblog(sqlite3_bind_int(select, 21, xreq.recalc))) return -1;

	auto rc = XreqsSelect(select, xreq.for_witness, xreq_out);
	if (rc) return rc;

	CCASSERT(xreq_out.IsSeller());

	CCASSERT(xreq_out.base_asset == xreq.base_asset);
	CCASSERT(xreq_out.quote_asset == xreq.quote_asset);
	CCASSERT(xreq_out.foreign_asset == xreq.foreign_asset);

	CCASSERT(xreq_out.SignedRate(xreq_out.open_rate_required) >= xreq.open_rate_required);
	CCASSERT(xreq_out.SignedRate(xreq_out.open_rate_required) >  xreq.open_rate_required || xreq_out.xreqnum >= xreq.xreqnum);
	CCASSERT(xreq_out.SignedRate(xreq_out.open_rate_required) >  xreq.open_rate_required || xreq_out.xreqnum >  xreq.xreqnum || xreq_out.seqnum > xreq.seqnum);
	CCASSERT(xreq_out.SignedRate(xreq_out.open_rate_required) >  xreq.open_rate_required
		 || (xreq_out.SignedRate(xreq_out.open_rate_required) == xreq.open_rate_required && xreq_out.xreqnum >  xreq.xreqnum)
		 || (xreq_out.SignedRate(xreq_out.open_rate_required) == xreq.open_rate_required && xreq_out.xreqnum >  xreq.xreqnum && xreq_out.seqnum > xreq.seqnum));

	CCASSERT(xreq.for_witness || xreq_out.xreqnum);
	CCASSERT(xreq_out.xreqnum <= xreq.db_search_max_xreqnum);

	CCASSERT(xreq_out.matching_amount);
	CCASSERT(xreq_out.matching_amount <= xreq_out.open_amount);
	CCASSERT(xreq_out.open_rate_required >= 0);
	CCASSERT(xreq_out.matching_rate_required >= 0);

	CCASSERT(xreq_out.type >= xreq.type);
	CCASSERT(xreq_out.type <= xreq.db_search_max || !xreq.db_search_max);
	CCASSERT(xreq_out.consideration_required <= xreq.consideration_required);
	CCASSERT(xreq_out.consideration_offered >= xreq.consideration_offered);
	CCASSERT(xreq_out.pledge <= xreq.pledge);
	CCASSERT(xreq_out.accept_time_required <= xreq.accept_time_required);
	CCASSERT(xreq_out.accept_time_offered >= xreq.accept_time_offered);
	CCASSERT(xreq_out.payment_time <= xreq.payment_time);
	CCASSERT(xreq_out.confirmations <= xreq.confirmations);
	CCASSERT(xreq_out.min_amount <= xreq.min_amount);
	CCASSERT(xreq_out.matching_amount >= xreq.matching_amount);
	CCASSERT(xreq_out.matching_rate_required <= xreq.matching_rate_required);
	CCASSERT(xreq_out.recalc >= xreq.recalc);

	xreq_out.db_search_max_xreqnum = xreq.db_search_max_xreqnum;

	return 0;
}

// returns 0=found, 1=not found, -1=server error
int DbConnXreqs::MatchingSelectMatch(const Xreq& xreq, Xreq& major, Xreq& minor)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingSelectMatch " << xreq.DebugString();

	major.Clear();
	minor.Clear();

	sqlite3_stmt* select;
	if (!xreq.for_witness)
	{
		select = Xreqs_select_nonwitness_match;
		if (dblog(sqlite3_bind_int64(select, 1, xreq.xreqnum))) return -1;
	}
	else if (xreq.xreqnum)
	{
		select = Xreqs_select_witness_match_xreqnum;
		if (dblog(sqlite3_bind_int64(select, 1, xreq.xreqnum))) return -1;
	}
	else
	{
		select = Xreqs_select_witness_match_seqnum;
		if (dblog(sqlite3_bind_int64(select, 1, xreq.seqnum))) return -1;
	}

	int rc;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::MatchingSelectMatch simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingSelectMatch returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::MatchingSelectNextPendingMatch returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != 2*xreq_cols)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::MatchingSelectNextPendingMatch returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	rc = XreqsSelectInternal(select, false, major);
	if (rc) return rc;

	CCASSERT(major.xreqnum >= xreq.xreqnum);
	CCASSERT(major.seqnum  >= xreq.seqnum);
	CCASSERT(xreq.for_witness || major.xreqnum);

	return XreqsSelectInternal(select, false, minor, xreq_cols);
}

int DbConnXreqs::MatchingInit(const uint64_t block_time, const bool first_pass, const uint64_t last_matched_num, const uint64_t max_xreqnum, const bool for_witness)
{
	//lock_guard<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);				// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingInit block_time " << block_time << " first_pass " << first_pass << " last_matched_num " << last_matched_num << " for_witness " << for_witness << " max_xreqnum " << max_xreqnum;

	// ?1 = ForWitness, ?2 = FirstPass, ?3 = LastMatchedNum, ?4 = BlockTime, ?5 = max_xreqnum
	if (dblog(sqlite3_bind_int(Xreqs_set_recalc, 1, for_witness))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_set_recalc, 2, first_pass))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_set_recalc, 3, last_matched_num))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_set_recalc, 4, block_time))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_set_recalc, 5, max_xreqnum))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::MatchingInit simulating database error pre Xreqs_set_recalc";

		return -1;
	}

	auto rc = sqlite3_step(Xreqs_set_recalc);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes64(Xreqs_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingInit Xreqs_set_recalc changes " << changes;


	// ?1 = ForWitness, ?2 = FirstPass, ?3 = max_xreqnum, ?4 = DBL_MAX
	if (dblog(sqlite3_bind_int(Xreqs_init_matching, 1, for_witness))) return -1;
	if (dblog(sqlite3_bind_int(Xreqs_init_matching, 2, first_pass))) return -1;
	if (dblog(sqlite3_bind_int64(Xreqs_init_matching, 3, max_xreqnum))) return -1;
	if (dblog(sqlite3_bind_double(Xreqs_init_matching, 4, DBL_MAX))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::MatchingInit simulating database error pre Xreqs_init_matching";

		return -1;
	}

	rc = sqlite3_step(Xreqs_init_matching);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	changes = sqlite3_changes64(Xreqs_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingInit Xreqs_init_matching changes " << changes;

	return 0;
}

void DbConnXreqs::SearchInitPairBase(const bool for_witness, uint64_t max_xreqnum, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::SearchInitPairBase max_xreqnum " << max_xreqnum << " for_witness " << for_witness;

	xreq.Clear();

	xreq.for_witness = for_witness;
	xreq.base_asset = INT64_MIN;
	xreq.xreqnum = (xreq.for_witness ? 0 : 1);
	xreq.db_search_max_xreqnum = (xreq.for_witness ? 0 : max_xreqnum);
}

void DbConnXreqs::SearchInitPairQuote(const Xreq& base, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::SearchInitPairQuote base " << base.DebugString();

	xreq.Clear();

	xreq.for_witness = base.for_witness;
	xreq.base_asset = base.base_asset;
	xreq.quote_asset = INT64_MIN;
	xreq.foreign_asset.clear();
	xreq.xreqnum = (xreq.for_witness ? 0 : 1);
	xreq.db_search_max_xreqnum = base.db_search_max_xreqnum;
}

void DbConnXreqs::SearchAdvancePairBase(const Xreq& match, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::SearchAdvancePairBase match base_asset " << match.base_asset;

	xreq.base_asset = match.base_asset + 1;
}

void DbConnXreqs::SearchAdvancePairQuote(const Xreq& match, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::SearchAdvancePairQuote match base_asset " << match.base_asset << " quote_asset " << match.quote_asset << " foreign_asset " << match.foreign_asset;

	xreq.quote_asset = match.quote_asset;
	xreq.foreign_asset = match.foreign_asset;
}

void DbConnXreqs::MatchingInitMajor(const Xreq& pair, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingInitMajor pair " << pair.DebugString();

	xreq.Clear();

	xreq.for_witness = pair.for_witness;
	xreq.base_asset = pair.base_asset;
	xreq.quote_asset = pair.quote_asset;
	xreq.foreign_asset = pair.foreign_asset;
	xreq.open_rate_required = -DBL_MAX;
	xreq.xreqnum = (xreq.for_witness ? 0 : 1);
	xreq.db_search_max_xreqnum = pair.db_search_max_xreqnum;
	xreq.seqnum = INT64_MIN;
}

void DbConnXreqs::MatchingInitTypeRange(unsigned type, bool for_query, Xreq& xreq)
{
	if (type == CC_TYPE_XCX_SIMPLE_BUY || type == CC_TYPE_XCX_MINING_BUY)
	{
		// simple and mining buy reqs only match to simple sell reqs on first pass matching
		xreq.type = CC_TYPE_XCX_SIMPLE_SELL;
		if (!for_query)
			xreq.db_search_max = CC_TYPE_XCX_SIMPLE_SELL;
		else
			xreq.db_search_max = CC_TYPE_XCX_MINING_SELL;
	}
	else if (type == CC_TYPE_XCX_NAKED_SELL || type == CC_TYPE_XCX_REQ_SELL)
	{
		// naked and full sell reqs cannot match simple or mining buy (converse of above)
		xreq.type = CC_TYPE_XCX_NAKED_BUY;
		xreq.db_search_max = CC_TYPE_XCX_REQ_BUY;
	}
	else if (type == CC_TYPE_XCX_NAKED_BUY)
	{
		// naked buy req cannot match simple or mining sell req (due to pledge)
		xreq.type = CC_TYPE_XCX_NAKED_SELL;
		xreq.db_search_max = CC_TYPE_XCX_REQ_SELL;
	}
	else
	{
		xreq.type = 0;
		xreq.db_search_max = 0;
	}
}

void DbConnXreqs::MatchingInitMinor(const Xreq& major, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingInitMinor major " << major.DebugString();

	xreq.Clear();

	MatchingInitTypeRange(major.type, false, xreq);

	xreq.for_witness = major.for_witness;
	xreq.base_asset = major.base_asset;
	xreq.quote_asset = major.quote_asset;
	xreq.foreign_asset = major.foreign_asset;
	xreq.open_rate_required = -DBL_MAX;
	xreq.xreqnum = (xreq.for_witness ? 0 : 1);
	xreq.db_search_max_xreqnum = major.db_search_max_xreqnum;
	xreq.seqnum = INT64_MIN;
	xreq.consideration_required = major.consideration_offered;
	xreq.consideration_offered = major.consideration_required;
	xreq.pledge = major.pledge;
	xreq.accept_time_required = major.accept_time_offered;
	xreq.accept_time_offered = major.accept_time_required;
	xreq.payment_time = major.payment_time;
	xreq.confirmations = major.confirmations;
	xreq.min_amount = major.matching_amount;
	xreq.matching_amount = major.min_amount;
	xreq.matching_rate_required = major.matching_rate_required;
	xreq.matching_rate_required = major.matching_rate_required;
	xreq.recalc = !major.recalc;
}

void DbConnXreqs::MatchingAdvanceMajor(const Xreq& match, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingAdvanceMajor match xreqnum " << match.xreqnum << " seqnum " << match.seqnum;

	xreq.open_rate_required = match.SignedRate(match.open_rate_required);
	xreq.xreqnum = match.xreqnum;
	xreq.seqnum = match.seqnum;
}

void DbConnXreqs::MatchingAdvanceMinor(const Xreq& match, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingAdvanceMinor match open_rate_required " << match.open_rate_required << " xreqnum " << match.xreqnum << " seqnum " << match.seqnum;

	xreq.open_rate_required = match.SignedRate(match.open_rate_required);
	xreq.xreqnum = match.xreqnum;
	xreq.seqnum = match.seqnum;
}

void DbConnXreqs::MatchingInitScan(const bool for_witness, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingInitScan for_witness " << for_witness;

	xreq.Clear();

	xreq.for_witness = for_witness;
	xreq.xreqnum = (for_witness ? 0 : 1);
	xreq.seqnum = INT64_MIN;
}

void DbConnXreqs::MatchingAdvanceScan(const Xreq& match, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingAdvanceMajor for_witness " << xreq.for_witness <<  " major xreqnum " << xreq.xreqnum << " seqnum " << xreq.seqnum << " match xreqnum " << match.xreqnum << " seqnum " << match.seqnum;

	if (xreq.for_witness && !xreq.xreqnum)
	{
		// first scan seqnum with xreqnum = 0
		xreq.seqnum = match.seqnum + 1;
	}
	else
	{
		// scan xreqnum > 0 with any seqnum
		xreq.xreqnum = match.xreqnum + 1;
		xreq.seqnum = INT64_MIN;
	}
}

bool DbConnXreqs::MatchingCheckRestartScan(bool not_found, const Xreq& match, Xreq& xreq)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingCheckRestartScan for_witness " << xreq.for_witness << " not_found " << not_found <<  " major xreqnum " << xreq.xreqnum << " seqnum " << xreq.seqnum << " match xreqnum " << match.xreqnum << " seqnum " << match.seqnum;

	if (xreq.for_witness && (not_found || (!xreq.xreqnum && match.xreqnum)))
	{
		// start a scan of xreqnum > 0 with any seqnum
		xreq.xreqnum = 1;
		xreq.seqnum = INT64_MIN;

		return true;
	}
	else
		return false;
}

int DbConnXreqs::MatchingSelectNextPendingMatch(Xreq& major, Xreq& minor)
{
	//boost::shared_lock<boost::shared_mutex> lock(Xreqs_db_mutex);	// sql statements must be reset before lock is released
	lock_guard<mutex> lock(Xreqs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnXreqs::DoXreqsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingSelectNextPendingMatch";

	major.Clear();
	minor.Clear();

	auto select = Xreqs_select_pending_match;

	int rc;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnXreqs::MatchingSelectNextPendingMatch simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnXreqs::MatchingSelectNextPendingMatch returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::MatchingSelectNextPendingMatch returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != 2*xreq_cols)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::MatchingSelectNextPendingMatch returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	rc = XreqsSelectInternal(select, false, minor);
	if (rc) return rc;

	return XreqsSelectInternal(select, false, major, xreq_cols);
}
