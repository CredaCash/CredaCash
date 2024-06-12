/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * interactive.h
*/

#pragma once

class DbConn;
class TxQuery;

string command_line_to_json();

void do_interactive(DbConn *dbconn, TxQuery& txquery);

int interactive_do_json_command(const string& json, DbConn *dbconn, TxQuery& txquery);

