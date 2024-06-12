/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * BlockChainStatus.hpp
*/

#pragma once

struct BlockChainStatus
{
	uint64_t last_indelible_level;
	uint64_t last_indelible_timestamp;
	uint64_t last_matching_completed_block_time;
	uint64_t last_matching_start_block_time;

	void Clear()
	{
		memset((void*)this, 0, sizeof(*this));
	}

	void Copy(const BlockChainStatus& other)
	{
		memcpy((void*)this, &other, sizeof(*this));
	}

	BlockChainStatus()
	{
		Clear();
	}
};
