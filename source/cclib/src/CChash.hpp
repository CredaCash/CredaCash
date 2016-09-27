/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CChash.hpp
*/

#pragma once

#include "zkhash.hpp"

typedef Hasher::inteval::HashInput<bigint_t> CCHashInput;
typedef Hasher::inteval::Hasher<bigint_t> CCHash;
