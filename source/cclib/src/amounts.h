/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * amounts.h
*/

#pragma once

#include <CCbigint.hpp>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
namespace mp = boost::multiprecision;

typedef mp::checked_int256_t amtint_t;
typedef mp::cpp_dec_float_50 amtfloat_t;	// 166 bit

const extern amtint_t amount_int_max;
const extern amtfloat_t amount_float_max;

#define ASSET_NO_SCALE	(-27)

#pragma pack(push, 1)

struct packed_unsigned_amount_t
{
	uint64_t hi;	// high word first so binary sort order is same as numeric sort order
	uint64_t lo;
};

struct packed_signed_amount_t
{
	uint64_t hi;	// high word first so binary sort order is same as numeric sort order
	uint64_t mid;
	uint64_t lo;
};

#pragma pack(pop)

#define AMOUNT_UNSIGNED_PACKED_BYTES	sizeof(packed_unsigned_amount_t)
#define AMOUNT_SIGNED_PACKED_BYTES		sizeof(packed_signed_amount_t)

void amount_from_bigint(const snarkfront::bigint_t& amount, amtint_t& val);
void amount_to_bigint(const amtint_t& amount, snarkfront::bigint_t& val);

amtfloat_t asset_scale_factor(uint64_t asset);

int amount_from_float(uint64_t asset, const amtfloat_t& val, amtint_t& amount);
int amount_from_float(uint64_t asset, const amtfloat_t& val, snarkfront::bigint_t& amount);

void amount_to_float(uint64_t asset, const amtint_t& amount,				amtfloat_t& val);
void amount_to_float(uint64_t asset, const snarkfront::bigint_t& amount,	amtfloat_t& val);

void amount_to_string(uint64_t asset, const amtint_t& amount,				string& s, bool add_decimal = false);
void amount_to_string(uint64_t asset, const snarkfront::bigint_t& amount,	string& s, bool add_decimal = false);
void amount_to_string(				  const amtfloat_t& amount,				string& s, bool add_decimal = false);

void unpack_unsigned_amount(const packed_unsigned_amount_t& packed, amtint_t& amount);
void unpack_unsigned_amount(const packed_unsigned_amount_t& packed, snarkfront::bigint_t& amount);

void unpack_unsigned_amount(const void *packed, amtint_t& amount);
void unpack_unsigned_amount(const void *packed, snarkfront::bigint_t& amount);

int pack_unsigned_amount(const amtint_t& amount,				packed_unsigned_amount_t& packed);
int pack_unsigned_amount(const snarkfront::bigint_t& amount,	packed_unsigned_amount_t& packed);

void unpack_signed_amount(const packed_signed_amount_t& packed, amtint_t& amount);
void unpack_signed_amount(const packed_signed_amount_t& packed, snarkfront::bigint_t& amount);

void unpack_signed_amount(const void *packed, amtint_t& amount);
void unpack_signed_amount(const void *packed, snarkfront::bigint_t& amount);

int pack_signed_amount(const amtint_t& amount,				packed_signed_amount_t& packed);
int pack_signed_amount(const snarkfront::bigint_t& amount,	packed_signed_amount_t& packed);
