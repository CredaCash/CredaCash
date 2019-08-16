'''
CredaCash(TM) Transaction Library Test Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2019 Creda Software, Inc.

This script runs several tests against the CredaCash transaction library:

- Creates transactions across the full range of bill amounts and full range of nout, nin and nin_with_path
- Attempts proof and verification with either the properly sized keypair, or a randomly chosen larger keypair
- Verifies a valid transaction is always accepted
- Creates transactions using bad inputs, and verifies these are always rejected
- Randomly flips one bit in the binary wire representation of a valid transaction,
	and verifies this always causes the transaction to be rejected;
	this also tests the immutability of the public inputs if the code is built with TEST_EXTRA_ON_WIRE

To run:
	/Python27x64/python burn-tx.py > burn.out 2>&1

To detect potential problems, search the output for the substring "fail" (case insensitive)

'''

import ctypes
import sys
import json
import random
import codecs
import time
import pprint
from inspect import currentframe

# note: when checking test coverage, check with:
#	test_only_good_tx = 1, test_use_all_tx_inputs = 1, test_prob_mint = 0,   txmaxinw = 0, test_nfuzz = 0, extra_on_wire = 1
#	test_only_good_tx = 1, test_use_all_tx_inputs = 1, test_prob_mint = 0,   txmaxinw = 0, test_nfuzz = 0, extra_on_wire = 0
#	test_only_good_tx = 1, test_use_all_tx_inputs = 1, test_prob_mint = 0,   txmaxinw > 0, test_nfuzz = 0, extra_on_wire = 0
#	test_only_good_tx = 1, test_use_all_tx_inputs = 1, test_prob_mint = 1,   txmaxinw = 0, test_nfuzz = 0, extra_on_wire = 0
#	test_only_good_tx = 0, test_use_all_tx_inputs = 0, test_prob_mint = 0.2, txmaxinw = 0, test_nfuzz = 0, extra_on_wire = 1
#	test_only_good_tx = 0, test_use_all_tx_inputs = 0, test_prob_mint = 0.2, txmaxinw = 0, test_nfuzz = 0, extra_on_wire = 0
# note: tests are much faster with txmaxinw = 0, but wire tests of good tx's with extra_on_wire = 0 can only be done with txmaxinw > 0

test_use_all_tx_inputs = 0		# !!! normally 0; set True to always use all tx inputs and outputs; useful for checking test coverage
test_larger_zkkey = 1			# !!! normally 1; set False for faster testing
test_only_good_tx = 0			# !!! normally 0; set True to only create good tx's and for benchmarking
test_only_bad_tx = 0			# !!! normally 0; set True to only create bad tx's
test_skip_wire_tests = 0		# !!! normally 0; set True to skip wire tests and for benchmarking

test_prob_mint = 0.1			# !!! normally 0.1; set to 1 for all mints, or 0 for no mints and benchmarking
test_nfuzz = 50					# !!! normally 50; set to 0 to skip wire fuzz tests

extra_on_wire = 0				# !!! normally 0; must match TEST_EXTRA_ON_WIRE in code; set True for full benchmarking coverage
cc_exceptions_break = 0			# !!! normally 0; must be True if TEST_BREAK_ON_ASSERT in code is True

if 0: # !!! normally 0; for testing, set to 1 to repeat a prior run
	print '*** WARNING: STATIC RANDOM SEED ***'
	rndseed = 1046380615
	badsel_range = 266
	static_rand = 1
else:
	rndseed = random.getrandbits(30)
	badsel_range = 20
	static_rand = 0

if not sys.version.startswith('2.7.') or not ('GCC' in sys.version or '64 bit' in sys.version or 'AMD64' in sys.version):
	print 'This script requires Python 2.7.x for x86-64.'
	exit()

txmaxout = 10
txmaxinw = 8
txmaxin  = 8

if 0: # !!! normally 0; for testing; values below must match zkproof key capacity limits
	print '*** WARNING: OVERRIDING TX MAX IN/OUT ***'
	txmaxout = 5
	txmaxinw = 4
	txmaxin = 4

txmaxinc = txmaxinw + 4

TX_CC_MINT_AMOUNT			= 50000*10**27
TX_CC_MINT_EXPONENT			= 22

TX_ACCEPT_REQ_DEST_MASK		= 0x01F
TX_STATIC_ADDRESS_MASK		= 0xFE0

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
TX_MAX_SECRETS_BITS			= 3
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

CCTx_DLL = ctypes.cdll.LoadLibrary('./cctx64.dll')
CCTx_JsonCmd = CCTx_DLL.CCTx_JsonCmd
CCTx_JsonCmd.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]

maxwiresize = 4000

hexencode = codecs.getencoder('hex')

ERR = '?'

forig = open('orig.out', 'w')
fflip = open('flip.out', 'w')

good_count = 0
bad_count = 0

def getbadsel():
	if test_only_good_tx:
		return False
	global badsel
	badsel -= 1
	return ((badsel == 0 and not random.getrandbits(2)) or not random.randrange(3 * badsel_range))

def resetbad():
	global badstack, wirebad, wireverifybad
	wirebad = False
	wireverifybad = False
	badstack = [False]
	#print '%% badstack wirebad reset'

def pushbad():
	global badstack
	badstack.append(False)
	#print '%% pushbad', badstack

def popbad():
	global badstack
	badstack = badstack[:-1]
	#print '%% popbad', badstack

def isbad(traceok = True):
	traceok = False
	if traceok:
		print '%% isbad', badstack
	return badstack[-1]

def clearbad():
	global badstack
	badstack[-1] = False
	#print '%% clearbad', badstack

def setbad(what = '', back = 0):
	global badstack
	#print '%% setbad', badstack
	frame = currentframe().f_back
	for i in range(back):
		frame = frame.f_back
	print '%% SETBAD level', len(badstack), badstack[-1], what, 'at line', frame.f_lineno
	badstack[-1] = True

def setwirebad(what = '', back = 0):
	global wirebad
	if len(badstack) > 1:
		print '%% setwirebad badstack', badstack
		return
	frame = currentframe().f_back
	for i in range(back):
		frame = frame.f_back
	print '%% SETBAD level', len(badstack), 'wirebad', wirebad, what, 'at line', frame.f_lineno
	wirebad = True

def setwireverifybad(what = '', back = 0):
	global wireverifybad
	if len(badstack) > 1:
		print '%% setwireverifybad badstack', badstack
		return
	frame = currentframe().f_back
	for i in range(back):
		frame = frame.f_back
	print '%% SETBAD level', len(badstack), 'wireverifybad', wireverifybad, what, 'at line', frame.f_lineno
	wireverifybad = True

def fail():
	print '*** FAIL'
	print '%% badstack', badstack
	sys.stdout.flush()
	raise Exception # !!! for overnight testing, comment out this line and search output for "FAIL"
	return False

def flipbit(wire):
	nwire = len(wire)
	byte = random.randrange(nwire - 56)		# 56 = proof of work size 48 + param_level bytes 8
	#byte = 8								# for testing
	#byte = nwire - 56						# for testing
	if byte >= 8:							# 8 = header size (size and tag words)
		byte += 56							# don't flip bit in proof of work or proof_params byte (bytes at offset 8-63 inclusive)
	bit = random.randrange(8)
	wire2 = bytearray(wire)
	oldval = wire2[byte]
	newval = oldval ^ (1 << bit)
	print 'flipping byte', byte, 'of', nwire, 'bit', bit, 'original value', hex(oldval), 'new value', hex(newval)
	if byte < 0 or byte >= nwire:
		print 'flipbit error byte', byte, 'nwire', nwire, 'msg size', GetMsgSize(wire)
		raise Exception
	wire2[byte] = newval
	return str(wire2)

def dojson(jstr, binbuf = None, shouldfail = False, skipmsg = False, skipfail = False):
	#print '>>>', jstr
	sys.stdout.flush()
	input = ctypes.c_char_p(jstr)
	output = ctypes.create_string_buffer(80000)
	rc = CCTx_JsonCmd(input, output, len(output), binbuf, len(binbuf or ''))
	#print 'CCTx_JsonCmd', rc
	sys.stdout.flush()
	sys.stderr.flush()
	if not skipmsg and ((rc and not shouldfail) or (not rc and shouldfail)):
		if rc:
			print '*** CCTx_JsonCmd error', rc
		else:
			print '*** CCTx_JsonCmd should have returned an error'
			print '%% badstack', badstack
		if not skipfail:
			print '>>>', jstr
		if len(output.value) and not 'fail' in output.value:	# don't print the word "fail" in the output
			print '<<<', output.value
		sys.stdout.flush()
		if not skipfail:
			fail()
	if rc:
		return ERR
	else:
		#print '<<<', output.value
		return output.value

def getsize(buf):
	nbytes = 0
	for i in range(3,-1,-1):
		nbytes = nbytes * 256 + ord(buf[i])
		#print i, ord(buf[i]), nbytes
	return nbytes

def randbits(what, bits, defval = None, wantdef = False, nowire = False, wirebits = None, nobad = False):
	if wirebits is None:
		wirebits = bits
	if random.getrandbits(3):
		genbits = wirebits
	else:
		genbits = bits

	if defval is not None and wantdef and not getbadsel():
		val = defval
	elif not nobad and genbits < 256 and getbadsel():
		if random.getrandbits(6):
			if genbits < TX_FIELD_BITS:
				val = (1 << genbits)
			else:
				val = FIELD_MODULUS
		else:
			val = random.getrandbits(genbits + 1)
	elif random.getrandbits(5):
		val = random.getrandbits(genbits)
	elif genbits > 6 and random.getrandbits(2):
		val = random.getrandbits(5)
	elif defval is not None and random.getrandbits(1):
		val = defval
	elif random.getrandbits(1):
		if genbits < TX_FIELD_BITS:
			val = (1 << genbits) - 1
		else:
			val = FIELD_MODULUS - 1
	else:
		val = 0
	#print 'randbits', what, 'val', val, 'bits', bits, 'wirebits', wirebits, 'genbits', genbits, 'defval', defval, 'wantdef', wantdef, 'nowire', nowire, 'nobad', nobad
	if not nobad:
		if bits < 256 and (val >= FIELD_MODULUS or (bits < TX_FIELD_BITS and val >= (1 << bits))):
			#print 'randbits setbad', what, bits, hex(val)
			setbad(what, 1)
			setbad(what, 2)
		if val >= (1 << wirebits) or (nowire and val != defval):
			setwirebad(what, 1)
			setwirebad(what, 2)
	return val

def generate_values(dest, keys, filter, ismint = False):
	for key, attrs in keys.items():
		if not key in dest and attrs['group'].startswith(filter) and not 'dep' in attrs and not (not extra_on_wire and 'xwire' in attrs and not 'defval' in attrs):
			#print filter, key, attrs
			nbits = attrs['nbits']
			wirebits = nbits
			if key == 'asset-mask' and not extra_on_wire:
				wirebits = TX_ASSET_WIRE_BITS
			if 'defval' in attrs:
				defval = attrs['defval']
			else:
				defval = None
			if (key == 'asset-mask' or key == 'amount-mask') and not ismint:
				defval = (1 << wirebits) - 1
			nowire = not extra_on_wire and 'xwire' in attrs
			wantdef = nowire or (not extra_on_wire and 'xdef' in attrs)
			if not 'array' in attrs:
				dest[key] = hex(randbits(key, nbits, defval, wantdef = wantdef, nowire = nowire, wirebits = wirebits))
			else:
				dest[key] = []
				for i in range(attrs['array']):
					dest[key].append(hex(randbits(key, nbits, defval, wantdef = wantdef, nowire = nowire)))

def copy_values(source, dest):
	for key in source:
		dest[key] = source[key]

def copy_keys(source, dest, keys, filter):
	# TODO: expand this to include or omit default values (and optionally corrupt a value?)
	for key, attrs in keys.items():
		if attrs['group'].startswith(filter) and key in source and source[key] != None:
			dest[key] = source[key]

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

# functions to convert amounts to and from integer and floating point representations

TRACE_AMOUNT_ENCODE = 0

def amount_mantissa_bits(is_donation):
	if is_donation:
		return TX_DONATION_BITS - TX_AMOUNT_EXPONENT_BITS
	else:
		return TX_AMOUNT_BITS - TX_AMOUNT_EXPONENT_BITS

def amount_decode_exponent(v):
	return v & TX_AMOUNT_EXPONENT_MASK

def amount_decode_dll(v, is_donation = False):
	if v is None:
		return None

	jstr = '{"decode-amount" :'
	jstr += '{ "amount-encoded" : "' + hex(v) + '"'
	jstr += ', "is-donation" : ' + str(int(is_donation))
	if is_donation:
		jstr += ', "donation-bits" : ' + str(TX_DONATION_BITS)
	else:
		jstr += ', "amount-bits" : ' + str(TX_AMOUNT_BITS)
	jstr += ', "exponent-bits" : ' + str(TX_AMOUNT_EXPONENT_BITS)
	jstr += '}}'
	result = dojson(jstr)
	result = json.loads(result)
	result = result['amount']

	return toint(result)

def amount_encode_dll(v, is_donation = False, min_exponent = 0, max_exponent = 999, rounding = -1):

	max_exponent = min(max_exponent, TX_AMOUNT_EXPONENT_MASK)

	jstr = '{"encode-amount" :'
	jstr += '{ "amount" : "' + hex(v) + '"'
	jstr += ', "is-donation" : ' + str(int(is_donation))
	if is_donation:
		jstr += ', "donation-bits" : ' + str(TX_DONATION_BITS)
	else:
		jstr += ', "amount-bits" : ' + str(TX_AMOUNT_BITS)
	jstr += ', "exponent-bits" : ' + str(TX_AMOUNT_EXPONENT_BITS)
	jstr += ', "minimum-exponent" : ' + str(min_exponent)
	jstr += ', "maximum-exponent" : ' + str(max_exponent)
	jstr += ', "rounding" : ' + str(int(rounding))
	jstr += '}}'
	result = dojson(jstr)
	result = json.loads(result)
	result = result['amount-encoded']

	if result == '0xffffffffffffffff':
		return None

	return toint(result)

def amount_truncate_dll(v, is_donation = False, min_exponent = 0, max_exponent = 999, rounding = -1):
	return amount_decode_dll(amount_encode_dll(v, is_donation, min_exponent, max_exponent, rounding), is_donation)

def amount_decode(v, is_donation = False):
	if v is None:
		return None

	exponent = v & TX_AMOUNT_EXPONENT_MASK
	mantissa = (v >> TX_AMOUNT_EXPONENT_BITS) + (exponent > 0)

	result = mantissa * 10**exponent

	if 0*TRACE_AMOUNT_ENCODE: print 'amount_decode', v, 'is_donation', is_donation, 'mantissa', mantissa, 'exp', exponent, 'result', result

	dll_result = amount_decode_dll(v, is_donation)
	try:
		int_dll = toint(dll_result)
	except:
		int_dll = -999.124836
	if int_dll != result:
		print 'amount_decode', v, 'is_donation', is_donation, 'result', result, 'dll_result', dll_result
		raise Exception

	return result

def amount_encode(v, is_donation = False, min_exponent = 0, max_exponent = 999, rounding = -1, randomize_exponent = True):
	# convert a value to floating point representation

	mantissa_bits = amount_mantissa_bits(is_donation)
	mantissa_max = (1 << mantissa_bits)

	max_exponent = min(max_exponent, TX_AMOUNT_EXPONENT_MASK)

	if TRACE_AMOUNT_ENCODE: print 'amount_encode', v, 'is_donation', is_donation, 'mantissa_bits', mantissa_bits, 'mantissa_max', mantissa_max, 'min_exp', min_exponent, 'max_exp', max_exponent, 'exp_max', TX_AMOUNT_EXPONENT_MASK

	if v < 0:
		result = None
		if TRACE_AMOUNT_ENCODE: print 'amount_encode', v, 'result', result
		return result

	if v == 0:
		result = 0
		if TRACE_AMOUNT_ENCODE: print 'amount_encode', v, 'result', result
		return result

	mantissa = v
	exponent = 0
	rounded = False

	orig = mantissa
	while mantissa > mantissa_max - (exponent == 0) or exponent < min_exponent:
		exponent += 1
		if mantissa == int(mantissa / 10) * 10:
			mantissa /= 10
		else:
			rounded = True
			if is_donation or rounding > 0:
				mantissa = (orig + 10**exponent) / 10**exponent
				if TRACE_AMOUNT_ENCODE: print 'amount_encode mantissa rounded up to', mantissa
			elif rounding == 0:
				mantissa = (orig + (10**exponent)/2) / 10**exponent
				if TRACE_AMOUNT_ENCODE: print 'amount_encode mantissa rounded up to', mantissa
			else:
				mantissa /= 10
		if TRACE_AMOUNT_ENCODE: print 'amount_encode mantissa', mantissa, 'exponent', exponent
		if exponent > 100:
			raise Exception

	if exponent > max_exponent:
		exponent = max_exponent
		mantissa = mantissa_max - (exponent == 0)
		rounded = True
		if is_donation and rounding != -1:
			rounding = -2 # force a rounding error

	if mantissa == 0:
		exponent = 0

	shift = 0
	while mantissa and exponent + shift < max_exponent:
		if mantissa != int(mantissa / 10**(shift+1)) * 10**(shift+1):
			break
		shift += 1

	if randomize_exponent:
		shift = random.randrange(shift + 1)

	mantissa /= 10**shift
	exponent += shift

	if rounded and rounding < -1:
		result = None
		if TRACE_AMOUNT_ENCODE: print 'amount_encode', v, 'is_donation', is_donation, 'rounding', rounding, 'rounded', rounded, 'min_exp', min_exponent, 'max_exp', max_exponent, 'randomize_exp', randomize_exponent, 'mantissa', mantissa, 'exp', exponent, 'result', result
	else:
		result = ((mantissa - (exponent > 0)) << TX_AMOUNT_EXPONENT_BITS) + exponent
		decoded = amount_decode(result, is_donation)
		if TRACE_AMOUNT_ENCODE: print 'amount_encode', v, 'is_donation', is_donation, 'rounding', rounding, 'rounded', rounded, 'min_exp', min_exponent, 'max_exp', max_exponent, 'randomize_exp', randomize_exponent, 'mantissa', mantissa, 'exp', exponent, 'result', result, 'decoded', decoded

		if mantissa > mantissa_max - (exponent == 0):
			raise Exception
		if exponent < min_exponent and result != 0:
			raise Exception
		if exponent > max_exponent:
			raise Exception
		if not rounded and decoded != v:
			raise Exception

	dll_result = amount_encode_dll(v, is_donation, min_exponent, max_exponent, rounding)
	if dll_result != result:
		if not randomize_exponent:
			print 'amount_encode', v, 'is_donation', is_donation, 'result', result, 'dll_result', dll_result
			raise Exception
		elif result is not None:
			dll_decoded = amount_decode(dll_result, is_donation)
			if dll_decoded != decoded:
				print 'amount_encode', v, 'is_donation', is_donation, 'result', result, 'dll_result', dll_result, 'decoded', decoded, 'dll_decoded', dll_decoded
				raise Exception

	return result

def amount_truncate(v, is_donation = False, min_exponent = 0, max_exponent = 999, rounding = -1):
	result = amount_decode(amount_encode(v, is_donation, min_exponent, max_exponent, rounding), is_donation)
	dll_result = amount_decode_dll(amount_encode_dll(v, is_donation, min_exponent, max_exponent, rounding), is_donation)
	if result != dll_result:
		print 'amount_truncate', v, 'is_donation', is_donation, 'rounding', rounding, 'min_exp', min_exponent, 'max_exp', max_exponent, 'result', result, 'dll_result', dll_result
		raise Exception
	return result

test_amount_encode_value_iter = 0

def test_amount_encode_value(v, is_donation, min_exponent, max_exponent, rounding, randomize_exponent):

	global test_amount_encode_value_iter
	test_amount_encode_value_iter += 1

	if test_amount_encode_value_iter < 0:
		return

	if TRACE_AMOUNT_ENCODE: print '----', test_amount_encode_value_iter

	mantissa_bits = amount_mantissa_bits(is_donation)
	mantissa_max = (1 << mantissa_bits)

	max_exponent = min(max_exponent, TX_AMOUNT_EXPONENT_MASK)

	mantissa = v
	exponent = 0
	rounded = False
	while mantissa and mantissa == int(mantissa / 10) * 10 and exponent < max_exponent:
		mantissa /= 10
		exponent += 1
	if v < 0:
		rounded = True
	if mantissa > mantissa_max - (exponent == 0):
		rounded = True
	if exponent < min_exponent and v != 0:
		rounded = True
	if exponent > max_exponent:
		rounded = True
	vmin = 0
	vmax = (mantissa_max - (max_exponent == 0)) * 10**max_exponent
	if TRACE_AMOUNT_ENCODE: print 'test_amount_encode_value', v, 'is_donation', is_donation, 'rounding', rounding, 'rounded', rounded, 'randomize_exp', randomize_exponent, 'mantissa', mantissa, 'exp', exponent, 'min_exp', min_exponent, 'max_exp', max_exponent, 'vmin', vmin, 'vmax', vmax
	encoded = amount_encode(v, is_donation, min_exponent, max_exponent, rounding, randomize_exponent = randomize_exponent)
	if encoded is None:
		if v >= 0 and not ((rounded and rounding < -1) or (is_donation and v > vmax and rounding != -1)):
			raise Exception
		return
	if v > vmax and is_donation and rounding != -1:
		raise Exception
	decoded = amount_decode(encoded, is_donation)
	diff = decoded - v
	while mantissa and (mantissa > mantissa_max - (exponent == 0) or exponent < min_exponent):
		exponent += 1
		if is_donation or rounding > 0:
			mantissa = (v + 10**exponent) / 10**exponent
		elif rounding == 0:
			mantissa = (v + (10**exponent)/2) / 10**exponent
		else:
			mantissa /= 10
	max_diff = 10**exponent
	if not is_donation and rounding == 0:
		max_diff /= 2
	if TRACE_AMOUNT_ENCODE: print 'test_amount_encode_value', v, 'is_donation', is_donation, 'rounding', rounding, 'rounded', rounded, 'randomize_exp', randomize_exponent, 'mantissa', mantissa, 'exp', exponent, 'decoded', decoded, 'diff', diff, 'max_diff' , max_diff
	if not rounded:
		if diff:
			raise Exception
	else:
		if not diff:
			raise Exception
		if rounding < -1:
			raise Exception
		if v < vmin and decoded != vmin:
			raise Exception
		if v > vmax and decoded != vmax:
			raise Exception
		if diff < 0:
			if is_donation:
				if not (v > vmax and rounding == -1):
					raise Exception
			else:
				if not (v > vmax or (v >= vmin and rounding <= 0)):
					raise Exception
			if diff < -max_diff and v <= vmax:
				raise Exception
		else:
			if is_donation:
				if not (v <= vmax):
					raise Exception
			else:
				if not (v < vmin or (v <= vmax and rounding >= 0)):
					raise Exception
			if diff > max_diff and v >= vmin:
				raise Exception

	tc = amount_truncate(v, is_donation, min_exponent, max_exponent, rounding)
	if not rounded and tc != decoded:
		print 'test_amount_encode_value', v, 'is_donation', is_donation, 'min_exp', min_exponent, 'max_exp', max_exponent, 'decoded', decoded, 'amount_truncate', tc
		raise Exception

def test_amount_encode_set(v, min_exponent = 0, max_exponent = 999):
	print 'test_amount_encode_set', v, 'min_exp', min_exponent, 'max_exp', max_exponent
	for i in range(2):
		for j in range(-2, 2):
			for k in range(2):
				test_amount_encode_value(v, i, min_exponent, max_exponent, j, k)

def test_amount_encode_random():
	min_exponent = random.randrange(TX_AMOUNT_EXPONENT_MASK + 1)
	max_exponent = random.randrange(TX_AMOUNT_EXPONENT_MASK + 1 - min_exponent) + min_exponent

	width = random.randrange(amount_mantissa_bits(False) + 2)
	if not width:
		v = 0
	else:
		v = random.getrandbits(width)
		v |= 1 << (width - 1)
		v |= 1
	shift = random.randrange(TX_AMOUNT_EXPONENT_MASK + 2)
	v *= 10**shift
	test_amount_encode_set(v, min_exponent, max_exponent)

def test_amount_encode_limits(min_exponent, max_exponent):
	offsets = (-11, -10, -9, -6, -5, -4, -2, -1, 0, 1, 2, 4, 5, 6, 9, 10, 11)
	for d in range(2):
		for ba in range(-1, 2):
			for ma in offsets:
				for ea in range(-1, 2):
					for va in range(-1, 2):
						bits = amount_mantissa_bits(d) + ba
						mantissa = (1 << bits) + ma
						exponent = max(0, max_exponent + ea)
						v = mantissa * 10**exponent + va
						test_amount_encode_set(v, min_exponent, max_exponent)

def test_amount_encode_pass(first = False):
	global rndseed
	print 'rndseed', rndseed
	random.seed(rndseed)
	rndseed += 1

	if first:
		test_amount_encode_set(-1)
		test_amount_encode_set(0)
		test_amount_encode_set(1)
		test_amount_encode_set(TX_CC_MINT_AMOUNT)

		exp = (0, 1, 2, 21, 22, TX_AMOUNT_EXPONENT_MASK-1, TX_AMOUNT_EXPONENT_MASK)
		for min_exponent in exp:
			for max_exponent in exp:
				if (min_exponent <= max_exponent):
					test_amount_encode_limits(min_exponent, max_exponent)
	else:
		for i in range(5000):
			test_amount_encode_random()

def test_amount_encode():
	print 'test_amount_encode'
	test_amount_encode_pass(True)
	while True:
		test_amount_encode_pass()
		break
	print 'test_amount_encode done'
	exit()

def define_tx_vars():
	pass

# 'xwire':1 means the variable must be set to its default value if TEST_EXTRA_ON_WIRE is false

tx_inout_vars = {
 'master-secret'			: {'group':'SE', 'nbits':TX_INPUT_BITS},
 'root-secret'				: {'group':'SE', 'nbits':TX_FIELD_BITS},
 'spend-secret'				: {'group':'SE', 'nbits':TX_INPUT_BITS},
 'trust-secret'				: {'group':'SE', 'nbits':TX_INPUT_BITS},
 'monitor-secret'			: {'group':'SE', 'nbits':TX_INPUT_BITS},
 'receive-secret'			: {'group':'SE', 'nbits':TX_FIELD_BITS},

 'spend-secrets'			: {'group':'SE', 'nbits':TX_INPUT_BITS,		'array':TX_MAX_SECRETS}, # REQ OMIT if !allow_multi_secrets or have spend-secret CAN HAVE NULL ELEMENTS
 'trust-secrets'			: {'group':'SE', 'nbits':TX_INPUT_BITS,		'array':TX_MAX_SECRETS}, # REQ OMIT if !allow_multi_secrets or have trust-secret CAN HAVE NULL ELEMENTS
 'monitor-secrets'			: {'group':'SE', 'nbits':TX_INPUT_BITS,		'array':TX_MAX_SECRETS}, # REQ OMIT if !allow_multi_secrets or have monitor-secret CAN HAVE NULL ELEMENTS

 'spend-secret-number'		: {'group':'SX', 'nbits':TX_SPEND_SECRETNUM_BITS, 'defval':0},		# REQ OMIT if !master-secret and !root-secret
 'enforce-spendspec-with-spend-secrets' :{'group':'SA', 'nbits':1,		'defval':0, 'xdef':1},
 'enforce-spendspec-with-trust-secrets' :{'group':'SA', 'nbits':1,		'defval':0, 'xdef':1},
 'required-spendspec-hash'	: {'group':'SA', 'nbits':TX_INPUT_BITS,		'defval':0, 'xdef':1},
 'allow-master-secret'		: {'group':'SA', 'nbits':1,					'defval':0},
 'allow-freeze'				: {'group':'SA', 'nbits':1,					'defval':0},
 'allow-trust-unfreeze'		: {'group':'SA', 'nbits':1,					'defval':0},
 'require-public-hashkey'	: {'group':'SA', 'nbits':1,					'defval':0},
 'restrict-addresses'		: {'group':'SA', 'nbits':1,					'defval':0},
 'spend-locktime'			: {'group':'SA', 'nbits':TX_TIME_BITS,		'defval':0},
 'trust-locktime'			: {'group':'SA', 'nbits':TX_TIME_BITS,		'defval':0},
 'spend-delaytime'			: {'group':'SA', 'nbits':TX_DELAYTIME_BITS,	'defval':0, 'xdef':1},
 'trust-delaytime'			: {'group':'SA', 'nbits':TX_DELAYTIME_BITS,	'defval':0, 'xdef':1},

 'use-spend-secrets'		: {'group':'DE', 'nbits':1,					'array':TX_MAX_SECRETS}, # OPT DEF = have_spend_secret[j]; CAN HAVE NULL ELEMENTS
 'use-trust-secrets'		: {'group':'DE', 'nbits':1,					'array':TX_MAX_SECRETS}, # OPT DEF = have_trust_secret[j]; CAN HAVE NULL ELEMENTS
 'required-spend-secrets'	: {'group':'DE', 'nbits':TX_MAX_SECRETS_BITS},						# OPT DEF = sum use-spend-secrets
 'required-trust-secrets'	: {'group':'DE', 'nbits':TX_MAX_SECRETS_BITS},						# OPT DEF = sum use-trust-secrets
 'destination-number'		: {'group':'DE', 'nbits':TX_DESTNUM_BITS},

 'payment-number'			: {'group':'IO', 'nbits':TX_PAYNUM_BITS},
 'asset'					: {'group':'IO', 'nbits':TX_ASSET_BITS,		'dep':1, 'defval':0, 'xwire':1},
 'amount'					: {'group':'IO', 'nbits':TX_AMOUNT_BITS,	'dep':1},

 'pool'						: {'group':'IP', 'nbits':TX_POOL_BITS,		'defval':0},			# this var handled differently on a bill input vs a bill output

 'commitment-iv'			: {'group':'IV', 'nbits':TX_COMMIT_IV_BITS},

 'destination'				: {'group':'OU', 'nbits':TX_FIELD_BITS,		'dep':1},
 'destination-chain'		: {'group':'OX', 'nbits':TX_CHAIN_BITS},
 'no-address'				: {'group':'OU', 'nbits':1,					'defval':0, 'xwire':1},
 'acceptance-required'		: {'group':'OX', 'nbits':1,					'defval':0},			# DEF from tx
 'repeat-count'				: {'group':'OU', 'nbits':16,				'dep':1, 'defval':0, 'xwire':1},
 'no-asset'					: {'group':'OU', 'nbits':1,					'defval':0, 'xwire':1},
 'asset-mask'				: {'group':'OU', 'nbits':TX_ASSET_BITS,		'defval':0, 'xwire':1},
 'no-amount'				: {'group':'OU', 'nbits':1,					'defval':0, 'xwire':1},
 'amount-mask'				: {'group':'OU', 'nbits':TX_AMOUNT_BITS,	'defval':0, 'xwire':1},

 'enforce-master-secret'	: {'group':'IN', 'nbits':1,					'defval':0, 'xwire':1},
 'enforce-spend-secrets'	: {'group':'IN', 'nbits':1,					'defval':0, 'xwire':1},
 'enforce-trust-secrets'	: {'group':'IN', 'nbits':1,					'defval':1, 'xwire':1},	# OPT DEF = 1 if !enforce-master-secret && !enforce-spend-secrets && !enforce-freeze && !enforce-unfreeze
 'enforce-freeze'			: {'group':'IN', 'nbits':1,					'defval':0, 'xwire':1},
 'enforce-unfreeze'			: {'group':'IN', 'nbits':1,					'defval':0, 'xwire':1},
 'master-secret-valid'		: {'group':'IN', 'nbits':1,					'defval':0},
 'spend-secrets-valid'		: {'group':'IN', 'nbits':1,					'defval':0},
 'trust-secrets-valid'		: {'group':'IN', 'nbits':1,					'defval':0},
 'merkle-root'				: {'group':'IN', 'nbits':TX_FIELD_BITS,		'dep':1},				# DEF from @tx
 'maximum-input-exponent'	: {'group':'IN', 'nbits':TX_AMOUNT_EXPONENT_BITS, 'dep':1},				# REQ or DEF from @tx
 'delaytime'					: {'group':'IX', 'nbits':TX_DELAYTIME_BITS,	'defval':0},			# DEF from @tx
 'commitment'				: {'group':'IN', 'nbits':TX_FIELD_BITS,		'dep':1},
 'hashed-spendspec'			: {'group':'IN', 'nbits':TX_INPUT_BITS,		'defval':0, 'xwire':1},
 'no-serial-number'			: {'group':'IN', 'nbits':1,					'defval':0, 'xwire':1},
 'hashkey'					: {'group':'IN', 'nbits':TX_HASHKEY_BITS,	'dep':1},
 'commitment-number'		: {'group':'IN', 'nbits':TX_COMMITNUM_BITS,	'dep':1},				# REQ if !no_serialnum
 'merkle-path'				: {'group':'IN', 'nbits':TX_FIELD_BITS,		'dep':1, 'defval':0, 'array':TX_MERKLE_DEPTH}
}

tx_vars = {
 'no-precheck'				: {'group':'TX', 'nbits':1,					'defval':0},
 'no-proof'					: {'group':'TX', 'nbits':1,					'defval':0},
 'no-verify'				: {'group':'TX', 'nbits':1,					'defval':0},
 'test-use-larger-zkkey'	: {'group':'TX', 'nbits':1,					'defval':0},
 'test-make-bad'			: {'group':'TX', 'nbits':32,				'defval':0},

 'random-seed'				: {'group':'TE', 'nbits':64,				'defval':0},

 'merkle-root'				: {'group':'TW', 'nbits':TX_FIELD_BITS,		'dep':1},				# REQ
 'destination-chain'		: {'group':'TW', 'nbits':TX_CHAIN_BITS},
 'default-output-pool'		: {'group':'TW', 'nbits':TX_POOL_BITS,		'defval':0},
 'acceptance-required'		: {'group':'TW', 'nbits':1,					'defval':0},
 'maximum-input-exponent'	: {'group':'TW', 'nbits':TX_AMOUNT_EXPONENT_BITS, 'dep':1},			# REQ if from_wire
 'delaytime'				: {'group':'TW', 'nbits':TX_DELAYTIME_BITS,	'defval':0},
 'parameter-level'			: {'group':'TW', 'nbits':TX_BLOCKLEVEL_BITS},						# REQ if !from_wire
 'parameter-time'			: {'group':'TW', 'nbits':TX_TIME_BITS},								# REQ if !from_wire
 'type'						: {'group':'TW', 'nbits':TX_TYPE_BITS,		'defval':0},
 'source-chain'				: {'group':'TW', 'nbits':TX_CHAIN_BITS},
 'revision'					: {'group':'TW', 'nbits':TX_REVISION_BITS,	'defval':0},
 'expiration'				: {'group':'TW', 'nbits':TX_TIME_BITS,		'defval':0},
 'reference-hash'			: {'group':'TW', 'nbits':TX_REFHASH_BITS,	'defval':0},
 'reserved'					: {'group':'TW', 'nbits':TX_RESERVED_BITS,	'defval':0},
 'donation'					: {'group':'TW', 'nbits':TX_DONATION_BITS,	'dep':1},				# REQ if !from_wire and !mint
 'minimum-output-exponent'	: {'group':'TW', 'nbits':TX_AMOUNT_EXPONENT_BITS, 'dep':1},
 'maximum-output-exponent'	: {'group':'TW', 'nbits':TX_AMOUNT_EXPONENT_BITS, 'dep':1},
 'allow-restricted-addresses':{'group':'TW', 'nbits':1,					'defval':1},
 #'output-commitment-iv-nonce': {'group':'TW', 'nbits':TX_COMMIT_IV_NONCE_BITS,'defval':0},
 'commitment-iv'			: {'group':'TW', 'nbits':TX_COMMIT_IV_BITS,	'dep':1}
}

master_secret_vars = [										'master-secret']

spend_secret_vars = list(master_secret_vars)
spend_secret_vars.append(									'root-secret')
spend_secret_vars.append(									'spend-secret')

trust_secret_vars = list(spend_secret_vars)
trust_secret_vars.append(									'trust-secret')

monitor_secret_vars = list(trust_secret_vars)
monitor_secret_vars.append(									'monitor-secret')

secret_vars = list(monitor_secret_vars)
secret_vars.append(											'receive-secret')

spend_multi_secret_vars = [									'spend-secrets']

trust_multi_secret_vars = list(spend_multi_secret_vars)
trust_multi_secret_vars.append(								'trust-secrets')

monitor_multi_secret_vars = list(trust_multi_secret_vars)
monitor_multi_secret_vars.append(							'monitor-secrets')

multi_secret_vars = monitor_multi_secret_vars

if 0:
	print master_secret_vars
	print spend_secret_vars
	print trust_secret_vars
	print monitor_secret_vars
	print secret_vars
	print spend_multi_secret_vars
	print trust_multi_secret_vars
	print monitor_multi_secret_vars
	print multi_secret_vars

def generate_secrets(dest, nbills, outaddrs):
	#print 'generate_secrets outaddrs', outaddrs
	orig_outaddrs_len = len(outaddrs)
	for i in range(nbills):
		dest.append([])
		pushbad()
		retry = True
		while retry:
			clearbad()
			dest[i] = []
			if i == 0:
				while len(outaddrs) > orig_outaddrs_len:
					outaddrs.pop()
				#print 'outaddrs', outaddrs
				if getbadsel():
					outaddrs.append(hex(randbits('restricted-address', TX_ADDRESS_BITS)))
			if len(outaddrs) > TX_MAX_RESTRICTED_ADDRESSES:
				setbad('outaddrs > TX_MAX_RESTRICTED_ADDRESSES')
			nsecrets = random.randrange(TX_MAX_SECRETS + getbadsel()) + 1
			if nsecrets > TX_MAX_SECRETS:
				setbad('nsecrets > TX_MAX_SECRETS')
			for j in range(nsecrets):
				dest[i].append({})
				generate_secret_set(dest[i][j], j == 0)
			dest[i][0]['destination'] = compute_destination(dest[i], outaddrs)
			retry = isbad()
		popbad()
		#print 'generate_secrets'
		#pprint.pprint(dest)

def copy_multi_secrets(source, dest, none_is_ok = False):
		#print 'copy_multi_secrets in:'
		#pprint.pprint(source)
		for i in range(len(source)):
			dest.append({})
			copy_secrets(source[i], dest[i], none_is_ok = none_is_ok, extra_is_ok = i)
		for key in multi_secret_vars:
			dest[0][key] = []
			for i in range(len(source)):
				if key[:-1] in source[i]:
					val = source[i][key[:-1]]
				else:
					val = None	# converts to json null
				dest[0][key].append(val)
			if key[:-1] in dest[0]:
				del dest[0][key[:-1]]
		#print 'copy_multi_secrets out:'
		#pprint.pprint(dest)

def generate_secret_set(secrets, isfirst):
	pushbad()
	while True:
		clearbad()
		secrets.clear()
		if isfirst:
			generate_values(secrets, tx_inout_vars, 'SA')
			if not (toint(secrets['enforce-spendspec-with-spend-secrets']) or toint(secrets['enforce-spendspec-with-trust-secrets'])) and not isbad():
				if getbadsel():
					setbad('required-spendspec-hash without enforce-spendspec')
				else:
					del secrets['required-spendspec-hash']
			generate_master_secret(secrets)
			if generate_secret(secrets, 'master-secret',	'root-secret'): continue
		if isfirst and (secrets['master-secret'] is not None or secrets['root-secret'] is not None):
			generate_values(secrets, tx_inout_vars, 'SX')
		if generate_secret(secrets, 'root-secret',	'spend-secret'): continue
		if generate_secret(secrets, 'spend-secret',	'trust-secret'): continue
		if generate_secret(secrets, 'trust-secret',	'monitor-secret'): continue
		break
	#print 'generate_secret_set', isfirst
	#pprint.pprint(secrets)
	retry = isbad()
	popbad()
	if retry:
		setbad('generate_secret_set retry')

def generate_master_secret(secrets):
	if random.getrandbits(1):
		secret = None
	else:
		secret = hex(randbits('master-secret', TX_INPUT_BITS))
	secrets['master-secret'] = secret

def generate_secret(secrets, superkey, key):
	retry = False
	if not superkey in secrets or secrets[superkey] is None:
		if random.getrandbits(1):
			secret = None
		elif key == 'spend-secret' and random.getrandbits(1):
			secret = hex(random.getrandbits(TX_HASHKEY_WIRE_BITS))	# generate a spend-secret that can be used a hashkey in a wire transaction
		else:
			secret = hex(randbits(key, tx_inout_vars[key]['nbits']))
	else:
		dest = {}
		copy_secrets(secrets, dest, test_none = True)
		jstr = json.dumps(dest)
		jstr = '{"compute-' + key + '" : ' + jstr + '}'
		#print jstr
		retry = isbad()
		secret = dojson(jstr, shouldfail = retry)
		if retry:
			return True
		secret = json.loads(secret)
		secret = secret[key]
	#print 'generate_secret', key, secret
	secrets[key] = secret
	return False

def copy_secrets(source, dest, test_none = False, none_is_ok = False, extra_is_ok = False):
	if test_none and getbadsel():
		if not none_is_ok:
			setbad('no secrets copied')
		return
	ncopy = random.randrange(len(secret_vars)) + 1
	nhave = 0
	keys = secret_vars
	random.shuffle(keys)
	for key in keys:
		if nhave == ncopy:
			break
		if key in source and source[key] != None:
			if toint(source[key]) == 0 or nhave < ncopy:	# always copy key if value is zero, because missing value might still be valid
				dest[key] = source[key]

	for key in source:
		if not key in secret_vars:
			dest[key] = source[key]

	if getbadsel():
		dest['extraneous-test-key'] = 0
		if not extra_is_ok:
			setbad('extraneous value in secrets')

def compute_destination(secrets, outaddrs):
	#print 'compute_destination secrets in'
	#pprint.pprint(secrets)
	#pushbad()
	generate_values(secrets[0], tx_inout_vars, 'DE')
	#print 'compute_destination'
	#pprint.pprint(secrets)
	payspec = []
	copy_multi_secrets(secrets, payspec)
	payspec = payspec[0]
	if len(outaddrs):
		payspec['restricted-addresses'] = outaddrs
		nopen = 0
		for j in range(TX_MAX_RESTRICTED_ADDRESSES/2):
			i = TX_MAX_SECRET_SLOTS - 1 - j
			if not (	(i < len(payspec['use-spend-secrets']) and toint(payspec['use-spend-secrets'][i]))
					or	(i < len(payspec['use-trust-secrets']) and toint(payspec['use-trust-secrets'][i]))):
				nopen += 2
				#print j, i, 'nopen', nopen
		if nopen < len(outaddrs):
			setbad('nopen < len(outaddrs)')
	#pprint.pprint(payspec)
	jstr = json.dumps(payspec)
	jstr = '{"payspec-encode" : ' + jstr + '}'
	retry = isbad()
	#popbad()
	payspec = dojson(jstr, shouldfail = retry)
	if retry:
		setbad('compute_destination retry')
		return None
	jstr = '{"payspec-decode" : ' + payspec + '}'
	payspec = dojson(jstr)
	#print payspec
	payspec = json.loads(payspec)
	#print payspec
	#print 'compute_destination secrets out'
	#pprint.pprint(secrets)
	return payspec['payspec']['destination']

def check_secrets(input, secret_vars, multi_secret_vars):
	have_secret = [0] * TX_MAX_SECRETS
	for key in secret_vars:
		if key in input:
			have_secret[0] = 1
	for key in multi_secret_vars:
		if key in input:
			for i in range(len(input[key])):
				if input[key][i] is not None:
					have_secret[i] = 1
	return have_secret

def check_output(output):
	#pprint.pprint(output)

	if (toint(output['destination']) & TX_ACCEPT_REQ_DEST_MASK) == 0 and not toint(output['acceptance-required']):
		if isbad() or wirebad or getbadsel():
			setbad('acceptance-required not set')
		else:
			print 'check_output changing acceptance-required from', output['acceptance-required'], 'to 1'
			output['acceptance-required'] = 1

	if (toint(output['destination']) & TX_STATIC_ADDRESS_MASK) == 0:
		if toint(output['payment-number']):
			setbad('static address and payment-number != 0')
		else:
			print 'testing static address with payment-number = 0'

	if toint(output['no-asset']):
		if isbad() or wirebad or getbadsel():
			setbad('asset-mask and no-asset')
		else:
			print 'check_output deleting asset-mask', output['asset-mask']
			del output['asset-mask']

	if toint(output['no-amount']):
		if isbad() or wirebad or getbadsel():
			setbad('amount-mask and no-amount')
		else:
			print 'check_output deleting amount-mask', output['amount-mask']
			del output['amount-mask']

def check_input(tx, input):
	#pprint.pprint(input)

	have_master_secret = check_secrets(input, master_secret_vars, [])
	have_spend_secret = check_secrets(input, spend_secret_vars, spend_multi_secret_vars)
	have_trust_secret = check_secrets(input, trust_secret_vars, trust_multi_secret_vars)
	have_monitor_secret = check_secrets(input, monitor_secret_vars, monitor_multi_secret_vars)

	if 0:
		print 'have_master_secret', have_master_secret
		print 'have_monitor_secret', have_monitor_secret

	# enforce-master-secret

	if toint(input['master-secret-valid']) and not toint(input['allow-master-secret']):
		if isbad() or wirebad or getbadsel():
			setbad('master-secret-valid but not allow-master-secret')
		else:
			print 'check_input changing master-secret-valid from', input['master-secret-valid'], 'to 0'
			input['master-secret-valid'] = 0

	if toint(input['master-secret-valid']) and not have_master_secret[0]:
		if isbad() or wirebad or getbadsel():
			setbad('master-secret-valid but no master-secret')
		else:
			print 'check_input changing master-secret-valid from', input['master-secret-valid'], 'to 0'
			input['master-secret-valid'] = 0

	if toint(input['enforce-master-secret']) and not toint(input['master-secret-valid']):
		if isbad() or wirebad or getbadsel():
			setbad('enforce-master-secret but not master-secret-valid')
		else:
			print 'check_input changing enforce-master-secret from', input['enforce-master-secret'], 'to 0'
			input['enforce-master-secret'] = 0

	# spend-secrets-valid

	spend_secret_count = 0
	for i in range(TX_MAX_SECRETS):
		if toint(input['spend-secrets-valid']):
			secret_valid = have_spend_secret[i]
		else:
			secret_valid = have_trust_secret[i]
		if toint(input['use-spend-secrets'][i]) and secret_valid:
			spend_secret_count += 1

	if 0:
		print 'spend-secrets-valid', input['spend-secrets-valid']
		if toint(input['spend-secrets-valid']):
			print 'have_spend_secret', have_spend_secret
		else:
			print 'have_trust_secret', have_trust_secret
		print 'use-spend-secrets', input['use-spend-secrets']
		print 'spend_secret_count', spend_secret_count
		print 'required-spend-secrets', input['required-spend-secrets']
		print 'spend-secrets-valid', input['spend-secrets-valid']
		print 'enforce-spend-secrets', input['enforce-spend-secrets']

	if toint(input['spend-secrets-valid']) and spend_secret_count < toint(input['required-spend-secrets']):
		if isbad() or wirebad or getbadsel():
			setbad('spend-secrets-valid but not enough spend-secrets')
		else:
			print 'check_input changing spend-secrets-valid from', input['spend-secrets-valid'], 'to 0'
			input['spend-secrets-valid'] = 0

	if toint(input['spend-secrets-valid']) and toint(tx['parameter-time']) < toint(input['spend-locktime']):
		if isbad() or wirebad or getbadsel():
			setbad('spend-secrets-valid but parameter-time < spend-locktime')
		else:
			print 'check_input changing parameter-time from', tx['parameter-time'], 'to', input['spend-locktime']
			tx['parameter-time'] = input['spend-locktime']

	if toint(input['spend-secrets-valid']) and toint(input['delaytime']) < toint(input['spend-delaytime']):
		if isbad() or wirebad or getbadsel():
			setbad('spend-secrets-valid but delaytime < spend-delaytime')
		else:
			print 'check_input changing delaytime from', input['delaytime'], 'to max'
			input['delaytime'] = hex((1 << TX_DELAYTIME_BITS) - 1)

	# trust-secrets-valid

	trust_secret_count = 0
	for i in range(TX_MAX_SECRETS):
		if toint(input['spend-secrets-valid']):
			secret_valid = have_spend_secret[i]
		else:
			secret_valid = have_trust_secret[i]
		if toint(input['use-trust-secrets'][i]) and secret_valid:
			trust_secret_count += 1

	if 0:
		print 'spend-secrets-valid', input['spend-secrets-valid']
		if toint(input['spend-secrets-valid']):
			print 'have_spend_secret', have_spend_secret
		else:
			print 'have_trust_secret', have_trust_secret
		print 'use-trust-secrets', input['use-trust-secrets']
		print 'trust_secret_count', trust_secret_count
		print 'required-trust-secrets', input['required-trust-secrets']
		print 'trust-secrets-valid', input['trust-secrets-valid']
		print 'enforce-trust-secrets', input['enforce-trust-secrets']
		print 'enforce-unfreeze', input['enforce-unfreeze']

	if toint(input['trust-secrets-valid']) and trust_secret_count < toint(input['required-trust-secrets']):
		if isbad() or wirebad or getbadsel():
			setbad('trust-secrets-valid but not enough trust-secrets')
		else:
			print 'check_input changing trust-secrets-valid from', input['trust-secrets-valid'], 'to 0'
			input['trust-secrets-valid'] = 0

	if toint(input['trust-secrets-valid']) and toint(input['enforce-unfreeze']) and not toint(input['allow-trust-unfreeze']):
		if isbad() or wirebad or getbadsel():
			setbad('trust-secrets-valid and enforce-unfreeze but allow-trust-unfreeze not set')
		elif random.getrandbits(1):
			print 'check_input changing trust-secrets-valid from', input['trust-secrets-valid'], 'to 0'
			input['trust-secrets-valid'] = 0
		else:
			print 'check_input changing enforce-unfreeze from', input['enforce-unfreeze'], 'to 0'
			input['enforce-unfreeze'] = 0

	if toint(input['trust-secrets-valid']) and toint(tx['parameter-time']) < toint(input['trust-locktime']):
		if isbad() or wirebad or getbadsel():
			setbad('trust-secrets-valid but parameter-time < trust-locktime')
		else:
			print 'check_input changing parameter-time from', tx['parameter-time'], 'to', input['trust-locktime']
			tx['parameter-time'] = input['trust-locktime']

	if toint(input['trust-secrets-valid']) and toint(input['delaytime']) < toint(input['trust-delaytime']):
		if isbad() or wirebad or getbadsel():
			setbad('trust-secrets-valid but delaytime < trust-delaytime')
		else:
			print 'check_input changing delaytime from', input['delaytime'], 'to max'
			input['delaytime'] = hex((1 << TX_DELAYTIME_BITS) - 1)

	# enforce-spend-secrets / enforce-trust-secrets

	if toint(input['enforce-spend-secrets']) and not toint(input['master-secret-valid']) and not toint(input['spend-secrets-valid']):
		if isbad() or wirebad or getbadsel():
			setbad('enforce-spend-secrets but not master-secret-valid nor spend-secrets-valid')
		else:
			print 'check_input changing enforce-spend-secrets from', input['enforce-spend-secrets'], 'to 0'
			input['enforce-spend-secrets'] = 0

	if toint(input['enforce-trust-secrets']) and not toint(input['master-secret-valid']) and not (toint(input['spend-secrets-valid']) or toint(input['trust-secrets-valid'])):
		if isbad() or wirebad or getbadsel():
			setbad('enforce-trust-secrets but not master-secret-valid nor spend-secrets-valid nor trust-secrets-valid')
		else:
			print 'check_input changing enforce-trust-secrets from', input['enforce-trust-secrets'], 'to 0'
			input['enforce-trust-secrets'] = 0
			if not extra_on_wire:
				setwirebad('enforce-trust-secrets = 0')

	# enforce-freeze / enforce-unfreeze

	if toint(input['enforce-freeze']) and not toint(input['allow-freeze']):
		if isbad() or wirebad or getbadsel():
			setbad('enforce-freeze but not allow-freeze')
		else:
			print 'check_input changing enforce-freeze from', input['enforce-freeze'], 'to 0'
			input['enforce-freeze'] = 0

	if toint(input['enforce-unfreeze']) and not toint(input['master-secret-valid']) and not toint(input['trust-secrets-valid']):
		if isbad() or wirebad or getbadsel():
			setbad('enforce-unfreeze but neither master-secret-valid nor trust-secrets-valid are set')
		else:
			print 'check_input changing enforce-unfreeze from', input['enforce-unfreeze'], 'to 0'
			input['enforce-unfreeze'] = 0

	# hashkey

	if toint(input['require-public-hashkey']) and toint(input['spend-secrets-valid']):
		if not have_spend_secret[1]:
			setbad('require-public-hashkey and spend-secrets-valid but second spend secret is invalid')
		elif getbadsel():
			setbad('require-public-hashkey and spend-secrets-valid but hashkey is invalid')
		else:
			input['hashkey'] = input['spend-secrets'][1]
			if toint(input['hashkey']) >= (1 << TX_HASHKEY_WIRE_BITS):
				setwirebad('hashkey > TX_HASHKEY_WIRE_BITS')
			else:
				print 'NOTE: testing hashkey on wire'

	if toint(input['require-public-hashkey']) and toint(input['spend-secrets-valid']) and not have_spend_secret[1]:
		setbad('require-public-hashkey and spend-secrets-valid but second spend secret is invalid')

	# spendspec

	if ((toint(input['spend-secrets-valid']) and toint(input['enforce-spendspec-with-spend-secrets']))
			or (toint(input['trust-secrets-valid']) and toint(input['enforce-spendspec-with-trust-secrets']))):
		input['hashed-spendspec'] = input['required-spendspec-hash']
		if getbadsel():
			input['hashed-spendspec'] = hex(1 ^ toint(input['hashed-spendspec']))
			setbad('hashed-spendspec != required-spendspec-hash')
		if toint(input['hashed-spendspec']) and not extra_on_wire:
			setwirebad('hashed-spendspec != 0')

	# restricted addresses

	if toint(input['restrict-addresses']) and not toint(input['master-secret-valid']) and not toint(input['enforce-freeze']):
		if tx['raddress_omitted']:
			setbad('restrict-addresses and raddress_omitted')
		if not toint(tx['allow-restricted-addresses']):
			if isbad() or wirebad or getbadsel():
				setbad('restrict-addresses but allow-restricted-addresses not set')
			else:
				print 'check_input changing allow-restricted-addresses from', tx['allow-restricted-addresses'], 'to 1'
				tx['allow-restricted-addresses'] = 1

	# randomly delete spend-secrets-valid / spend-secrets-valid to make sure DLL can fill these in

	if not isbad():
		if random.getrandbits(1):
			print 'check_input deleting spend-secrets-valid', input['spend-secrets-valid']
			del input['spend-secrets-valid']
		if random.getrandbits(1):
			print 'check_input deleting trust-secrets-valid', input['trust-secrets-valid']
			del input['trust-secrets-valid']

def print_worked_or_short(outsum, insum):
	if outsum == insum:
		print 'worked'
	else:
		print 'short by', outsum - insum

def make_tx(tx, ismint, nout, nin, ninw):

	# generate base tx

	if 0: # !!! normally 0; for testing
		print '*** WARNING: SETTING STATIC TX VALUES ***'
		tx['no-precheck'] = 0
		#tx['no-verify'] = 0

	if not test_larger_zkkey:
		tx['test-use-larger-zkkey'] = 0

	tx['no-proof'] = 0
	tx['test-make-bad'] = 0

	generate_values(tx, tx_vars, 'T', ismint)

	#pprint.pprint(tx)

	#if not isbad() and not wirebad and random.getrandbits(1):
	#	del tx['output-commitment-iv']

	# chose asset id'd

	max_nassets = min(nout, nin) + (nout != nin)
	if not max_nassets: max_nassets = 1
	nassets = random.randrange(max_nassets) + 1

	if nassets > 1 and not extra_on_wire and not getbadsel():
		nassets = 1

	assets_avail = []
	for i in range(nassets):
		if extra_on_wire:
			wirebits = TX_ASSET_BITS
		else:
			wirebits = TX_ASSET_WIRE_BITS
		assets_avail.append(randbits('asset', TX_ASSET_BITS, wirebits = wirebits, nobad = (i == 0)))
	assets_avail[0] = 0

	assets_used = {}
	assets_used[0] = 1

	print 'assets_avail', assets_avail

	# chose asset id, repeat count and a tentative amount for each output token
	outrep = []
	outasset = []
	outval = []
	mask = randbits('outval', TX_AMOUNT_EXPONENT_BITS, nobad = 1)
	mask |= randbits('outval', TX_AMOUNT_EXPONENT_BITS, nobad = 1)
	#mask = TX_AMOUNT_EXPONENT_MASK
	mask ^= (1 << TX_AMOUNT_BITS) - 1
	print 'mask', hex(mask)
	for i in range(nout):
		if random.getrandbits(1):
			rep = random.getrandbits(4)
		else:
			rep = randbits('rep', TX_REPEAT_BITS)
		if rep and not extra_on_wire:
			if isbad() or wirebad or getbadsel():
				setwirebad('output repeat')
			else:
				rep = 0
		val = mask | randbits('outval', TX_AMOUNT_BITS, nobad = 1)		# nobad because amounts will be adjusted
		asset = assets_avail[random.randrange(len(assets_avail))]
		assets_used[asset] = 1
		outasset.append(asset)
		outrep.append(rep)
		outval.append(val)

	# chose asset id and a tentative amount for each input token

	donation = 0
	max_donation = amount_encode_dll(1 << 200, True)
	max_donation = amount_decode_dll(max_donation, True)

	inasset = []
	inval = []
	if ismint:
		val = amount_encode_dll(TX_CC_MINT_AMOUNT)
		inasset.append(0)
		inval.append(val)
	else:
		for i in range(nin):
			val = mask | randbits('inval', TX_AMOUNT_BITS, nobad = 1)		# nobad because amounts will be adjusted
			asset = assets_avail[random.randrange(len(assets_avail))]
			assets_used[asset] = 1
			inasset.append(asset)
			inval.append(val)

	# adjust amounts to balance input and outputs

	for asset in assets_used.keys():
		outsum = insum = 0
		for i in range(nout):
			if outasset[i] == asset:
				outsum += amount_decode_dll(outval[i]) * (outrep[i] + 1)
		for i in range(nin):
			if inasset[i] == asset:
				insum += amount_decode_dll(inval[i])

		iter = 0
		#print 'iter asset outsum insum donation outasset outval inasset inval'
		while True:
			#print iter,asset,outsum,insum,donation,outasset,outval,inasset,inval
			if outsum == insum:
				break
			iter += 1
			if iter > 35:
				setbad('outsum != insum')
				break
			if asset == 0 and random.getrandbits(1):
				# delta donation = insum - outsum_0 --> outsum_1 = outsum_0 + insum - outsum_0 = insum
				val = donation + insum - outsum
				if val >= 0 and val <= max_donation:
					net = amount_truncate_dll(val, True)
					delta = net - donation
					if abs(outsum + delta - insum) >= abs(outsum - insum):
						continue
					outsum += delta
					print 'donation', donation, 'added', val - donation, 'net', delta,
					donation = net
					print_worked_or_short(outsum, insum)
					continue
			if nout and random.getrandbits(1):
				i = random.randrange(nout)
				if outasset[i] == asset:
					val = amount_decode_dll(outval[i])
					sub = min(val * (outrep[i] + 1), outsum - insum)
					sub = sub / (outrep[i] + 1)
					outval[i] = amount_encode_dll(val - sub, False, 0)
					delta = val - amount_decode_dll(outval[i])
					outsum -= delta * (outrep[i] + 1)
					print 'outval', i, 'value', val, 'subtracted', sub, 'net', delta,
					print_worked_or_short(outsum, insum)
					continue
			if nin:
				i = random.randrange(nin)
				if not ismint and inasset[i] == asset:
					val = amount_decode_dll(inval[i])
					sub = min(val, insum - outsum)
					inval[i] = amount_encode_dll(val - sub, False, 0)
					delta = val - amount_decode_dll(inval[i])
					insum -= delta
					print 'inval', i, 'value', val, 'subtracted', sub, 'net', delta,
					print_worked_or_short(outsum, insum)
					continue

	if not isbad() and getbadsel():
		val = amount_truncate_dll(donation + 1, True)
		if val != donation:
			donation = val
			setbad('donation += 1')
	if not isbad() and getbadsel():
		val = amount_decode_dll(amount_encode_dll(donation, True) - (TX_AMOUNT_EXPONENT_MASK + 1), True)
		if val != donation:
			donation = val
			setbad('donation -= 1')

	tx['donation'] = hex(amount_encode_dll(donation, True))

	# compute min & max values

	maxoutval = maxinval = 0
	minoutval = TX_AMOUNT_EXPONENT_MASK
	for i in range(nout):
		if outval[i] < 0 or outval[i] >= (1 << TX_AMOUNT_BITS):
			setbad('outval > TX_AMOUNT_MAX')
		if outasset[i] == 0:
			maxoutval = max(maxoutval, amount_decode_exponent(outval[i]))
			if outval[i]:
				minoutval = min(minoutval, amount_decode_exponent(outval[i]))
	for i in range(nin):
		if inval[i] < 0 or inval[i] >= (1 << TX_AMOUNT_BITS):
			setbad('inval > TX_AMOUNT_MAX')
		if inasset[i] == 0:
			maxinval = max(maxinval, amount_decode_exponent(inval[i]))

	print 'assets_used', assets_used.keys() #, 'max_nassets', max_nassets

	# this doesn't work because proving key could have larger capacity
	#if len(assets_used) > max_nassets:
	#	setbad('assets_used > max_nassets')

	if len(assets_used) > 1 and not extra_on_wire:
		setwirebad('non-zero asset')

	print 'donation', donation
	print 'repeat counts', outrep
	print 'outassets', outasset
	print 'outvals', outval
	print 'minoutval', minoutval, 'maxoutval', maxoutval
	print 'inassets', inasset
	print 'invals', inval
	print 'maxinval', maxinval

	if nout and getbadsel():
		tx['minimum-output-exponent'] = hex(minoutval + 1)
		setbad('minoutval += 1')
	else:
		tx['minimum-output-exponent'] = hex(minoutval)

	if nout and getbadsel():
		tx['maximum-output-exponent'] = hex(maxoutval - 1)
		setbad('maxoutval -= 1')
	else:
		tx['maximum-output-exponent'] = hex(maxoutval)

	if not extra_on_wire:	# TODO: test with key omitted
		if nin and not ismint and getbadsel():
			tx['maximum-input-exponent'] = hex(maxinval - 1)
			setbad('maxinval -= 1')
		else:
			tx['maximum-input-exponent'] = hex(maxinval)

	# generate outputs

	outsecrets = []
	generate_secrets(outsecrets, nout, [])

	tx['outputs'] = []
	tx['raddress_omitted'] = False
	outaddrs = []

	for i in range(nout):
		output = {}
		output['destination'] = outsecrets[i][0]['destination']
		generate_values(output, tx_inout_vars, 'IO', ismint)
		generate_values(output, tx_inout_vars, 'OU', ismint)
		if i == 0 or extra_on_wire or getbadsel():
			generate_values(output, tx_inout_vars, 'OX', ismint)
		else:
			output['destination-chain'] = tx['outputs'][0]['destination-chain']
			output['acceptance-required'] = tx['outputs'][0]['acceptance-required']
		if i == 0 or extra_on_wire or getbadsel():
			generate_values(output, tx_inout_vars, 'IP', ismint)
		else:
			output['pool'] = tx['outputs'][0]['pool']
		if not isbad() and (toint(output['destination']) & TX_STATIC_ADDRESS_MASK) == 0 and toint(output['payment-number']) and random.getrandbits(1):
			output['payment-number'] = str(0)
		output['asset'] = hex(outasset[i])
		output['repeat-count'] = hex(outrep[i])
		output['amount'] = hex(outval[i])

		tx['outputs'].append({})
		tx['outputs'][i] = output

		if not toint(output['no-address']):
			jstr = '{"compute-address" :'
			jstr += ' {"destination" : "' + output['destination'] + '"'
			jstr += ', "destination-chain" : "' + output['destination-chain'] + '"'
			jstr += ', "payment-number" : "' + output['payment-number'] + '"'
			jstr += '}}'
			address = dojson(jstr, skipfail = isbad())
			try:
				address = json.loads(address)
				address = address['address']
				if nout > TX_MAX_RESTRICTED_ADDRESSES or getbadsel():
					tx['raddress_omitted'] = True
				else:
					outaddrs.append(address)
			except:
				if not isbad():
					raise Exception

	print 'outaddrs', outaddrs
	print 'raddress_omitted', tx['raddress_omitted']

	# generate inputs

	if ismint:
		nin = 0

	insecrets = []
	generate_secrets(insecrets, nin, outaddrs)

	inputs = []
	for i in range(nin):
		inputs.append({})
		generate_values(inputs[i], tx_inout_vars, 'IO', ismint)
		generate_values(inputs[i], tx_inout_vars, 'IV', ismint)
		if extra_on_wire or not random.getrandbits(8):
			generate_values(inputs[i], tx_inout_vars, 'IP', ismint)
			if not extra_on_wire and toint(inputs[i]['pool']):
				setwireverifybad('input pool != 0')
		else:
			inputs[i]['pool'] = 0
		#print 'input:', inputs[i]
		inputs[i]['destination'] = insecrets[i][0]['destination']
		inputs[i]['asset'] = hex(inasset[i])
		inputs[i]['amount'] = hex(inval[i])

	jstr = json.dumps(inputs)
	jstr = '{"generate-test-inputs" : ' + jstr + '}'
	#print jstr
	inputs = dojson(jstr, skipfail = isbad())
	#print inputs
	if inputs == ERR:
		if isbad():
			print '*** generate-test-inputs nosuccess, possibly because isbad', isbad(0)
		return False

	inputs = '{' + inputs + '}'
	inputs = json.loads(inputs)

	tx['merkle-root'] = inputs['merkle-root']

	inputs = inputs['inputs']

	tx['inputs'] = []
	for i in range(nin):
		input = inputs[i]
		#print 'input:', input
		secrets = []
		copy_multi_secrets(insecrets[i], secrets, none_is_ok = True)
		#print 'secrets:', secrets[0]
		copy_values(secrets[0], input)
		if len(outaddrs):
			input['restricted-addresses'] = outaddrs
		del input['destination']

		generate_values(input, tx_inout_vars, 'IN', ismint)
		if i == 0 or extra_on_wire or getbadsel():
			generate_values(inputs[i], tx_inout_vars, 'IX', ismint)
		else:
			input['delaytime'] = inputs[0]['delaytime']

		if extra_on_wire:
			if toint(input['asset']):
				input['maximum-input-exponent'] = 0
			elif getbadsel():
				input['maximum-input-exponent'] = hex(amount_decode_exponent(toint(input['amount'])) - 1)
				setbad('maxinval -= 1')
			else:
				input['maximum-input-exponent'] = hex(amount_decode_exponent(toint(input['amount'])))

		#print 'input', i
		#pprint.pprint(input)

		tx['inputs'].append({})
		tx['inputs'][i] = input

	for output in tx['outputs']:
		check_output(output)

	for input in tx['inputs']:
		check_input(tx, input)

	if not isbad() and random.getrandbits(1):
		default_allow_restricted_addresses = (nout > 0)
		for output in tx['outputs']:
			if toint(output['no-address']):
				default_allow_restricted_addresses = False
		if toint(tx['allow-restricted-addresses']) == default_allow_restricted_addresses:
			print 'deleting allow-restricted-addresses', tx['allow-restricted-addresses']
			del tx['allow-restricted-addresses']

	if not extra_on_wire and not isbad():
		for output in tx['outputs']:
			tx['destination-chain'] = tx['outputs'][0]['destination-chain']		# note: tx-to-wire doesn't use tx value
			if toint(output['destination-chain']) != toint(tx['destination-chain']):
				setwirebad('destination-chain mismatch')
			tx['acceptance-required'] = tx['outputs'][0]['acceptance-required']	# note: tx-to-wire doesn't use tx value
			if toint(output['acceptance-required']) != toint(tx['acceptance-required']):
				setwirebad('acceptance-required mismatch')
			tx['default-output-pool'] = tx['outputs'][0]['pool']				# note: tx-to-wire doesn't use tx value
			if toint(output['pool']) != toint(tx['default-output-pool']):
				setwirebad('output pool mismatch')
		for input in tx['inputs']:
			if toint(input['pool']) != toint(tx['inputs'][0]['pool']):
				setwirebad('input pool mismatch')
			tx['delaytime'] = tx['inputs'][0]['delaytime']						# note: tx-to-wire doesn't use tx value
			if toint(input['delaytime']) != toint(tx['delaytime']):
				setwirebad('delaytime mismatch')
			# these are commented out for now because this test script currently sets these values only for the tx, not for the individual inputs:
			#tx['merkle-root'] = tx['inputs'][0]['merkle-root']
			#if toint(input['merkle-root']) != toint(tx['merkle-root']):
			#	setwirebad('merkle-root mismatch')
			#tx['maximum-input-exponent'] = tx['inputs'][0]['maximum-input-exponent']	# note: tx-to-wire doesn't use tx value
			#if toint(input['maximum-input-exponent']) != toint(tx['maximum-input-exponent']):
			#	setwirebad('maximum-input-exponent mismatch')

	# trim paths from random inputs until only ninw have paths

	ndel = nin - ninw
	while ndel:
		i = random.randrange(nin)
		if 'merkle-path' in tx['inputs'][i]:
			del tx['inputs'][i]['merkle-path']
			ndel -= 1

	# possibly set test-make-bad
	if not test_only_good_tx and not isbad() and not wirebad and random.getrandbits(1):
		tx['test-make-bad'] = max(1, rndseed)
		setbad('test-make-bad')

	return True

def do_create_verify(cmd, tx, jstr, shouldfail):
	result = dojson(jstr, skipfail = True)
	if result == ERR:
		if isbad():
			print '--OK-- (isbad', isbad(0), 'transaction', cmd, 'rejected)'
			return False
		if shouldfail:
			print '--OK-- (' + cmd, 'rejected)'
			return False
	elif result:
		print 'unexpected result', result
		raise Exception
	elif not shouldfail:
		return True

	if shouldfail:
		print '***', cmd, 'should have failed'
		print '%% badstack', badstack
		#return False # for testing with proof disabled
	else:
		print '***', cmd, 'failed'
	print jstr
	pprint.pprint(tx)
	print dojson('{"tx-dump" : {}}')
	return fail()

def tx_to_wire(tx, ismint, nout, nin, ninw):
	cmd = 'tx-to-wire'
	tx_to_wire_check = 1
	jstr = '{"' + cmd + '" : {'
	jstr += '"error-check":' + str(int(tx_to_wire_check))
	jstr += '}}'
	wirebuf = ctypes.create_string_buffer(maxwiresize)
	result = dojson(jstr, wirebuf, skipfail = True)
	if result == ERR:
		if isbad() or wirebad:
			print '--OK-- (isbad', isbad(0), 'wirebad', wirebad, cmd, 'rejected)'
			return False

	if (result == ERR and not wirebad) or (result != ERR and wirebad):
		if wirebad:
			print '***', cmd, 'should have failed'
			print '%% wirebad', wirebad
			#return False # for testing
		else:
			print '***', cmd, 'failed'
		print jstr
		pprint.pprint(tx)
		print dojson('{"tx-dump" : {}}')
		return fail()

	nwire = getsize(wirebuf)
	wire = wirebuf[:nwire]
	print 'wire bytes', nwire
	#print hexencode(wire)

	# check formula:
	pred_size = 304 + nout*57
	if not ismint:
		pred_size += nin*48 + (nin-ninw)*38
	else:
		pred_size -= 2 + nout*4
	if nwire != pred_size + 48 and not wirebad and not isbad() and not extra_on_wire:
		print '*** formula failed ismint', ismint, 'pred_size', pred_size + 48, 'nwire', nwire, 'extra_on_wire', extra_on_wire
		pprint.pprint(tx)
		print dojson('{"tx-dump" : {}}')
		raise Exception

	return wire

def tx_from_wire(wire, tx, ismint, nofail = False):

	xtra = {}

	copy_keys(tx, xtra, tx_vars, 'TE')

	if not extra_on_wire:
		copy_keys(tx, xtra, tx_vars, 'TW')
		if not ismint:
			copy_keys(tx, xtra, tx_vars, 'TM')

	jstr = json.dumps(xtra)

	cmd = 'tx-from-wire'
	jstr = '{"' + cmd + '" : ' + jstr + '}'
	#print cmd, '=', jstr

	wirebuf = ctypes.create_string_buffer(wire)
	result = dojson(jstr, wirebuf, skipfail = True)
	if result == ERR:
		if nofail:
			return False
		if isbad():
			print '--OK-- (isbad', isbad(0), cmd, 'rejected)'
			return False
		else:
			print '***', cmd, 'failed'
			pprint.pprint(tx)
			print cmd, '=', jstr
			print dojson('{"tx-dump" : {}}')
			return fail()

	return True

def dotx():
	print '------------------'

	global badsel, badsel_range, rndseed, good_count, bad_count

	print 'rndseed', rndseed
	random.seed(rndseed)
	rndseed += 1

	print 'badsel_range', badsel_range
	badsel_start = random.randrange(badsel_range)
	badsel = badsel_start

	print 'good_count', good_count, 'bad_count', bad_count

	resetbad()

	ismint = (random.random() < test_prob_mint)

	# choose values for # outputs, inputs

	nout = random.randrange(txmaxout + 1 + getbadsel())
	if not txmaxinw or random.getrandbits(1):
		nin = random.randrange(txmaxin + 1 + getbadsel())
		ninw = random.randrange(min(txmaxinw,nin) + 1 + getbadsel())
	else:
		ninw = random.randrange(txmaxinw + 1 + getbadsel())
		nin = random.randrange(max(0, txmaxin - ninw) + 1 + getbadsel()) + ninw

	if txmaxinw and not extra_on_wire and random.getrandbits(4):
		nin = ninw

	if ismint:
		nout = 1	# TODO: try bad mints?
		ninw = 0
		nin = 1
	elif 0: # !!! normally 0; for testing, set to 1 to override nout/nin/ninw
		print '*** WARNING: OVERRIDING NOUT/NIN ***'
		nout = 1
		ninw = 0
		nin = 1
	elif 0: # !!! normally 0; for testing, set to 1 to override ninw
		print '*** WARNING: OVERRIDING NINW ***'
		ninw = 0
	elif test_use_all_tx_inputs:
		print '*** WARNING: SETTING NOUT/NIN/NINW TO MAX ***'
		nout = txmaxout
		ninw = txmaxinw
		nin = txmaxin

	nin = max(nin, ninw)

	if 1: # normally 1
		if nout > txmaxout:
			setbad('nout > txmaxout')
		if ninw > txmaxinw:
			setbad('ninw > txmaxinw')
		if nin > txmaxin:
			setbad('nin > txmaxin')
		if nin > txmaxinc:
			setbad('nin > txmaxinc')
		if isbad():
			print 'isbad', isbad(0), 'nout', nout, 'txmaxout', txmaxout, 'ninw', ninw, 'txmaxinw', txmaxinw, 'nin', nin, 'txmaxin', txmaxin, 'txmaxinc', txmaxinc

	if not extra_on_wire and not ismint and not (test_use_all_tx_inputs and test_only_good_tx):
		if nout < 1:
			setwirebad('nout < 1')
		if nin < 1:
			setwirebad('nin < 1')
		if ninw > 0 and nin != ninw:
			setwirebad('nin != ninw')

	print 'isbad', isbad(0), 'wirebad', wirebad, 'ismint', ismint, 'nout', nout, 'ninw', ninw, 'nin', nin

	# make tx

	tx = {}

	if not make_tx(tx, ismint, nout, nin, ninw):
		print 'make_tx nosuccess'
		return

	print 'make_tx result isbad', isbad(0), 'wirebad', wirebad

	if isbad() or wirebad:
		bad_count += 1
		if not test_only_bad_tx and (test_only_good_tx or (bad_count > good_count and random.randrange(bad_count) > 4 * good_count)):
			print 'skipping tx isbad', isbad(0), 'wirebad', wirebad
			return
	else:
		good_count += 1
		if not test_only_good_tx and (test_only_bad_tx or (good_count > bad_count and random.randrange(good_count) > bad_count)):
			print 'skipping tx good'
			return

	del tx['raddress_omitted']

	#pprint.pprint(tx)

	jstr = json.dumps(tx)

	if ismint:
		jstr = '{"tx-create" : {"mint" : ' + jstr + '}}'
	else:
		jstr = '{"tx-create" : {"tx-pay" : ' + jstr + '}}'


	shouldfail = isbad() and not toint(tx['no-precheck']) and not toint(tx['no-verify'])

	if not do_create_verify('create', tx, jstr, shouldfail):
		return

	if 0: # !!! normally 0; for testing, set to 1 to dump tx and skip rest
		print '*** WARNING: TX DUMP AND DONE ***'
		pprint.pprint(tx)
		print dojson('{"tx-dump" : {}}')
		return

	jstr = '{"tx-verify" : {}}'
	if not do_create_verify('verify', tx, jstr, isbad()):
		return

	if not test_skip_wire_tests:

		# TODO: convert tx-to-json and compare? Note: would be hard to make round trip work.

		if not wirebad and not extra_on_wire and not ismint:
			if nout < 1:
				setwirebad('nout < 1')
			if nin < 1:
				setwirebad('nin < 1')
			if nin != ninw:
				setwirebad('nin != ninw')

		wire = tx_to_wire(tx, ismint, nout, nin, ninw)
		if not wire:
			return

		if not tx_from_wire(wire, tx, ismint):
			return

		jstr = '{"tx-verify" : {}}'
		if not do_create_verify('verify', tx, jstr, wireverifybad):
			return

		if not cc_exceptions_break:
			# flip some bits and make sure proof fails
			for t in range(test_nfuzz):
				wire2 = flipbit(wire)
				if not tx_from_wire(wire2, tx, ismint, True):
					continue
				jstr = '{"tx-verify" : {}}'
				result = dojson(jstr, skipmsg = True)
				if result != ERR:
					print '*** verify should have failed?'
					print 'wire bytes', len(wire)
					print 'original:', hexencode(wire)
					print 'bitflip :', hexencode(wire2)
					pprint.pprint(tx)
					fflip.write(dojson('{"tx-dump" : {}}'))
					tx_from_wire(wire, tx, ismint)
					forig.write(dojson('{"tx-dump" : {}}'))
					forig.flush()
					return fail()

	print '--OK-- passed'

	#print 'badsel start', badsel_start, 'end', badsel, 'range', badsel_range

	if badsel_start - badsel > badsel_range:
		badsel_range += 1

def test_simple_tx():
	# generate and test a simple tx

	amount_scale = 10

	payspec = {}
	payspec['spend-secret'] = 0
	payspec['destination-number'] = 0
	jstr = json.dumps(payspec)
	jstr = '{"payspec-encode" : ' + jstr + '}'
	payspec = dojson(jstr)
	if payspec == ERR:
		fail()

	jstr = '{"payspec-decode" : ' + payspec + '}'
	payspec = dojson(jstr)
	if payspec == ERR:
		fail()
	#print payspec
	payspec = json.loads(payspec)

	inputs = [{}]
	inputs[0] = payspec['payspec']
	inputs[0]['payment-number'] = 0
	inputs[0]['pool'] = 0
	inputs[0]['amount'] = hex(amount_encode_dll(3 * 10**amount_scale))
	inputs[0]['commitment-iv'] = 0

	jstr = json.dumps(inputs)
	jstr = '{"generate-test-inputs" : ' + jstr + '}'
	#print jstr
	inputs = dojson(jstr)
	if inputs == ERR:
		fail()
	#print inputs

	inputs = '{' + inputs + '}'
	tx = json.loads(inputs)

	del tx['inputs'][0]['destination']
	del tx['inputs'][0]['merkle-path']

	tx['inputs'][0]['spend-secret'] = 0
	tx['inputs'][0]['destination-number'] = 0

	tx['parameter-level'] = 0
	tx['parameter-time'] = 0
	tx['merkle-root'] = 0
	tx['source-chain'] = 0
	tx['destination-chain'] = 0
	tx['donation'] = hex(amount_encode_dll(1 * 10**amount_scale, True))
	tx['minimum-output-exponent'] = 0
	tx['maximum-output-exponent'] = hex(TX_AMOUNT_EXPONENT_MASK)
	tx['maximum-input-exponent'] = hex(TX_AMOUNT_EXPONENT_MASK)
	tx['outputs'] = [{}]
	tx['outputs'][0]['destination'] = 0
	tx['outputs'][0]['payment-number'] = 0
	tx['outputs'][0]['pool'] = 0
	tx['outputs'][0]['asset-mask'] = hex((1 << TX_ASSET_BITS) - 1)
	tx['outputs'][0]['amount-mask'] = hex((1 << TX_AMOUNT_BITS) - 1)
	tx['outputs'][0]['amount'] = hex(amount_encode_dll(2 * 10**amount_scale))
	tx['outputs'][0]['acceptance-required'] = 1

	jstr = json.dumps(tx)
	print jstr

	jstr = '{"tx-create" : {"tx-pay" : ' + jstr + '}}'
	print jstr
	result = dojson(jstr)
	if result:
		fail()
	exit()

def generate_mint_params():
	# generate mint parameters; library must be compiled with TEST_EXTRA_ON_WIRE
	jstr = '{"payspec-encode" : '
	jstr += '{ "destination-number" : 0'
	jstr += ', "required-spend-secrets": 0'
	jstr += ', "required-trust-secrets": 0'
	jstr += '}}'
	payspec = dojson(jstr)
	jstr = '{"payspec-decode" : ' + payspec + '}'
	payspec = dojson(jstr)
	payspec = json.loads(payspec)
	payspec = payspec['payspec']

	jstr = '{"tx-create" : {"tx-pay" : '
	jstr += '{ "no-precheck" : 1'
	jstr += ', "no-verify" : 1'
	jstr += ', "source-chain" : 0'
	jstr += ', "destination-chain" : 0'
	jstr += ', "parameter-level" : 0'
	jstr += ', "parameter-time" : 0'
	jstr += ', "merkle-root" : 0'
	jstr += ', "minimum-output-exponent" : 0'
	jstr += ', "maximum-output-exponent" : 0'
	jstr += ', "commitment-iv" : 0'
	jstr += ', "donation" : 0'
	jstr += ', "inputs" : [ ]'
	jstr += ', "outputs" : ['
	jstr += '{ "destination" : "' + payspec['destination'] + '"'
	jstr += ', "payment-number" : 0'
	jstr += ', "pool" : 0'
	jstr += ', "asset-mask" : 0'
	jstr += ', "amount-mask" : 0'
	jstr += ', "amount" : "' + hex(amount_encode_dll(TX_CC_MINT_AMOUNT, False, TX_CC_MINT_EXPONENT, TX_CC_MINT_EXPONENT)) + '"'
	jstr += '}]}}}'
	dojson(jstr)
	jstr = '{"tx-to-json" : {}}'
	output = dojson(jstr)
	output = json.loads(output)
	output = output['tx-pay']
	output = output['outputs'][0]
	print dojson('{"tx-dump" : {}}')
	print
	print 'amount', toint(output['amount'])
	print 'commitment', toint(output['commitment'])
	exit()

#test_amount_encode()
#test_simple_tx()
#generate_mint_params()

#for i in range(600):
while True:
	t0 = time.time()
	dotx()
	t1 = time.time()
	print 'total elapsed time', round(t1 - t0, 2)
	#time.sleep(999999)	# stop here to check peak working set size
	if static_rand:
		exit()
	#exit()
