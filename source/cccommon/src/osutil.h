/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * osutil.h
*/

#pragma once

void set_handlers();
void finish_handlers();
void start_shutdown();
bool wait_for_shutdown(unsigned millisec = -1);
bool ccsleep(int sec);
void set_nice(int nice);
