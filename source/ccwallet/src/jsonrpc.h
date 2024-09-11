/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * jsonrpc.h
*/

#pragma once

#define XCX_REQ_MIN			0.1
#define XCX_REQ_MAX			1000000	// max billet = 2^35/1000 = 3,435,973

extern volatile bool g_disable_malloc_logging;

class DbConn;
class TxQuery;

int do_json_rpc(const string& json, DbConn *dbconn, TxQuery& txquery, ostringstream& response);
