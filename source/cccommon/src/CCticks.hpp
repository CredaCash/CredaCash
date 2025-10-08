/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCticks.hpp
*/

#pragma once

#include <cstdint>
#include <time.h>

// ticks are measured in milliseconds
// maximum elapsed is 2^31/1000 = 24 days

#define TX_TIME_DIVISOR					30
#define TX_TIME_OFFSET					1704067200	//@@! must be divisible by TX_TIME_DIVISOR

#define CCTICKS_PER_SEC		1000

extern std::int64_t g_clock_offset;
std::int64_t unixtime();

std::uint64_t highres_ticks();
bool report_highres_ticks(std::uint64_t dt);

std::uint32_t ccticks(clockid_t clock_id = CLOCK_MONOTONIC);
std::uint32_t ccticksnz(std::uint32_t non_zero = 1, clockid_t clock_id = CLOCK_MONOTONIC);
std::int32_t ccticks_elapsed(std::uint32_t t0, std::uint32_t t1);
void ccticks_test();