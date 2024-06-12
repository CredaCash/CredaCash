/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * seqnum.hpp
*/

#pragma once

#define BLOCK_SEQNUM_MAX	-7000000000000000000

#define BLOCKSEQ	0
#define TXSEQ		1
#define XREQSEQ		2	// exchange requests
#define NSEQOBJ		3

#define VALIDSEQ	0
#define RELAYSEQ	1
#define NSEQTYPE	2

class Seqnum
{
protected:
	atomic<int64_t> seqnum;

public:
	const unsigned obj, type;
	const int64_t seqmin, seqmax;

	Seqnum(unsigned i, unsigned j, int64_t min, int64_t max)
	 :	seqnum(min),
		obj(i),
		type(j),
	 	seqmin(min),
		seqmax(max)
	{ }

	int64_t Peek()
	{
		return seqnum.load();
	}

	int64_t NextNum();
};

extern Seqnum g_seqnum[NSEQOBJ][NSEQTYPE];
