/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * seqnum.cpp
*/

#include "ccnode.h"
#include "seqnum.hpp"
#include "blockchain.hpp"

/*

#define BLOCKSEQ	0
#define TXSEQ		1
#define XREQSEQ		2	// exchange requests
#define NSEQOBJ		3

#define VALIDSEQ	0
#define RELAYSEQ	1
#define NSEQTYPE	2

note: seqnum = 0 is currently reserved for the genesis block
zero is also returned when the seqnum counter overflows

*/

Seqnum g_seqnum[NSEQOBJ][NSEQTYPE] =
{	{
		{	0,	0,		INT64_MIN+1				,	BLOCK_SEQNUM_MAX	},
		{	0,	1,		INT64_MIN+1				,	BLOCK_SEQNUM_MAX	}	},
	{

		{	1,	0,		BLOCK_SEQNUM_MAX + 1	,	-1				},
		{	1,	1,		BLOCK_SEQNUM_MAX + 1	,	-1				}	},
	{
		{	2,	0,		1						,	INT64_MAX-1		},
		{	2,	1,		1						,	INT64_MAX-1		}	}
};

int64_t Seqnum::NextNum()
{
	//for (unsigned i = 0; i < NSEQOBJ; ++i)
	//	for (unsigned j = 0; j < NSEQTYPE; ++j)
	//		cerr << "g_seqnum[" << i << "][" << j << "] min " << g_seqnum[i][j].seqmin << " max " << g_seqnum[i][j].seqmax << " next " << g_seqnum[i][j].Peek() << endl;

	auto next = seqnum.fetch_add(1);

	//cerr << "g_seqnum[" << obj << "][" << type << "] min " << seqmin << " max " << seqmax << " next " << next << endl;

	if (next - seqmax > 0)
	{
		string msg = "Sequence counter [" + to_string(obj) + ","  + to_string(type) + "] overflow error. The server must be restarted to continue.";

		g_blockchain.SetFatalError(msg.c_str());

		return 0;
	}

	return next;
}
