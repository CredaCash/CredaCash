/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * secrets.hpp
*/

#pragma once

#include <CCbigint.hpp>
#include <CCparams.h>
#include <transaction.hpp>

// note RULE tx input: @spend_secret[0] = zkhash(@root_secret, @spend_secret_number)
// note RULE tx input: @receive_secret = zkhash(@monitor_secret[0], @enforce_spendspec_with_spend_secret, @enforce_spendspec_with_trust_secret, @required_spendspec_hash, @allow_master_secret, @allow_freeze, @allow_trust_unfreeze, @require_public_hashkey, @restrict_addresses, @spend_locktime, @trust_locktime, @spend_delaytime, @trust_delaytime)
// note pre-destination = blake2s(receive_secret_id, monitor_secret_id[1..Q], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets) // FUTURE: add restricted addresses
// note RULE tx input: #dest = zkhash(@receive_secret, @monitor_secret[1..R], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets, @destnum)
// note pre-address = blake2s(dest_id, dest_chain)
// note RULE tx output: M_address = zkhash(#dest, dest_chain, #paynum)

#define SECRET_TYPE_VOID					0
#define SECRET_TYPE_MASTER					1	// + packed_params if known: values (seed, memory, iterations) used to derive master_secret from password
#define SECRET_TYPE_ROOT					2	// + next spend_secret_number
#define SECRET_TYPE_SPEND					3	// + spend_secret_number if known
#define SECRET_TYPE_TRUST					4
#define SECRET_TYPE_MONITOR					5
#define SECRET_TYPE_RECEIVE					6	// + packed_params if known: @enforce_spendspec_with_spend_secret, @enforce_spendspec_with_trust_secret, @required_spendspec_hash, @allow_master_secret, @allow_freeze, @allow_trust_unfreeze, @require_public_hashkey, @restrict_addresses, @spend_locktime, @trust_locktime, @spend_delaytime, @trust_delaytime
#define SECRET_TYPE_PRE_DESTINATION			7	// + next destnum + packed_params: @receive_secret, @monitor_secret[1..Q], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets
#define SECRET_TYPE_SPENDABLE_DESTINATION	8	// + destnum if known	(can spend)
#define SECRET_TYPE_TRACK_DESTINATION		9	// + destnum if known	(have at least pre-destination and monitor_secret[0])
#define SECRET_TYPE_WATCH_DESTINATION		10	// + destnum if known
#define SECRET_TYPE_SEND_DESTINATION		11	// + destnum if known
#define SECRET_TYPE_PRE_ADDRESS				12	// + next paynum (dest_chain is part of value hash, but is not stored in packed_params)
#define SECRET_TYPE_SEND_ADDRESS			13	// + paynum if known
#define SECRET_TYPE_SELF_ADDRESS			14	// + paynum if known
#define SECRET_TYPE_RECV_ADDRESS			15	// + paynum if known
#define SECRET_TYPE_POLL_ADDRESS			16	// + paynum if known
#define SECRET_TYPE_STATIC_ADDRESS			17	// + paynum if known
#define SECRET_TYPE_INVALID					18

#define MAIN_SECRET_ID				1
#define MAIN_ROOT_SECRET_ID			2
#define MAIN_SPEND_SECRET_ID		3
#define MAIN_RECEIVE_SECRET_ID		4
#define MAIN_PRE_DESTINATION_ID		5
#define MINT_DESTINATION_ID			6	// used for minting
#define SELF_DESTINATION_ID			7	// used for change

class DbConn;
class TxQuery;

class Secret
{
public:
	static const unsigned MAX_LABEL_LEN = 127;

	uint64_t id;
	unsigned type;
	uint64_t parent_id;
	uint64_t dest_id;
	uint64_t account_id;
	uint32_t packed_params_bytes;
	uint8_t packed_params[200];
	uint64_t number;
	uint64_t dest_chain;			// note: encoded and stored with the destination
	snarkfront::bigint_t value;
	char label[MAX_LABEL_LEN+1];
	uint64_t create_time;
	uint64_t first_receive;
	uint64_t last_receive;
	uint64_t last_check;
	uint64_t next_check;
	uint64_t query_commitnum;
	uint64_t expected_commitnum;

	Secret();

	void Clear();
	void Copy(const Secret& other);
	string DebugString() const;

	static bool TypeIsValid(unsigned type);
	bool IsValid() const;

	static bool TypeIsAddress(unsigned type)
	{
		return type >= SECRET_TYPE_SEND_ADDRESS && type <= SECRET_TYPE_STATIC_ADDRESS;
	}

	bool TypeIsAddress() const
	{
		return TypeIsAddress(type);
	}

	static bool TypeIsDestination(unsigned type)
	{
		return type >= SECRET_TYPE_SPENDABLE_DESTINATION && type <= SECRET_TYPE_SEND_DESTINATION;
	}

	bool TypeIsDestination() const
	{
		return TypeIsDestination(type);
	}

	bool TypeIsWatchOnlyDestination() const
	{
		return TypeIsWatchOnlyDestination(type);
	}

	static bool TypeIsWatchOnlyDestination(unsigned type)
	{
		return type >= SECRET_TYPE_TRACK_DESTINATION && type <= SECRET_TYPE_WATCH_DESTINATION;
	}

	static bool DestinationFromThisWallet(unsigned type, bool incwatch)
	{
		CCASSERT(TypeIsDestination(type));

		return type == SECRET_TYPE_SPENDABLE_DESTINATION || (incwatch && TypeIsWatchOnlyDestination(type));
	}

	bool DestinationFromThisWallet(bool incwatch) const
	{
		return DestinationFromThisWallet(type, incwatch);
	}

	static unsigned TypeHierarchy(unsigned type)
	{
		if (TypeIsAddress(type))
			return SECRET_TYPE_SEND_ADDRESS;
		if (TypeIsDestination(type))
			return SECRET_TYPE_SPENDABLE_DESTINATION;
		return type;
	}

	unsigned TypeHierarchy() const
	{
		return TypeHierarchy(type);
	}

	static uint64_t MaxNumber(unsigned type)
	{
		if (type == SECRET_TYPE_ROOT || type == SECRET_TYPE_SPEND)
			return ((uint64_t)1 << TX_SPEND_SECRETNUM_BITS) - 1;
		if (type >= SECRET_TYPE_PRE_DESTINATION && type <= SECRET_TYPE_SEND_DESTINATION)
			return ((uint64_t)1 << TX_DESTNUM_BITS) - 1;
		if (type >= SECRET_TYPE_PRE_ADDRESS && type <= SECRET_TYPE_POLL_ADDRESS)
			return ((uint64_t)1 << TX_PAYNUM_BITS) - 1;
		return 0;
	}

	uint64_t MaxNumber() const
	{
		return MaxNumber(type);
	}

	static unsigned ValueBytes(unsigned type)
	{
		return TypeIsAddress(type) ? TX_ADDRESS_BYTES : TX_INPUT_BYTES;
	}

	unsigned ValueBytes() const
	{
		return ValueBytes(type);
	}

	bool SetNumber(uint64_t _number);
	bool IncrementNumber();

	bool AcceptanceRequired() const;
	bool HasStaticAddress() const;

	int SetFromTxSecrets(unsigned _type, uint64_t _parent_id, const SpendSecretParams& params, SpendSecret *txsecrets, unsigned size = sizeof(SpendSecret));
	int ExtractToTxSecrets(SpendSecretParams& params, SpendSecret *txsecrets, unsigned size = sizeof(SpendSecret)) const;

	string EncodeDestination() const;
	static string EncodeDestination(const snarkfront::bigint_t& destination, uint64_t dest_chain);
	static int DecodeDestination(const string& encoded, uint64_t& dest_chain, snarkfront::bigint_t& destination);

	static string GetNewPassphrase(const char *use);
	static string GetPassphrase(const char *use);

	static void GenerateMasterSecret(string& encrypted_master_secret, string& passphrase);

	static void CreateBaseSecrets(DbConn *dbconn);

	int CreateNewSecret(DbConn *dbconn, unsigned _type, uint64_t _parent_id, uint64_t _dest_chain, SpendSecretParams& params, bool recurse = false);
	int DeriveSecret(DbConn *dbconn, unsigned _type, uint64_t _parent_id, SpendSecretParams& params);
	int DeriveSecret(DbConn *dbconn, unsigned _type, Secret& parent, SpendSecretParams& params);

	static int GetParentValue(DbConn *dbconn, unsigned _type, uint64_t _parent_id, SpendSecretParams& params, SpendSecret *txsecrets, unsigned size);

	int ImportSecret(DbConn *dbconn);
	int CheckForConflict(DbConn *dbconn, TxQuery& txquery, uint64_t _dest_chain) const;

	int CreatePollingAddresses(DbConn *dbconn, uint64_t _dest_chain, SpendSecretParams& params) const;

	int UpdateSavePollingTimes(DbConn *dbconn, uint64_t now = 0);
	int UpdatePollingTimes(uint64_t now = 0, bool checked_now = false);
	int PollAddress(DbConn *dbconn, TxQuery& txquery, bool update_times = true);

	static int PollDestination(DbConn *dbconn, TxQuery& txquery, uint64_t dest_id, uint64_t last_receive_max);

private:
	int CreateIntermediateSecret(DbConn *dbconn, unsigned _type, unsigned target_type, Secret& parent, SpendSecretParams& params);
	int CreateNextSecretNumber(DbConn *dbconn, unsigned _type, unsigned target_type, uint64_t _parent_id, SpendSecretParams& params);

	int UpdatePollingAddresses(DbConn *dbconn, const Secret &destination);
	int SetPollingAddresses(DbConn *dbconn, const Secret &destination, bool is_new = false);
};
