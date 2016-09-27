/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * transaction.cpp
*/

#include "CCdef.h"

#include <jsoncpp/json/json.h>
#include <blake2/blake2b.h>
#include <siphash/siphash.h>

#include "transaction.hpp"
#include "transaction.h"
#include "jsoninternal.h"
#include "jsonutil.h"
#include "CChash.hpp"
#include "CCproof.h"

#include <CCobjects.hpp>
#include <CCUtil.h>
#include <CCticks.hpp>

static const uint8_t zero_pow[TX_POW_SIZE] = {};

static CCRESULT get_tx_ptr(const string& fn, struct TxPay*& ptx, Json::Value& root, char *output, const uint32_t bufsize)
{
	// note, output can be NULL

	ptx = NULL;

	static TxPay stx;	// !!! for now

	ptx = &stx;

	return 0;
}

static void tx_init(TxPay& tx)
{
	memset(&tx, 0, sizeof(TxPay));

	tx.tag = CC_TAG_TX_STRUCT;	// tag the structure
}

static void dump_txout(ostream &os, const struct TxOut& tx, const char *prefix = "")
{
	os << prefix << "__dest " << tx.__dest << endl;
	os << prefix << "__paynum " << tx.__paynum << endl;
	os << prefix << "M_address " << tx.M_address << endl;
	os << prefix << "__value " << tx.__value << endl;
	os << prefix << "M_value_enc " << tx.M_value_enc << endl;
	os << prefix << "M_commitment " << tx.M_commitment << endl;
}

static void dump_txin(ostream &os, const struct TxIn& tx, const struct TxInPath *path = NULL)
{
	os << "____spendsecrets.____spend_secret " << tx.____spendsecrets.____spend_secret << endl;
	os << "____spendsecrets.____enforce_spendspec_hash " << tx.____spendsecrets.____enforce_spendspec_hash << endl;
	os << "__paynum " << tx.__paynum << endl;
	os << "__value " << tx.__value << endl;
	os << "___M_commitment_iv " << tx.__M_commitment_iv << endl;
	os << "__M_commitment " << tx.__M_commitment << endl;
	os << "S_spendspec_hashed " << tx.S_spendspec_hashed << endl;
	os << "S_serialnum " << tx.S_serialnum << endl;
	os << "pathnum " << tx.pathnum << endl;
	if (path)
	{
		os << "____merkle_leafindex " << path->____merkle_leafindex << endl;
		for (unsigned i = 0; i < TX_MERKLE_DEPTH; ++i)
			os << "____merkle_path[" << i << "] " << path->____merkle_path[i] << endl;
	}
}

CCRESULT tx_dump_stream(ostream &os, const struct TxPay& tx)
{
	os << hex;

	os << "----Transaction Dump----" << endl;
	os << "tag " << tx.tag << endl;
	os << "type " << tx.type << endl;
	os << "no_precheck " << tx.no_precheck << endl;
	os << "no_proof " << tx.no_proof << endl;
	os << "no_verify " << tx.no_verify << endl;
	os << "test_make_bad " << tx.test_make_bad << endl;
	os << "test_uselargerzkkey " << tx.test_uselargerzkkey << endl;
	os << "param_level " << tx.param_level << endl;
	os << "merkle_root " << tx.merkle_root << endl;
	os << "zkkeyid " << tx.zkkeyid << endl;
	for (unsigned i = 0; i < tx.zkproof.size(); ++i)
		os << "zkproof[" << i << "] " << tx.zkproof[i] << endl;
	os << "donation " << tx.donation << endl;
	os << "outvalmin " << tx.outvalmin << endl;
	os << "outvalmax " << tx.outvalmax << endl;
	os << "invalmax " << tx.invalmax << endl;
	os << "outvals-public " << tx.outvals_public << endl;
	os << "nonfinancial " << tx.nonfinancial << endl;
	os << "nout " << tx.nout << endl;
	os << "nin " << tx.nin << endl;
	os << "nin_with_path " << tx.nin_with_path << endl;
	for (unsigned i = 0; i < tx.nout; ++i)
	{
		os << "<< output[" << i << "]:" << endl;
		dump_txout(os, tx.output[i]);
	}
	for (unsigned i = 0; i < tx.nin; ++i)
	{
		os << ">> input[" << i << "]:" << endl;
		unsigned pathnum = tx.input[i].pathnum;
		if (pathnum)
			dump_txin(os, tx.input[i], &tx.inpath[pathnum - 1]);
		else
			dump_txin(os, tx.input[i]);
	}
	os << "----End of Transaction Dump----" << endl;

	os << dec;

	return 0;
}

CCRESULT tx_dump(const struct TxPay& tx, char *output, const uint32_t bufsize)
{
	ostringstream os;

	tx_dump_stream(os, tx);

	return copy_result_to_output("Transaction Dump", os.str(), output, bufsize);
}

CCRESULT json_tx_dump(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, ptx, root, output, bufsize);
	if (rc) return rc;
	CCASSERT(ptx);

	return tx_dump(*ptx, output, bufsize);
}

static void txpay_output_to_json(const string& fn, const struct TxOut& tx, ostream& os)
{
	os << "{\"destination\":\"0x" << tx.__dest << "\"" JSON_ENDL
	os << ",\"payment-number\":\"0x" << tx.__paynum << "\"" JSON_ENDL
	os << ",\"address\":\"0x" << tx.M_address << "\"" JSON_ENDL
	os << ",\"value\":\"0x" << tx.__value << "\"" JSON_ENDL
	os << ",\"encrypted-value\":\"0x" << tx.M_value_enc << "\"" JSON_ENDL
	os << ",\"commitment\":\"0x" << tx.M_commitment << "\"" JSON_ENDL
	os << "}" JSON_ENDL
}

static void txpay_input_to_json(const string& fn, const struct TxIn& tx, const struct TxInPath *path, ostream& os)
{
	os << "{\"spend-secret\":\"0x" << tx.____spendsecrets.____spend_secret << "\"" JSON_ENDL
	os << ",\"enforce-spend-spec-hash\":\"0x" << tx.____spendsecrets.____enforce_spendspec_hash << "\"" JSON_ENDL
	os << ",\"hashed-spend-spec\":\"0x" << tx.S_spendspec_hashed << "\"" JSON_ENDL

	os << ",\"payment-number\":\"0x" << tx.__paynum << "\"" JSON_ENDL
	os << ",\"value\":\"0x" << tx.__value << "\"" JSON_ENDL
	os << ",\"commitment-iv\":\"0x" << tx.__M_commitment_iv << "\"" JSON_ENDL
	os << ",\"commitment\":\"0x" << tx.__M_commitment << "\"" JSON_ENDL

	os << ",\"serial-number\":\"0x" << tx.S_serialnum << "\"" JSON_ENDL
	if (path)
	{
		os << ",\"commitment-number\":\"0x" << path->____merkle_leafindex << "\"" JSON_ENDL
		os << ",\"merkle-path\":" JSON_ENDL
		os << "[";
		for (unsigned i = 0; i < TX_MERKLE_DEPTH; ++i)
		{
			if (i) os << ",";
			os << "\"0x" << path->____merkle_path[i] << "\"" JSON_ENDL
		}
		os << "]}" JSON_ENDL
	}
	else
		os << "}" JSON_ENDL
}

static CCRESULT txpay_to_json(const string& fn, const struct TxPay& tx, char *output, const uint32_t bufsize)
{
	ostringstream os;
	os << hex;

	os << "{\"tx-pay\":" JSON_ENDL

	os << "{\"parameter-level\":\"0x" << tx.param_level << "\"" JSON_ENDL
	os << ",\"merkle-root\":\"0x" << tx.merkle_root << "\"" JSON_ENDL
	os << ",\"minimum-output-value\":\"0x" << tx.outvalmin << "\"" JSON_ENDL
	os << ",\"maximum-output-value\":\"0x" << tx.outvalmax << "\"" JSON_ENDL
	os << ",\"maximum-input-value\":\"0x" << tx.invalmax << "\"" JSON_ENDL
	os << ",\"outvals-public\":\"0x" << tx.outvals_public << "\"" JSON_ENDL
	os << ",\"nonfinancial\":\"0x" << tx.nonfinancial << "\"" JSON_ENDL
	os << ",\"zkkeyid\":\"0x" << tx.zkkeyid << "\"" JSON_ENDL
	os << ",\"zkproof\":" JSON_ENDL
	os << "[";
	for (unsigned i = 0; i < tx.zkproof.size(); ++i)
	{
		if (i) os << ",";
		os << "\"0x" << tx.zkproof[i] << "\"" JSON_ENDL
	}
	os << "]" JSON_ENDL

	os << ",\"donation\":\"0x" << (uint64_t)tx.donation << "\"" JSON_ENDL

	os << ",\"outputs\":" JSON_ENDL
	os << "[" JSON_ENDL
	for (unsigned i = 0; i < tx.nout; ++i)
	{
		if (i) os << "," JSON_ENDL
		txpay_output_to_json(fn, tx.output[i], os);
	}
	os << "]" JSON_ENDL

	os << ",\"inputs\":" JSON_ENDL
	os << "[" JSON_ENDL
	for (unsigned i = 0; i < tx.nin; ++i)
	{
		if (i) os << "," JSON_ENDL
		unsigned pathnum = tx.input[i].pathnum;
		if (pathnum)
			txpay_input_to_json(fn, tx.input[i], &tx.inpath[pathnum - 1], os);
		else
			txpay_input_to_json(fn, tx.input[i], NULL, os);
	}
	os << "]}}" JSON_ENDL

	return copy_result_to_output(fn, os.str(), output, bufsize);
}

static CCRESULT txpay_precheck_input(const string& fn, unsigned index, const struct TxIn& tx, char *output, const uint32_t bufsize)
{
	vector<CCHashInput> hashin(HASH_MAX_INPUTS);

	// RULE tx input: M-commitment = zkhash(#dest, #paynum, #value, M-commitment_iv)
	// RULE tx input:  where #dest = zkhash(@enforce_spendspec_hash, @enforce_spendspec_hash*stdhash(S-spendspec), zkhash(@secret))

	hashin.clear();
	hashin.resize(1);
	hashin[0].SetValue(tx.____spendsecrets.____spend_secret, TX_FIELD_BITS);
	auto secret_hashed = CCHash::Hash(hashin, HASH_BASES_RECEIVE_SECRET, TX_FIELD_BITS);

	bigint_t enforce_spendspec_hash((uint64_t)tx.____spendsecrets.____enforce_spendspec_hash);
	auto spendspec_prod = enforce_spendspec_hash * tx.S_spendspec_hashed;

	hashin.clear();
	hashin.resize(3);
	hashin[0].SetValue(enforce_spendspec_hash, 1);
	hashin[1].SetValue(spendspec_prod, TX_FIELD_BITS);
	hashin[2].SetValue(secret_hashed, TX_FIELD_BITS);
	auto dest = CCHash::Hash(hashin, HASH_BASES_DESTINATION, TX_FIELD_BITS);

	hashin.clear();
	hashin.resize(4);
	hashin[0].SetValue(dest, TX_FIELD_BITS);
	hashin[1].SetValue(tx.__paynum, TX_PAYNUM_BITS);
	hashin[2].SetValue(tx.__value, TX_VALUE_BITS);
	hashin[3].SetValue(tx.__M_commitment_iv, TX_COMMIT_IV_BITS);
	auto commitment = CCHash::Hash(hashin, HASH_BASES_COMMITMENT, TX_FIELD_BITS);
	if (commitment != tx.__M_commitment)
		return copy_error_to_output(fn + " error: inputs do not hash to the commitment for input " + to_string(index), output, bufsize);

	// RULE tx input: S-serialnum = zkhash(M-commitment, @secret)
	// this rule is taken care of in set_output_dependents

	return 0;
}

static CCRESULT txpay_precheck_input_path(const string& fn, unsigned index, const bigint_t& commitment, const struct TxInPath& txpath, const bigint_t& merkle_root, char *output, const uint32_t bufsize)
{
	// RULE tx input: enforce merkle path

	bigint_t val1, val2, hash;

	tx_commit_tree_hash_leaf(commitment, txpath.____merkle_leafindex, hash);

	for (unsigned i = 0; i < TX_MERKLE_DEPTH; ++i)
	{
		val1 = hash;
		val2 = txpath.____merkle_path[i];

		tx_commit_tree_hash_node(val1, val2, hash);
	}

	if (hash != merkle_root)
		return copy_error_to_output(fn + " error: commitment Merkle path does not hash to the Merkle root for input " + to_string(index), output, bufsize);

	return 0;
}

static CCRESULT txpay_precheck(const string& fn, const struct TxPay& tx, char *output, const uint32_t bufsize)
{
	if (tx.nout + tx.nin < 1)
		return copy_error_to_output(fn + " error: transaction requires at least one input or one output", output, bufsize);

	bigint_t valsum;

	if (tx.donation >= 0)
	{
		valsum = valsum + bigint_t((uint64_t)tx.donation);
		//cerr << "donation +" << hex << tx.donation << " valsum " << valsum << dec << endl;
	}
	else
	{
		valsum = valsum - bigint_t((uint64_t)(-tx.donation));
		//cerr << "donation -" << hex << -tx.donation << " valsum " << valsum << dec << endl;
	}

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		if (tx.output[i].__value < tx.outvalmin)
			return copy_error_to_output(fn + " error: output value < minimum for output " + to_string(i), output, bufsize);

		if (tx.output[i].__value > tx.outvalmax)
			return copy_error_to_output(fn + " error: output value > maximum for output " + to_string(i), output, bufsize);

		valsum = valsum + bigint_t(tx.output[i].__value);

		//cerr << "tx.output[" << i << "].__value " << hex << tx.output[i].__value << " valsum " << valsum << dec << endl;

		// note, all of the other output rules are taken care of in set_output_dependents
	}

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		if (tx.input[i].__value > tx.invalmax)
			return copy_error_to_output(fn + " error: input value > maximum for input " + to_string(i), output, bufsize);

		valsum = valsum - bigint_t(tx.input[i].__value);

		//cerr << "tx.input[" << i << "].__value " << hex << tx.input[i].__value << " valsum " << valsum << dec << endl;

		auto rc = txpay_precheck_input(fn, i, tx.input[i], output, bufsize);
		if (rc)
			return rc;

		auto pathnum = tx.input[i].pathnum;

		if (pathnum)
		{
			auto rc = txpay_precheck_input_path(fn, i, tx.input[i].__M_commitment, tx.inpath[pathnum - 1], tx.merkle_root, output, bufsize);
			if (rc)
				return rc;
		}
	}

	if (valsum)
		return copy_error_to_output(fn + " error: sum(input values) != sum(output values) + donation", output, bufsize);

	return 0;
}

static void set_output_dependents(const string& fn, struct TxPay& tx, char *output, const uint32_t bufsize)
{
	vector<CCHashInput> hashin(HASH_MAX_INPUTS);

	static const bigint_t commitment_iv_mask(TX_COMMIT_IV_MASK);
	bigint_t commitment_iv;
	for (unsigned j = 0; j < commitment_iv.numberLimbs(); ++j)
		BIGWORD(commitment_iv, j) = BIGWORD(tx.merkle_root, j) & BIGWORD(commitment_iv_mask, j);

	//cerr << "set_output_dependents merkle_root " << hex << tx.merkle_root << dec << endl;
	//cerr << "set_output_dependents commitment_iv  " << hex << commitment_iv << dec << endl;

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		// M-address = zkhash(#dest, #paynum)
		hashin.clear();
		hashin.resize(2);
		hashin[0].SetValue(tx.output[i].__dest, TX_FIELD_BITS);
		hashin[1].SetValue(tx.output[i].__paynum, TX_PAYNUM_BITS);
		tx.output[i].M_address = CCHash::Hash(hashin, HASH_BASES_ADDRESS, TX_FIELD_BITS);

		if (tx.outvals_public)
			tx.output[i].M_value_enc = tx.output[i].__value;
		else
		{
			// M-value_enc  = #value ^ zkhash(#dest, #paynum)
			hashin.clear();
			hashin.resize(2);
			hashin[0].SetValue(tx.output[i].__dest, TX_FIELD_BITS);
			hashin[1].SetValue(tx.output[i].__paynum, TX_PAYNUM_BITS);
			bigint_t one_time_pad = CCHash::Hash(hashin, HASH_BASES_VALUEENC, TX_VALUE_BITS);
			tx.output[i].M_value_enc = tx.output[i].__value ^ BIG64(one_time_pad);

			#if 0
			cerr << "set_output_dependents dest " << hex << tx.output[i].__dest << dec << endl;
			hashin[0].Dump();
			cerr << "set_output_dependents paynum " << hex << tx.output[i].__paynum << dec << endl;
			hashin[1].Dump();
			cerr << "set_output_dependents otp " << hex << one_time_pad << dec << endl;
			cerr << "set_output_dependents value " << hex << tx.output[i].__value << dec << endl;
			cerr << "set_output_dependents value_enc " << hex << tx.output[i].M_value_enc << dec << endl;
			#endif
		}

		// M-commitment = zkhash(#dest, #paynum, #value, M-commitment_iv)
		hashin.clear();
		hashin.resize(4);
		hashin[0].SetValue(tx.output[i].__dest, TX_FIELD_BITS);
		hashin[1].SetValue(tx.output[i].__paynum, TX_PAYNUM_BITS);
		hashin[2].SetValue(tx.output[i].__value, TX_VALUE_BITS);
		hashin[3].SetValue(commitment_iv, TX_COMMIT_IV_BITS);
		tx.output[i].M_commitment = CCHash::Hash(hashin, HASH_BASES_COMMITMENT, TX_FIELD_BITS);

		//for (unsigned j = 0; j < hashin.size(); ++j)
		//	hashin[j].Dump();
		//cerr << "tx.output[i].M_commitment " << tx.output[i].M_commitment << endl;
	}
}

static void set_input_dependents(const string& fn, struct TxPay& tx, char *output, const uint32_t bufsize)
{
	vector<CCHashInput> hashin(HASH_MAX_INPUTS);

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		// S-serialnum = zkhash(M-commitment, @secret)
		hashin.clear();
		hashin.resize(2);
		hashin[0].SetValue(tx.input[i].__M_commitment, TX_FIELD_BITS);
		hashin[1].SetValue(tx.input[i].____spendsecrets.____spend_secret, TX_FIELD_BITS);
		tx.input[i].S_serialnum = CCHash::Hash(hashin, HASH_BASES_SERIALNUM, TX_FIELD_BITS);
	}
}

static void set_proof(const string& fn, struct TxPay& tx, char *output, const uint32_t bufsize)
{
	CCProof_GenProof(tx);
}

static CCRESULT check_proof(const string& fn, struct TxPay& tx, char *output, const uint32_t bufsize)
{
	if (CCProof_VerifyProof(tx))
		return copy_error_to_output(fn + " error: transaction proof verification failed", output, bufsize);

	return 0;
}

static CCRESULT txpay_output_from_json(const string& fn, struct TxOut& tx, Json::Value& root, char *output, const uint32_t bufsize)
{
	bigint_t bigval;

	string key;
	Json::Value value;

	key = "destination";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, tx.__dest, output, bufsize);
	if (rc) return rc;

	key = "payment-number";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_PAYNUM_BITS, 0UL, tx.__paynum, output, bufsize);
	if (rc) return rc;

	key = "value";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	tx.__value = BIG64(bigval);

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	return 0;
}

static CCRESULT txpay_input_from_json(const string& fn, struct TxIn& tx, struct TxInPath *path, Json::Value& root, char *output, const uint32_t bufsize)
{
	bigint_t bigval;

	string key;
	Json::Value value;

	key = "spend-secret";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, 0UL, tx.____spendsecrets.____spend_secret, output, bufsize);
	if (rc) return rc;

	key = "enforce-spend-spec-hash";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		tx.____spendsecrets.____enforce_spendspec_hash = BIG64(bigval);
	}

#if TEST_EXTRA_ON_WIRE
	key = "hashed-spend-spec";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 0, 0UL, tx.S_spendspec_hashed, output, bufsize);
		if (rc) return rc;
	}
#endif

	key = "payment-number";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_PAYNUM_BITS, 0UL, tx.__paynum, output, bufsize);
	if (rc) return rc;

	key = "value";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	tx.__value = BIG64(bigval);

	key = "commitment-iv";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_COMMIT_IV_BITS, 0UL, tx.__M_commitment_iv, output, bufsize);
	if (rc) return rc;

	key = "commitment";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, tx.__M_commitment, output, bufsize);
	if (rc) return rc;

	if (path)
	{
		key = "commitment-number";
		if (!root.removeMember(key, &value))
			return error_missing_key(fn, key, output, bufsize);
		rc = parse_int_value(fn, key, value.asString(), TX_MERKLE_LEAFINDEX_BITS, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		path->____merkle_leafindex = BIG64(bigval);

		key = "merkle-path";
		if (!root.removeMember(key, &value))
			return error_missing_key(fn, key, output, bufsize);
		if (!value.isArray())
			return error_not_array(fn, key, output, bufsize);
		if (value.size() != TX_MERKLE_DEPTH)
			return error_num_values(fn, key, TX_MERKLE_DEPTH, output, bufsize);

		for (unsigned i = 0; i < TX_MERKLE_DEPTH; ++i)
		{
			rc = parse_int_value(fn, key, value[i].asString(), 0, TX_INPUT_MAX, path->____merkle_path[i], output, bufsize);
			if (rc) return rc;
		}
	}

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	return 0;
}

static CCRESULT txpay_outputs_from_json(const string& fn, struct TxPay& tx, const string& key, Json::Value& root, char *output, const uint32_t bufsize)
{
	//cerr << "txpay_outputs_from_json " << root.size()<< endl;

	if (!root.isArray()) // no longer enforced: || root.size() < 1)
		return error_not_array_objs(fn, key, output, bufsize);

	if (root.size() > TX_MAXOUT)
		return error_too_many_objs(fn, key, TX_MAXOUT, output, bufsize);

	for (unsigned i = 0; i < root.size(); ++i)
	{
		if (!root[i].isObject())
			return error_not_array_objs(fn, key, output, bufsize);

		tx.nout = i + 1;

		auto rc = txpay_output_from_json(fn, tx.output[i], root[i], output, bufsize);
		if (rc) return rc;
	}

	return 0;
}

static CCRESULT txpay_inputs_from_json(const string& fn, struct TxPay& tx, const string& key, Json::Value& root, char *output, const uint32_t bufsize)
{
	if (!root.isArray()) // no longer enforced: || root.size() < 1)
		return error_not_array_objs(fn, key, output, bufsize);

	if (root.size() > TX_MAXIN)
		return error_too_many_objs(fn, key, TX_MAXIN, output, bufsize);

	for (unsigned i = 0; i < root.size(); ++i)
	{
		if (!root[i].isObject())
			return error_not_array_objs(fn, key, output, bufsize);

		TxInPath *path = NULL;
		static const string key = "merkle-path";

		if (json_find(root[i], key.c_str()))
		{
			if (tx.nin_with_path == TX_MAXINPATH)
				return error_too_many_objs(fn, key, TX_MAXINPATH, output, bufsize);

			path = &tx.inpath[tx.nin_with_path++];
			tx.input[i].pathnum = tx.nin_with_path;
		}

		tx.nin = i + 1;

		auto rc = txpay_input_from_json(fn, tx.input[i], path, root[i], output, bufsize);
		if (rc) return rc;
	}

	return 0;
}

static CCRESULT txpay_create_from_json(const string& fn, struct TxPay& tx, Json::Value& root, char *output, const uint32_t bufsize)
{
	bigint_t bigval;

	string key;
	Json::Value value;

	key = "parameter-level";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	tx.param_level = BIG64(bigval);

	key = "merkle-root";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, tx.merkle_root, output, bufsize);
	if (rc) return rc;

	key = "minimum-output-value";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	tx.outvalmin = BIG64(bigval);

	key = "maximum-output-value";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	tx.outvalmax = BIG64(bigval);

	key = "maximum-input-value";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	tx.invalmax = BIG64(bigval);

	key = "donation";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	tx.donation = BIG64(bigval);

	key = "outputs";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = txpay_outputs_from_json(fn, tx, key, value, output, bufsize);
	if (rc) return rc;

	key = "inputs";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = txpay_inputs_from_json(fn, tx, key, value, output, bufsize);
	if (rc) return rc;

	key = "no-precheck";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		tx.no_precheck = BIG64(bigval);
	}

	key = "no-proof";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		tx.no_proof = BIG64(bigval);
	}

	key = "no-verify";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		tx.no_verify = BIG64(bigval);
	}

	key = "test-make-bad";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		tx.test_make_bad = BIG64(bigval);
	}

	key = "test-use-larger-zkkey";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		tx.test_uselargerzkkey = BIG64(bigval);
	}

	key = "outvals-public";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		tx.outvals_public = BIG64(bigval);
	}

	key = "nonfinancial";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		tx.nonfinancial = BIG64(bigval);
	}

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	if (tx.no_proof)
		return 0;

	if (!tx.no_precheck)
	{
		rc = txpay_precheck(fn, tx, output, bufsize);
		if (rc) return rc;
	}

	set_output_dependents(fn, tx, output, bufsize);

	set_input_dependents(fn, tx, output, bufsize);

	if (!tx.no_proof)
	{
		set_proof(fn, tx, output, bufsize);

		if (!tx.no_verify)
			return check_proof(fn, tx, output, bufsize);
	}

	return 0;
}

CCRESULT json_tx_create(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, ptx, root, output, bufsize);
	if (rc) return rc;
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	tx_init(tx);
	tx.type = TX_PAY;

	if (root.size() != 1)
		return copy_error_to_output(fn + " error: json transaction must contain exactly one object", output, bufsize);

	auto it = root.begin();
	auto key = it.name();
	root = *it;

	if (key == "tx-pay")
		return txpay_create_from_json(fn, tx, root, output, bufsize);

	return error_invalid_tx(fn, output, bufsize);
}

CCRESULT json_tx_verify(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, ptx, root, output, bufsize);
	if (rc) return rc;
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	if (tx.tag != CC_TAG_TX_STRUCT)
		return error_invalid_tx(fn, output, bufsize);

	if (tx.type == TX_PAY)
		return check_proof(fn, tx, output, bufsize);

	return error_invalid_tx(fn, output, bufsize);
}

CCRESULT json_tx_to_json(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, ptx, root, output, bufsize);
	if (rc) return rc;
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	if (tx.tag != CC_TAG_TX_STRUCT)
		return error_invalid_tx(fn, output, bufsize);

	if (tx.type == TX_PAY)
		return txpay_to_json(fn, tx, output, bufsize);

	return error_invalid_tx(fn, output, bufsize);
}

static void txpay_input_to_wire(const struct TxIn& tx, uint32_t& bufpos, char *output, const uint32_t bufsize, const bool bhex = false)
{
	copy_to_buf(&tx.S_serialnum, sizeof(tx.S_serialnum), bufpos, output, bufsize, bhex);

#if TEST_EXTRA_ON_WIRE
	copy_to_buf(&tx.S_spendspec_hashed, sizeof(tx.S_spendspec_hashed), bufpos, output, bufsize, bhex);
#endif

	static uint16_t nsigkeys = 0;	// always zero for now

	copy_to_buf(&nsigkeys, 1, bufpos, output, bufsize, bhex);
}

static CCRESULT txpay_to_wire(const string& fn, const struct TxPay& tx, char *output, const uint32_t bufsize, const bool bhex = false)
{
	uint32_t bufpos = 0;

	CCASSERT(sizeof(bufpos) + sizeof(tx.tag) == sizeof(CCObject::Header));

	copy_to_buf(&bufpos, sizeof(bufpos), bufpos, output, bufsize, bhex);  // save space for size word

	auto tag = tx.tag;
	tag = CC_TAG_TX_WIRE;
	copy_to_buf(&tag, sizeof(tx.tag), bufpos, output, bufsize, bhex);
	//cerr << "txpay_to_wire output tag " << tag << endl;

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(&zero_pow, sizeof(zero_pow), bufpos, output, bufsize, bhex);
	copy_to_buf(&tx.param_level, sizeof(tx.param_level), bufpos, output, bufsize, bhex);
	copy_to_buf(&tx.zkkeyid, sizeof(tx.zkkeyid), bufpos, output, bufsize, bhex);
	copy_to_buf(&tx.zkproof, sizeof(tx.zkproof), bufpos, output, bufsize, bhex);
	copy_to_buf(&tx.donation, sizeof(tx.donation), bufpos, output, bufsize, bhex);
#if TEST_EXTRA_ON_WIRE
	copy_to_buf(&tx.merkle_root, sizeof(tx.merkle_root), bufpos, output, bufsize, bhex);
	copy_to_buf(&tx.outvalmin, sizeof(tx.outvalmin), bufpos, output, bufsize, bhex);
	copy_to_buf(&tx.outvalmax, sizeof(tx.outvalmax), bufpos, output, bufsize, bhex);
	copy_to_buf(&tx.invalmax, sizeof(tx.invalmax), bufpos, output, bufsize, bhex);
	copy_to_buf(&tx.outvals_public, 1, bufpos, output, bufsize, bhex);
	copy_to_buf(&tx.nonfinancial, 1, bufpos, output, bufsize, bhex);
#endif

	CCASSERT(tx.nin >= tx.nin_with_path);
	uint16_t nin_without_path = (unsigned)(tx.nin - tx.nin_with_path);

	copy_to_buf(&tx.nout, 1, bufpos, output, bufsize, bhex);
	copy_to_buf(&tx.nin_with_path, 1, bufpos, output, bufsize, bhex);
	copy_to_buf(&nin_without_path, 1, bufpos, output, bufsize, bhex);

	unsigned i, count;

	for (i = 0; i < tx.nout; ++i)
	{
		copy_to_buf(&tx.output[i].M_address, sizeof(tx.output[i].M_address), bufpos, output, bufsize, bhex);
		copy_to_buf(&tx.output[i].M_value_enc, sizeof(tx.output[i].M_value_enc), bufpos, output, bufsize, bhex);
		copy_to_buf(&tx.output[i].M_commitment, sizeof(tx.output[i].M_commitment), bufpos, output, bufsize, bhex);
	}

	for (i = count = 0; i < tx.nin; ++i)
	{
		unsigned pathnum = tx.input[i].pathnum;
		if (!pathnum)
			continue;

		txpay_input_to_wire(tx.input[i], bufpos, output, bufsize, bhex);

		++count;
	}

	CCASSERT(count == tx.nin_with_path);

	for (i = count = 0; i < tx.nin; ++i)
	{
		unsigned pathnum = tx.input[i].pathnum;
		if (pathnum)
			continue;

		copy_to_buf(&tx.input[i].__M_commitment, sizeof(tx.input[i].__M_commitment), bufpos, output, bufsize, bhex);

		txpay_input_to_wire(tx.input[i], bufpos, output, bufsize, bhex);

		++count;
	}

	CCASSERT(count == nin_without_path);

	if (bufpos > bufsize)
		return copy_error_to_output(fn + " error: output buffer overflow", output, bufsize);

	//cerr << "txpay_to_wire nbytes " << bufpos << endl;

	memcpy(output, &bufpos, sizeof(bufpos));

	return 0;
}

static CCRESULT txpay_from_wire(struct TxPay& tx, uint32_t& bufpos, const char *output, const uint32_t bufsize, const bool bhex)
{
	copy_from_buf(&tx.param_level, sizeof(tx.param_level), bufpos, output, bufsize, bhex);
	copy_from_buf(&tx.zkkeyid, sizeof(tx.zkkeyid), bufpos, output, bufsize, bhex);
	copy_from_buf(&tx.zkproof, sizeof(tx.zkproof), bufpos, output, bufsize, bhex);
	copy_from_buf(&tx.donation, sizeof(tx.donation), bufpos, output, bufsize, bhex);
#if TEST_EXTRA_ON_WIRE
	copy_from_buf(&tx.merkle_root, sizeof(tx.merkle_root), bufpos, output, bufsize, bhex);
	copy_from_buf(&tx.outvalmin, sizeof(tx.outvalmin), bufpos, output, bufsize, bhex);
	copy_from_buf(&tx.outvalmax, sizeof(tx.outvalmax), bufpos, output, bufsize, bhex);
	copy_from_buf(&tx.invalmax, sizeof(tx.invalmax), bufpos, output, bufsize, bhex);
	copy_from_buf(&tx.outvals_public, 1, bufpos, output, bufsize, bhex);
	copy_from_buf(&tx.nonfinancial, 1, bufpos, output, bufsize, bhex);
#endif

	uint16_t nin_without_path = 0;

	copy_from_buf(&tx.nout, 1, bufpos, output, bufsize, bhex);
	copy_from_buf(&tx.nin_with_path, 1, bufpos, output, bufsize, bhex);
	copy_from_buf(&nin_without_path, 1, bufpos, output, bufsize, bhex);

	tx.nin = tx.nin_with_path + nin_without_path;

	if (tx.nout > TX_MAXOUT)
		return -1;
	if (tx.nin > TX_MAXIN)
		return -1;
	if (tx.nin_with_path > TX_MAXINPATH)
		return -1;

	for (unsigned i = 0; i < tx.nout; ++i)
	{
		copy_from_buf(&tx.output[i].M_address, sizeof(tx.output[i].M_address), bufpos, output, bufsize, bhex);
		copy_from_buf(&tx.output[i].M_value_enc, sizeof(tx.output[i].M_value_enc), bufpos, output, bufsize, bhex);
		copy_from_buf(&tx.output[i].M_commitment, sizeof(tx.output[i].M_commitment), bufpos, output, bufsize, bhex);
	}

	for (unsigned i = 0; i < tx.nin; ++i)
	{
		if (i < tx.nin_with_path)
		{
			tx.input[i].pathnum = i + 1;
		}
		else
		{
			copy_from_buf(&tx.input[i].__M_commitment, sizeof(tx.input[i].__M_commitment), bufpos, output, bufsize, bhex);
		}

		copy_from_buf(&tx.input[i].S_serialnum, sizeof(tx.input[i].S_serialnum), bufpos, output, bufsize, bhex);

#if TEST_EXTRA_ON_WIRE
		copy_from_buf(&tx.input[i].S_spendspec_hashed, sizeof(tx.input[i].S_spendspec_hashed), bufpos, output, bufsize, bhex);
#endif

		static uint16_t nsigkeys = 0;

		copy_from_buf(&nsigkeys, 1, bufpos, output, bufsize, bhex);
		if (nsigkeys)
			return -1;
	}

	return 0;
}

CCRESULT json_tx_to_wire(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	struct TxPay *ptx;
	auto rc = get_tx_ptr(fn, ptx, root, output, bufsize);
	if (rc) return rc;
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	if (tx.tag != CC_TAG_TX_STRUCT)
		return error_invalid_tx(fn, output, bufsize);

	if (tx.type == TX_PAY)
		return txpay_to_wire(fn, tx, output, bufsize);

	return error_invalid_tx(fn, output, bufsize);
}

CCRESULT tx_from_wire(struct TxPay& tx, const char *output, const uint32_t bufsize)
{
	tx_init(tx);

	static const bool bhex = false;

	uint32_t bufpos = 0;
	uint32_t wiresize = 0;

	//cerr << "json_tx_from_wire buf start: " << (unsigned)output[0] << " " << (unsigned)output[1] << " " << (unsigned)output[2] << " " << (unsigned)output[3] << endl;

	copy_from_buf(&wiresize, sizeof(wiresize), bufpos, output, bufsize, bhex);
	if (wiresize > bufsize)
		return -1;

	copy_from_buf(&tx.tag, sizeof(tx.tag), bufpos, output, bufsize, bhex);
	if (tx.tag != CC_TAG_TX_WIRE && tx.tag != CC_TAG_TX_BLOCK)
		return -1;

	CCASSERT(bufpos == sizeof(CCObject::Header));

	if (tx.tag == CC_TAG_TX_WIRE)
		bufpos += TX_POW_SIZE;

	tx.tag = CC_TAG_TX_STRUCT;
	tx.type = TX_PAY;

	auto rc = txpay_from_wire(tx, bufpos, output, bufsize, bhex);
	if (rc)
		return rc;

	//cerr << "json_tx_from_wire end bufpos " << bufpos << " wiresize " << wiresize << " bufsize " << bufsize << endl;

	if (bufpos > bufsize)
		return -1;

	if (bufpos != wiresize)
		return -1;

	return 0;
}

CCRESULT json_tx_from_wire(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	struct TxPay *ptx;
	CCRESULT rc = get_tx_ptr(fn, ptx, root, NULL, 0);
	if (rc) return rc;
	CCASSERT(ptx);

	struct TxPay& tx = *ptx;

	rc = tx_from_wire(tx, output, bufsize);
	if (rc) return rc;

	bigint_t bigval;

	string key;
	Json::Value value;

#if !TEST_EXTRA_ON_WIRE

	key = "merkle-root";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, tx.merkle_root, output, bufsize);
	if (rc) return rc;

	key = "minimum-output-value";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	tx.outvalmin = BIG64(bigval);

	key = "maximum-output-value";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	tx.outvalmax = BIG64(bigval);

	key = "maximum-input-value";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	tx.invalmax = BIG64(bigval);

	key = "outvals-public";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		tx.outvals_public = BIG64(bigval);
	}

	key = "nonfinancial";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, bufsize);
		if (rc) return rc;
		tx.nonfinancial = BIG64(bigval);
	}

#endif

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	return 0;
}

// !!! move this to a new source file
CCRESULT json_work_reset(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	auto pheader = (const CCObject::Header *)output;
	const unsigned data_offset = sizeof(CCObject::Header) + TX_POW_SIZE;

	//cerr << hex << "json_work_reset bufsize " << bufsize << " tx size " << pheader->size << " tag " << pheader->tag << dec << endl;

	if (pheader->size > bufsize)
		return error_invalid_tx(fn, output, bufsize);

	if (pheader->size < data_offset)
		return error_invalid_tx(fn, output, bufsize);

	bigint_t bigval;

	string key;
	Json::Value value;

	key = "timestamp";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	uint64_t timestamp = BIG64(bigval);

	//cerr << "json_work_reset timestamp " << timestamp << endl;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	return tx_reset_work(output, timestamp);
}

CCRESULT json_work_add(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	auto pheader = (const CCObject::Header *)output;
	const unsigned data_offset = sizeof(CCObject::Header) + TX_POW_SIZE;

	//cerr << hex << "json_work_add bufsize " << bufsize << " tx size " << pheader->size << " tag " << pheader->tag << dec << endl;

	if (pheader->size > bufsize)
		return error_invalid_tx(fn, output, bufsize);

	if (pheader->size < data_offset)
		return error_invalid_tx(fn, output, bufsize);

	bigint_t bigval;

	string key;
	Json::Value value;

	key = "index";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, (unsigned long)(TX_POW_NPROOFS - 1), bigval, output, bufsize);
	if (rc) return rc;
	unsigned proof_index = BIG64(bigval);

	key = "iterations";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	uint64_t iterations = BIG64(bigval);

	key = "difficulty";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	uint64_t proof_difficulty = BIG64(bigval);

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	ccoid_t txhash;

	rc = blake2b(&txhash, sizeof(txhash), NULL, 0, output + data_offset, pheader->size - data_offset);
	CCASSERTZ(rc);

	//cerr << "json_work_add hashed " << pheader->size - data_offset << " bytes starting with " << hex << *(uint64_t*)(output + data_offset) << " result " << *(uint64_t*)(txhash) << dec << endl;

	return tx_set_work(output, &txhash, proof_index, 1, iterations, proof_difficulty);
}

// non-json interface

CCRESULT tx_reset_work(const char *tx, uint64_t timestamp)
{
	auto ptime = (uint64_t*)(tx + sizeof(CCObject::Header));

	memset(ptime, 0, TX_POW_SIZE);

	*ptime = timestamp;

	//cerr << "tx_reset_work timestamp " << timestamp << endl;

	return 0;
}

CCRESULT tx_check_timestamp(uint64_t timestamp, unsigned allowance)
{
	uint64_t now = _time64(NULL);

	int64_t age = now - timestamp;

	//cerr << "tx_check_timestamp timestamp " << timestamp << " now " << now << " age " << age << " allowance " << allowance << endl;

	if (age < -5*60 || age > allowance)
		return -1;
	else
		return 0;
}

CCRESULT tx_set_work(const char *tx, const void *txhash, unsigned proof_start, unsigned proof_count, uint64_t iter_count, uint64_t proof_difficulty)
{
#if TEST_SEQ_TX_OID
	return 0;
#endif

	if (!proof_difficulty)
		return 0;

	CCRESULT result = 0;

	for (unsigned proof_index = proof_start; proof_index < proof_start + proof_count; ++proof_index)
	{
		CCASSERT(proof_index < TX_POW_NPROOFS);

		auto pnonce = (uint64_t*)(tx + sizeof(CCObject::Header) + sizeof(uint64_t) + proof_index * TX_POW_NONCE_SIZE);
		uint64_t iter_start = *pnonce & TX_POW_NONCE_MASK;

		const uint64_t iter_limit = TX_POW_NONCE_MASK - 1;
		uint64_t iter_end = iter_start + iter_count - 1;
		if (iter_end > iter_limit || iter_limit - iter_count < iter_start)
			iter_end = iter_limit;

		//cerr << hex << "tx_set_work proof_index " << proof_index << " iter_start " << iter_start << " iter_count " << iter_count << " iter_end " << iter_end << " proof_difficulty " << proof_difficulty << dec << endl;

		array<uint64_t, 2> hashkey;
		hashkey[0] = *(uint64_t*)(tx + sizeof(CCObject::Header));

		uint64_t nonce;
		for (nonce = iter_start; nonce <= iter_end; ++nonce)
		{
			hashkey[1] = ((uint64_t)proof_index << TX_POW_NONCE_BITS) | nonce;

			uint64_t hash = sip_hash24((uint8_t*)&hashkey, (uint8_t*)txhash, sizeof(ccoid_t), false);

			//cerr << hex << "tx_set_work nonce " << nonce << " hash " << hash << " proof_difficulty " << proof_difficulty << dec << endl;

			if (hash <= proof_difficulty)
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

	//cerr << hex << "tx_set_work result " << result << endl;

	return result;
}

CCRESULT tx_commit_tree_hash_leaf(const bigint_t& commitment, const uint64_t& leafindex, bigint_t& hash)
{
	vector<CCHashInput> hashin(2);

	hashin[0].SetValue(commitment, TX_FIELD_BITS);
	hashin[1].SetValue(leafindex, TX_MERKLE_LEAFINDEX_BITS);

	//hashin[0].Dump();
	//hashin[1].Dump();

	hash = CCHash::Hash(hashin, HASH_BASES_MERKLE_LEAF, TX_MERKLE_PATH_BITS);

	//cerr << "hash = " << hex << hash << dec << endl;

	return 0;
}

CCRESULT tx_commit_tree_hash_node(const bigint_t& val1, const bigint_t& val2, bigint_t& hash)
{
	vector<CCHashInput> hashin(2);

	hashin[0].SetValue(val1, TX_MERKLE_PATH_BITS);
	hashin[1].SetValue(val2, TX_MERKLE_PATH_BITS);

	//hashin[0].Dump();
	//hashin[1].Dump();

	hash = CCHash::Hash(hashin, HASH_BASES_MERKLE_NODE, TX_MERKLE_PATH_BITS);

	//cerr << "hash = " << hex << hash << dec << endl;

	return 0;
}
