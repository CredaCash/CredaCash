/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * transaction.cpp
*/

#include "cclib.h"

#include <jsoncpp/json/json.h>
#include <blake2/blake2.h>
#include <siphash/siphash.h>
#include <SpinLock.hpp>

#include "transaction.hpp"
#include "transaction-json.hpp"
#include "transaction.h"
#include "xtransaction.hpp"
#include "jsoninternal.h"
#include "jsonutil.h"
#include "CChash.hpp"
#include "CCparams.h"

#include <CCobjects.hpp>

//#define TEST_SHOW_WIRE_ERRORS	1	// for debugging

#ifndef TEST_SHOW_WIRE_ERRORS
#define TEST_SHOW_WIRE_ERRORS	0	// don't debug
#endif

#define TRACE_COMMITMENTS		0

// !!! TODO: make more consistent function names

static const uint8_t zero_pow[TX_POW_SIZE] = {};

static CCRESULT get_tx_ptr(const string& fn, Json::Value& root, TxPay*& ptx, char *output, const uint32_t outsize)
{
	// note, output can be NULL

	ptx = NULL;

	static TxPay stx;	// !!! for now

	ptx = &stx;

	return 0;
}

static void _tx_init_output(const TxPay& tx, TxOut& txout)
{

}

static void _tx_init_input(const TxPay& tx, TxIn& txin)
{

}

void tx_init(TxPay& tx)
{
	tx.Clear();

	tx.struct_tag = CC_TAG_TX_STRUCT;	// tag the structure

	for (unsigned i = 0; i < TX_MAXOUT; ++i)
		_tx_init_output(tx, tx.outputs[i]);

	for (unsigned i = 0; i < TX_MAXIN; ++i)
		_tx_init_input(tx, tx.inputs[i]);
}

static unsigned amount_mantissa_bits(bool is_donation = false)
{
	if (is_donation)
		return TX_DONATION_BITS - TX_AMOUNT_EXPONENT_BITS;
	else
		return TX_AMOUNT_BITS - TX_AMOUNT_EXPONENT_BITS;
}

/*
Normal rounding for amounts:
	value < min --> round up to min
	value > max --> round down to max
	precision > range --> round up to range (optionally round down or round to closest)

Normal rounding for donations:
	value < min --> round up to min
	value > max --> return error (optionally round down)
	precision > range --> round up to range

Implementation:
	rounding parameter, unsigned (-1 maps to >=2)
		 0 --> rounds to closest as described above
		 1 --> default: implements the normal rounding described above
		-1 --> = 3: implements the optional rounding down described above
		-2 --> = 2: any requirement to round returns an error (primarily used for testing)
*/

static bigint_t amount_factors[TX_AMOUNT_EXPONENT_MASK + 1];
static uint16_t amount_factors_limbs[TX_AMOUNT_EXPONENT_MASK + 1];
static bigint_t amount_maxs[4][TX_AMOUNT_EXPONENT_MASK + 1];

const bool TRACE_TX_AMOUNT_ENCODE = 0;

void tx_get_amount_factor(bigint_t& factor, unsigned exponent)
{
	CCASSERT(exponent <= TX_AMOUNT_EXPONENT_MASK);

	factor = amount_factors[exponent];
}

void tx_amount_factors_init()
{
	static FastSpinLock init_lock(__FILE__, __LINE__);

	static bool binited = false;

	if (binited)
		return;

	lock_guard<FastSpinLock> lock(init_lock);

	if (binited)
		return;

	binited = true;

	for (unsigned i = 0; i <= TX_AMOUNT_EXPONENT_MASK; ++i)
	{
		amount_factors[i] = (i ? bigint_t(10UL) * amount_factors[i-1] : bigint_t(1UL));

		amount_factors_limbs[i] = 1;
		for (int j = 1; j < amount_factors[i].numberLimbs() && BIGWORD(amount_factors[i], j); ++j)
			++amount_factors_limbs[i];

		// 2 = donation round up (always)
		amount_maxs[2][i] = bigint_t(((uint64_t)1 << amount_mantissa_bits(1)) - !i) * amount_factors[i];

		// 1 = amount round up
		amount_maxs[1][i] = bigint_t(((uint64_t)1 << amount_mantissa_bits(0)) - !i) * amount_factors[i];

		// 0 = amount round to closest
		bigint_t half = amount_factors[i];
		bigint_shift_down(half, 1);
		amount_maxs[0][i] = amount_maxs[1][i] + half - bigint_t((uint64_t)(i > 0));

		// 3 = amount round down
		amount_maxs[3][i] = amount_maxs[1][i] + (i ? amount_factors[i] - bigint_t(1UL) : bigint_t(0UL));
	}

	if (!TRACE_TX_AMOUNT_ENCODE)
		return;

	for (unsigned i = 0; i <= TX_AMOUNT_EXPONENT_MASK; ++i)
		cerr << "amount_factors[" << i << "] = " << amount_factors[i] << endl;
	for (unsigned i = 0; i <= TX_AMOUNT_EXPONENT_MASK; ++i)
		cerr << "amount_factors_limbs[" << i << "] = " << amount_factors_limbs[i] << endl;
	for (unsigned j = 0; j <= 3; ++j)
		for (unsigned i = 0; i <= TX_AMOUNT_EXPONENT_MASK; ++i)
			cerr << "amount_maxs["<<j<<"]["<<i<<"] = " << amount_maxs[j][i] << endl;
}

unsigned tx_amount_decode_exponent(uint64_t amount, unsigned exponent_bits)
{
	CCASSERT(exponent_bits == TX_AMOUNT_EXPONENT_BITS);

	return amount & TX_AMOUNT_EXPONENT_MASK;
}

void tx_amount_decode(uint64_t amount, bigint_t& result, bool is_donation, unsigned amount_bits, unsigned exponent_bits)
{
	tx_amount_factors_init();

	CCASSERT(amount_bits == (is_donation ? TX_DONATION_BITS : TX_AMOUNT_BITS));
	CCASSERT(exponent_bits == TX_AMOUNT_EXPONENT_BITS);

	// result = (mantissa + (exponent > 0)) * 10^exponent

	unsigned exponent = amount & TX_AMOUNT_EXPONENT_MASK;
	result = (amount >> TX_AMOUNT_EXPONENT_BITS) + (exponent > 0);
	result = result * amount_factors[exponent];
}

uint64_t tx_amount_encode(const bigint_t& amount, bool is_donation, unsigned amount_bits, unsigned exponent_bits, unsigned min_exponent, unsigned max_exponent, unsigned rounding)
{
	tx_amount_factors_init();

	CCASSERT(amount_bits == (is_donation ? TX_DONATION_BITS : TX_AMOUNT_BITS));
	CCASSERT(exponent_bits == TX_AMOUNT_EXPONENT_BITS);

	if (min_exponent > TX_AMOUNT_EXPONENT_MASK)
		min_exponent = TX_AMOUNT_EXPONENT_MASK;

	if (max_exponent > TX_AMOUNT_EXPONENT_MASK)
		max_exponent = TX_AMOUNT_EXPONENT_MASK;

	if (max_exponent < min_exponent)
		max_exponent = min_exponent;

	rounding &= 3;
	bool no_rounding = (rounding == 2);
	unsigned round_index = (rounding < 2 ? rounding : 3);
	if (is_donation) round_index = 2;

	if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode amount " << amount << " rounding " << rounding << " no_rounding " << no_rounding << " round_index " << round_index << endl;

	if (!amount)
	{
		if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode !amount --> returning 0" << endl;

		return 0;
	}

	int exponent;
	for (exponent = max_exponent; exponent >= (int)min_exponent; --exponent)
	{
		// note: a binary search would be slightly faster on average than scanning a 32 element table

		if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode checking if " << amount << " > " << amount_maxs[round_index][exponent] << endl;

		if (amount > amount_maxs[round_index][exponent])
		{
			++exponent;

			break;
		}
	}

	if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode min_exponent " << min_exponent << " max_exponent " << max_exponent << " exponent " << exponent << endl;

	if (exponent < (int)min_exponent)
	{
		if (!(amount > amount_maxs[round_index][min_exponent]))
		{
			exponent = min_exponent;

			if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode setting exponent = min_exponent = " << exponent << endl;
		}
		else if (no_rounding)
		{
			if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode exponent < min_exponent && no_rounding --> returning -1" << endl;

			return -1;
		}
		else
		{
			uint64_t rv = min_exponent;

			if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode exponent < min_exponent --> returning min_exponent = " << rv << endl;

			return rv;
		}
	}

	bigint_t quotient = 0UL;
	uint64_t mantissa;

	if (exponent <= (int)max_exponent)
	{
		// Divide num by den, put quotient at quot and remainder at rem (which may equal num).
		// mpn_tdiv_qr(mp_limb_t *quot, mp_limb_t *rem, 0, const mp_limb_t *num, mp_size_t n_num, const mp_limb_t *den, mp_size_t n_den)
		bigint_t rem = 0UL;
		mpn_tdiv_qr(BIGDATA(quotient), BIGDATA(rem), 0, BIGDATA(amount), amount.numberLimbs(), BIGDATA(amount_factors[exponent]), amount_factors_limbs[exponent]);

		if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode computing amount / " << amount_factors[exponent] << " --> result = " << mantissa << ", remainder = " << rem << endl;

		mantissa = BIG64(quotient);

		if (rem)
		{
			//if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode have mantissa remainder " << rem << " no_rounding " << no_rounding << " and round_index " << round_index << endl;

			if (no_rounding)
			{
				if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode have mantissa remainder && no_rounding --> returning -1" << endl;

				return -1;
			}
			else if (is_donation || rounding <= 1)
			{
				if (!is_donation && rounding == 0)
				{
					bigint_shift_up(rem, 1);

					if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode have mantissa remainder doubled --> " << rem << "; compare to " << amount_factors[exponent] << endl;
				}

				if (is_donation || rounding == 1 || rem >= amount_factors[exponent])
				{
					++mantissa;

					if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode have mantissa remainder --> rounding mantissa up to " << mantissa << endl;
				}
			}
		}
	}

	if (exponent > (int)max_exponent)
	{
		if (no_rounding || (is_donation && rounding != 3))
		{
			if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode exponent > max_exponent && no_rounding --> returning -1" << endl;

			return -1;
		}
		else
		{
			uint64_t rv = ((((uint64_t)1 << (amount_bits - exponent_bits)) - 1) << TX_AMOUNT_EXPONENT_BITS) | max_exponent;

			if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode exponent > max_exponent --> returning " << rv << endl;

			return rv;
		}
	}

	if (!mantissa)
	{
		if (no_rounding)
		{
			if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode !mantissa && no_rounding --> returning -1" << endl;

			return -1;
		}
		else
		{
			if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode !mantissa --> returning 0" << endl;

			return 0;
		}
	}

	// minimize mantissa

	while (exponent < (int)(max_exponent))
	{
		auto div = mantissa / 10;
		if (div * 10 != mantissa)
			break;

		mantissa = div;
		++exponent;
	}

	if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode minimized --> mantissa = " << mantissa << ", exponent = " << exponent << endl;

	uint64_t rv = ((mantissa - (exponent > 0)) << TX_AMOUNT_EXPONENT_BITS) | exponent;

	if (TRACE_TX_AMOUNT_ENCODE) cerr << "tx_amount_encode returning " << rv << endl;

	return rv;
}

uint64_t tx_amount_max(unsigned amount_bits, unsigned exponent_bits, unsigned max_exponent)
{
	if (!max_exponent || max_exponent > TX_AMOUNT_EXPONENT_MASK)
		max_exponent = TX_AMOUNT_EXPONENT_MASK;

	return ((((uint64_t)1 << amount_mantissa_bits()) - 1) << TX_AMOUNT_EXPONENT_BITS) | max_exponent;
}

void tx_amount_max(bigint_t& result, unsigned amount_bits, unsigned exponent_bits, unsigned max_exponent)
{
	auto amount = tx_amount_max(amount_bits, exponent_bits, max_exponent);

	//cerr << "tx_amount_max " << amount_bits << " " << exponent_bits << " " << max_exponent << " 0x" << hex << amount << dec << " " << (amount >> TX_AMOUNT_EXPONENT_BITS) << endl;

	tx_amount_decode(amount, result, false, amount_bits, exponent_bits);
}

static CCRESULT parse_amount_common(const string& fn, Json::Value& root, bool& is_donation, unsigned& amount_bits, unsigned& exponent_bits, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;
	bigint_t amount, bigval;

	is_donation = false;

	key = "is-donation";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		is_donation = BIG64(bigval);
	}

	if (is_donation)
		key = "donation-bits";
	else
		key = "amount-bits";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), 8, 0UL, bigval, output, outsize);
	if (rc) return rc;
	amount_bits = BIG64(bigval);

	if (amount_bits != (is_donation ? TX_DONATION_BITS : TX_AMOUNT_BITS))
		return error_invalid_value(fn, key, output, outsize);

	key = "exponent-bits";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), 8, 0UL, bigval, output, outsize);
	if (rc) return rc;
	exponent_bits = BIG64(bigval);

	if (exponent_bits != TX_AMOUNT_EXPONENT_BITS)
		return error_invalid_value(fn, key, output, outsize);

	return 0;
}

CCRESULT encode_amount_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;

	bigint_t amount, bigval;
	bool is_donation = false;
	unsigned amount_bits;
	unsigned exponent_bits;
	unsigned min_exponent = 0;
	unsigned max_exponent = -1;
	unsigned rounding = -1;

	auto rc = parse_amount_common(fn, root, is_donation, amount_bits, exponent_bits, output, outsize);
	if (rc) return rc;

	key = "minimum-exponent";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), sizeof(min_exponent) * CHAR_BIT, 0UL, bigval, output, outsize);
		if (rc) return rc;
		min_exponent = BIG64(bigval);
	}

	key = "maximum-exponent";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), sizeof(max_exponent) * CHAR_BIT, 0UL, bigval, output, outsize);
		if (rc) return rc;
		max_exponent = BIG64(bigval);
	}

	key = "rounding";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), sizeof(rounding) * CHAR_BIT, 0UL, bigval, output, outsize);
		if (rc) return rc;
		rounding = BIG64(bigval);
	}

	if (max_exponent < min_exponent)
		return error_invalid_value(fn, key, output, outsize);

	key = "amount";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_INPUT_BITS, 0UL, amount, output, outsize);
	if (rc) return rc;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	auto result = tx_amount_encode(amount, is_donation, amount_bits, exponent_bits, min_exponent, max_exponent, rounding);
	if (result == (unsigned)(-1))
		return error_invalid_value(fn, key, output, outsize);

	ostringstream os;
	os << hex;

	os << "{\"amount-encoded\":\"0x" << result << "\"";
	os << "}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, outsize);
}

CCRESULT decode_amount_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;

	uint64_t amount;
	bool is_donation = false;
	unsigned amount_bits;
	unsigned exponent_bits;
	bigint_t bigval;

	auto rc = parse_amount_common(fn, root, is_donation, amount_bits, exponent_bits, output, outsize);
	if (rc) return rc;

	key = "amount-encoded";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), (is_donation ? TX_DONATION_BITS : TX_AMOUNT_BITS), 0UL, bigval, output, outsize);
	if (rc) return rc;
	amount = BIG64(bigval);

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	tx_amount_decode(amount, bigval, is_donation, amount_bits, exponent_bits);

	ostringstream os;
	os << hex;

	os << "{\"amount\":\"0x" << bigval << "\"";
	os << "}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, outsize);
}

void tx_dump_address_params_stream(ostream& os, const AddressParams& params, const char *prefix)
{
	os << hex;

	os << prefix << "params.__dest_id "		<< params.__dest_id << endl;
	os << prefix << "params.__flags "		<< params.__flags << endl;

	os << prefix << "params.__dest "		<< params.__dest << endl;
	os << prefix << "params.dest_chain "	<< params.dest_chain << endl;
	os << prefix << "params.__paynum "		<< params.__paynum << endl;

	os << dec;
}

void txout_dump_stream(ostream& os, const TxOut& txout, const char *prefix)
{
	tx_dump_address_params_stream(os, txout.addrparams, prefix);

	os << hex;

	os << prefix << "no_address "			<< txout.no_address << endl;
	os << prefix << "M_address "			<< txout.M_address << endl;
	os << prefix << "acceptance_required "	<< txout.acceptance_required << endl;
	os << prefix << "repeat_count "			<< txout.repeat_count << endl;

	os << prefix << "M_domain "				<< txout.M_domain << endl;
	os << prefix << "__asset "				<< txout.__asset << endl;
	os << prefix << "no_asset "				<< txout.no_asset << endl;
	os << prefix << "asset_mask "			<< txout.asset_mask << endl;
	os << prefix << "__asset_pad "			<< txout.__asset_pad << endl;
	os << prefix << "M_asset_enc "			<< txout.M_asset_enc << endl;

	bigint_t bigval;
	tx_amount_decode(txout.__amount_fp, bigval, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS);
	os << prefix << "__amount_fp "			<< txout.__amount_fp << " (" << dec << bigval << hex << ")" << endl;
	os << prefix << "no_amount "			<< txout.no_amount << endl;
	os << prefix << "amount_mask "			<< txout.amount_mask << endl;
	os << prefix << "__amount_pad "			<< txout.__amount_pad << endl;
	os << prefix << "M_amount_enc "			<< txout.M_amount_enc << endl;

	//os << prefix << "M_commitment_iv "		<< txout.M_commitment_iv << endl;
	os << prefix << "M_commitment "			<< txout.M_commitment << endl;

	os << dec;
}

void tx_dump_spend_secret_params_stream(ostream& os, const SpendSecretParams& params, const char *prefix)
{
	os << hex;

	os << prefix << "params.____nsecrets "	<< params.____nsecrets << endl;
	os << prefix << "params.____nraddrs "	<< params.____nraddrs << endl;
	os << prefix << "params.____enforce_spendspec_with_spend_secrets "	<< params.____enforce_spendspec_with_spend_secrets << endl;
	os << prefix << "params.____enforce_spendspec_with_trust_secrets "	<< params.____enforce_spendspec_with_trust_secrets << endl;
	os << prefix << "params.____required_spendspec_hash "	<< params.____required_spendspec_hash << endl;
	os << prefix << "params.____allow_master_secret "		<< params.____allow_master_secret << endl;
	os << prefix << "params.____allow_freeze "				<< params.____allow_freeze << endl;
	os << prefix << "params.____allow_trust_unfreeze "		<< params.____allow_trust_unfreeze << endl;
	os << prefix << "params.____require_public_hashkey "	<< params.____require_public_hashkey << endl;
	os << prefix << "params.____restrict_addresses "		<< params.____restrict_addresses << endl;
	os << prefix << "params.____spend_locktime "			<< params.____spend_locktime << endl;
	os << prefix << "params.____trust_locktime "			<< params.____trust_locktime << endl;
	os << prefix << "params.____spend_delaytime "			<< params.____spend_delaytime << endl;
	os << prefix << "params.____trust_delaytime "			<< params.____trust_delaytime << endl;
	for (unsigned i = 0; i < TX_MAX_SECRETS; ++i)
	{
	os << prefix << "secrets.____use_spend_secret[" << i << "] "	<< params.____use_spend_secret[i] << endl;
	os << prefix << "secrets.____use_trust_secret[" << i << "] "	<< params.____use_trust_secret[i] << endl;
	}
	os << prefix << "params.____required_spend_secrets "	<< params.____required_spend_secrets << endl;
	os << prefix << "params.____required_trust_secrets "	<< params.____required_trust_secrets << endl;
	os << prefix << "params.____destnum "					<< params.____destnum << endl;
	os << prefix << "params.__acceptance_required "			<< params.__acceptance_required << endl;
	os << prefix << "params.__static_address "				<< params.__static_address << endl;

	tx_dump_address_params_stream(os, params.addrparams, prefix);

	os << dec;
}

void tx_dump_spend_secret_stream(ostream& os, const SpendSecret& secret, unsigned index, const char *prefix)
{
	os << hex;

	os << prefix << "secrets.____spend_secret_number[" << index << "] "	<< secret.____spend_secret_number << endl;

	os << prefix << "secrets.____have_master_secret[" << index << "] "	<< secret.____have_master_secret << endl;
	os << prefix << "secrets.____have_root_secret[" << index << "] "	<< secret.____have_root_secret << endl;
	os << prefix << "secrets.____have_spend_secret[" << index << "] "	<< secret.____have_spend_secret << endl;
	os << prefix << "secrets.____have_trust_secret[" << index << "] "	<< secret.____have_trust_secret << endl;
	os << prefix << "secrets.____have_monitor_secret[" << index << "] "	<< secret.____have_monitor_secret << endl;
	os << prefix << "secrets.____have_restricted_address[" << index << "] "	<< secret.____have_restricted_address << endl;
	os << prefix << "secrets.____have_receive_secret[" << index << "] "	<< secret.____have_receive_secret << endl;

	os << prefix << "secrets.____master_secret[" << index << "] "		<< secret.____master_secret << endl;
	os << prefix << "secrets.____root_secret[" << index << "] "			<< secret.____root_secret << endl;
	os << prefix << "secrets.____spend_secret[" << index << "] "		<< secret.____spend_secret << endl;
	os << prefix << "secrets.____trust_secret[" << index << "] "		<< secret.____trust_secret << endl;
	os << prefix << "secrets.____monitor_secret[" << index << "] "		<< secret.____monitor_secret << endl;
	os << prefix << "secrets.____receive_secret[" << index << "] "		<< secret.____receive_secret << endl;

	os << dec;
}

void txin_dump_stream(ostream& os, const TxIn& txin, const TxInPath *path, const char *prefix)
{
	os << hex;

	os << prefix << "enforce_master_secret "	<< txin.enforce_master_secret << endl;
	os << prefix << "enforce_spend_secrets "	<< txin.enforce_spend_secrets << endl;
	os << prefix << "enforce_trust_secrets "	<< txin.enforce_trust_secrets << endl;
	os << prefix << "enforce_freeze "			<< txin.enforce_freeze << endl;
	os << prefix << "enforce_unfreeze "			<< txin.enforce_unfreeze << endl;

	tx_dump_spend_secret_params_stream(os, txin.params, prefix);

	for (unsigned j = 0; j < TX_MAX_SECRET_SLOTS; ++j)
		tx_dump_spend_secret_stream(os, txin.secrets[j], j, prefix);

	os << hex;

	os << prefix << "____have_master_secret_valid "	<< txin.____have_master_secret_valid << endl;
	os << prefix << "____have_spend_secrets_valid "	<< txin.____have_spend_secrets_valid << endl;
	os << prefix << "____have_trust_secrets_valid "	<< txin.____have_trust_secrets_valid << endl;
	os << prefix << "____master_secret_valid "		<< txin.____master_secret_valid << endl;
	os << prefix << "____spend_secrets_valid "		<< txin.____spend_secrets_valid << endl;
	os << prefix << "____trust_secrets_valid "		<< txin.____trust_secrets_valid << endl;

	os << prefix << "merkle_root "			<< txin.merkle_root << endl;
	os << prefix << "invalmax "				<< txin.invalmax << endl;
	os << prefix << "delaytime "				<< txin.delaytime << endl;

	os << prefix << "M_domain "				<< txin.M_domain << endl;
	os << prefix << "__asset "				<< txin.__asset << endl;
	bigint_t bigval;
	tx_amount_decode(txin.__amount_fp, bigval, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS);
	os << prefix << "__amount_fp "			<< txin.__amount_fp << " (" << dec << bigval << hex << ")" << endl;
	os << prefix << "__M_commitment_iv "	<< txin.__M_commitment_iv << endl;
	//os << prefix << "__M_commitment_index "	<< txin.__M_commitment_index << endl;
	os << prefix << "_M_commitment "		<< txin._M_commitment << endl;
	os << prefix << "_M_commitnum "			<< txin._M_commitnum << endl;

	os << prefix << "no_serialnum "			<< txin.no_serialnum << endl;
	os << prefix << "S_serialnum "			<< txin.S_serialnum << endl;
	os << prefix << "S_hashkey "			<< txin.S_hashkey << endl;
	os << prefix << "S_spendspec_hashed "	<< txin.S_spendspec_hashed << endl;

	os << prefix << "pathnum " << txin.pathnum << endl;
	if (path)
	{
		for (unsigned i = 0; i < TX_MERKLE_DEPTH; ++i)
			os << prefix << "__M_merkle_path[" << i << "] " << path->__M_merkle_path[i] << endl;
	}

	os << dec;
}

void tx_dump_stream(ostream& os, const TxPay& tx, const char *prefix)
{
	os << hex;

	os << prefix << "----Transaction Dump----" << endl;

	os << prefix << "zero "							<< tx.zero << endl;
	os << prefix << "struct_tag "					<< tx.struct_tag << endl;
	os << prefix << "no_precheck "					<< tx.no_precheck << endl;
	os << prefix << "no_proof "						<< tx.no_proof << endl;
	os << prefix << "no_verify "					<< tx.no_verify << endl;
	os << prefix << "test_uselargerzkkey "			<< tx.test_uselargerzkkey << endl;
	os << prefix << "test_make_bad "				<< tx.test_make_bad << endl;
	os << prefix << "random_seed "					<< tx.random_seed << endl;

	os << prefix << "have_dest_chain__ "			<< tx.have_dest_chain__ << endl;
	os << prefix << "dest_chain__ "					<< tx.dest_chain__ << endl;
	os << prefix << "have_default_domain__ "			<< tx.have_default_domain__ << endl;
	os << prefix << "default_domain__ "				<< tx.default_domain__ << endl;
	os << prefix << "have_acceptance_required__ "	<< tx.have_acceptance_required__ << endl;
	os << prefix << "acceptance_required__ "		<< tx.acceptance_required__ << endl;
	os << prefix << "have_invalmax__ "				<< tx.have_invalmax__ << endl;
	os << prefix << "invalmax__ "					<< tx.invalmax__ << endl;
	os << prefix << "have_delaytime__ "				<< tx.have_delaytime__ << endl;
	os << prefix << "delaytime__ "					<< tx.delaytime__ << endl;

	os << prefix << "wire_tag "						<< tx.wire_tag << endl;
	os << prefix << "tag_type "						<< tx.tag_type << endl;
	os << prefix << "zkkeyid "						<< tx.zkkeyid << endl;
	for (unsigned i = 0; i < tx.zkproof.size(); ++i)
		os << prefix << "zkproof[" << i << "] "		<< tx.zkproof[i] << endl;

	os << prefix << "amount_carry_in "				<< tx.amount_carry_in << endl;
	os << prefix << "amount_carry_out "				<< tx.amount_carry_out << endl;

	os << prefix << "append_wire_offset "			<< tx.append_wire_offset << endl;
	os << prefix << "append_data_length "			<< tx.append_data_length << endl;
	os << prefix << "append_data "					<< buf2hex(tx.append_data.data(), (tx.append_data_length < 16 ? tx.append_data_length : 16)) << endl;

	os << prefix << "have_objid "					<< tx.have_objid << endl;
	os << prefix << "objid "						<< buf2hex(&tx.objid, CC_OID_TRACE_SIZE) << endl;

	os << prefix << "tx_type "						<< tx.tx_type << endl;
	os << prefix << "source_chain "					<< tx.source_chain << endl;
	os << prefix << "param_level "					<< tx.param_level << endl;
	os << prefix << "param_time "					<< tx.param_time << endl;
	os << prefix << "revision "						<< tx.revision << endl;
	os << prefix << "expiration "					<< tx.expiration << endl;
	os << prefix << "refhash "						<< tx.refhash << endl;
	os << prefix << "reserved "						<< tx.reserved << endl;
	bigint_t bigval;
	tx_amount_decode(tx.donation_fp, bigval, true, TX_DONATION_BITS, TX_AMOUNT_EXPONENT_BITS);
	os << prefix << "donation_fp "					<< tx.donation_fp << " (" << dec << bigval << hex << ")" << endl;
	os << prefix << "outvalmin "					<< tx.outvalmin << endl;
	os << prefix << "outvalmax "					<< tx.outvalmax << endl;
	os << prefix << "have_allow_restricted_addresses__ " << tx.have_allow_restricted_addresses__ << endl;
	os << prefix << "allow_restricted_addresses "	<< tx.allow_restricted_addresses << endl;
	os << prefix << "tx_merkle_root "				<< tx.tx_merkle_root << endl;
	//os << prefix << "M_commitment_iv_nonce "		<< tx.M_commitment_iv_nonce << endl;
	os << prefix << "override_commitment_iv__ "		<< tx.override_commitment_iv__ << endl;
	os << prefix << "M_commitment_iv "				<< tx.M_commitment_iv << endl;

	os << prefix << "____nsecrets "					<< tx.____nsecrets << endl;
	os << prefix << "____nraddrs "					<< tx.____nraddrs << endl;
	os << prefix << "__nassets "					<< tx.__nassets << endl;
	os << prefix << "__asset_list";
	for (unsigned i = 0; i < TX_MAX_NASSETS; ++i)
		os << prefix << " " << tx.__asset_list[i];
	os << endl;

	os << prefix << "nout "							<< tx.nout << endl;
	os << prefix << "nin "							<< tx.nin << endl;
	os << prefix << "nin_with_path "				<< tx.nin_with_path << endl;

	for (unsigned i = 0; i < tx.nout && i < TX_MAXOUT; ++i)
	{
		os << prefix << "<< output[" << i << "]:" << endl;
		txout_dump_stream(os, tx.outputs[i], prefix);
	}

	for (unsigned i = 0; i < tx.nin && i < TX_MAXIN; ++i)
	{
		os << prefix << ">> input[" << i << "]:" << endl;
		unsigned pathnum = tx.inputs[i].pathnum;
		if (pathnum)
			txin_dump_stream(os, tx.inputs[i], &tx.inpaths[pathnum - 1], prefix);
		else
			txin_dump_stream(os, tx.inputs[i], NULL, prefix);
	}

	os << prefix << "----End of Transaction Dump----" << endl;

	os << dec;

	//return 0;
}

CCRESULT tx_dump(const TxPay& tx, char *output, const uint32_t outsize, const char *prefix)
{
	ostringstream os;

	tx_dump_stream(os, tx, prefix);

	return copy_result_to_output("Transaction Dump", os.str(), output, outsize);
}

CCRESULT json_tx_dump(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, root, ptx, output, outsize);
	if (rc) return rc;
	CCASSERT(ptx);

	return tx_dump(*ptx, output, outsize);
}

static void txpay_output_to_json(const string& fn, const TxOut& txout, ostream& os)
{
	os << "{\"destination\":\"0x"			<< txout.addrparams.__dest << "\"" JSON_ENDL
	os << ",\"destination-chain\":\"0x"		<< txout.addrparams.dest_chain << "\"" JSON_ENDL
	os << ",\"payment-number\":\"0x"		<< txout.addrparams.__paynum << "\"" JSON_ENDL
	os << ",\"no-address\":\"0x"			<< txout.no_address << "\"" JSON_ENDL
	os << ",\"address\":\"0x"				<< txout.M_address << "\"" JSON_ENDL
	os << ",\"acceptance-required\":\"0x"	<< txout.acceptance_required << "\"" JSON_ENDL
	os << ",\"repeat-count\":\"0x"			<< txout.repeat_count << "\"" JSON_ENDL

	os << ",\"domain\":\"0x"					<< txout.M_domain << "\"" JSON_ENDL
	os << ",\"asset\":\"0x"					<< txout.__asset << "\"" JSON_ENDL
	os << ",\"no-asset\":\"0x"				<< txout.no_asset << "\"" JSON_ENDL
	os << ",\"asset-mask\":\"0x"			<< txout.asset_mask << "\"" JSON_ENDL
	os << ",\"encrypted-asset\":\"0x"		<< txout.M_asset_enc << "\"" JSON_ENDL

	os << ",\"amount\":\"0x"				<< txout.__amount_fp << "\"" JSON_ENDL
	os << ",\"no-amount\":\"0x"				<< txout.no_amount << "\"" JSON_ENDL
	os << ",\"amount-mask\":\"0x"			<< txout.amount_mask << "\"" JSON_ENDL
	os << ",\"encrypted-amount\":\"0x"		<< txout.M_amount_enc << "\"" JSON_ENDL

	//os << ",\"output-commitment-iv\":\"0x"	<< txout.M_commitment_iv << "\"" JSON_ENDL
	os << ",\"commitment\":\"0x"			<< txout.M_commitment << "\"" JSON_ENDL
	os << "}" JSON_ENDL
}

static void txpay_input_to_json(const string& fn, const TxIn& txin, const TxInPath *path, ostream& os)
{
	os << "{\"enforce-master-secret\":\"0x"	<< txin.enforce_master_secret << "\"" JSON_ENDL
	os << ",\"enforce-spend-secrets\":\"0x"	<< txin.enforce_spend_secrets << "\"" JSON_ENDL
	os << ",\"enforce-trust-secrets\":\"0x"	<< txin.enforce_trust_secrets << "\"" JSON_ENDL
	os << ",\"enforce-freeze\":\"0x"		<< txin.enforce_freeze << "\"" JSON_ENDL
	os << ",\"enforce-unfreeze\":\"0x"		<< txin.enforce_unfreeze << "\"" JSON_ENDL

	os << ",\"enforce-spendspec-with-spend-secrets\":\"0x"	<< txin.params.____enforce_spendspec_with_spend_secrets << "\"" JSON_ENDL
	os << ",\"enforce-spendspec-with-trust-secrets\":\"0x"	<< txin.params.____enforce_spendspec_with_trust_secrets << "\"" JSON_ENDL
	os << ",\"required-spendspec-hash\":\"0x"	<< txin.params.____required_spendspec_hash << "\"" JSON_ENDL
	os << ",\"allow-master-secret\":\"0x"		<< txin.params.____allow_master_secret << "\"" JSON_ENDL
	os << ",\"allow-freeze\":\"0x"				<< txin.params.____allow_freeze << "\"" JSON_ENDL
	os << ",\"allow-trust-unfreeze\":\"0x"		<< txin.params.____allow_trust_unfreeze << "\"" JSON_ENDL
	os << ",\"require-public-hashkey\":\"0x"	<< txin.params.____require_public_hashkey << "\"" JSON_ENDL
	os << ",\"restrict-addresses\":\"0x"		<< txin.params.____restrict_addresses << "\"" JSON_ENDL
	os << ",\"spend-locktime\":\"0x"			<< txin.params.____spend_locktime << "\"" JSON_ENDL
	os << ",\"trust-locktime\":\"0x"			<< txin.params.____trust_locktime << "\"" JSON_ENDL
	os << ",\"spend-delaytime\":\"0x"			<< txin.params.____spend_delaytime << "\"" JSON_ENDL
	os << ",\"trust-delaytime\":\"0x"			<< txin.params.____trust_delaytime << "\"" JSON_ENDL

	os << ",\"required-spend-secrets\":\"0x"	<< txin.params.____required_spend_secrets << "\"" JSON_ENDL
	os << ",\"required-trust-secrets\":\"0x"	<< txin.params.____required_trust_secrets << "\"" JSON_ENDL
	os << ",\"destination-number\":\"0x"		<< txin.params.____destnum << "\"" JSON_ENDL

	os << ",\"payment-number\":\"0x"			<< txin.params.addrparams.__paynum << "\"" JSON_ENDL

	if (txin.secrets[0].____have_master_secret || txin.secrets[0].____have_root_secret)
		os << ",\"spend-secret-number\":\"0x"	<< txin.secrets[0].____spend_secret_number << "\"" JSON_ENDL

	os << ",\"use-spend-secret\":[";
	for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
	{
		if (j) os << ",";
		os << "\"0x" << txin.params.____use_spend_secret[j] << "\"";
	}
	os << "]" JSON_ENDL;

	os << ",\"use-trust-secret\":[";
	for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
	{
		if (j) os << ",";
		os << "\"0x" << txin.params.____use_trust_secret[j] << "\"";
	}
	os << "]" JSON_ENDL;

	if (txin.secrets[0].____have_master_secret)
		os << ",\"master-secret\":\"0x"			<< txin.secrets[0].____master_secret << "\"" JSON_ENDL
	if (txin.secrets[0].____have_root_secret)
		os << ",\"root-secret\":\"0x"			<< txin.secrets[0].____root_secret << "\"" JSON_ENDL

	os << ",\"spend-secrets\":[";
	for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
	{
		if (j) os << ",";
		if (txin.secrets[j].____have_spend_secret)
			os << "\"0x" << txin.secrets[j].____spend_secret << "\"";
		else
			os << "null";
	}
	os << "]" JSON_ENDL;

	os << ",\"trust-secrets\":[";
	for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
	{
		if (j) os << ",";
		if (txin.secrets[j].____have_trust_secret)
			os << "\"0x" << txin.secrets[j].____trust_secret << "\"";
		else
			os << "null";
	}
	os << "]" JSON_ENDL;

	os << ",\"monitor-secrets\":[";
	for (unsigned j = 0; j < TX_MAX_SECRET_SLOTS; ++j)
	{
		if (j) os << ",";
		if (txin.secrets[j].____have_monitor_secret)
			os << "\"0x" << txin.secrets[j].____monitor_secret << "\"";
		else
			os << "null";
	}
	os << "]" JSON_ENDL;

	if (txin.secrets[0].____have_receive_secret)
		os << ",\"receive-secret\":\"0x" << txin.secrets[0].____receive_secret << "\"" JSON_ENDL

	os << ",\"merkle-root\":\"0x"			<< txin.merkle_root << "\"" JSON_ENDL
	os << ",\"maximum-input-exponent\":\"0x"	<< txin.invalmax << "\"" JSON_ENDL
	os << ",\"delaytime\":\"0x"				<< txin.delaytime << "\"" JSON_ENDL

	os << ",\"domain\":\"0x"					<< txin.M_domain << "\"" JSON_ENDL
	os << ",\"asset\":\"0x"					<< txin.__asset << "\"" JSON_ENDL
	os << ",\"amount\":\"0x"				<< txin.__amount_fp << "\"" JSON_ENDL
	os << ",\"commitment-iv\":\"0x"			<< txin.__M_commitment_iv << "\"" JSON_ENDL
	//os << ",\"commitment-index\":\"0x"	<< txin.__M_commitment_index << "\"" JSON_ENDL
	os << ",\"commitment\":\"0x"			<< txin._M_commitment << "\"" JSON_ENDL
	os << ",\"commitment-number\":\"0x"		<< txin._M_commitnum << "\"" JSON_ENDL

	os << ",\"no-serial-number\":\"0x"		<< txin.no_serialnum << "\"" JSON_ENDL
	os << ",\"serial-number\":\"0x"			<< txin.S_serialnum << "\"" JSON_ENDL
	os << ",\"hashkey\":\"0x"				<< txin.S_hashkey << "\"" JSON_ENDL
	os << ",\"hashed-spendspec\":\"0x"		<< txin.S_spendspec_hashed << "\"" JSON_ENDL

	if (path)
	{
		os << ",\"merkle-path\":" JSON_ENDL
		os << "[";
		for (unsigned i = 0; i < TX_MERKLE_DEPTH; ++i)
		{
			if (i) os << ",";
			os << "\"0x" << path->__M_merkle_path[i] << "\"" JSON_ENDL
		}
		os << "]}" JSON_ENDL
	}
	else
		os << "}" JSON_ENDL
}

static CCRESULT txpay_to_json(const string& fn, const TxPay& tx, char *output, const uint32_t outsize)
{
	ostringstream os;
	os << hex;

	if (tx.tag_type == CC_TYPE_TXPAY)
		os << "{\"tx-pay\":" JSON_ENDL
	else if (tx.tag_type == CC_TYPE_MINT)
		os << "{\"mint\":" JSON_ENDL
	else
		return error_invalid_tx_type(fn, output, outsize);

	os << "{\"zkkeyid\":\"0x" << tx.zkkeyid << "\"" JSON_ENDL
	os << ",\"zkproof\":" JSON_ENDL
	os << "[";
	for (unsigned i = 0; i < tx.zkproof.size(); ++i)
	{
		if (i) os << ",";
		os << "\"0x" << tx.zkproof[i] << "\"" JSON_ENDL
	}
	os << "]" JSON_ENDL

	os << ",\"type\":\"0x"				<< tx.tx_type << "\"" JSON_ENDL
	os << ",\"source-chain\":\"0x"		<< tx.source_chain << "\"" JSON_ENDL
	os << ",\"parameter-level\":\"0x"	<< tx.param_level << "\"" JSON_ENDL
	os << ",\"parameter-time\":\"0x"	<< tx.param_time << "\"" JSON_ENDL
	os << ",\"revision\":\"0x"			<< tx.revision << "\"" JSON_ENDL
	os << ",\"expiration\":\"0x"		<< tx.expiration << "\"" JSON_ENDL
	os << ",\"reference-hash\":\"0x"	<< tx.refhash << "\"" JSON_ENDL
	os << ",\"reserved\":\"0x"			<< tx.reserved << "\"" JSON_ENDL
	os << ",\"donation\":\"0x"			<< tx.donation_fp << "\"" JSON_ENDL
	os << ",\"minimum-output-exponent\":\"0x"	<< tx.outvalmin << "\"" JSON_ENDL
	os << ",\"maximum-output-exponent\":\"0x"	<< tx.outvalmax << "\"" JSON_ENDL
	os << ",\"merkle-root\":\"0x"			<< tx.tx_merkle_root << "\"" JSON_ENDL
	os << ",\"commitment-iv\":\"0x"			<< tx.M_commitment_iv << "\"" JSON_ENDL

	if (tx.nout)
		os << ",\"destination-chain\":\"0x"	<< tx.outputs[0].addrparams.dest_chain << "\"" JSON_ENDL
	else
		os << ",\"destination-chain\":\"0x"	<< tx.dest_chain__ << "\"" JSON_ENDL

	if (tx.nout)
		os << ",\"default-domain\":\"0x"		<< tx.outputs[0].M_domain << "\"" JSON_ENDL
	else
		os << ",\"default-domain\":\"0x"		<< tx.default_domain__ << "\"" JSON_ENDL

	if (tx.nout)
		os << ",\"acceptance-required\":\"0x" << tx.outputs[0].acceptance_required << "\"" JSON_ENDL
	else
		os << ",\"acceptance-required\":\"0x" << tx.acceptance_required__ << "\"" JSON_ENDL


	if (tx.tag_type != CC_TYPE_MINT)
	{
		if (tx.nin)
			os << ",\"maximum-input-exponent\":\"0x" << tx.inputs[0].invalmax << "\"" JSON_ENDL
		else
			os << ",\"maximum-input-exponent\":\"0x" << tx.invalmax__ << "\"" JSON_ENDL
	}

	if (tx.nin)
		os << ",\"delaytime\":\"0x" << tx.inputs[0].delaytime << "\"" JSON_ENDL
	else
		os << ",\"delaytime\":\"0x" << tx.delaytime__ << "\"" JSON_ENDL

	os << ",\"outputs\":" JSON_ENDL
	os << "[" JSON_ENDL
	for (unsigned i = 0; i < tx.nout; ++i)
	{
		if (i) os << "," JSON_ENDL
		txpay_output_to_json(fn, tx.outputs[i], os);
	}
	os << "]" JSON_ENDL

	os << ",\"inputs\":" JSON_ENDL
	os << "[" JSON_ENDL
	for (unsigned i = 0; i < tx.nin; ++i)
	{
		if (i) os << "," JSON_ENDL
		unsigned pathnum = tx.inputs[i].pathnum;
		if (pathnum)
			txpay_input_to_json(fn, tx.inputs[i], &tx.inpaths[pathnum - 1], os);
		else
			txpay_input_to_json(fn, tx.inputs[i], NULL, os);
	}
	os << "]}}" JSON_ENDL

	return copy_result_to_output(fn, os.str(), output, outsize);
}

void tx_set_commit_iv(TxPay& tx)
{
	if (tx.override_commitment_iv__)
		return;

	/* There are two goals here:
		1. Make it difficult for user to chose the value of the output commitments, thereby making a commitment collision attack
			(two billets with same commitment values but different assets and/or amounts) that much more difficult.
			This is accomplished by including tx_merkle_root in commitment_iv, where tx_merkle_root must be chosen from a set
			of recently valid merkle_root's for the merkle tree of all valid commitments.
		2. Make it difficult to create two payments to a single address that have identical commitment values,
			so that a payment can be uniquely referenced by concatenating the address and commitment value.
	*/

	tx.M_commitment_iv = tx.tx_merkle_root;

	bigint_mask(tx.M_commitment_iv, TX_COMMIT_IV_BITS);

	return;

	/* The following code is not used because it gives the payor control over the commit_iv, by allowing the payor to
	select the input billet serial numbers that are hashed to compute the commit_iv.  (If the payor had a pool of 2^16
	input billets and the tx allowed eight inputs, the payor would have 2^(16*8) = 2^128 bits of input into the hash.)
	By controlling the hash inputs, the payor can duplicate a given commit_iv with work factor 2^N, and create a
	commit_iv collision with work factor 2^(N/2). As a result, the commit_iv would have to be at least 128 bits in order
	to be useful.  A better approach is to simply use 64 bits of the Merkle root has the commit_iv, and then check for
	and disallow duplicate commitments during the time period that the Merkle root is valid.  For example, if a Merkle
	root is valid 8 hours, then all commitments added to the blockchain during the last 8 hours can be indexed in
	RAM--at 100 tx/sec and two commitments/tx, this would only require 176 MB of RAM.  In contrast, storing a 128-bit
	commit_iv instead of a 64-bit commit_iv would require an additional 250 GB of disk space to store 5 years of
	commitments. */

	const bool TRACE_COMMIT_IV = 1;

	blake2s_ctx ctx;

	auto rc = blake2s_init(&ctx, sizeof(tx.M_commitment_iv), NULL, 0);
	CCASSERTZ(rc);

	#if 0 // not used
	if (tx.M_commitment_iv_nonce)
	{
		if (TRACE_COMMIT_IV) cerr << "set_commit_iv nonce " << hex << tx.M_commitment_iv_nonce << dec << endl;
		blake2s_update(&ctx, &tx.M_commitment_iv_nonce, TX_COMMIT_IV_NONCE_BYTES);
	}
	#endif

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv merkle_root " << hex << tx.tx_merkle_root << dec << endl;
	blake2s_update(&ctx, &tx.tx_merkle_root, TX_MERKLE_BYTES);

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv tx_type " << hex << tx.tx_type << dec << endl;
	blake2s_update(&ctx, &tx.tx_type, TX_TYPE_BYTES);

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv revision " << hex << tx.revision << dec << endl;
	blake2s_update(&ctx, &tx.revision, TX_REVISION_BYTES);

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv expiration " << hex << tx.expiration << dec << endl;
	blake2s_update(&ctx, &tx.expiration, TX_TIME_BYTES);

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv refhash " << hex << tx.refhash << dec << endl;
	blake2s_update(&ctx, &tx.refhash, TX_REFHASH_BYTES);

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv reserved " << hex << tx.reserved << dec << endl;
	blake2s_update(&ctx, &tx.reserved, TX_RESERVED_BYTES);

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv donation_fp " << hex << tx.donation_fp << dec << endl;
	blake2s_update(&ctx, &tx.donation_fp, TX_DONATION_BYTES);

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv nout " << tx.nout << endl;
	blake2s_update(&ctx, &tx.nout, sizeof(tx.nout));

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv nin " << tx.nin << endl;
	blake2s_update(&ctx, &tx.nin, sizeof(tx.nin));

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv nin_with_path " << tx.nin_with_path << endl;
	blake2s_update(&ctx, &tx.nin_with_path, sizeof(tx.nin_with_path));

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		const TxOut& txout = tx.outputs[i];

		if (TRACE_COMMIT_IV) cerr << "set_commit_iv dest_chain " << i << " " << hex << txout.addrparams.dest_chain << dec << endl;
		blake2s_update(&ctx, &txout.addrparams.dest_chain, TX_CHAIN_BYTES);

		if (TRACE_COMMIT_IV) cerr << "set_commit_iv acceptance_required " << i << " " << hex << txout.acceptance_required << dec << endl;
		blake2s_update(&ctx, &txout.acceptance_required, 1);

		if (TRACE_COMMIT_IV) cerr << "set_commit_iv repeat_count " << i << " " << hex << txout.repeat_count << dec << endl;
		blake2s_update(&ctx, &txout.repeat_count, sizeof(txout.repeat_count));

		if (!txout.no_address)
		{
			if (TRACE_COMMIT_IV) cerr << "set_commit_iv M_address " << i << " " << hex << txout.M_address << dec << endl;
			blake2s_update(&ctx, &txout.M_address, TX_ADDRESS_BYTES);
		}

		if (!txout.no_asset && !txout.asset_mask)
		{
			if (TRACE_COMMIT_IV) cerr << "set_commit_iv M_asset_enc " << i << " " << hex << txout.M_asset_enc << dec << endl;
			blake2s_update(&ctx, &txout.M_asset_enc, TX_ASSET_BYTES);
		}

		if (!txout.no_amount && !txout.amount_mask)
		{
			if (TRACE_COMMIT_IV) cerr << "set_commit_iv M_amount_enc " << i << " " << hex << txout.M_amount_enc << dec << endl;
			blake2s_update(&ctx, &txout.M_amount_enc, TX_AMOUNT_BYTES);
		}
	}

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		const TxIn& txin = tx.inputs[i];

		uint16_t flags = 0;
		flags |= txin.enforce_master_secret << 0;
		flags |= txin.enforce_spend_secrets << 0;
		flags |= txin.enforce_trust_secrets << 0;
		flags |= txin.enforce_freeze << 0;
		flags |= txin.enforce_unfreeze << 0;

		if (TRACE_COMMIT_IV) cerr << "set_commit_iv flags " << i << " " << hex << flags << dec << endl;
		blake2s_update(&ctx, &flags, 1);

		if (TRACE_COMMIT_IV) cerr << "set_commit_iv delaytime " << i << " " << hex << txin.delaytime << dec << endl;
		blake2s_update(&ctx, &txin.delaytime, TX_DELAYTIME_BYTES);

		if (!txin.pathnum)
		{
			if (TRACE_COMMIT_IV) cerr << "set_commit_iv _M_commitment " << i << " " << hex << txin._M_commitment << dec << endl;
			blake2s_update(&ctx, &txin._M_commitment, TX_COMMITMENT_BYTES);

			if (!txin.no_serialnum)
			{
				if (TRACE_COMMIT_IV) cerr << "set_commit_iv _M_commitnum " << i << " " << hex << txin._M_commitnum << dec << endl;
				blake2s_update(&ctx, &txin._M_commitnum, TX_COMMITNUM_BYTES);
			}
		}

		if (!txin.no_serialnum)
		{
			if (TRACE_COMMIT_IV) cerr << "set_commit_iv S_serialnum " << i << " " << hex << txin.S_serialnum << dec << endl;
			blake2s_update(&ctx, &txin.S_serialnum, TX_SERIALNUM_BYTES);
		}

		if (TRACE_COMMIT_IV) cerr << "set_commit_iv S_hashkey " << i << " " << hex << txin.S_hashkey << dec << endl;
		blake2s_update(&ctx, &txin.S_hashkey, sizeof(txin.S_hashkey));

		if (TRACE_COMMIT_IV) cerr << "set_commit_iv S_spendspec_hashed " << i << " " << hex << txin.S_spendspec_hashed << dec << endl;
		blake2s_update(&ctx, &txin.S_spendspec_hashed, sizeof(txin.S_spendspec_hashed));
	}

	blake2s_final(&ctx, &tx.M_commitment_iv);

	bigint_mask(tx.M_commitment_iv, TX_COMMIT_IV_BITS);

	if (TRACE_COMMIT_IV) cerr << "set_commit_iv commitment_iv " << hex << tx.M_commitment_iv << dec << endl;

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		#if 0 // not used
		TxOut& txout = tx.outputs[i];

		txout.M_commitment_iv = tx.M_commitment_iv;
		BIG64(txout.M_commitment_iv) ^= i;

		if (TRACE_COMMIT_IV) cerr << "set_commit_iv M_commitment_iv " << i << " " << hex << txout.M_commitment_iv << dec << endl;
		#endif
	}
}

const char* compute_or_verify_secrets(const SpendSecretParams& params, SpendSecret& secrets, bool no_precheck)
{
	#if TRACE_COMMITMENTS
	cerr << "compute_or_verify_secrets no_precheck " << no_precheck << endl;
	cerr << "  > ____spend_secret_number " << hex << secrets.____spend_secret_number << dec << endl;
	cerr << "  > ____enforce_spendspec_with_spend_secrets " << params.____enforce_spendspec_with_spend_secrets << dec << endl;
	cerr << "  > ____enforce_spendspec_with_trust_secrets " << params.____enforce_spendspec_with_trust_secrets << dec << endl;
	cerr << "  > ____required_spendspec_hash " << hex << params.____required_spendspec_hash << dec << endl;
	cerr << "  > ____allow_master_secret " << params.____allow_master_secret << dec << endl;
	cerr << "  > ____allow_freeze " << params.____allow_freeze << dec << endl;
	cerr << "  > ____allow_trust_unfreeze " << params.____allow_trust_unfreeze << dec << endl;
	cerr << "  > ____require_public_hashkey " << params.____require_public_hashkey << dec << endl;
	cerr << "  > ____restrict_addresses " << params.____restrict_addresses << dec << endl;
	cerr << "  > ____spend_locktime " << params.____spend_locktime << dec << endl;
	cerr << "  > ____trust_locktime " << params.____trust_locktime << dec << endl;
	cerr << "  > ____spend_delaytime " << params.____spend_delaytime << dec << endl;
	cerr << "  > ____trust_delaytime " << params.____trust_delaytime << dec << endl;
	cerr << "  > have_master_secret " << secrets.____have_master_secret << " master_secret " << hex << secrets.____master_secret << dec << endl;
	cerr << "  > have_root_secret " << secrets.____have_root_secret << " root_secret " << hex << secrets.____root_secret << dec << endl;
	cerr << "  > have_spend_secret " << secrets.____have_spend_secret << " spend_secret " << hex << secrets.____spend_secret << dec << endl;
	cerr << "  > have_trust_secret " << secrets.____have_trust_secret << " trust_secret " << hex << secrets.____trust_secret << dec << endl;
	cerr << "  > have_monitor_secret " << secrets.____have_monitor_secret << " have_restricted_address " << secrets.____have_restricted_address << " monitor_secret " << hex << secrets.____monitor_secret << dec << endl;
	cerr << "  > have_receive_secret " << secrets.____have_receive_secret << " receive_secret " << hex << secrets.____receive_secret << dec << endl;
	#endif


	vector<CCHashInput> hashin(11);
	bigint_t root_check, spend_check, trust_check, freeze_check, monitor_check, receive_check;

	// RULE tx input: @root_secret = zkhash(@master_secret)

	if (secrets.____have_master_secret)
	{
		hashin.clear();
		hashin.resize(1);
		hashin[0].SetValue(secrets.____master_secret, TX_INPUT_BITS);
		root_check = CCHash::Hash(hashin, HASH_BASES_ROOT_SECRET, TX_FIELD_BITS);
	}
	else
		root_check = secrets.____root_secret;

	if (!secrets.____have_root_secret)
		secrets.____root_secret = root_check;
	else if (secrets.____root_secret != root_check && !no_precheck)
		return "root-secret != zkhash(master-secret)";
	else
		root_check = secrets.____root_secret;

	secrets.____have_root_secret |= secrets.____have_master_secret;

	// RULE tx input: @spend_secret[0] = zkhash(@root_secret, @spend_secret_number)

	if (secrets.____have_root_secret)
	{
		hashin.clear();
		hashin.resize(2);
		hashin[0].SetValue(root_check, TX_FIELD_BITS);
		hashin[1].SetValue(secrets.____spend_secret_number, TX_SPEND_SECRETNUM_BITS);
		spend_check = CCHash::Hash(hashin, HASH_BASES_SPEND_SECRET, TX_FIELD_BITS);
	}
	else
		spend_check = secrets.____spend_secret;

	if (!secrets.____have_spend_secret)
		secrets.____spend_secret = spend_check;
	else if (secrets.____spend_secret != spend_check && !no_precheck)
		return "spend-secret != hash(root-secret, spend-secret-number)";
	else
		spend_check = secrets.____spend_secret;

	secrets.____have_spend_secret |= secrets.____have_root_secret;

	// RULE tx input: @trust_secret[i] = zkhash(@spend_secret[i])

	if (secrets.____have_spend_secret)
	{
		hashin.clear();
		hashin.resize(1);
		hashin[0].SetValue(spend_check, TX_INPUT_BITS);
		trust_check = CCHash::Hash(hashin, HASH_BASES_TRUST_SECRET, TX_FIELD_BITS);
	}
	else
		trust_check = secrets.____trust_secret;

	if (!secrets.____have_trust_secret)
		secrets.____trust_secret = trust_check;
	else if (secrets.____trust_secret != trust_check && !no_precheck)
		return "trust-secret != hash(spend-secret)";
	else
		trust_check = secrets.____trust_secret;

	secrets.____have_trust_secret |= secrets.____have_spend_secret;

	// RULE tx input: @monitor_secret[i] = zkhash(@trust_secret[i])

	if (secrets.____have_trust_secret)
	{
		hashin.clear();
		hashin.resize(1);
		hashin[0].SetValue(trust_check, TX_INPUT_BITS);
		monitor_check = CCHash::Hash(hashin, HASH_BASES_MONITOR_SECRET, TX_FIELD_BITS);
	}
	else
		monitor_check = secrets.____monitor_secret;

	if (!secrets.____have_monitor_secret)
		secrets.____monitor_secret = monitor_check;
	else if (secrets.____monitor_secret != monitor_check && !secrets.____have_restricted_address && !no_precheck)
		return "monitor-secret != zkhash(trust-secret)";
	else
		monitor_check = secrets.____monitor_secret;

	secrets.____have_monitor_secret |= secrets.____have_trust_secret;

	// RULE tx input: @receive_secret = zkhash(@monitor_secret[0], @enforce_spendspec_with_spend_secret, @enforce_spendspec_with_trust_secret, @required_spendspec_hash, @allow_master_secret, @allow_freeze, @allow_trust_unfreeze, @require_public_hashkey, @restrict_addresses, @spend_locktime, @trust_locktime, @spend_delaytime, @trust_delaytime)

	if (secrets.____have_monitor_secret || !secrets.____have_receive_secret)	// must compute receive_secret from some value for billet to be useable
	{
		hashin.clear();
		hashin.resize(14);
		bigint_t hi_bits = monitor_check;
		bigint_shift_down(hi_bits, TX_INPUT_BITS/2);
		hashin[0].SetValue(monitor_check, TX_INPUT_BITS/2);
		hashin[1].SetValue(hi_bits, TX_INPUT_BITS/2);
		hashin[2].SetValue(params.____enforce_spendspec_with_spend_secrets, 1);
		hashin[3].SetValue(params.____enforce_spendspec_with_trust_secrets, 1);
		hashin[4].SetValue(params.____required_spendspec_hash, TX_INPUT_BITS);
		hashin[5].SetValue(params.____allow_master_secret, 1);
		hashin[6].SetValue(params.____allow_freeze, 1);
		hashin[7].SetValue(params.____allow_trust_unfreeze, 1);
		hashin[8].SetValue(params.____require_public_hashkey, 1);
		hashin[9].SetValue(params.____restrict_addresses, 1);
		hashin[10].SetValue(params.____spend_locktime, TX_TIME_BITS);
		hashin[11].SetValue(params.____trust_locktime, TX_TIME_BITS);
		hashin[12].SetValue(params.____spend_delaytime, TX_DELAYTIME_BITS);
		hashin[13].SetValue(params.____trust_delaytime, TX_DELAYTIME_BITS);
		receive_check = CCHash::Hash(hashin, HASH_BASES_RECEIVE_SECRET, TX_FIELD_BITS);
	}
	else
		receive_check = secrets.____receive_secret;

	if (!secrets.____have_receive_secret)
		secrets.____receive_secret = receive_check;
	else if (secrets.____receive_secret != receive_check && !no_precheck)
		return "receive-secret != zkhash(monitor-secret)";
	else
		receive_check = secrets.____receive_secret;

	secrets.____have_receive_secret = true;

	#if TRACE_COMMITMENTS
	cerr << "compute_or_verify_secrets no_precheck " << no_precheck << endl;
	cerr << " <  have_master_secret " << secrets.____have_master_secret << " master_secret " << hex << secrets.____master_secret << dec << endl;
	cerr << " <  have_root_secret " << secrets.____have_root_secret << " root_secret " << hex << secrets.____root_secret << dec << endl;
	cerr << " <  have_spend_secret " << secrets.____have_spend_secret << " spend_secret " << hex << secrets.____spend_secret << dec << endl;
	cerr << " <  have_trust_secret " << secrets.____have_trust_secret << " trust_secret " << hex << secrets.____trust_secret << dec << endl;
	cerr << " <  have_monitor_secret " << secrets.____have_monitor_secret << " have_restricted_address " << secrets.____have_restricted_address << " monitor_secret " << hex << secrets.____monitor_secret << dec << endl;
	cerr << " <  have_receive_secret " << secrets.____have_receive_secret << " receive_secret " << hex << secrets.____receive_secret << dec << endl;
	#endif

	return NULL;
}

void compute_destination(const SpendSecretParams& params, const SpendSecrets& secrets, bigint_t& destination)
{
	// RULE tx input: #dest = zkhash(@receive_secret, @monitor_secret[1..R], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets, @destnum)

	uint64_t use_spend_secret_bits = 0;
	uint64_t use_trust_secret_bits = 0;

	for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
	{
		use_spend_secret_bits |= (params.____use_spend_secret[j] << j);
		use_trust_secret_bits |= (params.____use_trust_secret[j] << j);
	}

	vector<CCHashInput> hashin(2*TX_MAX_SECRET_SLOTS + 4);
	unsigned c = 0;
	hashin[c++].SetValue(secrets[0].____receive_secret, TX_FIELD_BITS);
	for (unsigned j = 1; j < TX_MAX_SECRET_SLOTS; ++j)
	{
		bigint_t lo_bits = secrets[j].____monitor_secret;
		bigint_t hi_bits = lo_bits;
		bigint_mask(lo_bits, TX_INPUT_BITS/2);
		bigint_shift_down(hi_bits, TX_INPUT_BITS/2);
		hashin[c++].SetValue(lo_bits, TX_INPUT_BITS/2);
		hashin[c++].SetValue(hi_bits, TX_INPUT_BITS/2);
	}
	hashin[c++].SetValue(use_spend_secret_bits, TX_MAX_SECRETS);
	hashin[c++].SetValue(use_trust_secret_bits, TX_MAX_SECRETS);
	hashin[c++].SetValue(params.____required_spend_secrets, TX_MAX_SECRETS_BITS);
	hashin[c++].SetValue(params.____required_trust_secrets, TX_MAX_SECRETS_BITS);
	hashin[c++].SetValue(params.____destnum, TX_DESTNUM_BITS);
	CCASSERT(c == 2*TX_MAX_SECRET_SLOTS + 4);
	destination = CCHash::Hash(hashin, HASH_BASES_DESTINATION, TX_FIELD_BITS);

	#if TRACE_COMMITMENTS
	cerr << "compute_destination" << endl;
	for (unsigned j = 0; j < hashin.size(); ++j)
	{
		cerr << "hash input " << j << endl;
		hashin[j].Dump();
	}
	cerr << "destination " << hex << destination << dec << endl;
	#endif
}

void compute_address(const bigint_t& destination, uint64_t destination_chain, uint64_t paynum, bigint_t& address)
{
	// note RULE tx output: M_address = zkhash(#dest, dest_chain, #paynum)

	vector<CCHashInput> hashin(3);
	hashin[0].SetValue(destination, TX_FIELD_BITS);
	hashin[1].SetValue(destination_chain, TX_CHAIN_BITS);
	hashin[2].SetValue(paynum, TX_PAYNUM_BITS);
	address = CCHash::Hash(hashin, HASH_BASES_ADDRESS, TX_ADDRESS_BITS);

	#if 0
	cerr << "compute_address" << endl;
	for (unsigned j = 0; j < hashin.size(); ++j)
	{
		cerr << "hash input " << j << endl;
		hashin[j].Dump();
	}
	cerr << "address " << hex << address << dec << endl;
	#endif
}

void compute_amount_pad(const bigint_t& commit_iv, const bigint_t& dest, const uint32_t paynum, uint64_t& asset_pad, uint64_t& amount_pad)
{
	// set RULE tx output:	where #asset_xor = asset_mask & (zkhash(M_encrypt_iv, #dest, #paynum))
	// set RULE tx output:	where #amount_xor = amount_mask & (zkhash(M_encrypt_iv, #dest, #paynum) >> TX_ASSET_BITS)

	vector<CCHashInput> hashin(3);
	hashin[0].SetValue(commit_iv, TX_ENC_IV_BITS);
	hashin[1].SetValue(dest, TX_FIELD_BITS);
	hashin[2].SetValue(paynum, TX_PAYNUM_BITS);
	auto one_time_pad = CCHash::Hash(hashin, HASH_BASES_AMOUNT_ENC, TX_ASSET_BITS + TX_AMOUNT_BITS);

	#if TX_ASSET_BITS != 64
	#error TX_ASSET_BITS != 64
	#endif

	asset_pad = BIG64(one_time_pad) & TX_ASSET_MASK;
	amount_pad = BIGWORD(one_time_pad, 1) & TX_AMOUNT_MASK;

	#if 0
	cerr << "compute_amount_pad full pad commit_iv " << hex << commit_iv << dec << endl;
	hashin[0].Dump();
	cerr << "compute_amount_pad full pad dest " << hex << dest << dec << endl;
	hashin[1].Dump();
	cerr << "compute_amount_pad full pad paynum " << hex << paynum << dec << endl;
	hashin[2].Dump();
	cerr << "compute_amount_pad full pad " << hex << one_time_pad << dec << endl;
	cerr << "compute_amount_pad asset_pad " << hex << asset_pad << dec << endl;
	cerr << "compute_amount_pad amount_pad " << hex << amount_pad << dec << endl;
	#endif
}

void compute_commitment(const bigint_t& commit_iv, const bigint_t& dest, const uint32_t paynum, const uint32_t domain, const uint64_t asset, const uint64_t amount_fp, bigint_t& commitment)
{
	// set RULE tx output: M_commitment = zkhash(M_commitment_iv, #dest, #paynum, M_domain, #asset, #amount)

	vector<CCHashInput> hashin(6);
	hashin[0].SetValue(commit_iv, TX_COMMIT_IV_BITS);
	//hashin[1].SetValue(i, TX_COMMIT_INDEX_BITS);
	hashin[1].SetValue(dest, TX_FIELD_BITS);
	hashin[2].SetValue(paynum, TX_PAYNUM_BITS);
	hashin[3].SetValue(domain, TX_DOMAIN_BITS);
	hashin[4].SetValue(asset, TX_ASSET_BITS);
	hashin[5].SetValue(amount_fp, TX_AMOUNT_BITS);
	commitment = CCHash::Hash(hashin, HASH_BASES_COMMITMENT, TX_FIELD_BITS);

	#if TRACE_COMMITMENTS
	cerr << "set_output_iv_dependents M_commitment" << endl;
	for (unsigned j = 0; j < hashin.size(); ++j)
	{
		cerr << "hash input " << j << endl;
		hashin[j].Dump();
	}
	cerr << "commitment " << hex << commitment << dec << endl;
	#endif
}

void compute_serialnum(const bigint_t& monitor_secret, const bigint_t& commitment, uint64_t commitnum, bigint_t& serialnum)
{
	// note RULE tx input: S-serialnum = zkhash(@monitor_secret[0], M-commitment, M-commitnum)

	bigint_t hi_bits = monitor_secret;
	bigint_shift_down(hi_bits, TX_INPUT_BITS/2);

	vector<CCHashInput> hashin(4);
	hashin[0].SetValue(monitor_secret, TX_INPUT_BITS/2);
	hashin[1].SetValue(hi_bits, TX_INPUT_BITS/2);
	hashin[2].SetValue(commitment, TX_FIELD_BITS);
	hashin[3].SetValue(commitnum, TX_COMMITNUM_BITS);
	serialnum = CCHash::Hash(hashin, HASH_BASES_SERIALNUM, TX_SERIALNUM_BITS);
}

static void set_output_dependents(const TxPay& tx, TxOut& txout)
{
	// set RULE tx output: if enforce_address, then M_address = zkhash(#dest, dest_chain, #paynum)

	compute_address(txout.addrparams.__dest, txout.addrparams.dest_chain, txout.addrparams.__paynum, txout.M_address);
}

static void set_output_iv_dependents(const TxPay& tx, TxOut& txout)
{
	// set RULE tx output: if enforce_asset, then M_asset_enc = #asset ^ #asset_xor
	// set RULE tx output:	where #asset_xor = asset_mask & (zkhash(M_encrypt_iv, #dest, #paynum))

	compute_amount_pad(tx.M_commitment_iv, txout.addrparams.__dest, txout.addrparams.__paynum, txout.__asset_pad, txout.__amount_pad);

	if (!txout.no_asset)
		txout.M_asset_enc = txout.__asset ^ (txout.asset_mask & txout.__asset_pad);

	// set RULE tx output: if enforce_amount, then M_amount_enc = #amount ^ #amount_xor
	// set RULE tx output:	where #amount_xor = amount_mask & (zkhash(M_encrypt_iv, #dest, #paynum) >> TX_ASSET_BITS)

	if (!txout.no_amount)
		txout.M_amount_enc = txout.__amount_fp ^ (txout.amount_mask & txout.__amount_pad);

	#if 0
	cerr << "set_output_iv_dependents no_amount " << hex << txout.no_amount << dec << endl;
	cerr << "set_output_iv_dependents amount_mask " << hex << txout.amount_mask << dec << endl;
	cerr << "set_output_iv_dependents __amount_fp " << hex << txout.__amount_fp << dec << endl;
	cerr << "set_output_iv_dependents amount_enc " << hex << txout.M_amount_enc << dec << endl;
	#endif

	compute_commitment(tx.M_commitment_iv, txout.addrparams.__dest, txout.addrparams.__paynum,
			txout.M_domain, txout.__asset, txout.__amount_fp, txout.M_commitment);
}

static CCRESULT txpay_precheck_output(const string& fn, const TxPay& tx, unsigned index, const TxOut& txout, char *output, const uint32_t outsize)
{
	// check RULE tx output: if #asset = 0, then #amount = 0 or #amount_exponent >= valmin

	if (txout.__asset == 0 && txout.__amount_fp && tx_amount_decode_exponent(txout.__amount_fp, TX_AMOUNT_EXPONENT_BITS) < tx.outvalmin)
		return copy_error_to_output(fn, string("error: amount < minimum for output ") + to_string(index), output, outsize);

	// check RULE tx output: if #asset = 0, then #amount_exponent <= valmax

	if (txout.__asset == 0 && tx_amount_decode_exponent(txout.__amount_fp, TX_AMOUNT_EXPONENT_BITS) > tx.outvalmax)
		return copy_error_to_output(fn, string("error: amount > maximum for output ") + to_string(index), output, outsize);

	// check RULE tx output: if lowest X bits of the destination value are all 0, then acceptance_required = 1

	if ((BIG64(txout.addrparams.__dest) & TX_ACCEPT_REQ_DEST_MASK) == 0 && !txout.acceptance_required)
		return copy_error_to_output(fn, string("error: acceptance-required not set but required by destination for output ") + to_string(index), output, outsize);

	// check RULE tx output: if middle Y bits of the destination value are all 0, then paynum = 0
	// check RULE tx output restatement: (1 - dest_bit[n]) * ... * (1 - dest_bit[n+Y]) * paynum = 0

	if ((BIG64(txout.addrparams.__dest) & TX_STATIC_ADDRESS_MASK) == 0 && txout.addrparams.__paynum)
		return copy_error_to_output(fn, string("error: requires static address but paynum > 0 for output ") + to_string(index), output, outsize);

	// not checked RULE tx output: if enforce_asset, then M_asset_enc = #asset ^ #asset_xor
	// not checked RULE tx output:	where #asset_xor = asset_mask & (zkhash(M_encrypt_iv, #dest, #paynum))
	// not checked RULE tx output: if enforce_amount, then M_amount_enc = #amount ^ #amount_xor
	// not checked RULE tx output:	where #amount_xor = amount_mask & (zkhash(M_encrypt_iv, #dest, #paynum) >> TX_ASSET_BITS)
	// not checked RULE tx output: if enforce_address, then M_address = zkhash(#dest, dest_chain, #paynum)
	// not checked RULE tx output: M_commitment = zkhash(M_commitment_iv, #dest, #paynum, M_domain, #asset, #amount)

	return 0;
}

static void set_input_dependents(const TxPay& tx, TxIn& txin)
{
	// autocompute master_secret_valid

	if (!txin.____have_master_secret_valid && txin.params.____allow_master_secret && txin.secrets[0].____have_master_secret)
		txin.____master_secret_valid = 1;

	// set RULE tx input: if @spend_secrets_valid, then sum(@secret_valid[i] * @use_spend_secret[i]) >= @required_spend_secrets
	// set RULE tx input: if @trust_secrets_valid, then sum(@secret_valid[i] * @use_trust_secret[i]) >= @required_trust_secrets

	unsigned spend_count = 0;
	unsigned trust_count = 0;

	for (unsigned i = 0; i < TX_MAX_SECRETS; ++i)
	{
		if (txin.params.____use_spend_secret[i] && txin.secrets[i].____have_spend_secret)
			++spend_count;

		if (txin.params.____use_trust_secret[i] && txin.secrets[i].____have_trust_secret)
			++trust_count;
	}

	// autocompute trust_secrets_valid
	if (!txin.____have_trust_secrets_valid && (txin.enforce_trust_secrets || txin.enforce_unfreeze))
	{
		txin.____trust_secrets_valid = (trust_count >= txin.params.____required_trust_secrets && tx.param_time >= txin.params.____trust_locktime && txin.delaytime >= txin.params.____trust_delaytime);

		if (txin.____trust_secrets_valid && txin.enforce_unfreeze)
			txin.____trust_secrets_valid = txin.params.____allow_trust_unfreeze;

		if (txin.____trust_secrets_valid && txin.params.____enforce_spendspec_with_trust_secrets)
			txin.____trust_secrets_valid = (txin.S_spendspec_hashed == txin.params.____required_spendspec_hash);
	}

	// autocompute spend_secrets_valid
	// but don't set spend_secrets_valid if trust_secrets_valid is enough, cause setting spend_secrets_valid might unset trust_secrets_valid and make an unfreeze invalid
	if (!txin.____have_spend_secrets_valid && (txin.enforce_spend_secrets || (txin.enforce_trust_secrets && !txin.____trust_secrets_valid)))
	{
		txin.____spend_secrets_valid = (spend_count >= txin.params.____required_spend_secrets && tx.param_time >= txin.params.____spend_locktime && txin.delaytime >= txin.params.____spend_delaytime);

		if (txin.____spend_secrets_valid && txin.params.____require_public_hashkey)
			txin.____spend_secrets_valid = (txin.S_hashkey == txin.secrets[1].____spend_secret);

		if (txin.____spend_secrets_valid && txin.params.____enforce_spendspec_with_spend_secrets)
			txin.____spend_secrets_valid = (txin.S_spendspec_hashed == txin.params.____required_spendspec_hash);
	}

	if (!txin.____have_trust_secrets_valid && txin.____spend_secrets_valid && txin.____trust_secrets_valid)
	{
		// spend_secrets_valid is set, so secret_valid[i] will be set from spend_secrets which means trust_secrets_valid must be recomputed

		unsigned trust_count = 0;

		for (unsigned i = 0; i < TX_MAX_SECRETS; ++i)
		{
			if (txin.params.____use_trust_secret[i] && txin.secrets[i].____have_spend_secret)
				++trust_count;
		}

		txin.____trust_secrets_valid = (trust_count >= txin.params.____required_trust_secrets && tx.param_time >= txin.params.____trust_locktime && txin.delaytime >= txin.params.____trust_delaytime);
	}

	// set RULE tx input: if enforce_serialnum, then S-serialnum = zkhash(@monitor_secret[0], M-commitment, M-commitnum)

	compute_serialnum(txin.secrets[0].____monitor_secret, txin._M_commitment, txin._M_commitnum, txin.S_serialnum);

	#if 0 // !!! test this again

	// test adding the prime to the commitment to see if a valid proof can be created that has a different serialnum

	bigint_t prime = bigint_t(0UL) - bigint_t(1UL);
	addBigInt(prime, bigint_t(1UL), prime, false);
	//cerr << "prime = " << hex << prime << dec << endl;
	cerr << "old _M_commitment = " << hex << txin._M_commitment << dec << endl;
	addBigInt(prime, txin._M_commitment, txin._M_commitment, false);
	cerr << "new _M_commitment = " << hex << txin._M_commitment << dec << endl;
	cerr << "old S_serialnum = " << hex << txin.S_serialnum << dec << endl;
	compute_serialnum(txin.secrets[0].____monitor_secret, txin._M_commitment, txin._M_commitnum, txin.S_serialnum);
	cerr << "new S_serialnum = " << hex << txin.S_serialnum << dec << endl;

	#endif
}

static CCRESULT txpay_precheck_input(const string& fn, const TxPay& tx, unsigned index, const TxIn& txin, char *output, const uint32_t outsize)
{
	// check RULE tx input: if #asset = 0, then #amount_exponent <= valmax

	if (txin.__asset == 0 && tx_amount_decode_exponent(txin.__amount_fp, TX_AMOUNT_EXPONENT_BITS) > txin.invalmax)
		return copy_error_to_output(fn, string("error: amount > maximum for input ") + to_string(index), output, outsize);

	// check RULE tx input: if enforce_master_secret, then @master_secret_valid = 1

	if (txin.enforce_master_secret && !txin.____master_secret_valid)
		return copy_error_to_output(fn, string("error: enforce-master-secret set but master-secret-valid not set for input ") + to_string(index), output, outsize);

	// check RULE tx input: if @master_secret_valid, then @allow_master_secret = 1

	if (txin.____master_secret_valid && !txin.params.____allow_master_secret)
		return copy_error_to_output(fn, string("error: master-secret-valid set but allow-master-secret not set for input ") + to_string(index), output, outsize);

	// check RULE tx input: if enforce_freeze, then @allow_freeze = 1

	if (txin.enforce_freeze && !txin.params.____allow_freeze)
		return copy_error_to_output(fn, string("error: enforce-freeze set but allow-freeze not set for input ") + to_string(index), output, outsize);

	// check RULE tx input: if enforce_unfreeze, then @master_secret_valid = 1 or @trust_secrets_valid = 1

	if (txin.enforce_unfreeze && !txin.____master_secret_valid && !txin.____trust_secrets_valid)
		return copy_error_to_output(fn, string("error: enforce-unfreeze set but master-secret and trust secrets are both invalid for input ") + to_string(index), output, outsize);

	// check RULE tx input: if enforce_unfreeze and @trust_secrets_valid, then @allow_trust_unfreeze = 1

	if (txin.enforce_unfreeze && txin.____trust_secrets_valid && !txin.params.____allow_trust_unfreeze)
		return copy_error_to_output(fn, string("error: enforce-unfreeze and trust-secrets-valid set but allow-trust-unfreeze not set for input ") + to_string(index), output, outsize);

	// check RULE tx input: if enforce_spend_secrets, then @master_secret_valid = 1 or @spend_secrets_valid = 1

	if (txin.enforce_spend_secrets && !txin.____master_secret_valid && !txin.____spend_secrets_valid)
		return copy_error_to_output(fn, string("error: enforce-spend-secrets set but master-secret and spend secrets are both invalid, or delaytime are invalid for input ") + to_string(index), output, outsize);

	// check RULE tx input: if enforce_trust_secrets, then @master_secret_valid = 1 or @spend_secrets_valid = 1 or @trust_secrets_valid = 1

	if (txin.enforce_trust_secrets && !txin.____master_secret_valid && !txin.____spend_secrets_valid && !txin.____trust_secrets_valid)
		return copy_error_to_output(fn, string("error: enforce-trust-secrets set but master-secret, spend secrets and trust secrets are all invalid, or delaytime is invalid for input ") + to_string(index), output, outsize);

	// check RULE tx input: if @require_public_hashkey and @spend_secrets_valid, then @secret_valid[1] = 1

	if (txin.params.____require_public_hashkey && txin.____spend_secrets_valid && !txin.secrets[1].____have_spend_secret)
		return copy_error_to_output(fn, string("error: require-public-hashkey and spend secrets valid but second spend secret is invalid for input ") + to_string(index), output, outsize);

	// check RULE tx input: if @require_public_hashkey and @spend_secrets_valid, then S-hashkey = @spend_secret[1]

	if (txin.params.____require_public_hashkey && txin.____spend_secrets_valid && (txin.S_hashkey != txin.secrets[1].____spend_secret))
		return copy_error_to_output(fn, string("error: require-public-hashkey and spend secrets valid but public hashkey != second spend secret for input ") + to_string(index), output, outsize);

	// check RULE tx input: if @spend_secrets_valid and @enforce_spendspec_with_spend_secret, then S_spendspec = @required_spendspec_hash
	// check RULE tx input: if @trust_secrets_valid and @enforce_spendspec_with_trust_secret, then S_spendspec = @required_spendspec_hash

	if (((txin.____spend_secrets_valid && txin.params.____enforce_spendspec_with_spend_secrets) || (txin.____trust_secrets_valid && txin.params.____enforce_spendspec_with_trust_secrets))
			&& txin.S_spendspec_hashed != txin.params.____required_spendspec_hash)
		return copy_error_to_output(fn, string("error: hashed-spendspec != required-spendspec-hash for input ") + to_string(index), output, outsize);

	// check RULE tx input: if @spend_secrets_valid, then sum(@secret_valid[i] * @use_spend_secret[i]) >= @required_spend_secrets
	// check RULE tx input: if @trust_secrets_valid, then sum(@secret_valid[i] * @use_trust_secret[i]) >= @required_trust_secrets

	unsigned spend_count = 0;
	unsigned trust_count = 0;

	for (unsigned i = 0; i < TX_MAX_SECRETS; ++i)
	{
		if (txin.params.____use_spend_secret[i] && txin.secrets[i].____have_spend_secret)
			++spend_count;

		if (txin.params.____use_trust_secret[i] && txin.secrets[i].____have_trust_secret)
			++trust_count;
	}

	if (txin.____spend_secrets_valid && spend_count < txin.params.____required_spend_secrets)
		return copy_error_to_output(fn, string("error: insufficient spend-secrets for input ") + to_string(index), output, outsize);

	if (txin.____trust_secrets_valid && trust_count < txin.params.____required_trust_secrets)
		return copy_error_to_output(fn, string("error: insufficient trust-secrets for input ") + to_string(index), output, outsize);

	// check RULE tx input: if @spend_secrets_valid, then param_time >= @spend_locktime

	if (txin.____spend_secrets_valid && tx.param_time < txin.params.____spend_locktime)
		return copy_error_to_output(fn, string("error: parameter-time < spend-locktime for input ") + to_string(index), output, outsize);

	// check RULE tx input: if @trust_secrets_valid, then param_time >= @trust_locktime

	if (txin.____trust_secrets_valid && tx.param_time < txin.params.____trust_locktime)
		return copy_error_to_output(fn, string("error: parameter-time < trust-locktime for input ") + to_string(index), output, outsize);

	// check RULE tx input: if @spend_secrets_valid, then delaytime >= @spend_delaytime

	if (txin.____spend_secrets_valid && txin.delaytime < txin.params.____spend_delaytime)
		return copy_error_to_output(fn, string("error: delaytime < spend-delaytime for input ") + to_string(index), output, outsize);

	// check RULE tx input: if @trust_secrets_valid, then delaytime >= @trust_delaytime

	if (txin.____trust_secrets_valid && txin.delaytime < txin.params.____trust_delaytime)
		return copy_error_to_output(fn, string("error: delaytime < trust-delaytime for input ") + to_string(index), output, outsize);

	// check RULE tx input: if @master_secret_valid, then @spend_secret[0] = zkhash(@root_secret, @spend_secret_number)
	// check RULE tx input:	where @root_secret = zkhash(@master_secret)

	if (txin.____master_secret_valid && !txin.secrets[0].____have_master_secret)
		return copy_error_to_output(fn, string("error: master-secret-valid set but master-secret invalid for input ") + to_string(index), output, outsize);

	// not checked RULE tx input: for each i = 0..N, if (i == 0 and @master_secret_valid) or (@spend_secrets_valid and @secret_valid[i]), then @trust_secret[i] = zkhash(@spend_secret[i])
	// not checked RULE tx input: for each i = 0..N, if (i == 0 and @master_secret_valid) or @secret_valid[i], then @monitor_secret[i] = zkhash(@trust_secret[i])

	for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
	{
	//	auto errmsg = compute_or_verify_secrets(txin.params, const_cast<SpendSecret&>(txin.secrets[j]), false);
	//	if (errmsg)
	//		return copy_error_to_output(fn, string("error: " + errmsg + " for secret " + to_string(j) + " of input ") + to_string(index), output, outsize);
	}

	// check RULE tx input: M_commitment = zkhash(M_commitment_iv, #dest, #paynum, M_domain, #asset, #amount)
	// check RULE tx input:	where #dest = zkhash(@receive_secret, @monitor_secret[1..R], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets, @destnum)
	// not checked RULE tx input:	where @receive_secret = zkhash(@monitor_secret[0], @enforce_spendspec_with_spend_secret, @enforce_spendspec_with_trust_secret, @required_spendspec_hash, @allow_master_secret, @allow_freeze, @allow_trust_unfreeze, @require_public_hashkey, @restrict_addresses, @spend_locktime, @trust_locktime, @spend_delaytime, @trust_delaytime)

	bigint_t destination, commitment;
	compute_destination(txin.params, txin.secrets, destination);

	compute_commitment(txin.__M_commitment_iv, destination, txin.params.addrparams.__paynum,
			txin.M_domain, txin.__asset, txin.__amount_fp, commitment);

	//cerr << "commitment " << index << " = " << commitment << endl;

	if (commitment != txin._M_commitment)
		return copy_error_to_output(fn, string("error: inputs do not hash to the commitment for input ") + to_string(index), output, outsize);

	// not checked RULE tx input: if enforce_serialnum, then S-serialnum = zkhash(@monitor_secret[0], M-commitment, M-commitnum)
	// not checked RULE tx input: if enforce_path, then enforce Merkle path from M-commitment to merkle_root

	return 0;
}

static CCRESULT txpay_precheck_input_path(const string& fn, unsigned index, const TxIn& txin, const TxInPath& txpath, char *output, const uint32_t outsize)
{
	// check RULE tx input: if enforce_path, then enforce Merkle path from M-commitment to merkle_root

	bigint_t val1, val2, hash;

	tx_commit_tree_hash_leaf(txin._M_commitment, txin._M_commitnum, hash);

	for (unsigned i = 0; i < TX_MERKLE_DEPTH; ++i)
	{
		val1 = hash;
		val2 = txpath.__M_merkle_path[i];

		tx_commit_tree_hash_node(val1, val2, hash, i < TX_MERKLE_DEPTH - 1);
	}

	if (hash != txin.merkle_root)
		return copy_error_to_output(fn, string("error: commitment Merkle path does not hash to the Merkle root for input ") + to_string(index), output, outsize);

	return 0;
}

static bool asset_in_list(const TxPay& tx, uint64_t asset)
{
	for (unsigned i = 0; i < tx.__nassets; ++i)
	{
		if (asset == tx.__asset_list[i])
			return true;
	}

	return false;
}

static void update_asset_list(TxPay& tx, uint64_t asset)
{
	if (asset_in_list(tx, asset))
		return;

	CCASSERT(tx.__nassets < TX_MAX_NASSETS);

	if (tx.__nassets >= TX_MAX_NASSETS)
		return;

	tx.__asset_list[tx.__nassets++] = asset;
}


static void set_unused_asset_list(TxPay& tx)
{
	// set the unused list entries to an asset value that is not used in the tx, so the # of assets used by zkp can be expanded to match the key

	for (uint64_t asset = TX_ASSET_MASK; ; --asset)
	{
		if (!asset_in_list(tx, asset))
		{
			for (unsigned i = tx.__nassets; i < TX_MAX_NASSETS; ++i)
				tx.__asset_list[i] = asset;

			return;
		}
	}
}

static void set_refhash_from_append_data(TxPay& tx)
{
	if (!tx.refhash && tx.append_data_length)
	{
		auto rc = blake2s(&tx.refhash, sizeof(tx.refhash), NULL, 0, tx.append_data.data(), tx.append_data_length);
		CCASSERTZ(rc);
	}
}

static void set_dependents(TxPay& tx)
{
	#if 0 // test hash
	bigint_t tx_type;
	subBigInt(bigint_t(0UL), bigint_t(1UL), tx_type, false);
	//tx_type = bigint_t(0UL) - bigint_t(1UL);
	//addBigInt(tx_type, bigint_t(1UL + 0xfe), tx_type, false);

	unsigned nbits = TX_INPUT_BITS;
	nbits = 16;

	vector<CCHashInput> hashin(1);
	hashin[0].SetValue(tx_type, nbits);
	auto hash = CCHash::Hash(hashin, 1, TX_INPUT_BITS);

	cerr << "test_hash tx_type " << hex << tx_type << dec << endl;
	cerr << "test_hash hash " << hex << hash << dec << endl;
	return;
	#endif

	set_refhash_from_append_data(tx);

	// get max # secrets

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		if (tx.____nsecrets < tx.inputs[i].params.____nsecrets)
			tx.____nsecrets = tx.inputs[i].params.____nsecrets;

		if (tx.____nraddrs < tx.inputs[i].params.____nraddrs)
			tx.____nraddrs = tx.inputs[i].params.____nraddrs;
	}

	// get nassets and fill in asset_list

	tx.__nassets = 1;

	for (unsigned i = 0; i < tx.nout; ++i)
		update_asset_list(tx, tx.outputs[i].__asset);

	for (unsigned i = 0; i < tx.nin; ++i)
		update_asset_list(tx, tx.inputs[i].__asset);

	set_unused_asset_list(tx);

	// set input and output dependents

	for (unsigned i = 0; i < tx.nout; ++i)
		set_output_dependents(tx, tx.outputs[i]);

	for (unsigned i = 0; i < tx.nin; ++i)
		set_input_dependents(tx, tx.inputs[i]);

	tx_set_commit_iv(tx);

	for (unsigned i = 0; i < tx.nout; ++i)
		set_output_iv_dependents(tx, tx.outputs[i]);
}

static CCRESULT txpay_precheck(const string& fn, const TxPay& tx, char *output, const uint32_t outsize)
{
	//cerr << "txpay_precheck nout " << tx.nout << " nin " << tx.nin << endl;

#if 0 // this is now wirebad instead of !TEST_EXTRA_ON_WIRE

	if (tx.nout + tx.nin < 1)
		return copy_error_to_output(fn, "error: transaction requires at least one input or one output", output, outsize);

#endif

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		auto rc = txpay_precheck_output(fn, tx, i, tx.outputs[i], output, outsize);
		if (rc) return rc;
	}

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		auto rc = txpay_precheck_input(fn, tx, i, tx.inputs[i], output, outsize);
		if (rc) return rc;

		auto pathnum = tx.inputs[i].pathnum;

		if (pathnum)
		{
			auto rc = txpay_precheck_input_path(fn, i, tx.inputs[i], tx.inpaths[pathnum - 1], output, outsize);
			if (rc) return rc;
		}
	}

	// check RULE tx: for each i, sum_inputs(@is_asset[i] * #amount) = sum_outputs(@is_asset[i] * multiplier * #amount) + (i == 0) * donation

	for (unsigned j = 0; j < tx.__nassets; ++j)
	{
		bigint_t bigval, valsum = 0UL;

		auto asset = tx.__asset_list[j];

		if (asset == 0)
		{
			tx_amount_decode(tx.donation_fp, bigval, true, TX_DONATION_BITS, TX_AMOUNT_EXPONENT_BITS);
			valsum = bigval + tx.amount_carry_out - tx.amount_carry_in;
			//cerr << "donation_fp " << hex << tx.donation_fp << dec << " decoded " << bigval << endl;
			//cerr << "amount_carry_out " << tx.amount_carry_out << " amount_carry_in " << tx.amount_carry_in << " valsum " << valsum << endl;
		}

		for (unsigned i = 0; i < tx.nout; ++i)
		{
			if (tx.outputs[i].__asset == asset)
			{
				tx_amount_decode(tx.outputs[i].__amount_fp, bigval, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS);
				valsum = valsum + bigval * bigint_t((uint64_t)tx.outputs[i].repeat_count + 1);
				//cerr << "tx.outputs[" << i << "].__amount_fp " << hex << tx.outputs[i].__amount_fp << dec << " decoded " << bigval << " * " << (uint64_t)tx.outputs[i].repeat_count + 1 << " valsum " << valsum << endl;
			}
		}

		for (unsigned i = 0; i < tx.nin; ++i)
		{
			if (tx.inputs[i].__asset == asset)
			{
				tx_amount_decode(tx.inputs[i].__amount_fp, bigval, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS);
				valsum = valsum - bigval;
				//cerr << "tx.inputs[" << i << "].__amount_fp " << hex << tx.inputs[i].__amount_fp << dec << " decoded " << bigval << " valsum " << valsum << endl;
			}
		}

		if (valsum)
			return copy_error_to_output(fn, string("error: sum(input amounts) != sum(output amounts) for asset id ") + to_string(asset), output, outsize);
	}

	return 0;
}

static CCRESULT proof_error(const string& fn, int rc, char *output, const uint32_t outsize)
{
	if (rc == CCPROOF_ERR_NO_KEY)
		copy_error_to_output(fn, "error: no suitable zero knowledge proof key found", output, outsize);
	else if (rc == CCPROOF_ERR_INSUFFICIENT_KEY)
		copy_error_to_output(fn, "error: zero knowledge proof key has insufficient capacity", output, outsize);
	else if (rc == CCPROOF_ERR_LOADING_KEY)
		copy_error_to_output(fn, "error loading zero knowledge proof key", output, outsize);
	else if (rc == CCPROOF_ERR_NO_PROOF)
		copy_error_to_output(fn, "error zero knowledge proof omitted", output, outsize);
	else if (rc < 0)
		copy_error_to_output(fn, "error: transaction proof generation error", output, outsize);

	return rc;
}

static CCRESULT set_proof(const string& fn, TxPay& tx, char *output, const uint32_t outsize)
{
	if (Xtx::TypeHasBareMsg(tx.tag_type))
		return 0;

	auto blog = cc_malloc_logging_not_this_thread(true);

	auto rc = CCProof_GenProof(tx);

	cc_malloc_logging_not_this_thread(blog);

	return proof_error(fn, rc, output, outsize);
}

static CCRESULT check_proof(const string& fn, TxPay& tx, char *output, const uint32_t outsize)
{
	if (Xtx::TypeHasBareMsg(tx.tag_type))
		return 0;

	auto rc = CCProof_VerifyProof(tx);

	return proof_error(fn, rc, output, outsize);
}

static CCRESULT set_mint_inputs(TxPay& tx)
{
	if (tx.tag_type != CC_TYPE_MINT)
	{
		if (TEST_SHOW_WIRE_ERRORS) cerr << "error set_mint_inputs type " << tx.tag_type << endl;

		return -1;
	}

	if (tx.nin)
	{
		if (TEST_SHOW_WIRE_ERRORS) cerr << "error set_mint_inputs nin " << tx.nin << endl;

		return -1;
	}

	tx.nin = 1;

	TxIn& txin = tx.inputs[0];

	bigint_t bigval;
	bigval = TX_CC_MINT_AMOUNT;
	txin.invalmax = TX_CC_MINT_EXPONENT;
	txin.__amount_fp = tx_amount_encode(bigval, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS, TX_CC_MINT_EXPONENT, TX_CC_MINT_EXPONENT);
	//txin._M_commitment = "17001882429967903773100989557462178497740410097909483058299851875380585795018"; // 50K input
	txin._M_commitment = "973184326264892349829845330310452310635714594783699009126090380917429859595";	// 1K input
	txin.merkle_root = tx.tx_merkle_root;
	txin.enforce_trust_secrets = 1;

	//for (unsigned j = 0; j < TX_MAX_SECRET_SLOTS; ++j)
	//	BIGWORD(txin.secrets[j].____monitor_secret, 3) = TX_NONFIELD_HI_WORD;

	compute_or_verify_secrets(txin.params, txin.secrets[0], true);

	txin.no_serialnum = 1;

	return 0;
}

static CCRESULT txpay_output_from_json(const string& fn, Json::Value& root, const TxPay& tx, TxOut& txout, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;
	bigint_t bigval;

	key = "destination";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, txout.addrparams.__dest, output, outsize);
	if (rc) return rc;

	key = "destination-chain";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txout.addrparams.dest_chain = BIG64(bigval);
	}
	else if (tx.have_dest_chain__)
		txout.addrparams.dest_chain = tx.dest_chain__;
	else
		return error_missing_key(fn, key, output, outsize);

	key = "payment-number";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_PAYNUM_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	txout.addrparams.__paynum = BIG64(bigval);

	key = "no-address";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txout.no_address = BIG64(bigval);
	}

	key = "acceptance-required";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txout.acceptance_required = BIG64(bigval);
	}
	else if (tx.have_acceptance_required__)
		txout.acceptance_required = tx.acceptance_required__;

	key = "repeat-count";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txout.repeat_count = BIG64(bigval);
	}

	key = "domain";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_DOMAIN_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txout.M_domain = BIG64(bigval);
	}
	else if (tx.have_default_domain__)
		txout.M_domain = tx.default_domain__;
	else
		return error_missing_key(fn, key, output, outsize);

	key = "asset";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_ASSET_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txout.__asset = BIG64(bigval);
	}

	key = "no-asset";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txout.no_asset = BIG64(bigval);
	}

	key = "asset-mask";
	if (!txout.no_asset)
	{
		if (!root.removeMember(key, &value))
			return error_missing_key(fn, key, output, outsize);
		auto rc = parse_int_value(fn, key, value.asString(), TX_ASSET_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txout.asset_mask = BIG64(bigval);
	}
	else
	{
		txout.asset_mask = (uint64_t)(-1);		// for safety
	}

	key = "amount";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	txout.__amount_fp = BIG64(bigval);

	key = "no-amount";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txout.no_amount = BIG64(bigval);
	}

	key = "amount-mask";
	if (!txout.no_amount)
	{
		if (!root.removeMember(key, &value))
			return error_missing_key(fn, key, output, outsize);
		auto rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txout.amount_mask = BIG64(bigval);
	}
	else
	{
		txout.amount_mask = (uint64_t)(-1);		// for safety
	}

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	return 0;
}

CCRESULT tx_params_from_json(const string& fn, Json::Value& root, bool allow_multi_secrets, SpendSecretParams& params, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;
	bigint_t bigval;

	key = "enforce-spendspec-with-spend-secrets";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____enforce_spendspec_with_spend_secrets = BIG64(bigval);
	}

	key = "enforce-spendspec-with-trust-secrets";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____enforce_spendspec_with_trust_secrets = BIG64(bigval);
	}

	key = "required-spendspec-hash";
	if ((params.____enforce_spendspec_with_spend_secrets || params.____enforce_spendspec_with_trust_secrets) && root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_INPUT_BITS, 0UL, params.____required_spendspec_hash, output, outsize); // allow a stdhash that exceeds the prime modulus
		if (rc) return rc;
	}

	key = "allow-master-secret";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____allow_master_secret = BIG64(bigval);
	}

	key = "allow-freeze";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____allow_freeze = BIG64(bigval);
	}

	key = "allow-trust-unfreeze";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____allow_trust_unfreeze = BIG64(bigval);
	}

	key = "require-public-hashkey";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____require_public_hashkey = BIG64(bigval);
	}

	key = "restrict-addresses";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____restrict_addresses = BIG64(bigval);
	}

	key = "spend-locktime";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_TIME_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____spend_locktime = BIG64(bigval);
	}

	key = "trust-locktime";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_TIME_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____trust_locktime = BIG64(bigval);
	}

	key = "spend-delaytime";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_DELAYTIME_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____spend_delaytime = BIG64(bigval);
	}

	key = "trust-delaytime";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_DELAYTIME_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____trust_delaytime = BIG64(bigval);
	}

	if (allow_multi_secrets)
	{
		key = "required-spend-secrets";
		if (root.removeMember(key, &value))
		{
			auto rc = parse_int_value(fn, key, value.asString(), 0, TX_MAX_SECRETS, bigval, output, outsize);
			if (rc) return rc;
			params.____required_spend_secrets = BIG64(bigval);
		}

		key = "required-trust-secrets";
		if (root.removeMember(key, &value))
		{
			auto rc = parse_int_value(fn, key, value.asString(), 0, TX_MAX_SECRETS, bigval, output, outsize);
			if (rc) return rc;
			params.____required_trust_secrets = BIG64(bigval);
		}

		key = "destination-number";
		if (!root.removeMember(key, &value))
			return error_missing_key(fn, key, output, outsize);
		auto rc = parse_int_value(fn, key, value.asString(), TX_DESTNUM_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		params.____destnum = BIG64(bigval);
	}

	return 0;
}

unsigned restricted_address_secret_index(unsigned slot)
{
	unsigned secreti = TX_MAX_SECRET_SLOTS - 1 - slot/2;

	CCASSERT(slot < TX_MAX_RESTRICTED_ADDRESSES);
	CCASSERT(secreti < TX_MAX_SECRET_SLOTS);

	return secreti;
}

bool restricted_address_slot_open(const SpendSecretParams& params, unsigned slot)
{
	auto j = restricted_address_secret_index(slot);

	bool in_use = (j < TX_MAX_SECRETS && (params.____use_spend_secret[j] || params.____use_trust_secret[j]));

	//cerr << "restricted_address_slot_open slot " << slot << " index " << j << " in_use " << in_use << " use_spend_secret " << (j < TX_MAX_SECRETS ? params.____use_spend_secret[j] : 9) << " use_trust_secret " << (j < TX_MAX_SECRETS ? params.____use_trust_secret[j] : 9) << endl;

	return !in_use;
}

void set_restricted_address(SpendSecrets& secrets, unsigned slot, const bigint_t& value)
{
	auto j = restricted_address_secret_index(slot);

	secrets[j].____have_monitor_secret = true;
	secrets[j].____have_restricted_address = true;

	bigint_t& secret = secrets[j].____monitor_secret;

	for (unsigned i = 0; i < 2; ++i)
		BIGWORD(secret, (slot & 1) * 2 + i) = BIGWORD(value, i);
}

void get_restricted_address(const SpendSecrets& secrets, unsigned slot, bigint_t& value)
{
	auto j = restricted_address_secret_index(slot);
	value = secrets[j].____monitor_secret;
	if (slot & 1)
		bigint_shift_down(value, TX_INPUT_BITS/2);
	else
		bigint_mask(value, TX_INPUT_BITS/2);
}

CCRESULT tx_secrets_from_json(const string& fn, Json::Value& root, bool allow_multi_secrets, SpendSecretParams& params, SpendSecrets& secrets, bool no_precheck, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;
	bigint_t bigval;

	key = "master-secret";
	if (root.removeMember(key, &value))
	{
		secrets[0].____have_master_secret = true;

		auto rc = parse_int_value(fn, key, value.asString(), TX_INPUT_BITS, 0UL, secrets[0].____master_secret, output, outsize);
		if (rc) return rc;
	}

	key = "root-secret";
	if (root.removeMember(key, &value))
	{
		secrets[0].____have_root_secret = true;

		auto rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, secrets[0].____root_secret, output, outsize);
		if (rc) return rc;
	}

	if (secrets[0].____have_master_secret || secrets[0].____have_root_secret)
	{
			key = "spend-secret-number";
			if (root.removeMember(key, &value))
			{
				auto rc = parse_int_value(fn, key, value.asString(), TX_SPEND_SECRETNUM_BITS, 0UL, bigval, output, outsize);
				if (rc) return rc;
				secrets[0].____spend_secret_number = BIG64(bigval);
			}
	}

	key = "spend-secret";
	if (root.removeMember(key, &value))
	{
		secrets[0].____have_spend_secret = true;

		auto rc = parse_int_value(fn, key, value.asString(), TX_INPUT_BITS, 0UL, secrets[0].____spend_secret, output, outsize);
		if (rc) return rc;
	}

	key = "spend-secrets";
	if (allow_multi_secrets && !secrets[0].____have_spend_secret && root.removeMember(key, &value))
	{
		if (!value.isArray())
			return error_not_array(fn, key, output, outsize);
		if (value.size() > TX_MAX_SECRETS)
			return error_too_many_objs(fn, key, TX_MAX_SECRETS, output, outsize);
		for (unsigned j = 0; j < value.size(); ++j)
		{
			if (!value[j].isNull())
			{
				secrets[j].____have_spend_secret = true;

				auto rc = parse_int_value(fn, key, value[j].asString(), TX_INPUT_BITS, 0UL, secrets[j].____spend_secret, output, outsize);
				if (rc) return rc;
			}
		}
	}

	key = "trust-secret";
	if (root.removeMember(key, &value))
	{
		secrets[0].____have_trust_secret = true;

		auto rc = parse_int_value(fn, key, value.asString(), TX_INPUT_BITS, 0UL, secrets[0].____trust_secret, output, outsize);
		if (rc) return rc;
	}

	key = "trust-secrets";
	if (allow_multi_secrets && !secrets[0].____have_trust_secret && root.removeMember(key, &value))
	{
		if (!value.isArray())
			return error_not_array(fn, key, output, outsize);
		if (value.size() > TX_MAX_SECRETS)
			return error_too_many_objs(fn, key, TX_MAX_SECRETS, output, outsize);
		for (unsigned j = 0; j < value.size(); ++j)
		{
			if (!value[j].isNull())
			{
				secrets[j].____have_trust_secret = true;

				auto rc = parse_int_value(fn, key, value[j].asString(), TX_INPUT_BITS, 0UL, secrets[j].____trust_secret, output, outsize);
				if (rc) return rc;
			}
		}
	}

	for (unsigned j = 0; j < TX_MAX_SECRET_SLOTS; ++j)
	{
		secrets[j].____monitor_secret = 0UL;
		//BIGWORD(secrets[j].____monitor_secret, 3) = TX_NONFIELD_HI_WORD;
	}

	key = "monitor-secret";
	if (root.removeMember(key, &value))
	{
		secrets[0].____have_monitor_secret = true;

		auto rc = parse_int_value(fn, key, value.asString(), TX_INPUT_BITS, 0UL, secrets[0].____monitor_secret, output, outsize);
		if (rc) return rc;
	}

	key = "monitor-secrets";
	if (allow_multi_secrets && !secrets[0].____have_monitor_secret && root.removeMember(key, &value))
	{
		if (!value.isArray())
			return error_not_array(fn, key, output, outsize);
		if (value.size() > TX_MAX_SECRET_SLOTS)
			return error_too_many_objs(fn, key, TX_MAX_SECRET_SLOTS, output, outsize);
		for (unsigned j = 0; j < value.size(); ++j)
		{
			if (!value[j].isNull())
			{
				secrets[j].____have_monitor_secret = true;

				auto rc = parse_int_value(fn, key, value[j].asString(), TX_INPUT_BITS, 0UL, secrets[j].____monitor_secret, output, outsize);
				if (rc) return rc;
			}
		}
	}

	key = "receive-secret";
	if (root.removeMember(key, &value))
	{
		secrets[0].____have_receive_secret = true;

		auto rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, secrets[0].____receive_secret, output, outsize);
		if (rc) return rc;
	}

	if (allow_multi_secrets)
	{
		// parse ____use_trust_secret[j]
		key = "use-trust-secrets";
		if (root.removeMember(key, &value))
		{
			if (!value.isArray())
				return error_not_array(fn, key, output, outsize);
			if (value.size() > TX_MAX_SECRETS)
				return error_too_many_objs(fn, key, TX_MAX_SECRETS, output, outsize);
			for (unsigned j = 0; j < value.size(); ++j)
			{
				if (!value[j].isNull())
				{
					auto rc = parse_int_value(fn, key, value[j].asString(), 1, 0UL, bigval, output, outsize);
					if (rc) return rc;
					params.____use_trust_secret[j] = BIG64(bigval);
				}
			}
		}
		else
		{
			for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
				params.____use_trust_secret[j] = secrets[j].____have_trust_secret;
		}

		// parse ____use_spend_secret
		key = "use-spend-secrets";
		if (root.removeMember(key, &value))
		{
			if (!value.isArray())
				return error_not_array(fn, key, output, outsize);
			if (value.size() > TX_MAX_SECRETS)
				return error_too_many_objs(fn, key, TX_MAX_SECRETS, output, outsize);
			for (unsigned j = 0; j < value.size(); ++j)
			{
				if (!value[j].isNull())
				{
					auto rc = parse_int_value(fn, key, value[j].asString(), 1, 0UL, bigval, output, outsize);
					if (rc) return rc;
					params.____use_spend_secret[j] = BIG64(bigval);
				}
			}
		}
		else
		{
			for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
				params.____use_spend_secret[j] = (secrets[j].____have_spend_secret || (!params.____use_trust_secret[j] && (secrets[j].____have_monitor_secret || secrets[j].____have_receive_secret)));

			params.____use_spend_secret[0] |= (secrets[0].____have_master_secret || secrets[0].____have_root_secret);
		}

		// set defaults for params
		for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
		{
			if (params.____use_spend_secret[j])
			{
				params.____nsecrets = j + 1;
				params.____required_spend_secrets += 1;
			}

			if (params.____use_trust_secret[j])
			{
				params.____nsecrets = j + 1;
				params.____required_trust_secrets += 1;
			}
		}

		if (params.____required_spend_secrets < 1)
			params.____required_spend_secrets = 1;

		if (params.____required_trust_secrets < 1)
			params.____required_trust_secrets = 1;

		//tx_dump_spend_secret_params_stream(cout, params, "");

		key = "restricted-addresses";
		if (root.removeMember(key, &value))
		{
			CCASSERT(TX_MAX_RESTRICTED_ADDRESSES <= 2*(TX_MAX_SECRET_SLOTS-1));

			if (!value.isArray())
				return error_not_array(fn, key, output, outsize);
			if (value.size() > TX_MAX_RESTRICTED_ADDRESSES)
				return error_too_many_objs(fn, key, TX_MAX_RESTRICTED_ADDRESSES, output, outsize);

			for (unsigned j = 0; j < value.size(); ++j)
			{
				auto rc = parse_int_value(fn, key, value[j].asString(), TX_ADDRESS_BITS, 0UL, bigval, output, outsize);
				if (rc) return rc;

				while (true)
				{
					if (params.____nraddrs >= TX_MAX_RESTRICTED_ADDRESSES)
						return copy_error_to_output(fn, string("error: insufficient open slots for ") + key, output, outsize);

					if (restricted_address_slot_open(params, params.____nraddrs))
						break;

					++params.____nraddrs;
				}

				set_restricted_address(secrets, params.____nraddrs++, bigval);
			}

			// fill in all the unused slots with the last address so there is no stray value for the Payor to match

			for (unsigned j = params.____nraddrs; j < TX_MAX_RESTRICTED_ADDRESSES; ++j)
			{
				if (restricted_address_slot_open(params, j))
					set_restricted_address(secrets, j, bigval);
			}
		}
	}

	// parse param inputs
	auto rc = tx_params_from_json(fn, root, allow_multi_secrets, params, output, outsize);
	if (rc) return rc;

	// compute dependent secrets
	for (unsigned j = 0; j < TX_MAX_SECRETS; ++j)
	{
		auto errmsg = compute_or_verify_secrets(params, secrets[j], no_precheck);
		if (errmsg)
			return copy_error_to_output(fn, string("error: ") + errmsg + " for secret " + to_string(j), output, outsize);
	}

	return 0;
}

CCRESULT tx_input_common_from_json(const string& fn, Json::Value& root, TxIn& txin, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;
	bigint_t bigval;

	key = "payment-number";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), TX_PAYNUM_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	txin.params.addrparams.__paynum = BIG64(bigval);

	key = "domain";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_DOMAIN_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	txin.M_domain = BIG64(bigval);

	key = "asset";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_ASSET_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.__asset = BIG64(bigval);
	}

	key = "amount";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	txin.__amount_fp = BIG64(bigval);

	key = "hashkey";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_HASHKEY_BITS, 0UL, txin.S_hashkey, output, outsize);
		if (rc) return rc;
	}
	else
	{
		CCRandom(&txin.S_hashkey, TX_HASHKEY_WIRE_BYTES);
	}

	key = "commitment-iv";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_COMMIT_IV_BITS, 0UL, txin.__M_commitment_iv, output, outsize);
	if (rc) return rc;

	/*key = "commitment-index";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_COMMIT_INDEX_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	txin.__M_commitment_index = BIG64(bigval);*/

	return 0;
}

static CCRESULT txpay_input_from_json(const string& fn, Json::Value& root, const TxPay& tx, TxIn& txin, TxInPath *path, bool no_precheck, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;
	bigint_t bigval;

	key = "enforce-master-secret";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.enforce_master_secret = BIG64(bigval);
	}

	key = "enforce-spend-secrets";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.enforce_spend_secrets = BIG64(bigval);
	}

	key = "enforce-freeze";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.enforce_freeze = BIG64(bigval);
	}

	key = "enforce-unfreeze";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.enforce_unfreeze = BIG64(bigval);
	}

	key = "enforce-trust-secrets";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.enforce_trust_secrets = BIG64(bigval);
	}
	else if (!txin.enforce_master_secret && !txin.enforce_spend_secrets && !txin.enforce_freeze && !txin.enforce_unfreeze)
		txin.enforce_trust_secrets = 1;

	auto rc = tx_secrets_from_json(fn, root, true, txin.params, txin.secrets, no_precheck, output, outsize);
	if (rc) return rc;

	key = "master-secret-valid";
	if (root.removeMember(key, &value))
	{
		txin.____have_master_secret_valid = true;

		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.____master_secret_valid = BIG64(bigval);
	}

	key = "spend-secrets-valid";
	if (root.removeMember(key, &value))
	{
		txin.____have_spend_secrets_valid = true;

		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.____spend_secrets_valid = BIG64(bigval);
	}

	key = "trust-secrets-valid";
	if (root.removeMember(key, &value))
	{
		txin.____have_trust_secrets_valid = true;

		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.____trust_secrets_valid = BIG64(bigval);
	}

	key = "merkle-root";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, txin.merkle_root, output, outsize);
		if (rc) return rc;
	}
	else
		txin.merkle_root = tx.tx_merkle_root;

	key = "maximum-input-exponent";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_EXPONENT_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.invalmax = BIG64(bigval);
	}
	else if (tx.have_invalmax__)
		txin.invalmax = tx.invalmax__;
	else
		return error_missing_key(fn, key, output, outsize);

	key = "delaytime";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_DELAYTIME_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.delaytime = BIG64(bigval);
	}
	else if (tx.have_delaytime__)
		txin.delaytime = tx.delaytime__;

	rc = tx_input_common_from_json(fn, root, txin, output, outsize);
	if (rc) return rc;

	key = "commitment";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, txin._M_commitment, output, outsize);
	if (rc) return rc;

	key = "no-serial-number";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin.no_serialnum = BIG64(bigval);
	}

	key = "commitment-number";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		txin._M_commitnum = BIG64(bigval);
	}
	else if (!txin.no_serialnum)
		return error_missing_key(fn, key, output, outsize);

	key = "hashed-spendspec";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_INPUT_BITS, 0UL, txin.S_spendspec_hashed, output, outsize);
		if (rc) return rc;
	}

	if (path)
	{
		key = "merkle-path";
		if (!root.removeMember(key, &value))
			return error_missing_key(fn, key, output, outsize);
		if (!value.isArray())
			return error_not_array(fn, key, output, outsize);
		if (value.size() != TX_MERKLE_DEPTH)
			return error_num_values(fn, key, TX_MERKLE_DEPTH, output, outsize);

		for (unsigned i = 0; i < TX_MERKLE_DEPTH; ++i)
		{
			auto rc = parse_int_value(fn, key, value[i].asString(), 0, TX_FIELD_MAX, path->__M_merkle_path[i], output, outsize);
			if (rc) return rc;
		}
	}

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	return 0;
}

static CCRESULT txpay_outputs_from_json(const string& fn, const string& key, Json::Value& root, TxPay& tx, char *output, const uint32_t outsize)
{
	//cerr << "txpay_outputs_from_json " << root.size() << endl;

	if (!root.isArray())
		return error_not_array_objs(fn, key, output, outsize);

#if 0 // this is now wirebad instead of !TEST_EXTRA_ON_WIRE

	if (root.size() < 1)
		return error_num_values(fn, key, 1, output, outsize);

#endif

	if (root.size() > TX_MAXOUT)
		return error_too_many_objs(fn, key, TX_MAXOUT, output, outsize);

	if (root.size() && !tx.have_allow_restricted_addresses__)
		tx.allow_restricted_addresses = true;

	for (unsigned i = 0; i < root.size(); ++i)
	{
		if (!root[i].isObject())
			return error_not_array_objs(fn, key, output, outsize);

		tx.nout = i + 1;

		auto rc = txpay_output_from_json(fn, root[i], tx, tx.outputs[i], output, outsize);
		if (rc) return rc;

		if (tx.outputs[i].no_address && !tx.have_allow_restricted_addresses__)
			tx.allow_restricted_addresses = false;
	}

	return 0;
}

static CCRESULT txpay_inputs_from_json(const string& fn, const string& key, Json::Value& root, TxPay& tx, bool no_precheck, char *output, const uint32_t outsize)
{
	if (!root.isArray())
		return error_not_array_objs(fn, key, output, outsize);

#if 0 // this is now wirebad instead of !TEST_EXTRA_ON_WIRE

	if (tx.tag_type != CC_TYPE_MINT && root.size() < 1)
		return error_num_values(fn, key, 1, output, outsize);

#endif

	if (root.size() > TX_MAXIN)
		return error_too_many_objs(fn, key, TX_MAXIN, output, outsize);

	if (tx.tag_type == CC_TYPE_MINT && root.size() > 0)
		return error_too_many_objs(fn, key, 0, output, outsize);

	static const string key2 = "merkle-path";

	for (unsigned i = 0; i < root.size(); ++i)
	{
		if (!root[i].isObject())
			return error_not_array_objs(fn, key, output, outsize);

		TxInPath *path = NULL;

		if (json_find(root[i], key2.c_str()))
		{
			if (tx.nin_with_path == TX_MAXINPATH)
				return error_too_many_objs(fn, key2, TX_MAXINPATH, output, outsize);

			path = &tx.inpaths[tx.nin_with_path++];
			tx.inputs[i].pathnum = tx.nin_with_path;
		}

		tx.nin = i + 1;

		auto rc = txpay_input_from_json(fn, root[i], tx, tx.inputs[i], path, no_precheck, output, outsize);
		if (rc) return rc;
	}

#if 0 // this is now wirebad instead of !TEST_EXTRA_ON_WIRE

	if (tx.tag_type != CC_TYPE_MINT && tx.nin_with_path != tx.nin)
		return error_num_values(fn, key2, tx.nin, output, outsize);

#endif

	return 0;
}

static CCRESULT tx_common_from_json(const string& fn, Json::Value& root, bool from_wire, TxPay& tx, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;
	bigint_t bigval;

	key = "random-seed";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.random_seed = BIG64(bigval);
	}

	if (from_wire && TEST_EXTRA_ON_WIRE)
		return 0;

	key = "destination-chain";
	if (root.removeMember(key, &value))
	{
		tx.have_dest_chain__ = true;

		auto rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.dest_chain__ = BIG64(bigval);
	}

	key = "default-domain";
	if (root.removeMember(key, &value))
	{
		tx.have_default_domain__ = true;

		auto rc = parse_int_value(fn, key, value.asString(), TX_DOMAIN_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.default_domain__ = BIG64(bigval);
	}

	key = "acceptance-required";
	if (root.removeMember(key, &value))
	{
		tx.have_acceptance_required__ = true;

		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.acceptance_required__ = BIG64(bigval);
	}

	key = "maximum-input-exponent";
	if (root.removeMember(key, &value))
	{
		tx.have_invalmax__ = true;

		auto rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_EXPONENT_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.invalmax__ = BIG64(bigval);
	}
	else if (from_wire && !TEST_EXTRA_ON_WIRE)
		return error_missing_key(fn, key, output, outsize);

	key = "delaytime";
	if (root.removeMember(key, &value))
	{
		tx.have_delaytime__ = true;

		auto rc = parse_int_value(fn, key, value.asString(), TX_DELAYTIME_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.delaytime__ = BIG64(bigval);
	}

	key = "type";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_TYPE_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.tx_type = BIG64(bigval);
	}

	key = "source-chain";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	tx.source_chain = BIG64(bigval);

	key = "parameter-level";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_BLOCKLEVEL_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.param_level = BIG64(bigval);
	}
	else if (!from_wire)
		return error_missing_key(fn, key, output, outsize);

	key = "parameter-time";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_TIME_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.param_time = BIG64(bigval);
	}
	else if (!from_wire)
		return error_missing_key(fn, key, output, outsize);

	key = "revision";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_REVISION_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.revision = BIG64(bigval);
	}

	key = "expiration";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_TIME_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.expiration = BIG64(bigval);
	}

	key = "reference-hash";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_REFHASH_BITS, 0UL, tx.refhash, output, outsize);
		if (rc) return rc;
	}

	key = "reserved";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_RESERVED_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.reserved = BIG64(bigval);
	}

	key = "donation";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_DONATION_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.donation_fp = BIG64(bigval);
	}
	else if (!from_wire)
		return error_missing_key(fn, key, output, outsize);

	key = "minimum-output-exponent";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_EXPONENT_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	tx.outvalmin = BIG64(bigval);

	key = "maximum-output-exponent";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_EXPONENT_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	tx.outvalmax = BIG64(bigval);

	key = "allow-restricted-addresses";
	if (root.removeMember(key, &value))
	{
		tx.have_allow_restricted_addresses__ = true;

		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.allow_restricted_addresses = BIG64(bigval);
	}

	key = "merkle-root";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, tx.tx_merkle_root, output, outsize);
	if (rc) return rc;

	#if 0 // not used
	key = "output-commitment-iv-nonce";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), TX_COMMIT_IV_NONCE_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.M_commitment_iv_nonce = BIG64(bigval);
	}
	#endif

	key = "commitment-iv";
	if (root.removeMember(key, &value))
	{
		tx.override_commitment_iv__ = true;

		auto rc = parse_int_value(fn, key, value.asString(), TX_COMMIT_IV_BITS, 0UL, tx.M_commitment_iv, output, outsize);
		if (rc) return rc;
	}

	return 0;
}

CCRESULT txpay_create_finish(const string& fn, TxPay& tx, char *output, const uint32_t outsize)
{
	if (tx.tag_type == CC_TYPE_MINT)
	{
		auto rc = set_mint_inputs(tx);
		if (rc) return rc;
	}

	set_dependents(tx);

	if (!tx.no_precheck)
	{
		auto rc = txpay_precheck(fn, tx, output, outsize);
		if (rc) return rc;
	}

	auto rc = set_proof(fn, tx, output, outsize);
	if (tx.no_proof && rc == CCPROOF_ERR_NO_PROOF)
		return 0;
	if (rc) return rc;

	if (!tx.no_verify)
		return check_proof(fn, tx, output, outsize);

	return 0;
}

static CCRESULT txpay_create_from_json(const string& fn, Json::Value& root, TxPay& tx, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;
	bigint_t bigval;

	key = "no-precheck";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.no_precheck = BIG64(bigval);
	}

	key = "no-proof";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.no_proof = BIG64(bigval);
	}

	key = "no-verify";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.no_verify = BIG64(bigval);
	}

	key = "test-use-larger-zkkey";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.test_uselargerzkkey = BIG64(bigval);
	}

	key = "test-make-bad";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 32, 0UL, bigval, output, outsize);
		if (rc) return rc;
		tx.test_make_bad = BIG64(bigval);
	}

	auto rc = tx_common_from_json(fn, root, false, tx, output, outsize);
	if (rc) return rc;

	// TODO: randomize order of inputs and outputs?

	key = "outputs";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = txpay_outputs_from_json(fn, key, value, tx, output, outsize);
	if (rc) return rc;

	key = "inputs";
	if (root.removeMember(key, &value))
	{
		auto rc = txpay_inputs_from_json(fn, key, value, tx, tx.no_precheck, output, outsize);
		if (rc) return rc;
	}
	else if (tx.tag_type != CC_TYPE_MINT)
		return error_missing_key(fn, key, output, outsize);

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	return txpay_create_finish(fn, tx, output, outsize);
}

CCRESULT json_tx_create(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, root, ptx, output, outsize);
	if (rc) return rc;
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	tx_init(tx);

	if (root.size() != 1)
		return copy_error_to_output(fn, "error: json transaction must contain exactly one object", output, outsize);

	auto it = root.begin();
	auto key = it.name();
	root = *it;

	if (key == "tx-pay")
	{
		tx.tag_type = CC_TYPE_TXPAY;
	}
	else if (key == "mint")
	{
		tx.tag_type = CC_TYPE_MINT;
	}
	else
		return error_invalid_tx_type(fn, output, outsize);

	return txpay_create_from_json(fn, root,tx, output, outsize);
}

CCRESULT json_tx_verify(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, root, ptx, output, outsize);
	if (rc) return rc;
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	if (tx.struct_tag != CC_TAG_TX_STRUCT)
		return error_invalid_tx_type(fn, output, outsize);

	if (tx.tag_type == CC_TYPE_TXPAY || tx.tag_type == CC_TYPE_MINT)
		return check_proof(fn, tx, output, outsize);

	return error_invalid_tx_type(fn, output, outsize);
}

CCRESULT json_tx_to_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, root, ptx, output, outsize);
	if (rc) return rc;
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	if (tx.struct_tag != CC_TAG_TX_STRUCT)
		return error_invalid_tx_type(fn, output, outsize);

	if (tx.tag_type == CC_TYPE_TXPAY || tx.tag_type == CC_TYPE_MINT)
		return txpay_to_json(fn, tx, output, outsize);

	return error_invalid_tx_type(fn, output, outsize);
}

static CCRESULT txpay_output_to_wire(const string& fn, const TxPay& tx, const TxOut& txout, unsigned err_check, char *output, const uint32_t outsize, uint32_t &bufpos, char *binbuf, const uint32_t binsize)
{
	const bool bhex = false;

	#if TEST_EXTRA_ON_WIRE
	/*X*/ copy_to_buf(txout.no_address, 1, bufpos, binbuf, binsize, bhex);
	if (!txout.no_address)
		/*X*/ copy_to_buf(txout.addrparams.dest_chain, TX_CHAIN_BYTES, bufpos, binbuf, binsize, bhex);
	#else
	if (txout.no_address && err_check)
		return error_invalid_binary_tx_value(fn, "no-address != 0", output, outsize);
	if (txout.addrparams.dest_chain != tx.outputs[0].addrparams.dest_chain && err_check)
		return error_invalid_binary_tx_value(fn, "destination-chain values do not all match", output, outsize);
	#endif

	if (!txout.no_address)
		copy_to_buf(txout.M_address, TX_ADDRESS_BYTES, bufpos, binbuf, binsize, bhex);

	#if TEST_EXTRA_ON_WIRE
	/*X*/ copy_to_buf(txout.acceptance_required, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txout.repeat_count, sizeof(txout.repeat_count), bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txout.M_domain, TX_DOMAIN_BYTES, bufpos, binbuf, binsize, bhex);
	#else
	if (txout.acceptance_required != tx.outputs[0].acceptance_required && err_check)
		return error_invalid_binary_tx_value(fn, "acceptance-required values do not all match", output, outsize);
	if (txout.acceptance_required && err_check > 1)
		return error_invalid_binary_tx_value(fn, "acceptance-required != 0", output, outsize);
	if (txout.repeat_count && err_check)
		return error_invalid_binary_tx_value(fn, "repeat-count != 0", output, outsize);
	if (tx.wire_tag == CC_TAG_TX_XDOMAIN)
		copy_to_buf(txout.M_domain, TX_DOMAIN_BYTES, bufpos, binbuf, binsize, bhex);
	else
		CCASSERT(txout.M_domain == tx.default_domain__);
	#endif

	#if TEST_EXTRA_ON_WIRE
	/*X*/ copy_to_buf(txout.no_asset, 1, bufpos, binbuf, binsize, bhex);
	if (!txout.no_asset)
	{
		/*X*/ copy_to_buf(txout.asset_mask, TX_ASSET_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_to_buf(txout.M_asset_enc, TX_ASSET_BYTES, bufpos, binbuf, binsize, bhex);
	}

	/*X*/ copy_to_buf(txout.no_amount, 1, bufpos, binbuf, binsize, bhex);
	if (!txout.no_amount)
	{
		/*X*/ copy_to_buf(txout.amount_mask, TX_AMOUNT_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_to_buf(txout.M_amount_enc, TX_AMOUNT_BYTES, bufpos, binbuf, binsize, bhex);
	}
	#else
	if (txout.no_asset && err_check)
		return error_invalid_binary_tx_value(fn, "no-asset != 0", output, outsize);
	if (txout.no_amount && err_check)
		return error_invalid_binary_tx_value(fn, "no-amount != 0", output, outsize);

	switch (tx.tag_type)
	{
	case CC_TYPE_MINT:

		if (txout.asset_mask && err_check)
			return error_invalid_binary_tx_value(fn, "asset-mask != 0 in mint transaction", output, outsize);
		if (txout.amount_mask && err_check)
			return error_invalid_binary_tx_value(fn, "amount-mask != 0 in mint transaction", output, outsize);

		if (txout.M_asset_enc && err_check)
			return error_invalid_binary_tx_value(fn, "encrypted-asset != 0 in mint transaction", output, outsize);

		break;

	case CC_TYPE_TXPAY:
	case CC_TYPE_XCX_SIMPLE_BUY:
	case CC_TYPE_XCX_SIMPLE_SELL:
	case CC_TYPE_XCX_MINING_TRADE:
	case CC_TYPE_XCX_NAKED_BUY:
	case CC_TYPE_XCX_NAKED_SELL:

		if (txout.asset_mask != TX_ASSET_WIRE_MASK && err_check)
			return error_invalid_binary_tx_value(fn, "asset-mask != all 1's", output, outsize);
		if (txout.amount_mask != TX_AMOUNT_MASK && err_check)
			return error_invalid_binary_tx_value(fn, "amount-mask != all 1's", output, outsize);

		if ((txout.M_asset_enc & ~TX_ASSET_WIRE_MASK) && err_check)
			return error_invalid_binary_tx_value(fn, "encrypted-asset upper bits != all 0's", output, outsize);

		copy_to_buf(txout.M_asset_enc, TX_ASSET_WIRE_BYTES, bufpos, binbuf, binsize, bhex);

		break;

	default:
		return error_invalid_tx_type(fn, output, outsize);
	}

	if ((txout.M_amount_enc & ~TX_AMOUNT_MASK) && err_check)
		return error_invalid_binary_tx_value(fn, "encrypted-amount upper bits != all 0's", output, outsize);

	copy_to_buf(txout.M_amount_enc, TX_AMOUNT_BYTES, bufpos, binbuf, binsize, bhex);
	#endif

	copy_to_buf(txout.M_commitment, TX_COMMITMENT_BYTES, bufpos, binbuf, binsize, bhex);

	return 0;
}

static CCRESULT txpay_output_from_wire(const TxPay& tx, TxOut& txout, uint32_t &bufpos, char *binbuf, const uint32_t binsize)
{
	const bool bhex = false;

	#if TEST_EXTRA_ON_WIRE
	/*X*/ copy_from_buf(txout.no_address, 1, bufpos, binbuf, binsize, bhex);
	if (!txout.no_address)
		/*X*/ copy_from_buf(txout.addrparams.dest_chain, TX_CHAIN_BYTES, bufpos, binbuf, binsize, bhex);
	#else
	txout.addrparams.dest_chain = tx.dest_chain__;
	#endif

	if (!txout.no_address)
		copy_from_buf(txout.M_address, TX_ADDRESS_BYTES, bufpos, binbuf, binsize, bhex);

	#if TEST_EXTRA_ON_WIRE
	/*X*/ copy_from_buf(txout.acceptance_required, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txout.repeat_count, sizeof(txout.repeat_count), bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txout.M_domain, TX_DOMAIN_BYTES, bufpos, binbuf, binsize, bhex);
	#else
	txout.acceptance_required = tx.acceptance_required__;
	if (tx.wire_tag == CC_TAG_TX_XDOMAIN)
		copy_from_buf(txout.M_domain, TX_DOMAIN_BYTES, bufpos, binbuf, binsize, bhex);
	else
		txout.M_domain = tx.default_domain__;
	#endif

	#if TEST_EXTRA_ON_WIRE
	/*X*/ copy_from_buf(txout.no_asset, 1, bufpos, binbuf, binsize, bhex);
	if (!txout.no_asset)
	{
		/*X*/ copy_from_buf(txout.asset_mask, TX_ASSET_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_from_buf(txout.M_asset_enc, TX_ASSET_BYTES, bufpos, binbuf, binsize, bhex);
	}

	/*X*/ copy_from_buf(txout.no_amount, 1, bufpos, binbuf, binsize, bhex);
	if (!txout.no_amount)
	{
		/*X*/ copy_from_buf(txout.amount_mask, TX_AMOUNT_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_from_buf(txout.M_amount_enc, TX_AMOUNT_BYTES, bufpos, binbuf, binsize, bhex);
	}
	#else
	if (tx.tag_type != CC_TYPE_MINT)
	{
		txout.asset_mask = TX_ASSET_WIRE_MASK;
		txout.amount_mask = TX_AMOUNT_MASK;

		copy_from_buf(txout.M_asset_enc, TX_ASSET_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
	}

	copy_from_buf(txout.M_amount_enc, TX_AMOUNT_BYTES, bufpos, binbuf, binsize, bhex);
	#endif

	copy_from_buf(txout.M_commitment, TX_COMMITMENT_BYTES, bufpos, binbuf, binsize, bhex);

	return 0;
}

static CCRESULT txpay_input_to_wire(const string& fn, const TxPay& tx, const TxIn& txin, unsigned err_check, char *output, const uint32_t outsize, uint32_t &bufpos, char *binbuf, const uint32_t binsize)
{
	const bool bhex = false;

	#if TEST_EXTRA_ON_WIRE
	/*X*/ copy_to_buf(txin.enforce_master_secret, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txin.enforce_spend_secrets, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txin.enforce_trust_secrets, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txin.enforce_freeze, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txin.enforce_unfreeze, 1, bufpos, binbuf, binsize, bhex);
	#else
	if (txin.enforce_master_secret && err_check)
		return error_invalid_binary_tx_value(fn, "enforce-master-secret != 0", output, outsize);
	if (txin.enforce_spend_secrets && err_check)
		return error_invalid_binary_tx_value(fn, "enforce-spend-secrets != 0", output, outsize);
	if (txin.enforce_trust_secrets != 1 && err_check)
		return error_invalid_binary_tx_value(fn, "enforce-trust-secret != 1", output, outsize);
	if (txin.enforce_freeze && err_check)
		return error_invalid_binary_tx_value(fn, "enforce-freeze != 0", output, outsize);
	if (txin.enforce_unfreeze && err_check)
		return error_invalid_binary_tx_value(fn, "enforce-unfreeze != 0", output, outsize);
	#endif

	#if TEST_EXTRA_ON_WIRE
	if (txin.pathnum)
		/*X*/ copy_to_buf(txin.merkle_root, TX_MERKLE_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txin.invalmax, TX_AMOUNT_EXPONENT_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txin.delaytime, TX_DELAYTIME_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txin.M_domain, TX_DOMAIN_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txin.no_serialnum, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(txin.S_spendspec_hashed, sizeof(txin.S_spendspec_hashed), bufpos, binbuf, binsize, bhex);
	#else
	if (txin.merkle_root != tx.inputs[0].merkle_root && err_check)
		return error_invalid_binary_tx_value(fn, "merkle-root values do not all match", output, outsize);
	if (txin.invalmax != tx.inputs[0].invalmax && err_check)
		return error_invalid_binary_tx_value(fn, "maximum-input-exponent values do not all match", output, outsize);
	if (txin.delaytime != tx.inputs[0].delaytime && err_check)
		return error_invalid_binary_tx_value(fn, "delaytime values do not all match", output, outsize);
	if (txin.delaytime && err_check > 1)
		return error_invalid_binary_tx_value(fn, "delaytime != 0", output, outsize);
	if (tx.wire_tag == CC_TAG_TX_XDOMAIN)
		copy_to_buf(txin.M_domain, TX_DOMAIN_BYTES, bufpos, binbuf, binsize, bhex);
	else
		CCASSERT(txin.M_domain == tx.default_domain__);
	if (txin.no_serialnum && err_check)
		return error_invalid_binary_tx_value(fn, "no-serialnum != 0", output, outsize);
	if (txin.S_spendspec_hashed && err_check)
		return error_invalid_binary_tx_value(fn, "hashed-spendspec != 0", output, outsize);
	#endif

	if (!txin.pathnum)
	{
		copy_to_buf(txin._M_commitment, TX_COMMITMENT_BYTES, bufpos, binbuf, binsize, bhex);
		if (!txin.no_serialnum)
			copy_to_buf(txin._M_commitnum, TX_COMMITNUM_BYTES, bufpos, binbuf, binsize, bhex);
	}

	if (!txin.no_serialnum)
		copy_to_buf(txin.S_serialnum, TX_SERIALNUM_BYTES, bufpos, binbuf, binsize, bhex);

	bigint_t bigval = txin.S_hashkey;
	bigint_mask(bigval, TX_HASHKEY_WIRE_BITS);
	if (bigval != txin.S_hashkey && err_check)
		return error_invalid_binary_tx_value(fn, "hashkey exceeds wire bytes", output, outsize);
	copy_to_buf(txin.S_hashkey, TX_HASHKEY_WIRE_BYTES, bufpos, binbuf, binsize, bhex);

	//cout << "published hashkey = " << hex << txin.S_hashkey << dec << endl;

	#if 0
	static uint16_t nsigkeys = 0;	// always zero for now
	copy_to_buf(nsigkeys, 1, bufpos, binbuf, binsize, bhex);
	#endif

	return 0;
}

static CCRESULT txpay_input_from_wire(const TxPay& tx, TxIn& txin, uint32_t &bufpos, char *binbuf, const uint32_t binsize)
{
	const bool bhex = false;

	#if TEST_EXTRA_ON_WIRE
	/*X*/ copy_from_buf(txin.enforce_master_secret, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txin.enforce_spend_secrets, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txin.enforce_trust_secrets, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txin.enforce_freeze, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txin.enforce_unfreeze, 1, bufpos, binbuf, binsize, bhex);
	if (txin.pathnum)
		/*X*/ copy_from_buf(txin.merkle_root, TX_MERKLE_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txin.invalmax, TX_AMOUNT_EXPONENT_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txin.delaytime, TX_DELAYTIME_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txin.M_domain, TX_DOMAIN_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txin.no_serialnum, 1, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(txin.S_spendspec_hashed, sizeof(txin.S_spendspec_hashed), bufpos, binbuf, binsize, bhex);
	#else
	txin.enforce_trust_secrets = 1;
	txin.merkle_root = tx.tx_merkle_root;
	txin.invalmax = tx.invalmax__;
	txin.delaytime = tx.delaytime__;
	if (tx.wire_tag == CC_TAG_TX_XDOMAIN)
		copy_from_buf(txin.M_domain, TX_DOMAIN_BYTES, bufpos, binbuf, binsize, bhex);
	else
		txin.M_domain = tx.default_domain__;
	#endif

	if (!txin.pathnum)
	{
		copy_from_buf(txin._M_commitment, TX_COMMITMENT_BYTES, bufpos, binbuf, binsize, bhex);
		if (!txin.no_serialnum)
			copy_from_buf(txin._M_commitnum, TX_COMMITNUM_BYTES, bufpos, binbuf, binsize, bhex);
	}

	if (!txin.no_serialnum)
		copy_from_buf(txin.S_serialnum, TX_SERIALNUM_BYTES, bufpos, binbuf, binsize, bhex);

	copy_from_buf(txin.S_hashkey, TX_HASHKEY_WIRE_BYTES, bufpos, binbuf, binsize, bhex);

	#if 0
	static uint16_t nsigkeys = 0;
	copy_from_buf(nsigkeys, 1, bufpos, binbuf, binsize, bhex);
	if (nsigkeys)
	{
		if (TEST_SHOW_WIRE_ERRORS) cerr << "error nsigkeys " << nsigkeys << endl;

		return -1;
	}
	#endif

	return 0;
}

uint64_t txpay_param_level_from_wire(const CCObject *obj)
{
	if (!obj->IsValid())
		return -1;

	if (Xtx::TypeHasBareMsg(obj->ObjType()))
		return 0;

	unsigned param_level_offset = 0;

	if (obj->BodySize() < param_level_offset + TX_BLOCKLEVEL_BYTES)
		return -1;

	uint64_t param_level = *(const uint64_t*)(obj->BodyPtr() + param_level_offset);
	if (TX_BLOCKLEVEL_BYTES < sizeof(uint64_t))
		param_level &= ((uint64_t)1 << (TX_BLOCKLEVEL_BYTES * CHAR_BIT)) - 1;

	return param_level;
}

static CCRESULT txpay_body_to_wire(const string& fn, TxPay& tx, unsigned err_check, char *output, const uint32_t outsize, uint32_t &bufpos, char *binbuf, const uint32_t binsize)
{
	const bool bhex = false;

	// param_level must come first because txpay_param_level_from_wire() looks for it there
	copy_to_buf(tx.param_level, TX_BLOCKLEVEL_BYTES, bufpos, binbuf, binsize, bhex);

	copy_to_buf(tx.zkproof, sizeof(tx.zkproof) - 1, bufpos, binbuf, binsize, bhex);

	if (tx.tag_type != CC_TYPE_MINT)
		copy_to_buf(tx.zkkeyid, 1, bufpos, binbuf, binsize, bhex);
	else if (tx.zkkeyid != TX_MINT_ZKKEY_ID && err_check)
		return error_invalid_binary_tx_value(fn, "invalid proof key id for mint transaction", output, outsize);

	#if TEST_EXTRA_ON_WIRE
	/*X*/ copy_to_buf(tx.param_time, TX_TIME_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(tx.tx_type, TX_TYPE_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(tx.source_chain, TX_CHAIN_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(tx.revision, TX_REVISION_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(tx.expiration, TX_TIME_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(tx.refhash, TX_REFHASH_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_to_buf(tx.reserved, TX_RESERVED_BYTES, bufpos, binbuf, binsize, bhex);
	#endif

	copy_to_buf(tx.donation_fp, TX_DONATION_BYTES, bufpos, binbuf, binsize, bhex);

	// TODO: add tx type that has delaytime on wire

	CCASSERT(tx.nin >= tx.nin_with_path);
	uint16_t nin_without_path = (unsigned)(tx.nin - tx.nin_with_path);

	// TODO: make special tag for 2-in 2-out

	uint8_t nadj;

	switch (tx.tag_type)
	{
	case CC_TYPE_TXPAY:
	case CC_TYPE_XCX_SIMPLE_BUY:
	case CC_TYPE_XCX_SIMPLE_SELL:
	case CC_TYPE_XCX_MINING_TRADE:
	case CC_TYPE_XCX_NAKED_BUY:
	case CC_TYPE_XCX_NAKED_SELL:

		#if TEST_EXTRA_ON_WIRE
		/*X*/ copy_to_buf(tx.nout, 1, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_to_buf(tx.nin_with_path, 1, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_to_buf(nin_without_path, 1, bufpos, binbuf, binsize, bhex);
		#else
		if (!tx.nout && err_check)
			return error_invalid_binary_tx_value(fn, "# outputs = 0", output, outsize);
		if (!tx.nin && err_check)
			return error_invalid_binary_tx_value(fn, "# inputs = 0", output, outsize);
		if (nin_without_path && err_check)
			return error_invalid_binary_tx_value(fn, "input without Merkle paths", output, outsize);

		// compress and omit nin_without_path
		CCASSERT(tx.nout > 0);
		CCASSERT(tx.nin_with_path > 0);
		CCASSERT(nin_without_path == 0);
		CCASSERT(tx.nout <= 16);
		CCASSERT(tx.nin_with_path <= 16);
		nadj = ((tx.nout - 1) << 4) | (tx.nin_with_path - 1);
		copy_to_buf(nadj, 1, bufpos, binbuf, binsize, bhex);
		#endif

		break;

	case CC_TYPE_MINT:

		if (tx.nout != TX_MINT_NOUT && err_check)
			return error_invalid_binary_tx_value(fn, "invalid # outputs for mint transaction", output, outsize);
		if (tx.nin != 1 && err_check)
			return error_invalid_binary_tx_value(fn, "# inputs != 1 for mint transaction", output, outsize);

		break;

	default:
		return error_invalid_tx_type(fn, output, outsize);
	}

	#if TEST_EXTRA_ON_WIRE
	if (tx.nout)
	{
		/*X*/ copy_to_buf(tx.outvalmin, TX_AMOUNT_EXPONENT_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_to_buf(tx.outvalmax, TX_AMOUNT_EXPONENT_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ //copy_to_buf(tx.tx_merkle_root, TX_MERKLE_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ //copy_to_buf(tx.M_commitment_iv_nonce, TX_COMMIT_IV_NONCE_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_to_buf(tx.M_commitment_iv, TX_COMMIT_IV_BYTES, bufpos, binbuf, binsize, bhex);
	}

	/*X*/ copy_to_buf(tx.allow_restricted_addresses, 1, bufpos, binbuf, binsize, bhex);

	#endif

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		auto rc = txpay_output_to_wire(fn, tx, tx.outputs[i], err_check, output, outsize, bufpos, binbuf, binsize);
		if (rc) return rc;
	}

	unsigned i, count;

	if (tx.tag_type != CC_TYPE_MINT)
	{
		for (i = count = 0; i < tx.nin; ++i)
		{
			unsigned pathnum = tx.inputs[i].pathnum;
			if (!pathnum)
				continue;

			auto rc = txpay_input_to_wire(fn, tx, tx.inputs[i], err_check, output, outsize, bufpos, binbuf, binsize);
			if (rc) return rc;

			++count;
		}

		CCASSERT(count == tx.nin_with_path);

		for (i = count = 0; i < tx.nin; ++i)
		{
			unsigned pathnum = tx.inputs[i].pathnum;
			if (pathnum)
				continue;

			auto rc = txpay_input_to_wire(fn, tx, tx.inputs[i], err_check, output, outsize, bufpos, binbuf, binsize);
			if (rc) return rc;

			++count;
		}

		CCASSERT(count == nin_without_path);
	}

	return 0;
}

CCRESULT txpay_to_wire(const string& fn, TxPay& tx, unsigned err_check, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize, uint32_t *pbufpos)
{
	uint32_t lpos;
	uint32_t& bufpos = (pbufpos ? *pbufpos : lpos);

	bufpos = 0;
	const bool bhex = false;

	CCASSERT(sizeof(bufpos) == sizeof(CCObject::Header::size));

	copy_to_buf(bufpos, sizeof(bufpos), bufpos, binbuf, binsize, bhex);  // save space for size word

	tx.wire_tag = CCObject::TypeToWireTag(tx.tag_type);

	if (!tx.wire_tag || tx.wire_tag == CC_TAG_BLOCK)
		return error_invalid_tx_type(fn, output, outsize);

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		if (tx.wire_tag == CC_TAG_TX && tx.outputs[i].M_domain != tx.default_domain__)
			tx.wire_tag = CC_TAG_TX_XDOMAIN;
	}

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		if (tx.wire_tag == CC_TAG_TX && tx.inputs[i].M_domain != tx.default_domain__)
			tx.wire_tag = CC_TAG_TX_XDOMAIN;
	}

	copy_to_buf(tx.wire_tag, sizeof(tx.wire_tag), bufpos, binbuf, binsize, bhex);
	//cerr << "txpay_to_wire wire_tag " << hex << tx.wire_tag << dec << endl;

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(zero_pow, sizeof(zero_pow), bufpos, binbuf, binsize, bhex);

	if (!Xtx::TypeHasBareMsg(tx.tag_type))
	{
		auto rc = txpay_body_to_wire(fn, tx, err_check, output, outsize, bufpos, binbuf, binsize);
		if (rc) return rc;
	}

	if (tx.append_data_length)
	{
		tx.append_wire_offset = bufpos;

		copy_to_bufp(tx.append_data.data(), tx.append_data_length, bufpos, binbuf, binsize, bhex);
	}

	//cerr << "txpay_to_wire append_data_length " << tx.append_data_length << " total nbytes " << bufpos << endl;

	if (bufpos > binsize)
		return error_buffer_overflow(fn, output, outsize, bufpos);

	memcpy(binbuf, &bufpos, sizeof(bufpos));

#if TEST_SKIP_ZKPROOFS
	if (!Xtx::TypeHasBareMsg(tx.tag_type))
	{
		// replace the zkproof with a hash of the binary tx
		unsigned plevel = sizeof(CCObject::Header) + sizeof(zero_pow);
		unsigned zoff = plevel + TX_BLOCKLEVEL_BYTES;
		unsigned hstart = zoff + 2*64;
		//cerr << "hashing " << bufpos - hstart << " bytes " << buf2hex(binbuf + hstart, bufpos - hstart) << endl;
		blake2b(binbuf + zoff, 64, binbuf + plevel, TX_BLOCKLEVEL_BYTES, binbuf + hstart, bufpos - hstart);
	}
#endif

	tx.have_objid = true;
	CCObject::ComputeMessageObjId(binbuf, &tx.objid);

	//cerr << "txpay_to_wire nbytes " << bufpos << " data " << buf2hex(binbuf, bufpos) << endl;

	return 0;
}

static CCRESULT txpay_body_from_wire(TxPay& tx, uint32_t &bufpos, char *binbuf, const uint32_t binsize)
{
	const bool bhex = false;

	copy_from_buf(tx.param_level, TX_BLOCKLEVEL_BYTES, bufpos, binbuf, binsize, bhex);

	copy_from_buf(tx.zkproof, sizeof(tx.zkproof) - 1, bufpos, binbuf, binsize, bhex);

	if (tx.tag_type == CC_TYPE_MINT)
		tx.zkkeyid = TX_MINT_ZKKEY_ID;
	else
		copy_from_buf(tx.zkkeyid, 1, bufpos, binbuf, binsize, bhex);

	#if TEST_EXTRA_ON_WIRE
	/*X*/ copy_from_buf(tx.param_time, TX_TIME_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(tx.tx_type, TX_TYPE_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(tx.source_chain, TX_CHAIN_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(tx.revision, TX_REVISION_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(tx.expiration, TX_TIME_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(tx.refhash, TX_REFHASH_BYTES, bufpos, binbuf, binsize, bhex);
	/*X*/ copy_from_buf(tx.reserved, TX_RESERVED_BYTES, bufpos, binbuf, binsize, bhex);
	#endif

	copy_from_buf(tx.donation_fp, TX_DONATION_BYTES, bufpos, binbuf, binsize, bhex);

	uint16_t nin_without_path = 0;

	switch (tx.tag_type)
	{
	case CC_TYPE_TXPAY:
	case CC_TYPE_XCX_SIMPLE_BUY:
	case CC_TYPE_XCX_SIMPLE_SELL:
	case CC_TYPE_XCX_MINING_TRADE:
	case CC_TYPE_XCX_NAKED_BUY:
	case CC_TYPE_XCX_NAKED_SELL:

		#if TEST_EXTRA_ON_WIRE
		/*X*/ copy_from_buf(tx.nout, 1, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_from_buf(tx.nin_with_path, 1, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_from_buf(nin_without_path, 1, bufpos, binbuf, binsize, bhex);
		#else
		// compress and omit nin_without_path
		uint8_t nadj;
		copy_from_buf(nadj, 1, bufpos, binbuf, binsize, bhex);
		tx.nout = (nadj >> 4) + 1;
		tx.nin_with_path = (nadj & 15) + 1;
		#endif
		tx.nin = tx.nin_with_path + nin_without_path;
		break;

	case CC_TYPE_MINT:
		tx.nout = TX_MINT_NOUT;
		break;

	default:
		if (TEST_SHOW_WIRE_ERRORS) cerr << "invalid transaction type " << tx.tag_type << endl;

		return -1;
	}

	if (tx.nout > TX_MAXOUT)
	{
		if (TEST_SHOW_WIRE_ERRORS) cerr << "error tx.nout " << tx.nout << " > TX_MAXOUT " << TX_MAXOUT << endl;

		return -1;
	}

	if (tx.nin > TX_MAXIN)
	{
		if (TEST_SHOW_WIRE_ERRORS) cerr << "error tx.nin " << tx.nin << " > TX_MAXIN " << TX_MAXIN << endl;

		return -1;
	}

	if (tx.nin_with_path > TX_MAXINPATH)
	{
		if (TEST_SHOW_WIRE_ERRORS) cerr << "error tx.nin_with_path " << tx.nin_with_path << " > TX_MAXINPATH " << TX_MAXINPATH << endl;

		return -1;
	}

	#if TEST_EXTRA_ON_WIRE
	if (tx.nout)
	{
		/*X*/ copy_from_buf(tx.outvalmin, TX_AMOUNT_EXPONENT_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_from_buf(tx.outvalmax, TX_AMOUNT_EXPONENT_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ //copy_from_buf(tx.tx_merkle_root, TX_MERKLE_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ //copy_from_buf(tx.M_commitment_iv_nonce, TX_COMMIT_IV_NONCE_BYTES, bufpos, binbuf, binsize, bhex);
		/*X*/ copy_from_buf(tx.M_commitment_iv, TX_COMMIT_IV_BYTES, bufpos, binbuf, binsize, bhex);
		tx.override_commitment_iv__ = 1;
	}

	/*X*/ copy_from_buf(tx.allow_restricted_addresses, 1, bufpos, binbuf, binsize, bhex);

	#else
	if (tx.nout && !tx.have_allow_restricted_addresses__)
		tx.allow_restricted_addresses = true;
	#endif

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		auto rc = txpay_output_from_wire(tx, tx.outputs[i], bufpos, binbuf, binsize);
		if (rc) return rc;

		if (tx.outputs[i].no_address && !tx.have_allow_restricted_addresses__ && !TEST_EXTRA_ON_WIRE)
			tx.allow_restricted_addresses = false;
	}

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		if (i < tx.nin_with_path)
			tx.inputs[i].pathnum = i + 1;

		auto rc = txpay_input_from_wire(tx, tx.inputs[i], bufpos, binbuf, binsize);
		if (rc) return rc;
	}

	if (tx.tag_type == CC_TYPE_MINT)
	{
		auto rc = set_mint_inputs(tx);

		if (rc)
		{
			if (TEST_SHOW_WIRE_ERRORS) cerr << "error set_mint_inputs rc " << rc << endl;

			return rc;
		}
	}

	return 0;
}

CCRESULT json_tx_to_wire(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	if (!binbuf)
		return error_requires_binary_buffer(fn, output, outsize);

	*(uint32_t*)binbuf = 0;

	string key;
	Json::Value value;
	bigint_t bigval;

	/* error-check:
		0  = report anything blockchain won't accept
		1  = allow values that can be set through JSON interface when tx is extracted from wire
		2+ = no error checking
	*/

	unsigned err_check = -1;

	key = "error-check";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 2, 0UL, bigval, output, outsize);
		if (rc) return rc;
		err_check = BIG64(bigval);
	}

	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, root, ptx, output, outsize);
	if (rc) return rc;
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	if (tx.struct_tag != CC_TAG_TX_STRUCT)
		return error_invalid_tx_type(fn, output, outsize);

	if (tx.tag_type == CC_TYPE_TXPAY || tx.tag_type == CC_TYPE_MINT)
		return txpay_to_wire(fn, tx, err_check, output, outsize, binbuf, binsize);

	return error_invalid_tx_type(fn, output, outsize);
}

static CCRESULT tx_add_from_wire(TxPay& tx, char *binbuf, const uint32_t binsize)
{
	uint32_t wiresize = 0;
	uint32_t bufpos = 0;
	const bool bhex = false;

	//cerr << "tx_add_from_wire nbytes " << binsize << " data " << buf2hex(binbuf, binsize) << endl;

	copy_from_buf(wiresize, sizeof(wiresize), bufpos, binbuf, binsize, bhex);
	if (wiresize > binsize)
	{
		//cerr << "error wiresize " << wiresize << " > binsize " << binsize << endl;

		return -1;
	}

	copy_from_buf(tx.wire_tag, sizeof(tx.wire_tag), bufpos, binbuf, binsize, bhex);

	tx.tag_type = CCObject::ObjType(tx.wire_tag);

	if (!tx.tag_type || tx.tag_type == CC_TYPE_BLOCK || tx.tag_type == CC_TYPE_VOID)
	{
		tx.tag_type = CC_TYPE_VOID;

		return -1;
	}

	tx.tx_type = tx.tag_type;

	//cerr << "wire_tag " << hex << tx.wire_tag << dec << " tag_type " << tx.tag_type << " tx_type " << tx.tx_type << endl;

	CCASSERT(bufpos == sizeof(CCObject::Header));

	if (CCObject::HasPOW(tx.wire_tag))
		bufpos += TX_POW_SIZE;

	tx.wire_tag = CCObject::WireTag(tx.wire_tag);

#if TEST_SKIP_ZKPROOFS
	unsigned plevel = bufpos;
#endif

	if (!Xtx::TypeHasBareMsg(tx.tag_type))
	{
		auto rc = txpay_body_from_wire(tx, bufpos, binbuf, binsize);
		if (rc)
		{
			if (TEST_SHOW_WIRE_ERRORS) cerr << "error txpay_body_from_wire " << rc << endl;

			return rc;
		}
	}

	auto nappend = wiresize - bufpos;

	//cerr << "tx_add_from_wire end bufpos " << bufpos << " wiresize " << wiresize << " nappend " << nappend << endl;

	if (nappend)
	{
		if (!Xtx::TypeIsXtx(tx.tag_type))
		{
			if (TEST_SHOW_WIRE_ERRORS) cerr << "error tag_type " << tx.tag_type << " bufpos " << bufpos << " != binsize " << binsize << endl;

			return -1;
		}

		tx.append_wire_offset = bufpos;
		tx.append_data_length = nappend;

		//cerr << "tx_add_from_wire append_wire_offset " << tx.append_wire_offset << " append_data_length " << tx.append_data_length << " wiresize " << wiresize << " binsize " << binsize << endl;

		copy_from_bufp(tx.append_data.data(), tx.append_data.size(), nappend, bufpos, binbuf, binsize, bhex);

		set_refhash_from_append_data(tx);
	}

	//cerr << "tx_add_from_wire end bufpos " << bufpos << " wiresize " << wiresize << " binsize " << binsize << endl;

	if (bufpos > binsize)
	{
		if (TEST_SHOW_WIRE_ERRORS) cerr << "error bufpos " << bufpos << " > binsize " << binsize << endl;

		return 1;
	}

#if TEST_SKIP_ZKPROOFS
	if (!Xtx::TypeHasBareMsg(tx.tag_type))
	{
		// place hash of the binary tx into the zkproof
		for (unsigned i = 0; i < 64; ++i)
		{
			if (*((char*)&tx.zkproof + 64 + i))
				return 0;	// "proof" has been tampered with, so don't fill in with tx hash
		}
		unsigned hstart = plevel + TX_BLOCKLEVEL_BYTES + 2*64;
		//cerr << "hashing " << bufpos - hstart << " bytes " << buf2hex(binbuf + hstart, bufpos - hstart) << endl;
		blake2b((char*)&tx.zkproof + 64, 64, binbuf + plevel, TX_BLOCKLEVEL_BYTES, binbuf + hstart, bufpos - hstart);
		//cerr << buf2hex((char*)&tx.zkproof + 0*64, 64) << endl;
		//cerr << buf2hex((char*)&tx.zkproof + 1*64, 64) << endl;
		//cerr << "-- " << buf2hex((char*)&tx.zkproof + 2*64,  8) << endl;
	}
#endif

	return 0;
}

CCRESULT tx_from_wire(TxPay& tx, char *binbuf, const uint32_t binsize)
{
	tx_init(tx);

	return tx_add_from_wire(tx, binbuf, binsize);
}

CCRESULT json_tx_from_wire(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	if (!binbuf)
		return error_requires_binary_buffer(fn, output, outsize);

	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, root, ptx, NULL, 0);
	if (rc) return rc;
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	tx_init(tx);

	rc = tx_common_from_json(fn, root, true, tx, output, outsize);
	if (rc) return rc;

	rc = tx_add_from_wire(tx, binbuf, binsize);

	if (rc > 0)
		return error_buffer_overflow(fn, output, outsize);
	if (rc)
		return error_invalid_binary_tx(fn, output, outsize);

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	tx_set_commit_iv(tx);

	return 0;
}

// TODO: move POW functions to a new source file

CCRESULT json_work_reset(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	if (!binbuf)
		return error_requires_binary_buffer(fn, output, outsize);

	string key;
	Json::Value value;
	bigint_t bigval;

	key = "timestamp";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) return rc;
	uint64_t timestamp = BIG64(bigval);

	//cerr << "json_work_reset timestamp " << timestamp << endl;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	return tx_reset_work(fn, timestamp, binbuf, binsize);
}

CCRESULT json_work_add(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	if (!binbuf)
		return error_requires_binary_buffer(fn, output, outsize);

	string key;
	Json::Value value;
	bigint_t bigval;

	key = "index";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, (unsigned long)(TX_POW_NPROOFS - 1), bigval, output, outsize);
	if (rc) return rc;
	unsigned proof_index = BIG64(bigval);

	key = "iterations";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) return rc;
	uint64_t iterations = BIG64(bigval);

	key = "difficulty";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) return rc;
	uint64_t proof_difficulty = BIG64(bigval);

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	return tx_set_work(fn, proof_index, 1, iterations, proof_difficulty, binbuf, binsize);
}

// non-json interface

CCRESULT tx_reset_work(const string& fn, uint64_t timestamp, char *binbuf, const uint32_t binsize)
{
	CCASSERT(binbuf);
	CCASSERT(binsize);

	auto pheader = (const CCObject::Header *)binbuf;
	auto msize = pheader->size;
	const unsigned data_offset = sizeof(CCObject::Header) + TX_POW_SIZE;

	if (msize > binsize)
	{
		cerr << "tx_reset_work error msg size " << msize << " > bufsize " << binsize << hex << " tag " << hex << pheader->tag << dec << endl;
		return -1;
	}

	if (msize < data_offset)
	{
		cerr << "tx_reset_work error msg size " << msize << " < data_offset " << data_offset << hex << " tag " << hex << pheader->tag << dec << endl;
		return -1;
	}

	auto ptime = (uint64_t*)(binbuf + sizeof(CCObject::Header));

	memset(ptime, 0, TX_POW_SIZE);

	*ptime = timestamp;

	//cerr << "tx_reset_work timestamp " << timestamp << endl;

	return 0;
}

CCRESULT tx_check_timestamp(uint64_t timestamp, unsigned past_allowance, unsigned future_allowance)
{
	uint64_t now = unixtime();

	int64_t age = now - timestamp;

	//cerr << "tx_check_timestamp timestamp " << timestamp << " now " << now << " age " << age << " past_allowance " << past_allowance << endl;

	if (age > past_allowance || (age < 0 && -age > future_allowance))
		return -1;
	else
		return 0;
}

CCRESULT tx_set_work(const string& fn, unsigned proof_start, unsigned proof_count, uint64_t iter_count, uint64_t proof_difficulty, char *binbuf, const uint32_t binsize)
{
#if TEST_SEQ_TX_OID
	return 0;
#endif

	auto pheader = (const CCObject::Header *)binbuf;
	const unsigned data_offset = sizeof(CCObject::Header) + TX_POW_SIZE;

	//cerr << "tx_set_work binsize " << binsize << " tx size " << pheader->size << " tag " << pheader->tag << dec << endl;

	if (pheader->size > binsize)
		return -1;

	if (pheader->size < data_offset)
		return -1;

	ccoid_t txhash;

	auto rc = blake2b(&txhash, sizeof(txhash), &pheader->tag, sizeof(pheader->tag), binbuf + data_offset, pheader->size - data_offset);
	CCASSERTZ(rc);

	//cerr << "tx_set_work hashed " << pheader->size - data_offset << " bytes starting with " << hex << *(uint64_t*)(binbuf + data_offset) << " result " << *(uint64_t*)(&txhash) << dec << endl;

	return tx_set_work_internal(binbuf, &txhash, proof_start, proof_count, iter_count, proof_difficulty);
}

CCRESULT tx_set_work_internal(char *binbuf, const void *txhash, unsigned proof_start, unsigned proof_count, uint64_t iter_count, uint64_t proof_difficulty)
{
#if TEST_SEQ_TX_OID
	return 0;
#endif

	//auto t0 = ccticks();
	//cerr << "tx_set_work " << buf2hex(txhash, CC_OID_TRACE_SIZE) << endl;

	CCRESULT result = 0;

	if (!proof_difficulty)
		return result;

	uint64_t hashkey[2];
	hashkey[0] = *(uint64_t*)(binbuf + sizeof(CCObject::Header));	// hashkey[0] = the timestamp

	for (unsigned proof_index = proof_start; proof_index < proof_start + proof_count; ++proof_index)
	{
		CCASSERT(proof_index < TX_POW_NPROOFS);

		auto pnonce = (uint64_t*)(binbuf + sizeof(CCObject::Header) + sizeof(uint64_t) + proof_index * TX_POW_NONCE_SIZE);
		uint64_t iter_start = *pnonce & TX_POW_NONCE_MASK;

		const uint64_t iter_limit = TX_POW_NONCE_MASK - 1;
		uint64_t iter_end = iter_start + iter_count - 1;
		if (iter_end > iter_limit || iter_limit - iter_count < iter_start)
			iter_end = iter_limit;

		//cerr << hex << "tx_set_work proof_index " << proof_index << " iter_start " << iter_start << " iter_count " << iter_count << " iter_end " << iter_end << " proof_difficulty " << proof_difficulty << dec << endl;

		if (proof_index)
			hashkey[0] = *(pnonce - 1);	// hashkey[0] = 64 bits that includes the prior nonce, to prevent computing nonces in parallel

		uint64_t nonce;
		for (nonce = iter_start; nonce <= iter_end; ++nonce)
		{
			if (g_shutdown)
				return -3;

			hashkey[1] = ((uint64_t)proof_index << TX_POW_NONCE_BITS) | nonce;

			auto hash = siphash(txhash, sizeof(ccoid_t), hashkey, sizeof(hashkey));

			if (nonce == iter_end || hash < proof_difficulty)
			{
				//cerr << hex << "tx_set_work nonce " << nonce << " hash " << hash << " proof_difficulty " << proof_difficulty << dec << endl;
			}

			if (hash < proof_difficulty)
				break;
		}

		//cerr << hex << "tx_set_work proof_index " << proof_index << " iter_start " << iter_start << " iter_end " << iter_end << " nonce " << nonce << dec << endl;

		*pnonce &= ~((uint64_t)TX_POW_NONCE_MASK);
		*pnonce |= nonce;

		if (nonce > iter_limit)
			return -2;
		else if (nonce > iter_end)
			result = 1;
	}

	//lock_guard<mutex> lock(g_cerr_lock);
	//check_cerr_newline();
	//cerr << "tx_set_work result " << result << " difficulty " << proof_difficulty << " elapsed time " << ccticks_elapsed(t0, ccticks()) << endl;

	return result;
}

void tx_commit_tree_hash_leaf(const bigint_t& commitment, const uint64_t commitnum, bigint_t& hash)
{
	vector<CCHashInput> hashin(2);

	hashin[0].SetValue(commitment, TX_FIELD_BITS);
	hashin[1].SetValue(commitnum, TX_COMMITNUM_BITS);

	//hashin[0].Dump();
	//hashin[1].Dump();

	hash = CCHash::Hash(hashin, HASH_BASES_MERKLE_LEAF, TX_MERKLE_BITS);

	//cerr << "hash = " << hex << hash << dec << endl;

	//return 0;
}

void tx_commit_tree_hash_node(const bigint_t& val1, const bigint_t& val2, bigint_t& hash, bool skip_final_knapsack)
{
	vector<CCHashInput> hashin(2);

	hashin[0].SetValue(val1, TX_MERKLE_BITS);
	hashin[1].SetValue(val2, TX_MERKLE_BITS);

	//hashin[0].Dump();
	//hashin[1].Dump();

	hash = CCHash::Hash(hashin, HASH_BASES_MERKLE_NODE, TX_MERKLE_BITS, skip_final_knapsack);

	//cerr << "hash = " << hex << hash << dec << endl;

	//return 0;
}

CCRESULT json_test_parse_number(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;
	bigint_t bigval;

	key = "nbits";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto nbits = value.asInt();
	if (nbits > 256)
		return error_invalid_value(fn, key, output, outsize);

	key = "amount";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	//cerr << endl << ">> nbits " << nbits << " value " << value.asString() << endl;
	auto rc = parse_int_value(fn, key, value.asString(), nbits, (nbits ? 0UL : TX_FIELD_MAX), bigval, output, outsize);
	if (rc) return rc;

	bool negative = (value.asString().substr(0, 1) == "-" || value.asString().substr(0, 2) == "\"-");

	//cerr << "negative " << negative << " nbits " << nbits << " value " << bigval << " 0x" << hex << bigval << dec << endl;

	ostringstream os;

	if (bigval && negative)
	{
		if (nbits)
			subBigInt(bigint_t(0UL), bigval, bigval, false);
		else
			bigval = bigint_t(0UL) - bigval;

		os << '-' << bigval;	// << " 0x" << hex << bigval;
	}
	else
		os << bigval;

	return copy_result_to_output(fn, os.str(), output, outsize);
}
