/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * ed25519-randombytes-custom.c
*/

#include <ed25519-randombytes-custom.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void ed25519_randombytes_unsafe(void *p, size_t len)
{
	const char *msg = "ERROR: ed25519_randombytes_unsafe called\n";

	printf(msg);
	fprintf(stderr, msg);

	memset(p, 0, len);

	int i;

	for (i = 0; i < len; ++i)
		*((unsigned char *)p + i) = rand();
}
