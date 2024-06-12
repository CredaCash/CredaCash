/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * jsoncmd.cpp
*/

#include "cclib.h"

#include <jsoncpp/json/json.h>

#include "jsonapi.h"
#include "jsoninternal.h"
#include "payspec.h"
#include "txquery.h"
#include "jsonutil.h"
#include "CCproof.h"

static CCRESULT json_cmd(const string& fn, const char *json, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	//bigint_test();

	CCASSERT(json);
	CCASSERT(output);
	CCASSERT(outsize > 0);

	if (binbuf && binsize < sizeof(uint32_t))
		return copy_error_to_output(fn, "error: binary buffer too small", output, outsize);

	output[0] = 0;

	CCProof_Init();

#if TEST_SUPPORT_ZK_KEYGEN
	CCProof_PreloadVerifyKeys();	// generates keys if none are found
#endif

	Json::CharReaderBuilder builder;
	Json::CharReaderBuilder::strictMode(&builder.settings_);
	Json::Value root;
	string errs;

	auto reader = builder.newCharReader();

	bool rc;

	try
	{
		rc = reader->parse(json, json + strlen(json), &root, &errs);
	}
	catch (const exception& e)
	{
		errs = e.what();
		rc = false;
	}
	catch (...)
	{
		errs = "unknown";
		rc = false;
	}

	delete reader;

	if (!rc)
		return copy_error_to_output(fn, string("error: ") + errs, output, outsize);

	if (root.size() != 1)
		return copy_error_to_output(fn, "error: json root must contain exactly one object", output, outsize);

	auto it = root.begin();
	auto key = it.name();
	root = *it;

	if (key == "generate-random")
		return generate_random(key, root, output, outsize);

	if (key == "master-secret-generate")
		return generate_master_secret_json(key, root, output, outsize);

	if (key == "master-secret-decrypt")
		return compute_master_secret_json(key, root, output, outsize);

	if (key == "compute-root-secret" || key == "compute-spend-secret" || key == "compute-trust-secret" || key == "compute-monitor-secret" || key == "compute-receive-secret")
		return compute_secret(key, root, output, outsize);

	if (key == "payspec-encode")
		return payspec_from_json(key, root, output, outsize);

	if (key == "payspec-decode")
		return payspec_to_json(key, root, output, outsize);

	if (key == "compute-address")
		return compute_address_json(key, root, output, outsize);

	if (key == "encode-amount")
		return encode_amount_json(key, root, output, outsize);

	if (key == "decode-amount")
		return decode_amount_json(key, root, output, outsize);

	if (key == "compute-amount-encryption")
		return compute_amount_encyption_json(key, root, output, outsize);

	if (key == "compute-serial-number")
		return compute_serialnum_json(key, root, output, outsize);

#if SUPPORT_GENERATE_TEST_INPUTS

	if (key == "generate-test-inputs")
		return generate_test_inputs(key, root, output, outsize);

#endif

	if (key == "tx-create")
		return json_tx_create(key, root, output, outsize);

	if (key == "tx-verify")
		return json_tx_verify(key, root, output, outsize);

	if (key == "tx-to-json")
		return json_tx_to_json(key, root, output, outsize);

	if (key == "tx-to-wire")
		return json_tx_to_wire(key, root, output, outsize, binbuf, binsize);

	if (key == "tx-from-wire")
		return json_tx_from_wire(key, root, output, outsize, binbuf, binsize);

	if (key == "tx-dump")
		return json_tx_dump(key, root, output, outsize);

	if (key == "tx-query-create")
		return tx_query_from_json(key, root, output, outsize, binbuf, binsize);

	if (key == "work-reset")
		return json_work_reset(key, root, output, outsize, binbuf, binsize);

	if (key == "work-add")
		return json_work_add(key, root, output, outsize, binbuf, binsize);

	if (key == "test-parse-number")
		return json_test_parse_number(key, root, output, outsize);

	return copy_error_to_output(fn, string("error: unrecognized command \"") + key + "\"", output, outsize);
}

CCAPI CCTx_JsonCmd(const char *json, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	static const string fn("CCTx_JsonCmd");

	try
	{
		//CCASSERT(!true);

		return json_cmd(fn, json, output, outsize, binbuf, binsize);
	}
	catch (...)
	{
	}

	try
	{
		return copy_error_to_output(fn, "error: assert failed", output, outsize);
	}
	catch (...)
	{
	}

	return -1;
}
