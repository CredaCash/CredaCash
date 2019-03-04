'''
CredaCash(TM) Library Functions for Python

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2019 Creda Software, Inc.

'''

import sys
import json
import random
import hashlib
import codecs
import ctypes
import socket
import time
import pprint

if not sys.version.startswith('2.7.') or not ('GCC' in sys.version or '64 bit' in sys.version or 'AMD64' in sys.version):
	print 'This script requires Python 2.7x for x86-64.'
	exit()

# Global variables

class cclib:
	show_activity = False
	show_queries = False
	use_tor_proxy = False
	server_hostname = None
	net_port = None
	net_timeout = 30

# These values are used in mint transactions

TX_CC_MINT_AMOUNT		= 200000*10**27
TX_CC_MINT_DONATION		= 199000*10**27
TX_MINT_NOUT			= 1

# Value sizes

TX_ACCEPT_REQ_DEST_MASK		= 0x01F
TX_STATIC_ADDRESS_MASK		= 0xFE0

TX_TIME_DIVISOR				= 30
TX_TIME_OFFSET				= 1546300800

TX_CHAIN_BITS				= 32
TX_TYPE_BITS				= 16
TX_REVISION_BITS			= 32
TX_REFHASH_BITS				= 256
TX_RESERVED_BITS			= 64
TX_INPUT_BITS				= 256
TX_FIELD_BITS				= 253	# 253 instead of 254 so random numbers will be less than the prime
TX_BLOCKLEVEL_BITS			= 40
TX_TIME_BITS				= 32
TX_SPEND_SECRETNUM_BITS		= 18
TX_DESTNUM_BITS				= 30
TX_DELAYTIME_BITS			= 8
TX_PAYNUM_BITS				= 20
TX_ADDRESS_BITS				= 128
TX_REPEAT_BITS				= 16
TX_POOL_BITS				= 20
TX_ASSET_BITS				= 64
TX_ASSET_WIRE_BITS			= 32
TX_AMOUNT_BITS				= 40
TX_AMOUNT_EXPONENT_BITS		= 5
TX_DONATION_BITS			= 16
TX_COMMIT_IV_BITS			= 128
TX_MERKLE_DEPTH				= 40
TX_COMMITNUM_BITS			= TX_MERKLE_DEPTH + 8
TX_HASHKEY_BITS				= TX_INPUT_BITS
TX_HASHKEY_WIRE_BITS		= 128

TX_MAX_SECRETS				= 7
TX_MAX_SECRET_SLOTS			= TX_MAX_SECRETS + 1
TX_MAX_RESTRICTED_ADDRESSES	= 6

TX_AMOUNT_EXPONENT_MASK		= (1 << TX_AMOUNT_EXPONENT_BITS) - 1

FIELD_MODULUS = 21888242871839275222246405745257275088548364400416034343698204186575808495617

####################################################################################
#
# Functions to interface with the CredaCash library
#

# convert value to a python integer

def toint(s):
	#print type(s)
	if not isinstance(s, str) and not isinstance(s, unicode):
		v = int(s)
	else:
		s = s.lower().rstrip('l')
		if s.startswith('x') or s.startswith('0x'):
			v = int(s, 16)
		else:
			v = int(s)
	#print 'toint',s,v
	return v

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

CCTx_DLL = ctypes.cdll.LoadLibrary('./cctx64.dll')
CCTx_JsonCmd = CCTx_DLL.CCTx_JsonCmd
CCTx_JsonCmd.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]

jsoncmd_maxoutsize = 40000			# size of text output buffer
jsoncmd_maxbinsize = 2000			# size of binary buffer

ERR = '?'

class CCCmdFailed(Exception):
    pass

def DoJsonCmd(jstr, binary = False, buf = None, returnrc = False):
	if cclib.show_queries and not jstr.startswith('{"work-'):
		print '>>>', jstr
	input = ctypes.c_char_p(jstr)										# create ctypes buffer from JSON string
	output = ctypes.create_string_buffer(jsoncmd_maxoutsize)			# create mutable ctypes buffer for result
	if buf:
		binbuf = ctypes.create_string_buffer(buf, len(buf))				# create mutable ctypes buffer from buf
	elif binary:
		binbuf = ctypes.create_string_buffer(jsoncmd_maxbinsize)		# create an empty mutable ctypes buffer
	else:
		binbuf = None
	#print 'binbuf', binbuf, len(binbuf or '')
	sys.stdout.flush()				# prevents output from getting interleaved when the DLL writes to stdout
	rc = CCTx_JsonCmd(input, output, len(output), binbuf, len(binbuf or ''))
	#print 'CCTx_JsonCmd', rc
	if rc and not returnrc:			# handle errors the function caller doesn't want returned
		print 'CCTx_JsonCmd failed', rc
		print '>>>', jstr
		if len(output.value):
			print '<<<', output.value
		raise CCCmdFailed
	if cclib.show_queries and not jstr.startswith('{"work-') and len(output.value):
		print '<<<', output.value
	if binary:
		if not rc and len(output.value):
			print '*** Unexpected output from CCTx_JsonCmd'
			print '<<<', output.value
			raise Exception
		if returnrc:
			return rc, output.value, binbuf.raw		# return output as string and binbuf as binary
		return binbuf.raw							# return binbuf as binary
	else:
		if returnrc:
			return rc, output.value					# return output as string
		return output.value							# return output as string

####################################################################################
#
# Functions to interface with the transaction server
#

hexencode = codecs.getencoder('hex')

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
def SocksConnectString(server, proxyuser = None):
	if proxyuser is None:
		proxyuser = RandomString(12)
	string = "\x04\x01\x01\xBB\x00\x00\x00\x01"	# the string is: Socks4, connect, port 443, ip 0.0.0.1
	string += proxyuser							# add a Socks4 user id; Tor gives every user id it's own circuit
	string += chr(0)
	string += server
	string += chr(0)
	return string

# Send binary data to a transaction server optionally via Tor proxy
def SendServer(msg, proxyuser = None):
	sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	sock.settimeout(cclib.net_timeout)
	sock.connect(('127.0.0.1', cclib.net_port))
	if cclib.use_tor_proxy:
		connstring = SocksConnectString(cclib.server_hostname, proxyuser)
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
		data = sock.recv(4096)
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
	if cclib.show_activity:
		print 'adding proof-of-work...'

	jstr = '{"work-reset" :'
	jstr += ' {"timestamp" : "' + str(int(time.time()) + NetParams.clock_diff) + '"'
	jstr += '}}'
	msg = DoJsonCmd(jstr, True, msg)

	for i in range(8):		# update all 8 nonces
		while True:
			jstr = '{"work-add" :'
			jstr += ' {"index" : "' + str(i) + '"'
			jstr += ', "iterations" : "' + hex(1 << 34) + '"'	# do 2^34 iterations at a time, then come back here in case the user tries to stop the program
			jstr += ', "difficulty" : "' + difficulty + '"'
			jstr += '}}'

			rc, text, msg = DoJsonCmd(jstr, True, msg, returnrc = True)
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
	blockchain = 0
	query_work_difficulty = '0'
	tx_work_difficulty = '0'

	@staticmethod
	def Update(reply, show = False):
		NetParams.clock_diff = toint(reply['timestamp']) - int(time.time())
		if show:
			print 'clock_diff', NetParams.clock_diff
		NetParams.blockchain = reply['blockchain-number']
		if show:
			print 'blockchain-number', NetParams.blockchain
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
			if cclib.show_activity:
				print 'Fetching network parameters...'
			reply = TryServer('query', query)
			if not reply.startswith('ERROR'):
				if cclib.show_queries:
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
			print 'tx-parameters-query unexpected reply:', reply
			raise Exception
		NetParams.Update(reply, cclib.show_activity)

# Try sending a message to the transaction server
# If retry is true, it keeps retrying until it gets a response
def TryServer(label, msg, proxyuser = None, isquery = True, retry = True):
	nbytes = GetMsgSize(msg)
	if cclib.show_activity:
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
			if cclib.show_activity:
				print 'Sending', label, 'to server...'
			reply = SendServer(msg, proxyuser)
			if len(reply) > 0 and reply[-1] == '\0':
				reply = reply[:-1]
			if len(reply) < 1 or (reply[0] == '{' and (len(reply) < 2 or reply[-1] != '}')):
				print 'SubmitServer incomplete response length', len(reply)
				#print 'SubmitServer response', reply
				#print 'SubmitServer last two chars', reply[-2], '=', ord(reply[-2]), ';', reply[-1], '=', ord(reply[-1])
			else:
				return reply
		except Exception as e:
			print 'SubmitServer exception', e
		if not retry:
			return 'SubmitServer failed'
		time.sleep(10)	# pause so we don't overload tor

# Send a message to the transaction server
# If retry is true and it gets a response that starts with ERROR, then it will retry 20 times before giving up
def SubmitServer(label, msg, proxyuser = None, isquery = True, retry = True, return_timeout = False):
	retries = 0
	while True:
		reply = TryServer(label, msg, proxyuser, isquery, retry)
		if not reply.startswith('ERROR') or (return_timeout and reply == 'ERROR:server timeout'):
			if cclib.show_queries:
				print '<<<', reply
			return reply
		if cclib.show_activity:
			print 'Server reply:', '"' + reply + '"'
		if not retry:
			return reply
		retries += 1
		if retries > 20:
			raise Exception
		time.sleep(2)
		if cclib.show_activity:
			print 'Updating network parameters...'
		NetParams.Query()
		if cclib.show_activity:
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

# This class holds the transaction amount and donation parameters and computes donations
class Amounts():
	@staticmethod
	def UpdateParams(reply):
		Amounts.donation_per_tx = toint(reply['donation-per-transaction'])
		Amounts.donation_per_byte = toint(reply['donation-per-byte'])
		Amounts.donation_per_output = toint(reply['donation-per-output'])
		Amounts.donation_per_input = toint(reply['donation-per-input'])
		Amounts.minimum_donation = toint(reply['minimum-donation-per-transaction'])
		Amounts.asset_bits = toint(reply['asset-bits'])
		Amounts.amount_bits = toint(reply['amount-bits'])
		Amounts.donation_bits = toint(reply['donation-bits'])
		Amounts.exponent_bits = toint(reply['exponent-bits'])
		key = 'tx-input-query-results'
		if key in reply:
			limits = reply[key]
		else:
			limits = reply
		Amounts.outvalmin = toint(limits['minimum-output-exponent'])
		Amounts.outvalmax = toint(limits['maximum-output-exponent'])
		Amounts.invalmax = toint(limits['maximum-input-exponent'])

	@staticmethod
	def ComputeDonation(nout, nin):
		pred_size = 304 + nout*57
		if nin:
			pred_size += nin*48
		else:
			pred_size -= 2 + nout*4
		donation = max(			  Amounts.Decode(Amounts.minimum_donation, True),
				1				* Amounts.Decode(Amounts.donation_per_tx, True)
				+ pred_size		* Amounts.Decode(Amounts.donation_per_byte, True)
				+		nout	* Amounts.Decode(Amounts.donation_per_output, True)
				+		nin		* Amounts.Decode(Amounts.donation_per_input, True)
			)

		#print 'pred_size', pred_size, 'nout', nout, 'nin', nin, 'donation', donation

		return (pred_size + 48, donation)

	@staticmethod
	def DecodeExponent(v):
		return v & ((1 << Amounts.exponent_bits) - 1)

	@staticmethod
	def Decode(v, is_donation, return_exponent = False):
		exponent = v & ((1 << Amounts.exponent_bits) - 1)
		mantissa = (v >> Amounts.exponent_bits)
		if exponent:
			mantissa += 1

		if return_exponent:
			return (mantissa * 10**exponent, exponent)
		else:
			return mantissa * 10**exponent

	@staticmethod
	def Encode(v, asset, is_donation, min_exponent = -1, max_exponent = -1, rounding = -1):
		if asset or is_donation:
			if min_exponent < 0: min_exponent = 0
			if max_exponent < 0: max_exponent = 999
		else:
			if min_exponent < 0: min_exponent = Amounts.outvalmin
			if max_exponent < 0: max_exponent = Amounts.outvalmax

		max_exponent = min(max_exponent, (1 << Amounts.exponent_bits) - 1)

		jstr = '{"encode-amount" :'
		jstr += '{ "amount" : "' + hex(v) + '"'
		jstr += ', "is-donation" : ' + str(int(is_donation))
		if is_donation:
			jstr += ', "donation-bits" : ' + str(Amounts.donation_bits)
		else:
			jstr += ', "amount-bits" : ' + str(Amounts.amount_bits)
		jstr += ', "exponent-bits" : ' + str(Amounts.exponent_bits)
		jstr += ', "minimum-exponent" : ' + str(min_exponent)
		jstr += ', "maximum-exponent" : ' + str(max_exponent)
		jstr += ', "rounding" : ' + str(int(rounding))
		jstr += '}}'
		result = DoJsonCmd(jstr)
		result = json.loads(result)
		result = result['amount-encoded']

		result = toint(result)

		if result >= (1 << 64) - 1:
			return None

		return result

	@staticmethod
	def Truncate(v, asset, is_donation, min_exponent = -1, max_exponent = -1, rounding = -1):
		return Amounts.Decode(Amounts.Encode(v, asset, is_donation, min_exponent, max_exponent, rounding), is_donation)

	@staticmethod
	def MaxAmount(asset, min_exponent = -1, max_exponent = -1):
		return Amounts.Truncate(1 << 200, asset, False, min_exponent, max_exponent)

def UnixtimeToLocktime(unixtime):
	locktime = (unixtime + NetParams.clock_diff - TX_TIME_OFFSET + TX_TIME_DIVISOR - 1) / TX_TIME_DIVISOR
	if locktime < 1:
		return 1
	else:
		return locktime

def LocktimeToUnixtime(locktime):
	return locktime * TX_TIME_DIVISOR + TX_TIME_OFFSET - NetParams.clock_diff

def HasAcceptanceRequired(destination):
	return not (toint(destination) & TX_ACCEPT_REQ_DEST_MASK)

def HasStaticAddress(destination):
	return not (toint(destination) & TX_STATIC_ADDRESS_MASK)

def DumpTx():
	result = DoJsonCmd('{"tx-dump":{}}')
	if not cclib.show_queries:
		print result

def SubmitTx(jstr, tx_to_wire_check = 2, test_nfuzz = 0):
		rc, text = DoJsonCmd(jstr, returnrc = True)
		if rc:
			return text

		# Extract the output
		jstr = '{"tx-to-json" : {}}'
		output = DoJsonCmd(jstr)
		output = json.loads(output)
		if 'mint' in output:
			output = output['mint']
		else:
			output = output['tx-pay']

		# convert the library's internal transaction buffer to binary "wire" format
		jstr = '{"tx-to-wire" : {'
		jstr += '"error-check":' + str(int(tx_to_wire_check))
		jstr += '}}'
		rc, text, wire = DoJsonCmd(jstr, True, returnrc = True)
		if rc:
			return text
		txsize = GetMsgSize(wire)

		if test_nfuzz:
			# test that flipping a random bit in the binary tx makes it invalid
			# do this before submitting the valid tx to make sure the invalid tx's don't affect the valid tx
			proxyuser = RandomString(12)
			#print 'test_nfuzz', test_nfuzz
			for t in range(test_nfuzz):
				wire2 = flipbit(wire, txsize)
				try:
					reply = SubmitServer('transaction', wire2, proxyuser, False, False)
				except Exception as e:
					pass
					#print 'SubmitTx SubmitServer exception', e
					#raise Exception
				else:
					#print 'Server reply:', '"' + reply + '"'
					if reply == 'SubmitServer failed' or reply.startswith('ERROR') or reply.startswith('INVALID'):
						pass
					else:
						print 'SubmitTx server unexpected reply:', '"' + reply + '"'
						raise Exception
			print 'Submitting original transaction...'

		if 0:
			print '***** TEST: waiting for multiprocess sync...'
			now = time.time()
			target = int(1 + now / 32) * 32
			sleep = target - now - 0.5
			if sleep > 0:
				time.sleep(sleep)
			while time.time() < target:
				pass

		# submit binary transaction to the server
		reply = SubmitServer('transaction', wire, None, False, return_timeout = True)
		#print 'Server reply:', '"' + reply + '"'

		reply_split = reply.split(':')

		if reply_split[0] == 'OK' or reply == 'ERROR:server timeout':
			# treat timeout as OK since if the transaction was valid, it should still be accepted even when server validation times out
			output['tx-accepted'] = True
		else:
			output['tx-accepted'] = False

		if reply_split[0] == 'OK':
			output['next-commitment-number'] = toint(reply_split[1])
		else:
			output['next-commitment-number'] = 0

		output['wire-size'] = txsize
		output['submit-response'] = reply

		return output

def QueryAddress(address, commit_start, proxyuser = None):
	jstr = '{"tx-query-create" :'
	jstr += ' {"tx-address-query" :'
	jstr += ' {"blockchain" : "' + NetParams.blockchain + '"'
	jstr += ', "address" : "' + address + '"'
	jstr += ', "commitment-number-start" : "' + str(commit_start) + '"'
	jstr += '}}}'
	query = DoJsonCmd(jstr, True)
	reply = SubmitServer('query', query, proxyuser)
	if reply.startswith('Not Found'):
		return reply
	try:
		parsed = json.loads(reply)
		parsed = parsed['tx-address-query-report']['tx-address-query-results']
		return parsed
	except:
		print 'tx-address-query unexpected reply:', reply
		raise Exception

def DecryptAmount(entry, destination, paynum):
	if bool(entry['encrypted']):
		# The amount-xor depends on the commitment_iv, the destination and the payment-number
		# The Payee can compute the amount-xor using the "compute-amount-encryption" function, or it can be provided by the Payor.
		jstr = '{"compute-amount-encryption" :'
		jstr += ' {"commitment-iv" : "' + entry['commitment-iv'] + '"'
		jstr += ', "destination" : "' + destination + '"'
		jstr += ', "payment-number" : "' + hex(paynum) + '"'
		jstr += '}}'
		result = DoJsonCmd(jstr)
		result = json.loads(result)
		#print result

		entry['asset'] = toint(entry['encrypted-asset']) ^ (((1 << toint(entry['asset-bits'])) - 1) & toint(result['asset-encrypt-xor']))
		entry['amount'] = toint(entry['encrypted-amount']) ^ (((1 << toint(entry['amount-bits'])) - 1) & toint(result['amount-encrypt-xor']))

def QueryInputs(inputs):
	jstr = '{"tx-query-create" :'
	jstr += ' {"tx-input-query" :'
	jstr += ' {"blockchain" : "' + NetParams.blockchain + '"'
	jstr += ', "inputs" : ['
	jstr += inputs
	jstr += ']}}}'
	#print jstr

	query = DoJsonCmd(jstr, True)
	reply = SubmitServer('query', query)
	try:
		parsed = json.loads(reply)
		parsed = parsed['tx-input-query-report']

		NetParams.Update(parsed)
		Amounts.UpdateParams(parsed)

		parsed = parsed['tx-input-query-results']
		return parsed
	except:
		print 'tx-input-query unexpected reply:', reply
		raise Exception

def ComputeSerialnum(secret_name, secret, commitment, commitnum):
	jstr = '{"compute-serial-number" :'
	jstr += ' {"' + secret_name + '" : "' + secret + '"'
	jstr += ', "commitment" : "' + commitment + '"'
	jstr += ', "commitment-number" : "' + hex(commitnum) + '"'
	jstr += '}}'
	result = DoJsonCmd(jstr)
	result = json.loads(result)
	result = result['serial-number']
	return result

def QuerySerialnum(serialnum, return_hashkey = False, proxyuser = None):
	jstr = '{"tx-query-create" :'
	jstr += ' {"tx-serial-number-query" :'
	jstr += ' {"blockchain" : "' + NetParams.blockchain + '"'
	jstr += ', "serial-numbers" : ["' + serialnum + '"]'
	jstr += '}}}'
	query = DoJsonCmd(jstr, True)
	reply = SubmitServer('query', query, proxyuser)
	try:
		parsed = json.loads(reply)
		parsed = parsed['tx-serial-number-query-results'][0]
		if not return_hashkey:
			return parsed['status']
		elif parsed['status'] == 'indelible':
			return parsed['hashkey']
		else:
			return None
	except:
		print 'tx-serial-number-query unexpected reply:', reply
		raise Exception
