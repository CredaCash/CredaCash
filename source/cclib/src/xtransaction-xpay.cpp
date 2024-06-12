/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * xtransaction-xpay.cpp
*/

#include "cclib.h"
#include "xtransaction-xpay.hpp"
#include "xtransaction-xreq.hpp"
#include "encode.h"

#include <blake2/blake2.h>

string Xpay::DebugString() const
{
	if (type && !TypeIsXpay(type))
		return Xtx::DebugString();

	ostringstream out;

	out << "Xpay";
	//out << "type " << type;
	out << " xmatchnum " << xmatchnum;
	out << " match_timestamp " << match_timestamp;
	out << " foreign_blockchain " << foreign_blockchain;
	out << " foreign_amount " << foreign_amount;
	out << " foreign_amount_fp " << foreign_amount_fp;
	out << " foreign_confirmations_required " << foreign_confirmations_required;
	out << " payment_time " << payment_time;
	out << " foreign_block_id " << foreign_block_id;
	out << " foreign_txid " << foreign_txid;
	out << " foreign_address " << foreign_address;

	return out.str();
}

// Compute a hash of the payment identifier for a CC_TYPE_XCX_PAYMENT message
// The fields included in the hash below must be in canonical form, so there is only one unique way to refer to each payment on the foreign_blockchain
// The hash is used as a pseudo-serialnum on the CredaCash blockchain, and saved in the node persistent db
//		to ensure no payment can be claimed more than once on the CredaCash blockchain (analogous to no double spends)
void Xpay::ComputePaymentIdHash(void *hash, unsigned hashsize) const
{
	CCASSERT(type == CC_TYPE_XCX_PAYMENT);

	//if (!foreign_blockchain) cerr << "ERROR foreign_blockchain must be set from Xmatch; xpay " << hex << (uintptr_t)this << dec << " values " << DebugString() << endl;

	CCASSERT(foreign_blockchain);

	blake2s_ctx ctx;

	auto rc = blake2s_init(&ctx, hashsize, &foreign_blockchain, sizeof(foreign_blockchain));
	CCASSERTZ(rc);

	if (foreign_blockchain > XREQ_BLOCKCHAIN_BCH)
		blake2s_update(&ctx, foreign_block_id.c_str(), foreign_block_id.length()); // possibly use this for some future foreign blockchains

	blake2s_update(&ctx, foreign_txid.c_str(), foreign_txid.length());
	blake2s_update(&ctx, foreign_address.c_str(), foreign_address.length());

	blake2s_final(&ctx, hash);
}

void Xpay::DataToWire(const string& fn, void *binbuf, const uint32_t binsize, uint32_t &bufpos)
{
	const bool bhex = false;
	vector<uint8_t> encoded_sval;

	if (!TypeIsXpay(type))
		return Xtx::DataToWire(fn, binbuf, binsize, bufpos);

	// foreign_blockchain and foreign_address are not serialized; they must be set from the Xmatch
	// foreign_block_id is last because it is optional (for some blockchains, foreign_txid might be enough)

	copy_to_buf(xmatchnum, sizeof(xmatchnum), bufpos, binbuf, binsize, bhex);

	auto amount_fp = UniFloat::WireEncode(foreign_amount, -1);	// round down so claimed amount does not exceed actual amount
	copy_to_buf(amount_fp, UNIFLOAT_WIRE_BYTES, bufpos, binbuf, binsize, bhex);

	auto rc = cc_alpha_encode_best(foreign_txid.data(), foreign_txid.length(), encoded_sval);
	if (rc) throw range_error("failure encoding foreign_txid");
	unsigned encoded_size = encoded_sval.size() - 1;
	if (encoded_size > XTX_MAX_ITEM_SIZE)
		throw range_error("foreign_txid length exceeds limit");
	copy_to_buf(encoded_size, 1, bufpos, binbuf, binsize, bhex);
	copy_to_bufp(encoded_sval.data(), encoded_sval.size(), bufpos, binbuf, binsize, bhex);

	rc = cc_alpha_encode_best(foreign_block_id.data(), foreign_block_id.length(), encoded_sval);
	if (rc) throw range_error("failure encoding foreign_block_id");
	copy_to_bufp(encoded_sval.data(), encoded_sval.size(), bufpos, binbuf, binsize, bhex);
}

void Xpay::DataFromWire(const string& fn, const void *binbuf, const uint32_t binsize, uint32_t &bufpos)
{
	const bool bhex = false;
	uint64_t encoded;
	char strbuf[288];

	if (!TypeIsXpay(type))
		return Xtx::DataFromWire(fn, binbuf, binsize, bufpos);

	copy_from_buf(xmatchnum, sizeof(xmatchnum), bufpos, binbuf, binsize, bhex);

	copy_from_buf(foreign_amount_fp, UNIFLOAT_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
	foreign_amount = UniFloat::WireDecode(foreign_amount_fp);

	copy_from_buf(encoded, 1, bufpos, binbuf, binsize, bhex);
	encoded += 1;
	copy_from_buf(strbuf, encoded, bufpos, binbuf, binsize, bhex);
	auto rc = cc_alpha_decode_best(strbuf, encoded, foreign_txid);
	if (rc) throw range_error("failure decoding foreign_txid");

	encoded = binsize - bufpos;
	copy_from_buf(strbuf, encoded, bufpos, binbuf, binsize, bhex);
	if (bufpos > binsize) return;
	rc = cc_alpha_decode_best(strbuf, encoded, foreign_block_id);
	if (rc) throw range_error("failure decoding foreign_block_id");
}
