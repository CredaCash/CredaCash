'''
CredaCash(TM) Mint Initialization Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2019 Creda Software, Inc.

The purpose of this script is to initialize the commitment Merkle tree root hash 
to a provably random value.  All subsequent mint transactions must be based on 
that Merkle root hash, so setting it to a random value prevents pre-computation 
of the mint transactions.

This is accomplished by creating an initial first mint transaction that has a 
single zero-value and unspendable output.  To be provably random, the output 
destination will be set from numbers drawn in one or more recent lotteries. This 
destination is then hashed to a commitment value that is added to the Merkle 
tree.

'''

mint_seed = '''
2019-06-15 Powerball 8 11 14 16 49 14
2019-06-12 Powerball 5 35 38 42 57 13
2019-06-08 Powerball 9 13 42 48 60 18
2019-06-14 Mega Millions 19 40 47 57 65 6
2019-06-11 Mega Millions 20 34 39 43 57 13
2019-06-07 Mega Millions 17 19 27 40 68 2
'''

import hashlib

from cclib import *

cclib.net_port = 9220

def main(argv):

	dest = int(hashlib.sha256(mint_seed).hexdigest(), 16)
	dest &= (1 << TX_FIELD_BITS) - 1
	if not (dest & TX_ACCEPT_REQ_DEST_MASK):
		dest |= TX_ACCEPT_REQ_DEST_MASK
	dest = hex(dest)

	print
	print 'mint_seed:', mint_seed
	print 'destination:', dest
	print

	NetParams.Query()					# get network parameters

	inputs = QueryInputs('')

	if toint(inputs['parameter-level']) != 0:
		print 'ERROR: blockchain already initialized (parameter-level != 0)'
		exit()

	pool = inputs['default-output-pool']

	inputs = json.dumps(inputs)
	inputs = inputs[1:-1]	# strip off outer brackets
	#print inputs

	jstr = '{"tx-create":'
	jstr += '{"mint":'
	jstr += '{"source-chain" : "' + NetParams.blockchain + '"'
	jstr += ',"destination-chain" : "' + NetParams.blockchain + '"'
	jstr += ',"donation" : "' + hex(Amounts.Encode(TX_CC_MINT_AMOUNT, 0, True)) + '"'

	jstr += ',' + inputs
	jstr += ',"outputs":['

	jstr += '{"destination" : "' + dest + '"'
	jstr += ',"payment-number" : 0'
	jstr += ',"pool" : "' + pool + '"'
	jstr += ',"amount" : "' + hex(Amounts.Encode(0, 0, False)) + '"'
	jstr += ',"asset-mask" : 0'
	jstr += ',"amount-mask" : 0'

	jstr += '}]'
	jstr += '}}}'
	print jstr

	rc, text = DoJsonCmd(jstr, returnrc = True)
	if rc:
		print 'ERROR creating transaction:', text
		exit()

	jstr = '{"tx-to-json" : {}}'
	output = DoJsonCmd(jstr)
	output = json.loads(output)
	output = output['mint']

	jstr = '{"tx-to-wire" : {'
	jstr += '"error-check":1'
	jstr += '}}'
	rc, text, wire = DoJsonCmd(jstr, True, returnrc = True)
	if rc:
		print 'ERROR converting transaction to wire format:', text
		exit()
	#print wire

	nbytes = GetMsgSize(wire)
	wire = wire[:nbytes]
	print 'mint transaction size =', nbytes

	f = open('first_mint.dat', 'w+b')
	f.write(wire)
	f.close()

if __name__ == '__main__':
	main(sys.argv)
