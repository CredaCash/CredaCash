#!/usr/bin/env python2
 #
 # CredaCash (TM) cryptocurrency and blockchain
 #
 # Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 #
 # calc-basis.py
#

import sys
import hashlib

if not sys.version.startswith('2.7.') or not ('GCC' in sys.version or '64 bit' in sys.version or 'AMD64' in sys.version):
	print 'ERROR: This script requires Python 2.7.x (64 bit version).'
	exit()

wordm = (1 << 64) - 1

print
print "static const uint64_t hash_bases_prfkeys[] = {"

for i in range(16):
	h = hashlib.sha256()
	s = "prfkey %02d" % i
	h.update(s)
	b = h.hexdigest()
	x = int(b, 16)
	line = "\t/* sha256(\"%s\") */\t" % s
	for j in range(2):
		line += "0x%016x," % ((x >> (j*64)) & wordm)
	print line

print "};"


p = int("21888242871839275222246405745257275088548364400416034343698204186575808495617")

print
print "// modulus decimal %d" % p
print "// modulus hex   0x%x" % p
print
print "#define HASHBASES_RANDOM_START\t\t256"
print "#define HASHBASES_NRANDOM\t\t\t2048"
print

print "static const mp_limb_t hash_bases[] = {"

for i in range(256 + 2048):
	for j in range(99999):
		if i < 256:
			x = 1 << i
			line = "\t\t\t/* bit %03d */\t\t" % i
			break
		else:
			h = hashlib.sha256()
			s = "basis %04d.%d" % (i-256, j)
			h.update(s)
			b = h.hexdigest()
			x = int(b, 16)
			x &= (1 << 254) - 1
			#print s, b, x, "%x" % x
			if (x < p):
				line = "/* sha256(\"%s\") */\t" % s
				break
	for j in range(4):
		line += "0x%016x," % ((x >> (j*64)) & wordm)
	print line

print "};"
