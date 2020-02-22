/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * relay_request_params.h
*/

#pragma once

#include <CCobjdefs.h>

#pragma pack(push, 1)

struct relay_request_wire_params_t
{
	ccoid_t oid;
	ccoid_t prior_oid;
	uint64_t level;
	uint32_t size;
	uint8_t witness;
};

struct relay_request_params_extended_t
{
	ccoid_t oid;
	ccoid_t prior_oid;
	uint64_t level;
	uint32_t size;
	uint8_t witness;
	uint32_t announce_ticks;		// must be last, so beginning of this struct matches the above struct
};

typedef array<relay_request_params_extended_t, CC_TX_SEND_MAX> relay_request_param_buf_t;

#pragma pack(pop)
