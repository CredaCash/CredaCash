/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * walletutil.h
*/

#pragma once

class DbConn;

string unique_id_generate(DbConn *dbconn, const string& prefix, unsigned random_bits, unsigned checksum_chars);
