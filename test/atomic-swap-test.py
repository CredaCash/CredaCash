#!/usr/bin/env python2

'''
CredaCash(TM) Atomic Swap Test

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors


Protocol:

B generates b.spend_secret, sends monitor_hash(b.spend_secret) to A

A generates random hashkey
A generates transaction a2b (B will claim this output bill last):
	hashkey is not published when B claims
	monitor_secret[0] = monitor_hash(b.spend_secret)
	spend_secret[1] = hashkey
	trust_secrets[2] = a.trust_secret
	trust_locktime is later, since this tx will be claimed later

A generates a.spend_secret, sends monitor_hash(a.spend_secret) to B
A sends monitor_hash(hash_key) to B

B verifies that a2b has cleared and:
	tx is from A
	has expected amount
	B could spend tx output with:
		monitor_secret[0] = monitor_hash(b.spend_secret)
		monitor_secret[1] = some value that B does not yet know
		requires 2 of 2 spend_secrets corresponding to above monitor_secret[0,1]
	spend through trust_secrets has sufficient (later) locktime

B generates transaction b2a (A will claim this output bill first):
	A must publish hashkey when claiming
	monitor_secret[0] = monitor_hash(a.spend_secret)
	monitor_secret[1] = a2b.monitor_secret[1]
	trust_secret[2] = b.trust_secret
	trust_locktime is earlier, since this tx will be claimed earlier

A verifies that b2a has cleared and:
	tx is from B
	has expected amount
	A can spend tx output:
		monitor_secret[0] = monitor_hash(a.spend_secret)
		monitor_secret[1] = monitor_hash(hash_key)
		requires 2 of 2 spend_secrets corresponding to above monitor_secret[0,1]
	spend through trust_secrets has sufficient locktime

A claims b2a well before before B can reclaim this output
	hashkey must be published in this claiming tx

At this point, B can reclaim b2a if A didn't claim

B claims a2b using hashkey published in blockchain
	hashkey kept private in this claiming tx

At this point, A can reclaim a2b if B didn't claim

'''

import collections

from cclib import *

cclib.server_hostname = 'lbua2etw6sgkmtqtmtoubhkthragv7gh6lraephmqor6vmlmw5rkvpad.onion'		# hostname of the Tx server @@!

# note: output search string: ===|<<< tx|<<< [^C][A-Z]

tx_precheck = 0
atomic_locktime = 90

####################################################################################

def GenerateSpendSecret(bits):
	return hex(random.getrandbits(bits))

def ComputeMonitorSecret(spend_secret):
	jstr = '{"compute-monitor-secret" : '
	jstr += '{"spend-secret" : "' + spend_secret + '"'
	jstr += '}}'
	result = DoJsonCmd(jstr)
	result = json.loads(result)
	result = result['monitor-secret']
	return result

def GenerateDestination(monitor_secrets, destination_params = ''):
	destnum = 0
	while True:
		jstr = '{"payspec-encode" :'
		jstr += ' {"monitor-secrets" : [' + monitor_secrets + ']'
		jstr += ', "destination-number" : "' + hex(destnum) + '"'
		if len(destination_params):
			jstr += ', ' + destination_params
		jstr += '}}'
		payspec = DoJsonCmd(jstr)

		# decode payspec and extract destination
		jstr = '{"payspec-decode" : ' + payspec + '}'
		payspec = DoJsonCmd(jstr)
		payspec = json.loads(payspec)
		payspec = payspec['payspec']
		destination = payspec['destination']

		if HasAcceptanceRequired(destination):
			# this destination requires acceptance, so pick a different one
			destnum += 1
			continue

		if len(destination_params):
			destination_params += ', '
		destination_params += '"monitor-secrets" : [' + monitor_secrets + ']'
		destination_params += ',"destination-number" : "' + hex(destnum) + '"'

		return destination, destination_params

class Bill:
	def __init__(self, locktime, monitor_secret, destination, address, commitment, next_commitnum, destination_params, paynum):
		self.locktime = locktime
		self.monitor_secret = monitor_secret
		self.destination = destination
		self.address = address
		self.commitment = commitment
		self.next_query_commitnum = next_commitnum
		self.paynum = paynum
		self.params = destination_params
		self.params += ', "payment-number" : "' + hex(paynum) + '"'
		self.params += ', "commitment" : "' + commitment + '"'
		self.cleared = False

	def WaitForCleared(self):
		while not self.QueryCleared():
			time.sleep(0.5)

	def QueryCleared(self):
		if self.cleared:
			return True

		reply = QueryAddress(self.address, self.next_query_commitnum)
		if isinstance(reply, str):
			#print 'Server reply to address query:', '"' + reply + '"'
			return False
		for entry in reply:
			commitnum = entry['commitment-number']
			commitnum = toint(commitnum)

			if toint(entry['commitment']) == toint(self.commitment):
				DecryptAmount(entry, self.destination, self.paynum)
				self.amount = Amounts.Decode(toint(entry['amount']), False)
				self.commitnum = commitnum
				self.params += ', "commitment-iv" : "' + entry['commitment-iv'] + '"'
				self.params += ', "domain" : "' + hex(toint(entry['domain'])) + '"'
				self.params += ', "asset" : "' + hex(toint(entry['asset'])) + '"'
				self.params += ', "amount" : "' + hex(toint(entry['amount'])) + '"'
				self.serialnum = ComputeSerialnum('monitor-secret', self.monitor_secret, self.commitment, commitnum)
				self.cleared = True
				return True

		self.next_query_commitnum = commitnum + 1
		return False

	def WaitForSpent(self):
		self.QueryPublishedHashkey(True)

	def QueryPublishedHashkey(self, wait_for_cleared = True):
		while True:
			result = QuerySerialnum(self.serialnum, True)
			#print result
			if result is None and wait_for_cleared:
				time.sleep(0.5)
			else:
				return result

class Participant:

	def __init__(self):
		self.spend_secret = GenerateSpendSecret(TX_INPUT_BITS)
		self.monitor_secret = ComputeMonitorSecret(self.spend_secret)
		self.destination, self.destination_params  = GenerateDestination('"' + self.monitor_secret + '"')

		self.hashkey = GenerateSpendSecret(TX_HASHKEY_WIRE_BITS)
		self.hashed_hashkey = ComputeMonitorSecret(self.hashkey)

	def SubmitMint(self):
		monitor_secret = self.monitor_secret
		destination = self.destination
		destination_params = self.destination_params
		paynum = 0
		inputs = QueryInputs('')
		inputs = json.dumps(inputs)
		inputs = inputs[1:-1]	# strip off outer brackets
		jstr = '{"tx-create" : {'
		jstr += '"mint" : {'
		jstr += inputs
		jstr += ', "no-precheck" : "' + str(int(not tx_precheck)) + '"'
		jstr += ', "source-chain" : "' + NetParams.blockchain + '"'
		jstr += ', "destination-chain" : "' + NetParams.blockchain + '"'
		jstr += ', "donation" : "' + hex(Amounts.Encode(TX_CC_MINT_DONATION, 0, True)) + '"'
		jstr += ', "outputs" : ['
		jstr += '{"destination" : "' + destination + '"'
		jstr += ', "payment-number" : "' + hex(paynum) + '"'
		jstr += ', "amount" : "' + hex(Amounts.Encode(TX_CC_MINT_AMOUNT - TX_CC_MINT_DONATION, 0, False)) + '"'
		jstr += ', "asset-mask" : 0'
		jstr += ', "amount-mask" : 0'
		jstr += '}]}}}'

		output = SubmitTx(jstr)
		if isinstance(output, str) or not output['tx-accepted']:
			print output
			DumpTx()
			raise Exception

		next_commitnum = output['next-commitment-number']
		output = output['outputs'][0]
		self.minted_bill = Bill(0, monitor_secret, destination, output['address'], output['commitment'], next_commitnum, destination_params, paynum)

	def WaitForMint(self):
		self.minted_bill.WaitForCleared()

	# Send a hashlocked payment, such that:
	#	- The sender can reclaim after a time delay
	#	- The recipient can claim immediately by including the lock secret as a publicly-published or a hidden input,
	#		depending on value of the @require_public_hashkey parameter chosen when the Tx is created
	def SendHashlockedTx(self, payor_monitor_secret, hashed_key, require_public_hashkey, locktime):
		unixtime = int(time.time()) + locktime
		locktime = UnixtimeToLocktime(unixtime)
		checkdelta = LocktimeToUnixtime(locktime) - unixtime
		if checkdelta < 0 or checkdelta >= TX_TIME_DIVISOR:
			raise Exception
		monitor_secret = payor_monitor_secret
		output_monitor_secrets = '"' + payor_monitor_secret + '","' + hashed_key + '","' + self.monitor_secret + '"'
		destination_params = '"require-public-hashkey":' + str(int(require_public_hashkey))
		destination_params += ',"use-spend-secrets":[1,1,0]'
		destination_params += ',"use-trust-secrets":[0,0,1]'
		destination_params += ',"trust-locktime":' + str(locktime)
		destination, destination_params = GenerateDestination(output_monitor_secrets, destination_params)
		paynum = 0
		bill = self.minted_bill
		inputs = QueryInputs(str(bill.commitnum))
		input = json.dumps(inputs['inputs'][0])
		input = input[:-1]	# strip off trailing bracket
		input += ', ' + bill.params
		input += ', "spend-secret" : "' + self.spend_secret + '"'
		input += '}'
		inputs['inputs'][0] = json.loads(input)
		return FinishTx(locktime, monitor_secret, destination, destination_params, paynum, bill.amount, inputs, False)

	# Claim contingent Tx sent by another party
	def ClaimIncomingHashlockedTx(self, bill, hashkey, publish_hashkey, expect_fail = False):
		return self.ClaimTx(bill, False, hashkey, publish_hashkey, expect_fail)

	# Reclaim contingent Tx sent by this party
	def ReclaimSentTx(self, bill, expect_fail = False, submit_only_if_locked = False):
		return self.ClaimTx(bill, True, None, False, expect_fail, submit_only_if_locked)

	# Common implementation of Claim/Reclaim operations
	def ClaimTx(self, bill, reclaim, hashkey, publish_hashkey, expect_fail, submit_only_if_locked = False):
		monitor_secret = self.monitor_secret
		destination = self.destination
		destination_params = self.destination_params
		paynum = 0
		inputs = QueryInputs(str(bill.commitnum))
		input = json.dumps(inputs['inputs'][0])
		input = input[:-1]	# strip off trailing bracket
		input += ', ' + bill.params
		if reclaim:
			input += ', "spend-secrets" : [null, null, "' + self.spend_secret + '"]'
		else:
			input += ', "spend-secrets" : ["' + self.spend_secret + '","' + hashkey + '"]'
		if publish_hashkey:
			input += ', "hashkey" : "' + hashkey + '"'
		input += '}'
		inputs['inputs'][0] = json.loads(input)
		if submit_only_if_locked:
			#print 'locktime', bill.locktime, 'parameter-time', toint(inputs['parameter-time'])
			if bill.locktime > toint(inputs['parameter-time']):
				expect_fail = True
			else:
				return True
		return FinishTx(0, monitor_secret, destination, destination_params, paynum, bill.amount, inputs, expect_fail)

def FinishTx(locktime, monitor_secret, destination, destination_params, paynum, input_amount, inputs, expect_fail):
	#pprint.pprint(inputs)
	inputs = json.dumps(inputs)
	inputs = inputs[1:-1]	# strip off outer brackets
	jstr = '{"tx-create" : {'
	jstr += '"tx-pay" : {'
	jstr += inputs
	jstr += ', "no-precheck" : "' + str(int(not tx_precheck)) + '"'
	jstr += ', "source-chain" : "' + NetParams.blockchain + '"'
	jstr += ', "destination-chain" : "' + NetParams.blockchain + '"'
	est_txsize, donation = Amounts.ComputeDonation(1,1)
	while True:
		output_amount = Amounts.Truncate(input_amount - donation, 0, False)
		donation = Amounts.Truncate(input_amount - output_amount, 0, True)
		if input_amount == output_amount + donation:
			break
	jstr += ', "donation" : "' + hex(Amounts.Encode(donation, 0, True)) + '"'
	jstr += ', "outputs" : ['
	jstr += '{"destination" : "' + destination + '"'
	jstr += ', "payment-number" : "' + hex(paynum) + '"'
	jstr += ', "amount" : "' + hex(Amounts.Encode(output_amount, 0, False)) + '"'
	jstr += ', "asset-mask" : "' + hex((1 << Amounts.asset_bits) - 1) + '"'
	jstr += ', "amount-mask" : "' + hex((1 << Amounts.amount_bits) - 1) + '"'
	jstr += '}]}}}'

	output = SubmitTx(jstr)
	if isinstance(output, str) or not output['tx-accepted']:
		if expect_fail:
			print '=== OK: tx correctly failed'
			return False
		print '=== ERROR: tx failed'
		print output
		DumpTx()
		raise Exception

	next_commitnum = toint(output['next-commitment-number'])
	#print 'next_commitnum', next_commitnum, not next_commitnum

	if not next_commitnum:
		# next_commitnum == 0 means input billet was already spent
		if expect_fail:
			print '=== OK: tx correctly failed because it is a double-spend'
			return False
		print '=== ERROR: tx failed because it is a double-spend'
		DumpTx()
		raise Exception

	if expect_fail:
		print '=== ERROR: tx should have failed'
		DumpTx()
		raise Exception

	print '=== OK: tx succeeded'

	output = output['outputs'][0]
	return Bill(locktime, monitor_secret, destination, output['address'], output['commitment'], next_commitnum, destination_params, paynum)

####################################################################################
#
# tests
#

def Test2WaySetup():

	a = Participant()
	b = Participant()

	print '\n===== Minting one billet for each Participant =====\n'

	a.SubmitMint()
	b.SubmitMint()

	a.WaitForMint()
	b.WaitForMint()

	print '\n===== Participant A sending hashlocked tx A->B =====\n'
	a2b = a.SendHashlockedTx(payor_monitor_secret = b.monitor_secret, hashed_key = a.hashed_hashkey, require_public_hashkey = False, locktime = 2 * atomic_locktime)
	a2b.WaitForCleared()

	print '\n===== Participant B sending hashlocked tx B->A -- A must publish hashkey to claim =====\n'
	b2a = b.SendHashlockedTx(payor_monitor_secret = a.monitor_secret, hashed_key = a.hashed_hashkey, require_public_hashkey = True, locktime = atomic_locktime)
	b2a.WaitForCleared()

	return (a, b, a2b, b2a)

def Test2WayCheckClaim(a, b, a2b, b2a, expect_fail = False):

	print '\n===== Participant A attempting to claim B->A by publishing the hashkey -- expect_fail', expect_fail, '=====\n'
	a.ClaimIncomingHashlockedTx(b2a, a.hashkey, True, expect_fail = expect_fail)

	published_hashkey = b2a.QueryPublishedHashkey()

	if not expect_fail and toint(published_hashkey) != toint(a.hashkey):
		print 'ERROR: published hash key mismatch'
		raise Exception

	print '\n===== Participant B attempting to claim A->B by privately using the hashkey published by A -- expect_fail', expect_fail, '=====\n'
	b.ClaimIncomingHashlockedTx(a2b, published_hashkey, False, expect_fail = expect_fail)

	published_hashkey = a2b.QueryPublishedHashkey()

	if toint(published_hashkey) == toint(a.hashkey) or toint(published_hashkey) == toint(b.hashkey):
		print 'ERROR: published hash key was not random'
		raise Exception

def Test2Way():

	print '\n===== Test2Way =====\n'

	(a, b, a2b, b2a) = Test2WaySetup()

	print '\n===== Participant A attempting to claim hashlocked tx B->A without publishing hashkey -- should fail =====\n'
	a.ClaimIncomingHashlockedTx(b2a, a.hashkey, False, expect_fail = True)

	print '\n===== Participant A attempting to claim hashlocked tx B->A with incorrect hashkey -- should fail =====\n'
	a.ClaimIncomingHashlockedTx(b2a, b.hashkey, True, expect_fail = True)

	print '\n===== Participant B attempting to claim hashlocked tx A->B with incorrect hashkey -- should fail =====\n'
	b.ClaimIncomingHashlockedTx(a2b, b.hashkey, False, expect_fail = True)

	Test2WayCheckClaim(a, b, a2b, b2a)

	Test2WayCheckReclaim(a, b, a2b, b2a, expect_fail = True)
	Test2WayCheckClaim(a, b, a2b, b2a, expect_fail = True)

	print '\n===== Test2Way done\n'

def Test2WayCheckReclaim(a, b, a2b, b2a, expect_fail = False):

	print '\n===== Participant B waiting until B->A reclaim has unlocked\n'
	while True:
		if b.ReclaimSentTx(b2a, submit_only_if_locked = True):
			break
		Participant().SubmitMint()	# cause the blockchain to advance
	print '=== OK: unlocked'

	print '\n===== Participant B attempting to reclaim B->A -- expect_fail', expect_fail, '=====\n'
	b.ReclaimSentTx(b2a, expect_fail = expect_fail)

	if not expect_fail:
		published_hashkey = b2a.QueryPublishedHashkey()

		if toint(published_hashkey) == toint(a.hashkey) or toint(published_hashkey) == toint(b.hashkey):
			print 'ERROR: published hash key was not random'
			raise Exception

	print '\n===== Participant A waiting until A->B reclaim has unlocked\n'
	while True:
		if a.ReclaimSentTx(a2b, submit_only_if_locked = True):
			break
		Participant().SubmitMint()	# cause the blockchain to advance
	print '=== OK: unlocked'

	print '\n===== Participant A attempting to reclaim A->B -- expect_fail', expect_fail , '=====\n'
	a.ReclaimSentTx(a2b, expect_fail = expect_fail)

	if not expect_fail:
		published_hashkey = a2b.QueryPublishedHashkey()

		if toint(published_hashkey) == toint(a.hashkey) or toint(published_hashkey) == toint(b.hashkey):
			print 'ERROR: published hash key was not random'
			raise Exception

def Test2WayReclaim():

	print '\n===== Test2WayReclaim =====\n'

	(a, b, a2b, b2a) = Test2WaySetup()

	Test2WayCheckReclaim(a, b, a2b, b2a)

	Test2WayCheckClaim(a, b, a2b, b2a, expect_fail = True)
	Test2WayCheckReclaim(a, b, a2b, b2a, expect_fail = True)

	print '\n===== Test2WayReclaim done\n'



####################################################################################
#
# main
#

def main(argv):
	if len(argv) < 2 or len(argv) > 6:
		print
		print 'Usage: python atomic-swap-test.py <port> [<locktime>] [<tx_precheck=0>] [<show_queries=0>] [<use_tor_proxy>]'
		print
		print ' port:'
		print '       The standalone Tor proxy by default listens at port 9050'
		print '       The Tor Browser bundle   by default listens at port 9150'
		print '       Tx server on localhost   by default listens at port 9220'
		print
		print ' locktime:'
		print '       Time in seconds until sender can abort and reclaim hashlocked output'
		print '       Defaults to 40 if use_tor_proxy is false; defaults to 90 if use_tor_proxy is true'
		print
		print ' tx_precheck:'
		print '       0 = tx validity is checked only by verifying the Zero Knowledge Proof'
		print '       1 = tx validity is checked prior to creating a Zero Knowledge Proof'
		print
		print ' show_queries:'
		print '       0 = normal script output'
		print '       1 = log messages to/from tx server/network'
		print
		print ' use_tor_proxy:'
		print '       Defaults to 1 if port is 9050, 9150, 9226, or 29206; otherwise defaults to 0'

		exit()

	cclib.net_port = int(argv[1])

	if len(argv) > 3:
		global tx_precheck
		tx_precheck = int(argv[3])

	if len(argv) > 4:
		cclib.show_queries = int(argv[4])
		cclib.show_activity = cclib.show_queries

	if len(argv) > 5:
		cclib.use_tor_proxy = int(argv[5])
	elif cclib.net_port == 9050 or cclib.net_port == 9150 or cclib.net_port == 9226 or cclib.net_port == 29206:
		cclib.use_tor_proxy = True
	else:
		cclib.use_tor_proxy = False

	if len(argv) > 2:
		atomic_locktime = int(argv[2])
	elif cclib.use_tor_proxy:
		atomic_locktime = 90
	else:
		atomic_locktime = 40

	NetParams.Query()	# get network parameters

	while True:
		Test2WayReclaim()
		Test2Way()
		break

if __name__ == '__main__':
	main(sys.argv)
