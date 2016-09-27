/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * jsoncmd.cpp
*/

#include "CCdef.h"

#include <jsoncpp/json/json.h>

#include "jsonapi.h"
#include "jsoninternal.h"
#include "payspec.h"
#include "txquery.h"
#include "jsonutil.h"
#include "CCproof.h"

static CCRESULT json_cmd(const string& fn, const char *json, char *output, const uint32_t bufsize)
{
	CCASSERT(json);
	CCASSERT(output);
	CCASSERT(bufsize > 0);

	char firstbyte = output[0];
	output[0] = 0;

	CCProof_Init();

	Json::Reader reader;
	Json::Value root;

	reader.parse(json, json + strlen(json), root);

	if (!reader.good())
		return copy_error_to_output(fn + ": " + reader.getFormattedErrorMessages(), output, bufsize);

	if (root.size() != 1)
		return copy_error_to_output(fn + ": json root must contain exactly one object", output, bufsize);

	auto it = root.begin();
	auto key = it.name();
	root = *it;

	if (key == "master-secret-generate")
		return generate_master_secret(key, root, output, bufsize);

	if (key == "master-secret-descramble")
		return master_secret_to_json(key, root, output, bufsize);

	if (key == "hash-spend-secret")
		return hash_spend_secret(key, root, output, bufsize);

	if (key == "payspec-encode")
		return payspec_from_json(key, root, output, bufsize);

	if (key == "payspec-decode")
		return payspec_to_json(key, root, output, bufsize);

	if (key == "compute-address")
		return compute_address(key, root, output, bufsize);

#if SUPPORT_GENERATE_TEST_INPUTS

	if (key == "generate-test-inputs")
		return generate_test_inputs(key, root, output, bufsize);

#endif

	if (key == "tx-create")
		return json_tx_create(key, root, output, bufsize);

	if (key == "tx-verify")
		return json_tx_verify(key, root, output, bufsize);

	if (key == "tx-to-json")
		return json_tx_to_json(key, root, output, bufsize);

	if (key == "tx-to-wire")
		return json_tx_to_wire(key, root, output, bufsize);

	if (key == "tx-from-wire")
	{
		output[0] = firstbyte;
		return json_tx_from_wire(key, root, output, bufsize);
	}

	if (key == "tx-dump")
		return json_tx_dump(key, root, output, bufsize);

	if (key == "tx-dump")
		return json_tx_dump(key, root, output, bufsize);

	if (key == "tx-query-create")
		return tx_query_from_json(key, root, output, bufsize);

	if (key == "work-reset")
	{
		output[0] = firstbyte;
		return json_work_reset(key, root, output, bufsize);
	}

	if (key == "work-add")
	{
		output[0] = firstbyte;
		return json_work_add(key, root, output, bufsize);
	}

	return copy_error_to_output(fn + ": unrecognized command \"" + key + "\"", output, bufsize);
}

CCAPI CCTx_JsonCmd(const char *json, char *output, const uint32_t bufsize)
{
	static const string fn("CCTx_JsonCmd");

	try
	{
		//CCASSERT(!true);

		return json_cmd(fn, json, output, bufsize);
	}
	catch (...)
	{
	}

	try
	{
		return copy_error_to_output(fn + ": assert error", output, bufsize);
	}
	catch (...)
	{
	}

	return -1;
}
