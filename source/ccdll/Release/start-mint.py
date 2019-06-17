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
2019-05-22 Powerball 7 10 20 44 57 3
2019-05-18 Powerball 2 10 25 66 67 26
2019-05-15 Powerball 2 7 17 33 61 68 4
2019-05-21 Mega Millions 10 50 55 56 58 15
2019-05-17 Mega Millions 5 17 28 32 63 11
2019-05-14 Mega Millions 11 59 66 67 68 18
'''

import hashlib

from cclib import *

cclib.net_port = 9210

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
