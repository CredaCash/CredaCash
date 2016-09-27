'''
CredaCash(TM) Wallet Simulation Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2016 Creda Software, Inc.

This program exercises the CredaCash network by simulating a wallet that sends
and receives transactions. The differences between what this script does and
what a real wallet might do are noted, and if not noted, it is safe to assume a
real wallet would work identically.

For further information on the how a wallet interacts with the CredaCash
network, and the API it can use, see:

	The CredaCash Wallet Developer's Guide
	The CredaCash Transaction API Reference Manual
	The CredaCash Transaction Protocol

This script can also be used to randomly double-spend a valid output, and verifies
that the double-spend attempt never clears.

This script can also be used to submit transactions in which one bit has been
randomly flipped, and verifies these are always rejected and do not crash the server.

'''

import ctypes
import sys
import json
import random
import hashlib
import codecs
import collections
import time
import socket

hexencode = codecs.getencoder('hex')

####################################################################################
#
# Simulation Parameters (these can be changed)
#

# Maximum number of intput and output bills in each transaction

txmaxin = 2
txmaxout = 4

# More than one payment can be sent to each destination
# This is the probability the simulation will create a new destination;
#	otherwise, it will reuse the last destination

prob_create_new_destination = 0.5

# The Payor can send payments to paynum 0, or paynum 1, 2, 3, etc., or to a random paynum
# This simulation will try paynum = 0 or paynum = random
# This is the probability to sends a payment to paynum = 0;
#	otherwise, it will send it to a random paynum

prob_pay_to_paynum_zero = 0.5

# After a transaction in submitted to the transaction server, this is the minimum amount of time
# in seconds that will pass before the simulation checks to see if the transaction has cleared.
# In the meantime, it will create more transactions.
# Set this to zero to see how long it takes transactions to clear.
# Set this to a higher number to spend more time creating transactions and less time waiting.

cleared_check_time_lag = 0

# This is the target number of unspent bills in the wallet for simulation purposes

unspent_target = 5

# These parameters are passed directly to the "tx-create" function

skip_tx_precheck = 0
test_use_larger_zkkey = 0

# This is a hostname of a public transaction server

server_hostname = 'vgadzkt75uulci7k.onion'

# This parameters are used to connect to the Tx server

net_timeout = 30

# These are for testing

extra_on_wire = False			# must match TEST_EXTRA_ON_WIRE in code
double_spend_probability = 0.5	# probability of attempting to double-spend a bill, if test_double_spends is True
double_spend_wait_time = 60		# seconds that must elapse before deciding a double-spend attempt did not succeed
test_nfuzz = 5					# number of times to fuzz a transaction, if test_fuzz_txs is True

test_spent_serialnums = {}		# to check for doubles-spends; a real wallet does not need this

# Notes on double-spend testing:
#	txmaxin must be at least 2 to test double-spends inside the same transaction
#	The witness block rate must be slower than this scipt's transaction rate to test double-spends in the same block
#	Set witness-test-mal = 1 for at least one witness so it will generate blocks that contain double-spends

####################################################################################
#
# Functions to interface with the CredaCash library
#

# DoJsonCmd
#
# DoJsonCmd calls the CredaCash library entry point:
#		int32_t CCTx_JsonCmd(const char *json, char *buffer, const uint32_t bufsize)
#
# DoJsonCmd itself takes four parameters:
#	jstr := string containing the JSON command and parameters
#	binary := boolean indicating whether the JSON command will place binary data in the buffer if successful
#	buf := data to place in the buffer before calling CCTx_JsonCmd; if None, CCTx_JsonCmd will be passed an empty buffer to use for its output
#		This is intended to be used with JSON commands that take binary input data
#	returnrc := boolean indicating whether to return a tuple consisting of the CCTx_JsonCmd return code, and the buffer contents
#		If false, only the buffer contents are returned
#		This is useful where the CCTx_JsonCmd return code is required to interpret the buffer contents

CCTx_JsonCmd = ctypes.windll.cctx64.CCTx_JsonCmd
CCTx_JsonCmd.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]

jsoncmd_bufsize = 40000			# size of buffer

class CCCmdFailed(Exception):
    pass

def DoJsonCmd(jstr, binary = False, buf = None, returnrc = False):
	if show_queries and not jstr.startswith('{"work-'):
		print '>>>', jstr
	input = ctypes.c_char_p(jstr)		# create ctypes buffer from JSON string
	if buf:
		buffer = ctypes.create_string_buffer(buf, jsoncmd_bufsize)	# create ctypes buffer from buf
	else:
		buffer = ctypes.create_string_buffer(jsoncmd_bufsize)		# create an empty ctypes buffer
	bufsize = ctypes.c_uint(jsoncmd_bufsize)

	sys.stdout.flush()				# prevents output from getting interleaved when the DLL writes to stdout
	rc = CCTx_JsonCmd(input, buffer, bufsize)
	#print 'CCTx_JsonCmd', rc
	if rc and not returnrc:			# handle errors the function caller doesn't want returned
		print 'CCTx_JsonCmd failed', rc
		print '>>>', jstr
		if not buf:
			print '<<<', buffer.value
		raise CCCmdFailed
	if binary:
		if returnrc:
			return rc, buffer.raw	# return buffer as binary
		return buffer.raw			# return buffer as binary
	if show_queries and not jstr.startswith('{"work-'):
		print '<<<', buffer.value
	if returnrc:
		return rc, buffer.value		# return buffer as string
	return buffer.value				# return buffer as string

####################################################################################
#
# Functions to interface with the transaction server
#

# RandomLetter and RandomString are used to create a random username for the Tor proxy
# This causes Tor proxy to open a fresh circuit for increased privacy (although it takes longer, too)
def RandomLetter():
	c = random.randrange(52)
	if c < 26:
		return chr(ord('a') + c)
	else:
		return chr(ord('A') + c - 26)

def RandomString(n):
	string = ''
	for i in range(n):
		string += RandomLetter()
	return string

# Build a Socks4 connect string for the Tor proxy
def SocksConnectString(server, proxyuser):
	string = "\x04\x01\x01\xBB\x00\x00\x00\x01"	# the string is: Socks4, connect, port 443, ip 0.0.0.1
	string += proxyuser							# add a Socks4 user id; Tor gives every user id it's own circuit
	string += chr(0)
	string += server
	string += chr(0)
	return string

# Send binary data to a transaction server optionally via Tor proxy
def SendServer(msg, proxyuser):
	sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	sock.settimeout(net_timeout)
	sock.connect(('127.0.0.1', net_port))
	if use_tor_proxy:
		connstring = SocksConnectString(server_hostname, proxyuser)
		#print hexencode(connstring)
		#print connstring
		sock.sendall(connstring)	# send Tor proxy a Socks4 connection string

		data = sock.recv(32)
		#print hexencode(data)
		if len(data) != 8:			# Tor should always return 8 bytes
			print 'reply', len(data), 'nbytes'
			raise Exception
		if ord(data[0]):
			raise Exception
		result = ord(data[1])		# second byte should be 90
		if result != 90:
			print 'Socks status', result
			raise Exception

	#time.sleep(20)	# for testing server timeout
	sock.sendall(msg)			# send the binary data directed to the transaction server
	reply = ''
	while True:					# read reply from transaction server
		data = sock.recv(4000)
		if not data:
			break
		reply += data
	#print 'server reply:', reply

	sock.shutdown(socket.SHUT_RDWR)
	sock.close()
	return reply

# GetMsgSize extracts the size from a binary message
def GetMsgSize(msg):
	size = 0
	for i in range(3,-1,-1):					# size is 32-bit unsigned int in little-endian format
		size = size * 256 + ord(msg[i])
	return size

# Add proof-of-work to a binary message
def TryAddProofOfWork(msg, difficulty):
	print 'adding proof-of-work...'

	jstr = '{"work-reset" :'
	jstr += ' {"timestamp" : "' + str(int(time.time()) + NetParams.clock_diff) + '"'
	jstr += '}}'
	msg = DoJsonCmd(jstr, True, msg)

	for i in range(8):		# update all 8 nonces
		while True:
			jstr = '{"work-add" :'
			jstr += ' {"index" : "' + str(i) + '"'
			jstr += ', "iterations" : "' + str(1 << 34) + '"'	# do 2^34 iterations at a time, then come back here in case the user tries to stop the program
			jstr += ', "difficulty" : "' + difficulty + '"'
			jstr += '}}'

			rc, msg = DoJsonCmd(jstr, True, msg, True)
			if rc < 0:
				print '"work-add" failed; retrying'
				return False
			if rc == 0:			# this nonce is now valid
				break
	return msg

def AddProofOfWork(msg, difficulty):
	while True:
		rv = TryAddProofOfWork(msg, difficulty)
		if rv == False:
			continue	# keep retrying until success
		return rv

# This class holds the parameters needed to communicate with the transaction server
class NetParams():
	clock_diff = 0
	query_work_difficulty = '0'
	tx_work_difficulty = '0'

	@staticmethod
	def Update(reply, show = False):
		NetParams.clock_diff = int(reply['timestamp'], 16) - int(time.time())
		if show:
			print 'clock_diff', NetParams.clock_diff
		NetParams.query_work_difficulty = reply['query-work-difficulty']
		if show:
			print 'query-work-difficulty', NetParams.query_work_difficulty
		NetParams.tx_work_difficulty = reply['tx-work-difficulty']
		if show:
			print 'tx-work-difficulty', NetParams.tx_work_difficulty

	@staticmethod
	def Query():
		jstr = '{"tx-query-create": {"tx-parameters-query": {}}}'
		query = DoJsonCmd(jstr, True)
		retries = 0
		while True:
			print 'Fetching network parameters...'
			reply = TryServer('query', query, RandomString(12))
			if not reply.startswith('ERROR'):
				if show_queries:
					print '<<<', reply
				break
			print 'Server reply to parameter query:', reply
			retries += 1
			if retries > 20:
				raise Exception
			time.sleep(2)
		try:
			reply = json.loads(reply)
			reply = reply['tx-parameters-query-results']
		except:
			print 'Parameter query unexpected reply:', reply
			raise Exception
		NetParams.Update(reply, True)

# Try sending a message to the transaction server
# If retry is true, it keeps retrying until it gets a response
def TryServer(label, msg, proxyuser, isquery = True, retry = True):
	nbytes = GetMsgSize(msg)
	print label, 'length', nbytes, 'bytes'
	#print hexencode(msg[:nbytes])

	# add proof-of-work
	if isquery:
		difficulty = NetParams.query_work_difficulty	# query needs proof-of-work with query_work_difficulty
	else:
		difficulty = NetParams.tx_work_difficulty		# transaction needs proof-of-work with tx_work_difficulty
	msg = AddProofOfWork(msg, difficulty)

	# send msg to server
	msg = msg[:nbytes]		# strip off the unused msg bytes
	#print hexencode(msg)
	while True:
		try:
			#print int(time.time()),
			print 'Sending', label, 'to server...'
			reply = SendServer(msg, proxyuser)
			if len(reply) < 1 or (reply[0] == '{' and (len(reply) < 2 or reply[-1] != '}')):
				print 'SubmitServer incomplete response length', len(reply)
			else:
				return reply
		except Exception as e:
			print 'SubmitServer exception', e
			if retry:
				time.sleep(10)	# pause so we don't overload tor
		if not retry:
			return 'SubmitServer failed'

# Send a message to the transaction server
# If retry is true and it gets a response that starts with ERROR, then it will retry 20 times before giving up
def SubmitServer(label, msg, proxyuser, isquery = True, retry = True):
	retries = 0
	while True:
		reply = TryServer(label, msg, proxyuser, isquery, retry)
		if not reply.startswith('ERROR'):
			if show_queries:
				print '<<<', reply
			return reply
		print 'Server reply:', '"' + reply + '"'
		if not retry:
			return reply
		retries += 1
		if retries > 20:
			raise Exception
		time.sleep(2)
		print 'Updating network parameters...'
		NetParams.Query()
		print 'Retrying server request'

# used for testing to fuzz the server
def flipbit(wire, nwire):
	wire2 = bytearray(wire)
	byte = random.randrange(nwire - 48)		# proof of work size = 48
	#byte = 8								# for testing
	#byte = nwire - 48 - 1					# for testing
	if byte >= 8:							# 8 = header size (size and tag words)
		byte += 48							# don't flip bit in proof of work because it might not make the tx invalid (bytes at offset 8-55 inclusive)
	bit = random.randrange(8)
	print 'flipping byte', byte, 'bit', bit, 'original value', hex(wire2[byte])
	if byte < 0 or byte >= nwire:
		print 'flipbit error byte', byte, 'nwire', nwire, 'msg size', GetMsgSize(wire)
		raise Exception
	wire2[byte] = wire2[byte] ^ (1 << bit)
	if byte >= 56 and byte < 64:
		return flipbit(wire, nwire)			# bit flipped in param_level, which might not make the tx invalid, so flip another bit to make sure
	return str(wire2)

####################################################################################
#
# Bill class -- stores all attributes associated with a bill
#

class Bill:
	# For the purpose of debugging and reporting, this simulation assigns a sequential seqnum to each bill
	# A real wallet doesn't have to do this, but might find it helpful.
	next_bill_seqnum = 0

	def __init__(self, wallet, destnum, outvalmin, outvalmax, outvals_public):
		self.wallet = wallet		# store a reference to the wallet because it generates the spend-secret
		self.seqnum = Bill.next_bill_seqnum
		self.already_spent = False
		Bill.next_bill_seqnum += 1

		# The destination number is chosen by the Payor to get different bill spend secrets which results in different payment destinations
		#	which in turn results in different payment addresses. The payment address is the value that is publicly published in the blockchain.
		# Once a payment destination is sent to another user, she could make multiple payments to the same address.  In this simulation,
		# the destination number and payment number are sometimes reused so we can test and demonstrate handling multiple payments to the same address.
		self.destnum = destnum

		# The payment number should be chosen by the Payor using the "sequence-type" specified by the Payee when the payspec was generated.
		# This simulation tests the range of sequence-type's by chosing a paynum of either zero or a random 128-bit number.
		if random.random() < prob_pay_to_paynum_zero:
			self.paynum = 0 	# if the payspec sequence-type is "1" and multiple payments are sent to the same payspec, the wallet should use a different sequential payment-number (0, 1, 2, etc) for each payment
		else:
			self.paynum = random.getrandbits(128)	# a real wallet should do this whenever the payspec sequence-type is "9"

		# The bill value would normally be chosen by the Payor, possibly using the number suggested by the Payee and included in the payspec
		# in this simulation, we chose a random 64 bit number scaled down so we can use the donation value to balance the transaction input and output values
		maxval = min(outvalmax + 1, (1 << 59))
		self.value = random.randrange(outvalmin, maxval)

		# Since we already know the payment-number, we can compute the address and value-xor now and save them for later
		self.ComputeAddressAndValueEnc(outvals_public)

	def SpendSecret(self):
		# The spend secret can be regenerated at anytime using the bill's destnum and the wallet_master_secret
		return self.wallet.DestinationSpendSecret(self.destnum)

	def Seqnum(self):
		return self.seqnum

	def Destnum(self):
		return self.destnum

	def Destination(self):
		# The payment destination depends only on the bill's spend-secret
		# Normally, the Payee would use the spend-secret to generate a payspec and send it to the Payor who would decode it to determine the payment destination
		# This simulation is both the Payee and Payor, so it both generates a payspec and decodes it to extract the payment desination.
		jstr = '{"payspec-encode" :'
		jstr += ' {"payspec" :'
		jstr += ' {"sequence-type" : "0"'	# this simulation uses only sequence-type "0"; a real wallet would use "1" or "9"
		jstr += ', "spend-secret" : "' + self.SpendSecret() + '"'
		jstr += '}}}'
		payspec = DoJsonCmd(jstr)

		# decode payspec and extract destination
		jstr = '{"payspec-decode" : "' + payspec + '"}'
		payspec = DoJsonCmd(jstr)
		payspec = json.loads(payspec)
		payspec = payspec['payspec']
		return payspec['destination']		# this function only returns the destination; a real wallet would also need sequence-type and requested-amount

	def Paynum(self):
		return self.paynum

	def ComputeAddressAndValueEnc(self, outvals_public):
		# The address and value-xor depend only on the destination and the payment-number
		# The Payee can computes the address using the "compute-address" function, or it can be provided by the Payor.
		# The Payor can get the address from "tx-to-json" after calling "tx-create".
		# In this simulation, we do both and make sure they match
		jstr = '{"compute-address" :'
		jstr += ' {"destination" : "' + self.Destination() + '"'
		jstr += ', "payment-number" : "' + hex(self.Paynum()) + '"'
		jstr += '}}'
		results = DoJsonCmd(jstr)

		results = json.loads(results)
		#print results
		self.address = results['address']
		if outvals_public:
			self.value_xor = 0
		else:
			self.value_xor = int(results['value-encode-xor'], 16)

	def Address(self):
		return self.address

	def Value(self):
		return self.value

	# Construct json string to publish this bill in a transaction
	def ValueAndJson(self, input = False):
		jstr = '{"destination" : "' + self.Destination() + '"'
		jstr += ', "payment-number" : "' + hex(self.Paynum()) + '"'
		value = self.Value()
		jstr += ', "value" : "' + hex(value) + '"'
		if input:
			jstr += ', "commitment-iv" : "' + self.CommitIV() + '"'
			jstr += ', "spend-secret" : "' + self.SpendSecret() + '"'
		jstr += '}'
		return value, jstr

	# These values are needed to spend the bill
	def SetCommitment(self, address, value_enc, commitment_iv, commitment):
		#print 'bill', self.seqnum, 'address', address, 'commitment-iv', commitment_iv, 'commitment', commitment
		# check the values that come out of "compute-address" against the value from "tx-create"
		if address != self.address:
			print 'SetCommitment address mismatch', address, self.address
			raise Exception
		value_decrypted = int(value_enc, 16) ^ self.value_xor
		if value_decrypted != self.value:
			print 'SetCommitment value mismatch', value_enc, hex(self.value_xor), hex(value_decrypted), hex(self.value)
			raise Exception
		self.commitment_iv = commitment_iv
		self.commitment = commitment

	def CommitIV(self):
		return self.commitment_iv

	def Commitment(self):
		return self.commitment

	def SetCommitnum(self, commitnum):
		#print 'SetCommitnum bill', self.seqnum, 'commitnum', commitnum
		self.commitnum = commitnum

	def Commitnum(self):
		return self.commitnum

####################################################################################
#
# TxOuts class
#
# This class stores an address, commitment and serial-number from a transaction, so the wallet can look them up and see if the transaction has cleared
# Note, the wallet could look up all of the commitments and/or all of the serial numbers in a transaction, but looking up just one is enough

class TxOuts:
	def __init__(self):
		self.bills = []
		self.next_query_commitnum = 0
	def AddBill(self, bill):
		self.bills.append(bill)
	def AddOutput(self, i, address, value_enc, commitment_iv, commitment):
		self.bills[i].SetCommitment(address, value_enc, commitment_iv, commitment)
	def AddInputs(self, serialnums):
		self.serialnums = serialnums
	def NBills(self):
		return len(self.bills)
	def FirstSeqnum(self):
		return self.bills[0].Seqnum()
	def Bills(self):
		return self.bills
	def SetTime(self):
		self.time = time.time()
	def Time(self):
		return self.time
	def SetNextQueryCommitnum(self, n):
		self.next_query_commitnum = n
	def NextQueryCommitnum(self):
		return self.next_query_commitnum

####################################################################################
#
# Wallet class
#

class Wallet:
	def __init__(self):
		self.wallet_next_destnum = 0
		self.txs_unconfirmed = collections.deque()
		self.bills_unspent = {}

		passphrase_hash_milliseconds = 4000

		print
		print 'Master secret target generation time =', passphrase_hash_milliseconds/1000, 'seconds'

		jstr = '{"master-secret-generate" :'
		jstr += '{"milliseconds" : ' + str(passphrase_hash_milliseconds)
		jstr += '}}'
		self.scrambled_master_secret = DoJsonCmd(jstr)

		print 'The scrambled master secret =', self.scrambled_master_secret

	def DecodeMasterSecret(self):
		passphrase = "this is a test"		# this would normally be input by the user
		print 'The wallet passphrase =', '"' + passphrase +'"'

		jstr = '{"master-secret-descramble" :'
		jstr += '{"scrambled-master-secret" : "' + self.scrambled_master_secret + '"'
		jstr += ',"passphrase" : "' + passphrase + '"'
		jstr += '}}'
		self.wallet_master_secret = DoJsonCmd(jstr)

		print 'The wallet master secret =', self.wallet_master_secret

	def DestinationSpendSecret(self, destnum):
		# This wallet generates pseudorandom bill spend-secret's using the wallet_master_secret as a seed
		# This function return the n'th spend-secret
		hasher = hashlib.sha256()
		hasher.update('ss' + str(destnum) + self.wallet_master_secret)
		return '0x' + hasher.hexdigest()

	# Simulate a transation
	def SimulateTx(self):
		# chose random number of inputs and outputs
		while True:
			nout = random.randrange(txmaxout + 1)
			nin = random.randrange(txmaxin + 1)
			if len(self.bills_unspent) < unspent_target and nin > nout:
				nin = min(nout, txmaxin)
			if len(self.bills_unspent) > unspent_target and nin < nout:
				nin = min(nout, txmaxin)
			if nin > len(self.bills_unspent):
				nin = len(self.bills_unspent)
			if nout + nin > 0:
				break

		print
		print
		if nout > 1:
			print 'Creating bills seqnum''s', Bill.next_bill_seqnum, '-', Bill.next_bill_seqnum + nout - 1,
		elif nout == 1:
			print 'Creating bill seqnum', Bill.next_bill_seqnum,
		else:
			print 'Creating transaction with no outputs',
		print 'with unspent pool of', len(self.bills_unspent), 'bills'

		inbills = []
		insum = outsum = 0

		inseqnums = []		# used only for trace/debugging output
		invals = []			# used only for trace/debugging output
		outvals = []		# used only for trace/debugging output

		# figure out the transaction inputs
		unspent_keys = self.bills_unspent.keys()
		unspent_keys.sort()
		double_spend_attempt = False
		inputs = '{"tx-query-create" :'
		inputs += ' {"tx-input-query" : ['
		for i in range(nin):
			if i:
				inputs += ', '
			bill = self.bills_unspent[unspent_keys[i]]
			del self.bills_unspent[unspent_keys[i]]
			if bill.already_spent and not double_spend_attempt:
				double_spend_attempt = True
				print 'This transaction will be a double-spend attempt'
			if not double_spend_attempt and test_double_spends and random.random() < double_spend_probability:
				# requeue the bill to be spent again at a random time, possibly even in this same transaction
				print 'Requeuing bill for a double-spend attempt'
				spend_order = random.getrandbits(32)
				#spend_order = 1							# spend this bill next
				self.bills_unspent[spend_order] = bill
				unspent_keys = self.bills_unspent.keys()
				#unspent_keys.insert(0, 0)					# spend this bill in same tx
				unspent_keys.sort()
			bill.already_spent = True
			inbills.append(bill)
			inseqnums.append(bill.seqnum)
			val, input = bill.ValueAndJson(True)
			invals.append(val)
			insum += val
			inputs += ' {"address" : "' + bill.Address() + '"'
			inputs += ', "commitment-number-start" : "' + hex(bill.Commitnum()) + '"'
			inputs += '}'
		inputs += ']}}'
		#print inputs

		query = DoJsonCmd(inputs, True)
		print 'Fetching Merkle paths...'
		rawreply = SubmitServer('query', query, RandomString(12))
		try:
			reply = json.loads(rawreply)
			reply = reply['tx-input-query-report']
		except:
			print 'Input query unexpected reply:', rawreply
			raise Exception

		NetParams.Update(reply)
		donation_per_tx = int(reply['donation-per-transaction'], 16)
		donation_per_byte = int(reply['donation-per-byte'], 16)
		donation_per_output = int(reply['donation-per-output'], 16)
		donation_per_input = int(reply['donation-per-input'], 16)

		inputs = reply['tx-input-query-results']
		outvalmin = int(inputs['minimum-output-value'], 16)
		outvalmax = int(inputs['maximum-output-value'], 16)

		for i in range(nin):
			input = inputs['inputs'][i]
			bill = inbills[i]
			if input['address'] != bill.Address():
				print 'address mismatch', bill.Seqnum(), input['address'], bill.Address()
				raise Exception
			if input['commitment-iv'] != bill.CommitIV():
				print 'commitment-iv mismatch', bill.Seqnum(), input['commitment-iv'], bill.CommitIV()
				raise Exception
			if input['commitment'] != bill.Commitment():
				print 'commitment mismatch', bill.Seqnum(), input['commitment'], bill.Commitment()
				raise Exception
			if int(input['commitment-number'], 16) != bill.Commitnum():
				print 'commitment-number mismatch', bill.Seqnum(), input['commitment-number'], int(input['commitment-number'], 16), bill.Commitnum()
				raise Exception
			# in the results returned by the server, the wallet needs to make some substitutions and additions...
			del input['address']
			del input['encrypted-value']
			input['payment-number'] = hex(bill.Paynum())
			input['value'] = hex(bill.Value())
			input['spend-secret'] = bill.SpendSecret()
		inputs = json.dumps(inputs)
		inputs = inputs[1:-1]	# strip off outer brackets
		#print inputs

		if extra_on_wire:
			outvals_public = random.getrandbits(1)
			if outvals_public:
				print 'outvals_public', outvals_public
		else:
			outvals_public = 0

		# starting creating transaction in json format
		jstr = '{"tx-create" : {"tx-pay" : {'
		jstr += '"no-precheck" : "' + str(random.getrandbits(1) | skip_tx_precheck) + '"'	# either skip randomly or skip always
		jstr += ', "test-use-larger-zkkey" : "' + str(random.getrandbits(1) * test_use_larger_zkkey) + '"'
		if outvals_public or random.getrandbits(1):
			jstr += ', "outvals-public" : "' + hex(outvals_public) + '"'
		if extra_on_wire and random.getrandbits(1):
			jstr += ', "nonfinancial" : 1'
		jstr += ', ' + inputs
		jstr += ', "outputs" : ['

		# figure out the transaction outputs
		if nout:
			# note: this script only creates TxOuts and adds then to txs_unconfirmed when nout > 0
			# that means tx's with no outputs will not be queried to see if/when they clear
			txouts = TxOuts()
			txouts.double_spend = None
			self.txs_unconfirmed.append(txouts)
			for i in range(nout):
				if i:
					jstr += ', '
				if not self.wallet_next_destnum or random.random() < prob_create_new_destination:
					destnum = self.wallet_next_destnum
					self.wallet_next_destnum += 1
				else:
					destnum = random.randrange(self.wallet_next_destnum)
				bill = Bill(self, destnum, outvalmin, outvalmax, outvals_public)
				txouts.AddBill(bill)
				val, output = bill.ValueAndJson()
				outvals.append(val)
				outsum += val
				jstr += output
		jstr += ']'

		# compute suggested donation...
		ninw = nin
		est_txsize = 365 + nout * 72 + nin * 33 + (nin - ninw) * 32
		donation = donation_per_tx + est_txsize * donation_per_byte + nout * donation_per_output + nin * donation_per_input
		print 'The estimated transaction size is', est_txsize, 'bytes and the suggested donation is', donation

		# ...but don't use it because this simulation uses the donation to balance the inputs and outputs
		donation = insum - outsum

		print 'Input bill sequence numbers:', inseqnums
		print 'Input bill values:', invals
		print 'Output bill values:', outvals
		print 'Witness donation:', donation

		jstr += ', "donation" : "' + hex(donation) + '"'

		if extra_on_wire:
			if len(outvals):
				jstr += ', "minimum-output-value" : "' + hex(min(outvals)) + '"'
				jstr += ', "maximum-output-value" : "' + hex(max(outvals)) + '"'
			if len(invals):
				jstr += ', "maximum-input-value" : "' + hex(max(invals)) + '"'

		jstr += '}}}'

		# create the transaction and place it into the library's internal transaction buffer
		print 'Generating transaction...'
		try:
			result = DoJsonCmd(jstr)
			if result:
				print result
				raise CCCmdFailed
		except CCCmdFailed:
			print '"tx-create" failed'
			jstr = '{"tx-dump" : {}}'
			result = DoJsonCmd(jstr)
			print result
			raise Exception

		if 0:	# for debugging
			jstr = '{"tx-dump" : {}}'
			result = DoJsonCmd(jstr)
			print result

		# Extract and store the input serialnums, and all output addresses and commitments, so we can later look them up to see
		#	when the bills have cleared and spend them
		# If the Payor does not send this info to the Payee, the Payee has to guess at the payment-number used by the Payor,
		#	use the "compute-address" command to get the address, then query a transaction server for new commitments
		jstr = '{"tx-to-json" : {}}'
		output = DoJsonCmd(jstr)
		output = json.loads(output)
		output = output['tx-pay']
		commitment_iv = hex(int(output['merkle-root'], 16) & ((1 << 128) - 1))
		commitment_iv = commitment_iv.rstrip('L') # remove this so string comparisons work
		if nout:
			serialnums = []
			for i in range(nin):
				serialnum = output['inputs'][i]['serial-number']
				for j in serialnums:
					if j == serialnum and not txouts.double_spend:
						print 'This is a double-spend attempt with duplicate serial numbers'
						txouts.double_spend = True
				serialnums.append(serialnum)
			txouts.AddInputs(serialnums)		# note: a real wallet doesn't need to track the input serial numbers
		output = output['outputs']
		#print output
		for i in range(nout):
			address = output[i]['address']
			value_enc = output[i]['encrypted-value']
			commitment = output[i]['commitment']
			#print address, commitment_iv, commitment
			txouts.AddOutput(i, address, value_enc, commitment_iv, commitment)

		# convert the library's internal transaction buffer to a binary "wire" output format
		jstr = '{"tx-to-wire" : {}}'
		wire = DoJsonCmd(jstr, True)
		# double check the size computation
		txsize = GetMsgSize(wire)
		if est_txsize != txsize and not extra_on_wire:
			print 'ERROR: the transaction size estimate was wrong', est_txsize, '!=', txsize
			raise Exception

		# test that flipping a random bit in the binary tx makes it invalid
		# do this before submitting the valid tx to make sure the invalid tx's don't affect the valid tx
		if test_fuzz_txs:
			proxyuser = RandomString(12)
			for t in range(test_nfuzz):
				wire2 = flipbit(wire, txsize)
				try:
					reply = SubmitServer('transaction', wire2, proxyuser, False, False)
				except Exception as e:
					print 'SubmitServer exception', e
				else:
					print 'Server reply:', '"' + reply + '"'
					if not reply.startswith('INVALID') and not reply.startswith('ERROR') and not reply == 'SubmitServer failed':
						raise Exception

		# submit binary transaction to the server
		reply = SubmitServer('transaction', wire, RandomString(12), False)
		#print 'Server reply:', '"' + reply + '"'
		reply_split = reply.split(':')
		if reply_split[0] == 'OK':
			print 'Transaction accepted'
		elif double_spend_attempt and reply_split[0] == 'INVALID' and (reply_split[1] == 'already spent' or reply_split[1] == 'duplicate serial number'):
			print 'Server reply:', '"' + reply + '"'
			reply_split[1] = 0	# let this tx through and SetNextQueryCommitnum = 0 to make sure it doesn't clear
		else:
			print 'Server reply:', '"' + reply + '"'
			raise Exception
		if nout:
			txouts.SetTime()
			txouts.SetNextQueryCommitnum(int(reply_split[1]))

	# Query the next entry in the self.txs_unconfirmed list to see if it has cleared
	# return True if this function should be called again to check the next txouts
	def UpdateCleared(self):
		if not len(self.txs_unconfirmed):
			return False
		txouts = self.txs_unconfirmed[0]
		if test_double_spends and txouts.double_spend is None:
			# a double spend attempt could clear before the initial spend, so we need to check a table of serialnums to make sure only one clears
			txouts.double_spend = False
			txouts.recheck_double_spend = True
			for i in txouts.serialnums:
				if i in test_spent_serialnums:
					txouts.double_spend = True
					break
		txtime = txouts.Time()
		dt = time.time() - txtime
		#print 'txtime', txtime, 'cleared_check_time_lag', cleared_check_time_lag, 'elapsed time', dt
		if dt < cleared_check_time_lag:
			return False
		if txouts.double_spend and dt < double_spend_wait_time:
			return False
		self.txs_unconfirmed.popleft()
		check_seqnum = txouts.FirstSeqnum()
		proxyuser = RandomString(12)
		qcount = 0
		print
		while True:
			if test_double_spends and not txouts.double_spend and (txouts.recheck_double_spend or qcount > 25):
				# When a double-spend attempt is submitted, either the earlier submitted or the later submitted
				# transaction will succeed, while the other will fail.  This script checks transactions in the
				# order they were submitted, so the test_spent_serialnums dictionary won't indicate if a later
				# submitted double-spend causes this transaction to fail.  Instead, after double_spend_wait_time
				# elapses, query the server to see if one of the transaction's serialnums is already indelible,
				# which indicates a double-spend that is causing this transaction to fail.
				dt = time.time() - txtime
				if dt > double_spend_wait_time:
					txouts.recheck_double_spend = False
					print
					print 'Checking if this transaction is a double-spend...'
					for i in txouts.serialnums:
						if QueryTxSerialnum(i, proxyuser):
							print 'This transaction is a double-spend of an already cleared transaction.'
							txouts.double_spend = True
							break	# we could just return, but instead continue on to print "Querying...did not clear"
			print
			print 'Querying status of',
			if txouts.double_spend:
				print 'double-spent',
			if txouts.NBills() > 1:
				print 'seqnum''s', check_seqnum, '-', check_seqnum + txouts.NBills() - 1
			else:
				print 'seqnum', check_seqnum
			cleared = QueryTxOutputs(txouts, proxyuser)
			if cleared:
				break
			if txouts.double_spend:
				print 'Double-spend did not clear'
				return True
			if qcount > 35:	# !!! for debugging
				print vars(txouts)
				print vars(txouts.Bills()[0])
			qcount += 1
			if qcount > 20:
				time.sleep(qcount-20)
			else:
				time.sleep(0.5)
			if qcount > 120:
				print
				print 'Timeout waiting for the transaction to clear'
				print
				raise Exception
		if txouts.NBills() > 1:
			print 'seqnum''s', check_seqnum, '-', check_seqnum + txouts.NBills() - 1, 'have cleared;',
		else:
			print 'seqnum', check_seqnum, 'has cleared;',
		print 'elapsed time', round(time.time() - txouts.Time(), 1), 'seconds'
		if test_double_spends:
			for i in txouts.serialnums:
				test_spent_serialnums[i] = True
		if len(txouts.serialnums):
			# Make sure the serialnum query works, too.  A real wallet doesn't need to do this.
			print
			print 'Verifying the transaction inputs are listed as spent...'
			for i in txouts.serialnums:
				if not QueryTxSerialnum(i, proxyuser):
					print i
					raise Exception
		# queue the outputs to be spent
		for bill in txouts.Bills():
			spend_order = random.getrandbits(32)
			self.bills_unspent[spend_order] = bill
		return True

####################################################################################
#
# Functions to check if a transaction has cleared
#

# This is the query the Payor would normally use to see if a transaction has cleared
def QueryTxSerialnum(serialnum, proxyuser):
	jstr = '{"tx-query-create" :'
	jstr += ' {"tx-serial-number-query" :'
	jstr += ' {"serial-number" : "' + serialnum + '"'
	jstr += '}}}'
	query = DoJsonCmd(jstr, True)
	reply = SubmitServer('query', query, proxyuser)
	print 'Server reply to serial-number query:', '"' + reply + '"'
	if reply.startswith('Indelible'):
		return True
	return False

# This is the query the Payee would normally use to see if it has received any payments
def QueryTxOutputs(txouts, proxyuser):
	bill = txouts.Bills()[0]
	jstr = '{"tx-query-create" :'
	jstr += ' {"tx-address-query" :'
	jstr += ' {"address" : "' + bill.Address() + '"'
	jstr += ', "commitment-number-start" : "' + hex(txouts.NextQueryCommitnum()) + '"'
	jstr += '}}}'
	query = DoJsonCmd(jstr, True)
	reply = SubmitServer('query', query, proxyuser)
	if reply.startswith('Not Found'):
		print 'Server reply to address query:', '"' + reply + '"'
		return False
	try:
		reply = json.loads(reply)
		reply = reply['tx-address-query-report']['tx-address-query-results']
	except:
		print 'Address query unexpected reply:', reply
		raise Exception
	commitnum = None
	for entry in reply:
		commitnum = entry['commitment-number']
		commitnum = int(commitnum, 16)
		# Here we compare strings instead of the comparing the values, which is dangerous because the same value can be
		# represented by different strings. But it works here because all strings come from the same json parser.
		#print 'W', entry['commitment']
		#print 'S', bill.Commitment()
		if entry['commitment'] == bill.Commitment():
			# This wallet's bill was found in the results returned by the server
			# Let's double-check the commitment-iv returned by the server
			if entry['commitment-iv'] != bill.CommitIV():
				print 'commitment-iv mismatch', bill.Seqnum(), entry['commitment-iv'], bill.CommitIV()
				raise Exception
			# The bill has cleared.	At this point, we want to store the commitment-number with the bill which is required to spend it.
			# A real wallet that recieves payments would have to monitor all of its payment addresses for incoming bills, and record the
			# commitment-numbers.  Here however this simulation cheats. If one bill in the transaction has cleared, then they have all
			# cleared and they have sequential commitment-numbers. So instead of taking the time to lookup all the bills, the simulation
			# simply sets all of the commitment-numbers in the transaction, which it can do because it sent the payment to itself and
			# knows what output bills are in the transaction.
			for bill in txouts.Bills():
				bill.SetCommitnum(commitnum)
				commitnum += 1		# all output bills in a transaction have sequential commitment-number's
			return True		# found
	# Next time we query, skip past any bills we're already seen by setting NextQueryCommitnum to the last seen commitnum + 1
	# In a real wallet, NextQueryCommitnum would be associated with each address.  In this simulation, it is associated with the
	# transaction and used in conjunction with the address of the first output bill in the transaction.
	if commitnum is not None:
		txouts.SetNextQueryCommitnum(commitnum + 1)
	return False	# not found

####################################################################################
#
# main
#

def main(argv):
	global net_port, test_double_spends, test_fuzz_txs, show_queries, use_tor_proxy, wallet

	if len(argv) < 2 or len(argv) > 4:
		print
		print 'Usage: python wallet-sim.py <port> [<test_bad_txs>] [<show_queries>] [<use_tor_proxy>]'
		print
		print ' Note: The standalone Tor proxy by default listens at port 9050'
		print '       The Tor Browser bundle   by default listens at port 9150'
		print '       Tx server on localhost   by default listens at port 9223'
		print
		exit()

	net_port = int(argv[1])

	test_double_spends = False
	test_fuzz_txs = False
	if len(argv) > 2 and int(argv[2]):
		test_double_spends = True	# test double-spending a bill
		test_fuzz_txs = True		# test sending random tx data to server (TEST_BREAK_ON_ASSERT in code must be false)

	show_queries = False
	if len(argv) > 3:
		show_queries = int(argv[3])

	if net_port == 9050 or net_port == 9150:
		use_tor_proxy = True
	else:
		use_tor_proxy = False
	if len(argv) > 4:
		use_tor_proxy = int(argv[4])

	wallet = Wallet()
	wallet.DecodeMasterSecret()

	NetParams.Query()					# get network parameters

	while True:
		wallet.SimulateTx()				# simulate one transaction
		while wallet.UpdateCleared():	# possibly check if a transaction has cleared, and if one was found, check for next
			pass
		time.sleep(0.20)

if __name__ == '__main__':
	main(sys.argv)
