/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * map_values.h
*/

#pragma once

struct exp_map_t
{
	// input params:
	uint32_t offset;
	uint32_t step;
	uint32_t enc_max;
	uint32_t dec_max;

	// computed params:
	double	 base;
	uint32_t unity_index;
};

void map_exp_init_params(exp_map_t& params, unsigned offset, unsigned step, unsigned enc_max, unsigned dec_max, unsigned unity_index);

unsigned map_exp_decode(const exp_map_t& params, unsigned enc);

unsigned map_exp_encode(const exp_map_t& params, unsigned val, bool roundup = true);

void map_exp_test();
void map_exp_test_check(const exp_map_t& params);
