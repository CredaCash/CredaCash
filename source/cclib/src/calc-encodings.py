#!/usr/bin/env python2
 #
 # CredaCash (TM) cryptocurrency and blockchain
 #
 # Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 #
 # calc-encodings.py
#

# make encoding tables

import sys
import math

if not sys.version.startswith('2.7.') or not ('GCC' in sys.version or '64 bit' in sys.version or 'AMD64' in sys.version):
	print 'ERROR: This script requires Python 2.7.x (64 bit version).'
	exit()

def sv(c):
	if c >= ' ' and c <= '~' and c != "'" and c != "\\":
		return "'" + c + "'"
	else:
		return str(ord(c))

def rev(base, conv, extra = ''):
	c0 = min(conv.keys())
	c1 = max(conv.keys())
	print 'const uint8_t base' + str(base) + extra + 'bin[' + str(ord(c1) - ord(c0) + 4) + ']'
	outs = ' = {' + str(base % 256) + ','	# 1st entry is mod
	outs += sv(c0) + ","					# 2nd entry is lowest index
	outs += sv(c1) + ","					# 3rd entry is highest index
	for i in range(ord(c0), ord(c1) + 1):
		c = chr(i)
		if c in conv:
			if conv[c] > base:
				raise Exception
			outs += str(conv[c])
		else:
			outs += '255';
		outs += ','
	print outs[:-1] + '};'
	print

def name(base, extra = ''):
	resize = int(math.log(base)/math.log(256) * 0x10000 + 0.5)
	if resize >= 0x10000:
		resize = 0
	return 'const uint8_t base' + str(base) + extra + 'sym[' + str(base + 3) + ']\n = {' + \
		str(base % 256) + ',' + str(resize>>8) + ',' + str(resize & 0xff) + ','

# all of them
base = 256
conv = {}
outs = name(base)
c = chr(0)
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if ord(c) < 255:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# ascii extended
base = 224
conv = {}
outs = name(base)
c = ' '
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if ord(c) < 255:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# json-friendly ascii extended
base = 222
conv = {}
outs = name(base)
c = ' '
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if ord(c) < 255:
		c = chr(ord(c) + 1)
	if c == '"' or c == '\\':
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# acsii
base = 95
conv = {}
outs = name(base)
c = ' '
for i in range(base):
	conv[c] = i
	outs += sv(c)
	c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# json-friendly ascii
base = 93
conv = {}
outs = name(base)
c = ' '
for i in range(base):
	conv[c] = i
	outs += sv(c)
	c = chr(ord(c) + 1)
	if c == '"' or c == '\\':
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# btoa and Adobe ASCII-85 charset
base = 87
conv = {}
outs = name(base)
c = '!'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == 'u':
		c = 'y'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# RFC-1924 charset
base = 85
conv = {}
outs = name(base)
c = '!'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	c = chr(ord(c) + 1)
	if c == '"' or c == "'" or c == ',' or c == '.' or c == ':' or c == '[':
		c = chr(ord(c) + 1)
	if c == '/' or c == '\\':
		c = chr(ord(c) + 1)
	if c == ']':
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# base64 plus base64url
base = 66
conv = {}
outs = name(base)
c = 'A'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == 'Z':
		c = 'a'
	elif c == 'z':
		c = '0'
	elif c == '9':
		c = '+'
	elif c == '+':
		c = '/'
	elif c == '/':
		c = '-'
	elif c == '-':
		c = '_'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# base64
base = 64
conv = {}
outs = name(base)
c = 'A'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == 'Z':
		c = 'a'
	elif c == 'z':
		c = '0'
	elif c == '9':
		c = '+'
	elif c == '+':
		c = '/'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# base64 - url encoding
base = 64
conv = {}
outs = name(base, 'url')
c = 'A'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == 'Z':
		c = 'a'
	elif c == 'z':
		c = '0'
	elif c == '9':
		c = '-'
	elif c == '-':
		c = '_'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv, 'url')

# bitcoin base58
# All alphanumeric characters except for "0", "I", "O", and "l"
# 123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz
base = 58
conv = {}
outs = name(base)
c = '1'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == '9':
		c = 'A'
	elif c == 'Z':
		c = 'a'
	else:
		c = chr(ord(c) + 1)
	if c == 'I' or c == 'O' or c == 'l':
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# CredaCash base57 = same alphabet as bitcoin base58 without the digit 1
# but different numeric order than bitcoin base58 (digits come first in base58, but come last in base57)
base = 57
conv = {}
outs = name(base)
c = 'A'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == 'Z':
		c = 'a'
	elif c == 'z':		# skip 0, 1 (digits 0, 1)
		c = '2'
	else:
		c = chr(ord(c) + 1)
	if c == 'I' or c == 'O' or c == 'l':
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# base38 - alphanumeric with _ and - added
base = 38
conv = {}
outs = name(base)
c = '0'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == '9':
		c = 'a'
	elif c == 'z':
		c = '-'
	elif c == '-':
		c = '_'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

conv2 = {}
outs = name(base, 'uc')
c = '0'
for i in range(base):
	conv2[c] = i
	outs += sv(c)
	if c == '9':
		c = 'A'
	elif c == 'Z':
		c = '-'
	elif c == '-':
		c = '_'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv2, 'uc')
conv2.update(conv)
rev(base, conv2, 'combo')

# base36 - alphanumeric
base = 36
conv = {}
outs = name(base)
c = '0'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == '9':
		c = 'a'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

conv2 = {}
outs = name(base, 'uc')
c = '0'
for i in range(base):
	conv2[c] = i
	outs += sv(c)
	if c == '9':
		c = 'A'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv2, 'uc')
conv2.update(conv)
rev(base, conv2, 'combo')

# Bech32 charset (excludes i and o)
base = 34
conv = {}
outs = name(base)
c = '0'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == '9':
		c = 'a'
	else:
		c = chr(ord(c) + 1)
	if c == 'i' or c == 'o':
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# base32
base = 32
conv = {}
outs = name(base)
c = 'A'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == 'Z':		# skip 0, 1 (digits 0, 1)
		c = '2'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# base32 - zbase character set
base = 32
conv = {}
outs = name(base, 'z')
c = 'a'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == 'z':
		c = '1'
	else:
		c = chr(ord(c) + 1)
	if c == 'l' or c == 'v' or c == '2':
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv, 'z')

# base26 - letters
base = 26
conv = {}
outs = name(base)
c = 'a'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

conv2 = {}
outs = name(base, 'uc')
c = 'A'
for i in range(base):
	conv2[c] = i
	outs += sv(c)
	c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv2, 'uc')
conv2.update(conv)
rev(base, conv2, 'combo')

# base17 - hex plus x
base = 17
conv = {}
outs = name(base)
c = 'x'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == 'x':
		c = '0'
	elif c == '9':
		c = 'a'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

conv2 = {}
outs = name(base, 'uc')
c = 'X'
for i in range(base):
	conv2[c] = i
	outs += sv(c)
	if c == 'X':
		c = '0'
	elif c == '9':
		c = 'A'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv2, 'uc')
conv2.update(conv)
rev(base, conv2, 'combo')

# base16 - hex
base = 16
conv = {}
outs = name(base)
c = '0'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	if c == '9':
		c = 'a'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

conv2 = {}
outs = name(base, 'uc')
c = '0'
for i in range(base):
	conv2[c] = i
	outs += sv(c)
	if c == '9':
		c = 'A'
	else:
		c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv2, 'uc')
conv2.update(conv)
rev(base, conv2, 'combo')

# base10
base = 10
conv = {}
outs = name(base)
c = '0'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)

# base8
base = 8
conv = {}
outs = name(base)
c = '2'
for i in range(base):
	conv[c] = i
	outs += sv(c)
	c = chr(ord(c) + 1)
	outs += ','
print outs[:-1] + '};'
print

rev(base, conv)
