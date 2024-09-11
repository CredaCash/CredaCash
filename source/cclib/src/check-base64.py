#!/usr/bin/env python2
 #
 # CredaCash (TM) cryptocurrency and blockchain
 #
 # Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 #
 # check-base64.py
#

import base64

f = open('encode.out')

while True:
	s = f.readline()
	e = f.readline()
	if not s:
		break

	s = s.rstrip('\n')
	e = e.rstrip('\n')

	#print s, e

	enc = base64.b64encode(bytearray.fromhex(s))

	if enc != e:
		print 'encode error:'
		print 'input', s
		print 'output', e
		print 'base64', enc
		print
