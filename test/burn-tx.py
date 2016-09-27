'''
CredaCash(TM) Transaction Library Test Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2016 Creda Software, Inc.

This script runs several tests against the CredaCash transaction library:

- Creates transactions across the full range of bill values and full range of nout, nin and nin_with_path
	including positive and negative donations, and values chosen randomly or randomly set to either 0 or 2^64-1
- Attempts proof and verification with either the properly sized keypair, or a randomly chosen larger keypair
- Verifies a valid transaction is always accepted
- Verifies a randomly chosen output from a valid transaction can be spent
- Creates transactions using bad inputs, and verifies these are always rejected
- Randomly flips one bit in the binary wire representation of a valid transaction,
	and verifies this always causes the transaction to be rejected
	this also tests the immutability of value min/max's if code was built with TEST_EXTRA_ON_WIRE

To run:
	\Python27x64\python ../src/burn-tx.py > burn.out 2>&1

To detect potential problems, search the output for the substring "fail" (case insensitive)

'''

from ctypes import *
import sys
import json
import random
import codecs

txmaxout = 16
txmaxinw = 16
txmaxin = txmaxinw + 2

if 1:
	txmaxout = 16
	txmaxinw = 8
	#txmaxin = txmaxinw + 2
	txmaxin = txmaxinw

if 1:
	txmaxout = 16
	txmaxinw = 4
	#txmaxin = txmaxinw + 2
	txmaxin = txmaxinw

prob_bad = 0.5

extra_on_wire = False			# must match TEST_EXTRA_ON_WIRE in code
exceptions_break = False		# must match TEST_BREAK_ON_ASSERT in code

dllif = windll

CCTx_JsonCmd = dllif.cctx64.CCTx_JsonCmd
CCTx_JsonCmd.argtypes = [c_char_p, c_char_p, c_uint]

hexencode = codecs.getencoder('hex')

ERR = '?'

forig = open('orig.out', 'w')
fflip = open('flip.out', 'w')

def fail():
	print 'FAIL'
	raise Exception # !!!

def dojson(jstr, binary = False, buf = None, skiperr = False):
	#print '>>>', jstr
	sys.stdout.flush()
	input = c_char_p(jstr)
	bufsize = 80000
	if buf:
		#print 'buf', ord(buf[0]), ord(buf[1]), ord(buf[2]), ord(buf[3])
		output = create_string_buffer(buf, bufsize)
	else:
		output = create_string_buffer(bufsize)
	bufsize = c_uint(bufsize)
	rc = CCTx_JsonCmd(input, output, bufsize)
	#print 'CCTx_JsonCmd', rc
	sys.stdout.flush()
	sys.stderr.flush()
	if rc:
		if not skiperr:
			print 'CCTx_JsonCmd failed', rc
			print '>>>', jstr
			if not buf:
				print '<<<', output.value
			sys.stdout.flush()
			fail()
		return ERR
	if binary:
		return output.raw
	else:
		return output.value

def getsize(buf):
	nbytes = 0
	for i in range(3,-1,-1):
		nbytes = nbytes * 256 + ord(buf[i])
		#print i, ord(buf[i]), nbytes
	return nbytes

def makebill(value, spend_secret, spendspec_hashed, enforce_spend_spec_hash, input):
	jstr = '{"payspec" : '
	jstr += '{"sequence-type" : "0"'
	jstr += ', "spend-secret" : "' + hex(spend_secret) + '"'
	jstr += ', "enforce-spend-spec-hash" : "' + hex(enforce_spend_spec_hash) + '"'
	if extra_on_wire and (enforce_spend_spec_hash or random.getrandbits(1)):
		jstr += ', "hashed-spend-spec" : "' + hex(spendspec_hashed) + '"'
	jstr += '}}'
	jstr = '{"payspec-encode" : ' + jstr + '}'
	payspec = dojson(jstr)
	if payspec == ERR:
		return ERR
	jstr = '{"payspec-decode" : "' + payspec + '"}'
	payspec = dojson(jstr)
	if payspec == ERR:
		return ERR
	payspec = json.loads(payspec)
	payspec = payspec['payspec']
	jstr = '{"destination" : "' + payspec['destination'] + '"'
	jstr += ', "payment-number" : "' + hex(random.getrandbits(128)) + '"'
	jstr += ', "value" : "' + hex(value) + '"'
	if input:
		jstr += ', "commitment-iv" : "' + hex(random.getrandbits(128)) + '"'
		jstr += ', "spend-secret" : "' + hex(spend_secret) + '"'
		jstr += ', "enforce-spend-spec-hash" : "' + hex(enforce_spend_spec_hash) + '"'
		if extra_on_wire and (enforce_spend_spec_hash or random.getrandbits(1)):
			jstr += ', "hashed-spend-spec" : "' + hex(spendspec_hashed) + '"'
	jstr += '}'
	return jstr

def flipbit(wire, nwire):
	wire2 = bytearray(wire)
	byte = random.randrange(nwire - 56)		# 56 = proof of work size 48 + param_level bytes 8
	#byte = 8								# for testing
	#byte = nwire - 56						# for testing
	if byte >= 8:							# 8 = header size (size and tag words)
		byte += 56							# don't flip bit in proof of work or proof_params byte (bytes at offset 8-63 inclusive)
	bit = random.randrange(8)
	print 'flipping byte', byte, 'bit', bit, 'original value', hex(wire2[byte])
	if byte < 0 or byte >= nwire:
		print 'flipbit error byte', byte, 'nwire', nwire, 'msg size', GetMsgSize(wire)
		raise Exception
	wire2[byte] = wire2[byte] ^ (1 << bit)
	return str(wire2)

noutmin = ninmin = ninwmin = ninwomin = 999
noutmax = ninmax = ninwmax = ninwomax = 0

badsel = 0

def dotx():
	global badsel

	print '------------------'

	while True:
		nout = random.randrange(txmaxout + 1)
		if random.getrandbits(1):
			nin = random.randrange(txmaxin + 1)
			ninw = random.randrange(min(txmaxinw,nin) + 1)
		else:
			ninw = random.randrange(txmaxinw + 1)
			nin = random.randrange(txmaxin - ninw + 1) + ninw
		if nout + nin > 0:
			break

	if 0:
		nout = 4
		ninw = 1
		nin = 1

	if 0:
		nout = 16
		ninw = 16
		nin = 18

	print 'nout', nout, 'nin', nin, 'ninw', ninw

	global noutmin, ninmin, ninwmin, ninwomin, noutmax, ninmax, ninwmax, ninwomax
	noutmin = min(noutmin, nout)
	ninmin = min(ninmin, nin)
	ninwmin = min(ninwmin, ninw)
	ninwomin = min(ninwomin, nin - ninw)
	noutmax = max(noutmax, nout)
	ninmax = max(ninmax, nin)
	ninwmax = max(ninwmax, ninw)
	ninwomax = max(ninwomax, nin - ninw)
	#return

	insum = outsum = donation = 0
	outval = []
	inval = []
	for i in range(nout):
		if not random.randrange(10):
			val = (1 << 64) - 1
		else:
			val = random.getrandbits(64)
		outval.append(val)
		outsum += val
	for i in range(nin):
		if not random.randrange(10):
			val = (1 << 64) - 1
		else:
			val = random.getrandbits(64)
		inval.append(val)
		insum += val

	#print 'insum', insum, 'outsum', outsum, 'donation', donation, hex(donation)
	#print 'outvals', outval
	#print 'invals', inval

	if random.getrandbits(1):
		while insum > outsum and nin > 0:
			if insum - outsum < (1 << 63):
				donation = insum - outsum
				outsum += donation
				break
			i = random.randrange(nin)
			sub = min(inval[i], insum - outsum)
			inval[i] -= sub
			insum -= sub
	while outsum > insum and nout > 0:
		if outsum - insum < (1 << 63):
			donation = insum - outsum
			outsum += donation
			break
		i = random.randrange(nout)
		sub = min(outval[i], outsum - insum)
		outval[i] -= sub
		outsum -= sub
	while insum > outsum and nin > 0:
		if insum - outsum < (1 << 63):
			donation = insum - outsum
			outsum += donation
			break
		i = random.randrange(nin)
		sub = min(inval[i], insum - outsum)
		inval[i] -= sub
		insum -= sub

	print 'insum', insum, 'outsum', outsum, 'donation', donation, hex(donation)
	print 'outvals', outval
	print 'invals', inval
	#return

	maxoutval = maxinval = 0
	minoutval = (1 << 64) - 1
	for i in range(nout):
		minoutval = min(minoutval, outval[i])
		maxoutval = max(maxoutval, outval[i])
	for i in range(nin):
		maxinval = max(maxinval, inval[i])
	#print 'maxoutval', maxoutval, 'maxinval', maxinval, 'minoutval', minoutval

	outvals_public = random.getrandbits(1)
	nonfinancial = random.getrandbits(1)

	badsel = 0
	test_make_bad = 0
	isbad = False

	if random.random() < prob_bad:
		if random.getrandbits(1):
			#print 'makebad: setting test_make_bad'
			test_make_bad = 1
			isbad = True
		else:
			badsel = 18
			badsel %= 19
			badsel = random.randrange(18) + 1
			print 'starting badsel', badsel
			if nin:
				rin = random.randrange(nin)
			if nout:
				rout = random.randrange(nout)
			rpath = random.randrange(48)

	badsel -= 1
	if badsel == 0 and donation < ((1 << 64) - 1):
		print 'makebad: incrementing donation', donation
		donation = donation + 1
		print 'makebad:  incremented donation', donation
		isbad = True
	badsel -= 1
	if badsel == 0 and minoutval < ((1 << 64) - 1):
		print 'makebad: incrementing minoutval', minoutval
		minoutval = minoutval + 1
		print 'makebad:   incremented minoutval', minoutval
		isbad = True
	badsel -= 1
	if badsel == 0 and maxoutval > 0:
		print 'makebad: decrementing maxoutval', maxoutval
		maxoutval = maxoutval - 1
		print 'makebad:  decremented maxoutval', maxoutval
		isbad = True
	badsel -= 1
	if badsel == 0 and maxinval > 0:
		print 'makebad: decrementing maxinval', maxinval
		maxinval = maxinval - 1
		print 'makebad:  decremented maxinval', maxinval
		isbad = True

	jstr = '{"tx-create" : {"tx-pay" : {'
	jstr += '"no-precheck" : "' + str(random.getrandbits(1)) + '"'
	jstr += ', "test-make-bad" : "' + str(test_make_bad) + '"'
	jstr += ', "test-use-larger-zkkey" : "' + str(random.getrandbits(1)*1) + '"'	# !!! should be enabled
	jstr += ', "donation" : "' + hex(donation) + '"'
	jstr += ', "minimum-output-value" : "' + hex(minoutval) + '"'
	jstr += ', "maximum-output-value" : "' + hex(maxoutval) + '"'
	jstr += ', "maximum-input-value" : "' + hex(maxinval) + '"'
	if outvals_public:
		jstr += ', "outvals-public" : "' + hex(outvals_public) + '"'
	if nonfinancial:
		jstr += ', "nonfinancial" : "' + hex(nonfinancial) + '"'
	jstr += ', "outputs" : ['

	spend_out_index = random.randrange(max(1,nout))	# pick one to later spend

	for i in range(nout):
		if i:
			jstr += ', '
		spend_secret = random.getrandbits(256)
		spendspec_hashed = random.getrandbits(256)
		if extra_on_wire:
			enforce_spend_spec_hash = random.getrandbits(1)
		else:
			enforce_spend_spec_hash = 0
		output = makebill(outval[i], spend_secret, spendspec_hashed, enforce_spend_spec_hash, False)
		jstr += output
		if i == spend_out_index:
			spend_output_secret = spend_secret
			spend_output_spendspec_hashed = spendspec_hashed
			spend_enforce_spend_spec_hash = enforce_spend_spec_hash
			spend_out_outvals_public = outvals_public
			spend_out_nonfinancial = nonfinancial
			if outval[i] >= (1 << 63):
				spend_out_index = -1	# this amount can't be spent in a tx with no outputs

	jstr += '], '

	# generate test inputs:
	# for each input, computes M-commitment, then generates a merkle tree containing those M-commitment's
	inputs = '{"generate-test-inputs" : ['
	for i in range(nin):
		if i:
			inputs += ', '
		spend_secret = random.getrandbits(256)
		spendspec_hashed = random.getrandbits(256)
		if extra_on_wire:
			enforce_spend_spec_hash = random.getrandbits(1)
		else:
			enforce_spend_spec_hash = 0
		inputs += makebill(inval[i], spend_secret, spendspec_hashed, enforce_spend_spec_hash, True)
	inputs += ']}'
	inputs = dojson(inputs)
	if inputs == ERR:
		return
	inputs = json.loads('{'+inputs+'}')

	ninw2 = nin
	while ninw2 > ninw:
		i = random.randrange(nin)
		if 'merkle-path' in inputs['inputs'][i]:
			del inputs['inputs'][i]['commitment-number']
			del inputs['inputs'][i]['merkle-path']
			ninw2 -= 1

	badsel -= 1
	if badsel == 0 and ninw > 0:
		print 'makebad: replacing merkle-root', rin, inputs['merkle-root']
		inputs['merkle-root'] = hex(random.getrandbits(253))
		print 'makebad: replaced  merkle-root', rin, inputs['merkle-root']
		isbad = True
	badsel -= 1
	if badsel == 0 and nin > 0:
		print 'makebad: replacing spend-secret', rin, inputs['inputs'][rin]['spend-secret']
		inputs['inputs'][rin]['spend-secret'] = hex(random.getrandbits(256))
		print 'makebad: replaced  spend-secret', rin, inputs['inputs'][rin]['spend-secret']
		isbad = True
	badsel -= 1
	if badsel == 0 and nin > 0 and extra_on_wire and int(inputs['inputs'][i]['enforce-spend-spec-hash'], 16):
		print 'makebad: replacing hashed-spend-spec', i, inputs['inputs'][i]['hashed-spend-spec']
		inputs['inputs'][i]['hashed-spend-spec'] = hex(random.getrandbits(256))
		print 'makebad: replaced  hashed-spend-spec', i, inputs['inputs'][i]['hashed-spend-spec']
		isbad = True
	badsel -= 1
	if badsel == 0 and nin > 0 and extra_on_wire:
		print 'makebad: replacing enforce-spend-spec-hash', i, inputs['inputs'][i]['enforce-spend-spec-hash']
		inputs['inputs'][i]['enforce-spend-spec-hash'] = hex(int(inputs['inputs'][i]['enforce-spend-spec-hash'], 16) ^ 1)
		print 'makebad: replaced  enforce-spend-spec-hash', i, inputs['inputs'][i]['enforce-spend-spec-hash']
		isbad = True
	badsel -= 1
	if badsel == 0 and nin > 0:
		print 'makebad: replacing payment-number', rin, inputs['inputs'][rin]['payment-number']
		inputs['inputs'][rin]['payment-number'] = hex(random.getrandbits(128))
		print 'makebad: replaced  payment-number', rin, inputs['inputs'][rin]['payment-number']
		isbad = True
	badsel -= 1
	if badsel == 0 and nin > 0:
		print 'makebad: replacing value', rin, inputs['inputs'][rin]['value']
		inputs['inputs'][rin]['value'] = hex(random.getrandbits(64))
		print 'makebad: replaced  value', rin, inputs['inputs'][rin]['value']
		isbad = True
	badsel -= 1
	if badsel == 0 and nin > 0:
		print 'makebad: replacing commitment_iv', rin, inputs['inputs'][rin]['commitment-iv']
		inputs['inputs'][rin]['commitment-iv'] = hex(random.getrandbits(128))
		print 'makebad: replaced  commitment_iv', rin, inputs['inputs'][rin]['commitment-iv']
		isbad = True
	badsel -= 1
	if badsel == 0 and nin > 0:
		print 'makebad: replacing commitment', rin, inputs['inputs'][rin]['commitment']
		inputs['inputs'][rin]['commitment'] = hex(random.getrandbits(253))
		print 'makebad: replaced  commitment', rin, inputs['inputs'][rin]['commitment']
		isbad = True
	badsel -= 1
	if badsel == 0 and nin > 0 and 'commitment-number' in inputs['inputs'][rin]:
		print 'makebad: replacing commitment-number', rin, inputs['inputs'][rin]['commitment-number']
		inputs['inputs'][rin]['commitment-number'] = hex(random.getrandbits(253))
		print 'makebad: replaced  commitment-number', rin, inputs['inputs'][rin]['commitment-number']
		isbad = True
	badsel -= 1
	if badsel == 0 and nin > 0 and 'merkle-path' in inputs['inputs'][rin]:
		print 'makebad: replacing merkle-path', rin, rpath, inputs['inputs'][rin]['merkle-path'][rpath]
		inputs['inputs'][rin]['merkle-path'][rpath] = hex(random.getrandbits(253))
		print 'makebad: replaced  merkle-path', rin, rpath, inputs['inputs'][rin]['merkle-path'][rpath]
		isbad = True

	merkle_root = inputs['merkle-root']
	#print merkle_root

	inputs = json.dumps(inputs)
	#print '------'
	#print inputs
	jstr += inputs[1:]	# strip leading '{'

	jstr += '}}'

	txcreate = jstr
	result = dojson(jstr, skiperr = isbad)
	if isbad:
		if result != ERR:
			print '*** create should have failed'
			return fail()
		else:
			print '--OK--'
		return
	if result:
		return fail()

	if 0:
		print jstr
		jstr = '{"tx-dump" : {}}'
		result = dojson(jstr)
		print result

	# retrieve output that we'll spend shortly
	if nout > 0 and spend_out_index >= 0:
		jstr = '{"tx-to-json" : {}}'
		output = dojson(jstr)
		if output == ERR:
			return
		#print output
		try:
			output = json.loads(output)
		except Exception as e:
			print output
			raise e
		output = output['tx-pay']
		commitment_iv = int(output['merkle-root'], 16) & ((1 << 128) - 1)
		output = output['outputs'][spend_out_index]
		del output['destination']
		del output['address']
		del output['encrypted-value']
		output['commitment-iv'] = hex(commitment_iv)
		output['spend-secret'] = hex(spend_output_secret)
		output['enforce-spend-spec-hash'] = hex(spend_enforce_spend_spec_hash)
		if not spend_enforce_spend_spec_hash:
			spend_output_spendspec_hashed = random.getrandbits(256)
		if extra_on_wire and (spend_enforce_spend_spec_hash or random.getrandbits(1)):
			output['hashed-spend-spec'] = hex(spend_output_spendspec_hashed)
		output = json.dumps(output)
		#print output

	jstr = '{"tx-to-wire" : {}}'
	result = dojson(jstr, True)
	if result == ERR:
		print 'txcreate = ', txcreate
		return fail()

	nwire = getsize(result)
	wire = result[:nwire]
	print 'wire bytes', nwire
	#print hexencode(wire)

	# check formula:
	npred = 365 + extra_on_wire * 58 + nout * 72 + nin * (33 + extra_on_wire * 32) + (nin - ninw) * 32
	if nwire != npred:
		print '*** npred formula failed', npred, nwire
		raise Exception

	badsel -= 1
	if badsel == 0 and minoutval < ((1 << 64) - 1) and not extra_on_wire:
		print 'makebad: incrementing minoutval pre-verify', minoutval
		minoutval = minoutval + 1
		print 'makebad:   incremented minoutval pre-verify', minoutval
		isbad = True
	badsel -= 1
	if badsel == 0 and maxoutval > 0 and not extra_on_wire:
		print 'makebad: decrementing maxoutval pre-verify', maxoutval
		maxoutval = maxoutval - 1
		print 'makebad:  decremented maxoutval pre-verify', maxoutval
		isbad = True
	badsel -= 1
	if badsel == 0 and maxinval > 0 and not extra_on_wire:
		print 'makebad: decrementing maxinval pre-verify', maxinval
		maxinval = maxinval - 1
		print 'makebad:  decremented maxinval pre-verify', maxinval
		isbad = True
	badsel -= 1
	if badsel == 0 and not extra_on_wire:
		print 'makebad:   flipping outvals_public pre-verify', outvals_public
		outvals_public = outvals_public ^ 1
		print 'makebad:    flipped outvals_public pre-verify', outvals_public
		isbad = True
	badsel -= 1
	if badsel == 0 and not extra_on_wire:
		print 'makebad:     flipping nonfinancial pre-verify', nonfinancial
		nonfinancial = nonfinancial ^ 1
		print 'makebad:      flipped nonfinancial pre-verify', nonfinancial
		isbad = True

	#print 'end of tests badsel', badsel, '(should be zero or less)'
	if badsel > 0:
		print 'badsel > 0 failure', badsel
		fail()

	jstr = '{"tx-from-wire" : {'
	if not extra_on_wire:
		jstr += '  "merkle-root" : "' + merkle_root + '"'
		jstr += ', "minimum-output-value" : "' + hex(minoutval) + '"'
		jstr += ', "maximum-output-value" : "' + hex(maxoutval) + '"'
		jstr += ', "maximum-input-value" : "' + hex(maxinval) + '"'
		if outvals_public:
			jstr += ', "outvals-public" : "' + hex(outvals_public) + '"'
		if nonfinancial:
			jstr += ', "nonfinancial" : "' + hex(nonfinancial) + '"'
	jstr += '}}'
	result = dojson(jstr, True, wire)
	if result == ERR:
		print 'txcreate = ', txcreate
		jstr = '{"tx-dump" : {}}'
		result = dojson(jstr)
		print result
		return fail()

	if 0:
		print jstr
		jstr = '{"tx-dump" : {}}'
		result = dojson(jstr)
		print result

	jstr = '{"tx-verify" : {}}'
	result = dojson(jstr, skiperr = True)
	if isbad:
		if result != ERR:
			print '*** verify should have failed'
			return fail()
		else:
			print '--OK--'
		return
	if result == ERR:
		print 'txcreate = ', txcreate
		jstr = '{"tx-dump" : {}}'
		result = dojson(jstr)
		print result
		return fail()

	if not exceptions_break:
		# flip some bits and make sure proof fails
		for t in range(50):
			wire2 = flipbit(wire, nwire)

			jstr = '{"tx-from-wire" : {'
			if not extra_on_wire:
				jstr += '  "merkle-root" : "' + merkle_root + '"'
				jstr += ', "minimum-output-value" : "' + hex(minoutval) + '"'
				jstr += ', "maximum-output-value" : "' + hex(maxoutval) + '"'
				jstr += ', "maximum-input-value" : "' + hex(maxinval) + '"'
				if outvals_public:
					jstr += ', "outvals-public" : "' + hex(outvals_public) + '"'
				if nonfinancial:
					jstr += ', "nonfinancial" : "' + hex(nonfinancial) + '"'
			jstr += '}}'
			result = dojson(jstr, True, wire2, skiperr = True)
			if result == ERR:
				continue

			jstr = '{"tx-verify" : {}}'
			result = dojson(jstr, skiperr = True)
			if result != ERR:
				print '*** verify should have failed?'
				print 'wire bytes', nwire
				print 'original:', hexencode(wire)
				print 'bitflip :', hexencode(wire2)
				#print 'txcreate = ', txcreate
				jstr = '{"tx-dump" : {}}'
				result = dojson(jstr)
				fflip.write(result)
				fflip.flush()
				jstr = '{"tx-from-wire" : {'
				if not extra_on_wire:
					jstr += '  "merkle-root" : "' + merkle_root + '"'
					jstr += ', "minimum-output-value" : "' + hex(minoutval) + '"'
					jstr += ', "maximum-output-value" : "' + hex(maxoutval) + '"'
					jstr += ', "maximum-input-value" : "' + hex(maxinval) + '"'
					if outvals_public:
						jstr += ', "outvals-public" : "' + hex(outvals_public) + '"'
					if nonfinancial:
						jstr += ', "nonfinancial" : "' + hex(nonfinancial) + '"'
				jstr += '}}'
				dojson(jstr, True, wire)
				jstr = '{"tx-dump" : {}}'
				result = dojson(jstr)
				forig.write(result)
				forig.flush()
				return fail()

	# try to spend the output selected earlier
	if nout > 0 and spend_out_index >= 0:
		print 'attempting to spend one of the earlier outputs...'
		jstr = '{"tx-create" : {"tx-pay" : {'
		jstr += '"no-precheck" : "' + str(random.getrandbits(1)) + '"'
		jstr += ', "test-use-larger-zkkey" : 0'
		jstr += ', "parameter-level" : 0'
		jstr += ', "merkle-root" : "' + hex(random.getrandbits(253)) + '"'	# must be < prime modulus
		jstr += ', "minimum-output-value" : 0'
		jstr += ', "maximum-output-value" : "' + hex((1 << 64) - 1) + '"'
		jstr += ', "maximum-input-value" : "' + hex((1 << 64) - 1) + '"'
		if spend_out_outvals_public:
			jstr += ', "outvals-public" : "' + hex(spend_out_outvals_public) + '"'
		if spend_out_nonfinancial:
			jstr += ', "nonfinancial" : "' + hex(spend_out_nonfinancial) + '"'
		jstr += ', "donation" : "' + hex(outval[spend_out_index]) + '"'
		jstr += ', "outputs" : [ ]'
		jstr += ', "inputs" : [' + output + ']'
		jstr += '}}}'

		result = dojson(jstr)
		if result:
			return fail()

	print '--OK--'


while True:
	#for iter in range(1):
	dotx()

print 'noutmin', noutmin, 'ninmin', ninmin, 'ninwmin', ninwmin, 'ninwomin', ninwomin
print 'noutmax', noutmax, 'ninmax', ninmax, 'ninwmax', ninwmax, 'ninwomax', ninwomax
