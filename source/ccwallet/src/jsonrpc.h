/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * jsonrpc.h
*/

#pragma once

class DbConn;
class TxQuery;

int do_json_rpc(const string& json, DbConn *dbconn, TxQuery& txquery, ostringstream& response);

