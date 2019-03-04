/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * CCticks.hpp
*/

#pragma once

#include <cstdint>
#include <time.h>

// ticks are measured in milliseconds
// maximum elapsed is 2^31/1000 = 24 days

#define CCTICKS_PER_SEC		1000

std::uint32_t ccticks(clockid_t clock_id = CLOCK_MONOTONIC);
std::int32_t ccticks_elapsed(std::uint32_t t0, std::uint32_t t1);
void ccticks_test();