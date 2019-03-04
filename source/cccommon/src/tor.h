/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * tor.h
*/

#pragma once

void tor_start(const wstring& process_dir, const wstring& tor_exe, const wstring& tor_config, const wstring& app_data_dir, bool need_outgoing, vector<TorService*>& services, unsigned tor_control_service_index = -1);
