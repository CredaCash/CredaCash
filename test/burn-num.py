#!/usr/bin/env python2

'''
CredaCash(TM) Test Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors

This script tests the parsing of JSON numbers in the CredaCash transaction library

'''

import ctypes
import sys
import json
import random
import time
import re

TRACE = 0

PRIMEMOD = 21888242871839275222246405745257275088548364400416034343698204186575808495617

if not sys.version.startswith('2.7.') or not ('GCC' in sys.version or '64 bit' in sys.version or 'AMD64' in sys.version):
	print 'ERROR: This script requires Python 2.7.x (64 bit version).'
	exit()

#random.seed(0)	# for testing

CCTx_DLL = ctypes.cdll.LoadLibrary('./cctx64.dll')
CCTx_JsonCmd = CCTx_DLL.CCTx_JsonCmd
CCTx_JsonCmd.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]

ERR = '?'

def fail():
	print 'FAIL'
	sys.stdout.flush()
	raise Exception

def dojson(jstr, shouldfail = False, skipmsg = False, skipfail = False):
	#print '>>>', jstr
	sys.stdout.flush()
	input = ctypes.c_char_p(jstr)
	output = ctypes.create_string_buffer(2000)
	rc = CCTx_JsonCmd(input, output, len(output), None, 0)
	#print 'CCTx_JsonCmd', rc
	sys.stdout.flush()
	sys.stderr.flush()
	if not skipmsg and ((rc and not shouldfail) or (not rc and shouldfail)):
		if rc:
			print '*** CCTx_JsonCmd error', rc
		else:
			print '*** CCTx_JsonCmd should have returned an error'
		print '>>>', jstr
		if len(output.value):
			print '<<<', output.value
		sys.stdout.flush()
		if not skipfail:
			fail()
	if rc:
		return ERR
	else:
		#print '<<<', output.value
		return output.value

def str2num(val, ret = None):
	if TRACE: print 'str2num val', val
	m = re.match(r'(-?\d*)\.?(\d*)[Ee]?\+?(.*)', val)
	if m is None:
		return ret
	g = m.groups()
	if TRACE: print 'str2num groups', g
	if not len(g[0]) and not len(g[1]):
		return ret
	val = int(g[0] + g[1])
	if len(g[2]):
		try:
			exp = int(g[2])
		except:
			return ret
	else:
		exp = 0
	exp -= len(g[1])
	if TRACE: print 'str2num val', val, 'exp', exp
	val *= 10**exp
	if TRACE: print 'str2num val', val
	return val

def parse(nbits, val, sval = None, isbad = None, add_quotes = True, recurse = True):
	if TRACE:
		print '>>> parse isbad', isbad, 'nbits', nbits, 'val', sval, val
		try:
			nb = 0
			sv = int(val)
			while sv and nb < 299:
				sv >>= 1
				nb += 1
			print nb, hex(val)
		except:
			pass
	if recurse:
		if isinstance(val, int) and sval is None:
			parse(nbits, val, str(val), isbad, False)
		if sval is None:
			sval = str(val)
		if isinstance(val, str):
			val = str2num(val, val)
		if isinstance(val, str):
			try:
				val = int(val)
			except:
				try:
					val = float(val)
				except:
					if isbad:
						val = 0
					else:
						raise Exception
		if add_quotes:
			sval = '"' + sval + '"'

	if isbad is None and isinstance(val, float) and not val.is_integer():
		isbad = 1
		print 'isbad', isbad, 'is_integer', val.is_integer(), 'val', val

	if isbad is None and nbits > 256:
		isbad = 1
		print 'isbad', isbad, 'nbits =', nbits

	if isbad is None and nbits and (val >= (1 << nbits) or val < -(1 << nbits) or val <= -(1 << 256)):
		isbad = 1
		print 'isbad', isbad, 'nbits', nbits, 'val', val

	if isbad is None and not nbits and (val >= PRIMEMOD or val <= -PRIMEMOD):
		isbad = 1
		print 'isbad', isbad, 'nbits', nbits, 'val', val

	if isbad is None and sval is not None:
		s = sval.upper().strip('"').rstrip('L')
		if s[0] == '-':
			s = s[1:]
		s = s.lstrip('0')
		e = s.find('E')
		if e >= 0:
			s = s[:e]
		s = s.replace('.', '')
		if TRACE: print 'parse exp test', sval, s
		if s and s[0] != 'X' and int(s) >= (1 << 256):
			isbad = 1
			print '_isbad', isbad, 'sval', sval, 'mantissa', s

	if isbad is None:
		isbad = 0

	jstr = '{"test-parse-number" :'
	jstr += ' {"amount" : ' + sval
	jstr += ', "nbits" : ' + str(nbits)
	jstr += '}}'
	if not TRACE:
		print 'parse isbad', isbad, 'nbits', nbits, 'val', sval, val, ''
	result = dojson(jstr, shouldfail = isbad)
	if result != ERR and int(result) != val:
			print '*** parse value mismatch isbad', isbad, 'nbits', nbits, 'val', sval, val, result, int(result)
			fail()

def test_random():

	sval = None
	isbad = None
	ishex = 0

	# select a random # bits and random value

	nbits = random.randrange(265) + 1

	val = random.getrandbits(nbits + random.getrandbits(1) + random.getrandbits(1))

	if not random.getrandbits(6):
		val = (1 << nbits) - 3
	if not random.getrandbits(6):
		val = (1 << nbits) - 2
	if not random.getrandbits(6):
		val = (1 << nbits) - 1
	if not random.getrandbits(6):
		val = (1 << nbits)
	if not random.getrandbits(6):
		val = (1 << nbits) + 1
	if not random.getrandbits(6):
		val = (1 << nbits) + 2
	if not random.getrandbits(6):
		val = (1 << nbits) + 3

	if not random.getrandbits(6):
		val = PRIMEMOD - 3
	if not random.getrandbits(6):
		val = PRIMEMOD - 2
	if not random.getrandbits(6):
		val = PRIMEMOD - 1
	if not random.getrandbits(6):
		val = PRIMEMOD
	if not random.getrandbits(6):
		val = PRIMEMOD + 1
	if not random.getrandbits(6):
		val = PRIMEMOD + 2
	if not random.getrandbits(6):
		val = PRIMEMOD + 3

	if not random.getrandbits(1):
		nbits = 0

	if TRACE: print '1 isbad', isbad, val, sval

	# randomly make a fractional value

	if not random.getrandbits(3):
		val = val / float(random.randrange(10) + 2)
	if not random.getrandbits(3):
		val = val / (random.random() + 1)

	if TRACE: print '2 isbad', isbad, val, sval

	# randomly convert integer value to floating point representation

	if not random.getrandbits(2):
		val = float(val)

	# randomly make value negative

	if not random.getrandbits(1):
		val = -val

	if TRACE: print '3 isbad', isbad, val, sval

	# convert value to a integer, float or hex string

	if isinstance(val, float):
		sval = str(val)
		val = str2num(sval)

	if sval is None and not random.getrandbits(1):
		try:
			sval = hex(val)
			ishex = 1
		except:
			pass

	if sval is None:
		sval = str(val)

	if TRACE: print '4 isbad', isbad, val, sval

	# normalize value: no trailing L, no leading zeros

	if sval[-1] == 'L':
		sval = sval[:-1]

	sval = sval.lstrip('0')
	if sval == '':
		sval = '0'

	prefix = sval[0]
	if prefix == '-':
		sval = sval[1:].lstrip('0')
		if sval == '':
			sval = '0'
		sval = prefix + sval

	if TRACE: print '5 isbad', isbad, val, sval

	# check that hex value and exponent don't have leading zeros

	if (sval[0] == 'x' or sval[0] == 'X') and sval[1] == '0' and len(sval) > 2:
		raise Exception

	e = sval.upper().find('E')
	if ishex: e = -1
	if e >= 0 and sval[e+1] == '0':
		raise Exception
	if e == 0 and (sval[e+1] == '+' or sval[e+1] == '-') and sval[e+2] == '0':
		raise Exception

	# randomly insert digit between the E and the sign
	if not isbad and e >= 0 and not random.getrandbits(5):
		ichar = chr(ord('0') + random.randrange(10))
		if TRACE: print '__pre E+ insert sval', sval
		sval = sval[:e+1] + ichar + sval[e+1:]
		if TRACE: print '_post E+ insert sval', sval
		isbad = 1
		print 'isbad', isbad, 'E ichar', ichar, 'sval', sval[sval.upper().find('E'):]

	# randomly remove + sign from the exponent
	if e >= 0 and random.getrandbits(1) and sval[e+1] == '+':
		if TRACE: print '__pre E+ strip sval', sval
		sval = sval[:e+1] + sval[e+2:]
		if TRACE: print '_post E+ strip sval', sval

	if TRACE: print '6 isbad', isbad, val, sval

	# randomly add an exponent
	if not isbad and not ishex:
		if sval.upper().find('E') < 0:
			if not random.getrandbits(5):
				sval += 'e0'
			elif not random.getrandbits(5):
				sval += 'e+0'
			elif not random.getrandbits(5):
				sval += 'e-0'
		else:
			if not isbad and not random.getrandbits(5):
				sval += 'e12'
				isbad = 1
			if not isbad and not random.getrandbits(5):
				sval += 'e+12'
				isbad = 1
			if not isbad and not random.getrandbits(5):
				sval += 'e-12'
				isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += 'e12.'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += 'e.'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += 'e-.2'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += 'e+.2'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += 'e2+'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += 'e2-'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += 'e2+2'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += 'e2-2'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += 'e-+2'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += 'e+-2'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += '+e2'
			isbad = 1
		if not isbad and not random.getrandbits(5):
			sval += '-e2'
			isbad = 1
		if isbad:
			print 'isbad', isbad, 'E sval', sval[sval.upper().find('E'):]

	# randomly convert to upper or lower
	if not random.getrandbits(2):
		sval = sval.upper()
	elif not random.getrandbits(1):
		sval = sval.lower()

	# randomly insert a random non-digit

	if not isbad:
		ipos = random.randrange(len(sval) + 1)
		ichar = chr(random.randrange(128))
		#ichar = '\\'
		if ichar < ' ' or ichar == '"' or ichar >= '~':
			ichar = None
		if ichar >= '0' and ichar <= '9':
			ichar = None
		if ishex and ichar >= 'a' and ichar <= 'f':
			ichar = None
		if ishex and ichar >= 'A' and ichar <= 'F':
			ichar = None
		if ichar is None:
			pass
		elif not ishex and ichar == '.' and sval.find('.') < 0:
			pass
		else:
			if TRACE: print 'ichar', ichar, 'ipos', ipos, 'sval', sval
			if ipos == 0:
				if ichar == '-' and sval[0] != '-':
					pass
				elif (ichar == 'x' or ichar == 'X') and re.match(r'[0-9A-Fa-f]+$', sval) is not None:
					pass
				else:
					sval = ichar + sval
					isbad = 1
			elif ipos == len(sval):
				if ichar == 'L':
					ichar = 'LL'
				else:
					sval += ichar
					isbad = 1
			else:
				if not ishex and (ichar == '+' or ichar == '-') and (sval[ipos-1] == 'e' or sval[ipos-1] == 'E') and (sval[ipos] != '+' and sval[ipos] != '-'):
					pass
				elif not ishex and (ichar == 'e' or ichar == 'E') and sval.upper().find('E') < 0 and sval[ipos:].find('.') < 0:
					pass
				elif (ichar == 'x' or ichar == 'X') and ipos == 1 and (sval[0] == '-' or sval[0] == '0') and re.match(r'[0-9A-Fa-f]+$', sval[1:]) is not None:
					pass
				else:
					if TRACE: print '__pre random insert sval', sval
					sval = sval[:ipos] + ichar + sval[ipos:]
					if TRACE: print '_post random insert sval', sval
					isbad = 1
			if isbad:
				print 'isbad', isbad, 'ichar', ichar, 'ipos', ipos, 'sval', sval

	if TRACE: print '7 isbad', isbad, val, sval

	# randomly add random number of leading zeros

	if not ishex and not random.getrandbits(4):
		if sval[0] == '-' and sval[1] != 'e' and sval[1] != 'E':
			sval = '-' + '0' * random.randrange(100) + sval[1:]
		elif sval[0] != 'e' and sval[0] != 'E':
			sval = '0' * random.randrange(100) + sval

	x = sval.upper().find('X')
	if ishex and not random.getrandbits(4):
		if x >= 0:
			if TRACE: print '__pre X insert sval', sval
			sval = sval[:x+1] + '0' * random.randrange(100) + sval[x+1:]
			if TRACE: print '_post X insert sval', sval

	if ishex and not random.getrandbits(1):
		if x == 0 or x > 0 and sval[x-1] != '0':
			if TRACE: print '__pre 0X insert sval', sval
			sval = sval[:x] + '0' + sval[x:]
			if TRACE: print '_post 0X insert sval', sval

	if TRACE: print '8 isbad', isbad, val, sval

	# randomly add a random number of leading zeros to the exponent
	e = sval.upper().find('E')
	if ishex: e = -1
	if e >= 0 and e+1 < len(sval) and (sval[e+1] == '+' or sval[e+1] == '-'):
		e += 1
	if e >= 0 and e+1 < len(sval) and random.getrandbits(2):
		if TRACE: print '__pre E insert sval', sval
		sval = sval[:e+1] + '0' * random.randrange(100) + sval[e+1:]
		if TRACE: print '_post E insert sval', sval

	if TRACE: print '9 isbad', isbad, val, sval

	# randomly add a trailing L
	if not random.getrandbits(2):
		sval += 'L'

	# set to bad if number contains both an L and an E
	if not isbad and not ishex and sval.find('L') >= 0 and sval.upper().find('E') >= 0:
		isbad = 1
		print 'isbad', isbad, 'L sval', sval[sval.upper().find('E'):]

	parse(nbits, val, sval, isbad = isbad)


def test_fracb(val, sval):
	for nbits in range(258):
		parse(nbits, val, sval)
		parse(nbits, -val, '-' + sval)

def test_frac(val):
	s = str(val)
	l = len(s)
	for z in range(2):
		for e in range(79-l):
			print 'test_frac', val, z, e
			sval = '0' * z*100 + s + '0' * e + '.' + '0' * (78-l-e)
			parse(256, val, sval + '0')
			if not e:
				test_fracb(val, sval)
			test_fracb(val, sval + 'E-' + str(e))

def test_fracs():
	for i in range(79):
		val = ((1 << 256) - 1) / int(10**i)
		test_frac(val)
		test_frac(val+1)


def test_static():
	parse(0, None, isbad = 1)
	parse(0, True, isbad = 1)
	parse(0, False, isbad = 1)
	parse(0, 'a', isbad = 1)

	parse(0, 0, 'null', isbad = 1)
	parse(0, 0, 'true', isbad = 1)
	parse(0, 0, 'false', isbad = 1)
	parse(0, 0, 'a', isbad = 1)
	parse(0, 0, '"a"', isbad = 1)
	parse(0, 0, '""a""', isbad = 1)
	parse(0, 0, r'\"a\"', isbad = 1)

	parse(0, None, isbad = 1, add_quotes = 0)
	parse(0, True, isbad = 1, add_quotes = 0)
	parse(0, False, isbad = 1, add_quotes = 0)
	parse(0, 'a', isbad = 1, add_quotes = 0)

	parse(0, 0, 'null', isbad = 1, add_quotes = 0)
	parse(0, 0, 'true', isbad = 1, add_quotes = 0)
	parse(0, 0, 'false', isbad = 1, add_quotes = 0)
	parse(0, 0, 'a', isbad = 1, add_quotes = 0)
	parse(0, 0, '"a"', isbad = 1, add_quotes = 0)
	parse(0, 0, '""a""', isbad = 1, add_quotes = 0)
	parse(0, 0, r'\"a\"', isbad = 1, add_quotes = 0)

	parse(0, 0, isbad = 0)
	parse(0, 1, isbad = 0)
	parse(0, -1, isbad = 0)
	parse(0, 123, isbad = 0)
	parse(0, -123, isbad = 0)
	parse(4, 15, isbad = 0)
	parse(4, 16, isbad = 1)
	parse(4, -16, isbad = 0)
	parse(4, -17, isbad = 1)

	parse(0, '0', isbad = 0)
	parse(0, '+0', isbad = 1)
	parse(0, '-0', isbad = 0)
	parse(0, '1', isbad = 0)
	parse(0, '+1', isbad = 1)
	parse(0, '-1', isbad = 0)
	parse(0, '123', isbad = 0)
	parse(0, '+123', isbad = 1)
	parse(0, '-123', isbad = 0)
	parse(4, '15', isbad = 0)
	parse(4, '+15', isbad = 1)
	parse(4, '16', isbad = 1)
	parse(4, '+16', isbad = 1)
	parse(4, '-16', isbad = 0)
	parse(4, '-17', isbad = 1)

	parse(0, '00', isbad = 0)
	parse(0, '+00', isbad = 1)
	parse(0, '-00', isbad = 0)
	parse(0, '01', isbad = 0)
	parse(0, '+01', isbad = 1)
	parse(0, '-01', isbad = 0)
	parse(0, '0123', isbad = 0)
	parse(0, '+0123', isbad = 1)
	parse(0, '-0123', isbad = 0)
	parse(4, '015', isbad = 0)
	parse(4, '+015', isbad = 1)
	parse(4, '016', isbad = 1)
	parse(4, '+016', isbad = 1)
	parse(4, '-016', isbad = 0)
	parse(4, '-017', isbad = 1)

	parse(0, 0, '0x', isbad = 1)
	parse(0, 0, '0x0', isbad = 0)
	parse(0, 1, '0x1', isbad = 0)
	parse(0, 1, '0x01', isbad = 0)

	parse(0, 0, '+0x', isbad = 1)
	parse(0, 0, '+0x0', isbad = 1)
	parse(0, 1, '+0x1', isbad = 1)
	parse(0, 1, '+0x01', isbad = 1)

	parse(0, 0, '-0x', isbad = 1)
	parse(0, 0, '-0x0', isbad = 0)
	parse(0, -1, '-0x1', isbad = 0)
	parse(0, -1, '-0x01', isbad = 0)

	parse(0, 0, '0x.', isbad = 1)
	parse(0, 0, '0x0.', isbad = 1)
	parse(0, 1, '0x1.', isbad = 1)
	parse(0, 1, '0x01.', isbad = 1)

	parse(0, 0, '0x.0', isbad = 1)
	parse(0, 0, '0x0.0', isbad = 1)
	parse(0, 1, '0x1.0', isbad = 1)
	parse(0, 1, '0x01.0', isbad = 1)

	parse(0, 123., isbad = 0)
	parse(0, 123.0, isbad = 0)
	parse(0, 123.5, isbad = 1)

	parse(0, -123., isbad = 0)
	parse(0, -123.0, isbad = 0)
	parse(0, -123.5, isbad = 1)

	parse(0, '.123', isbad = 1)
	parse(0, '0.123', isbad = 1)
	parse(0, '123.', isbad = 0)
	parse(0, '123.0', isbad = 0)
	parse(0, '123.00', isbad = 0)

	parse(0, '+.123', isbad = 1)
	parse(0, '+0.123', isbad = 1)
	parse(0, '+123.', isbad = 1)
	parse(0, '+123.0', isbad = 1)
	parse(0, '+123.00', isbad = 1)

	parse(0, '-.123', isbad = 1)
	parse(0, '-0.123', isbad = 1)
	parse(0, '-123.', isbad = 0)
	parse(0, '-123.0', isbad = 0)
	parse(0, '-123.00', isbad = 0)

	parse(0, 123E0, isbad = 0)
	parse(0, 123.E0, isbad = 0)
	parse(0, 123.0E0, isbad = 0)
	parse(0, 123.00E0, isbad = 0)

	parse(0, 123E+0, isbad = 0)
	parse(0, 123.E+0, isbad = 0)
	parse(0, 123.0E+0, isbad = 0)
	parse(0, 123.00E+0, isbad = 0)

	parse(0, 123E-0, isbad = 0)
	parse(0, 123.E-0, isbad = 0)
	parse(0, 123.0E-0, isbad = 0)
	parse(0, 123.00E-0, isbad = 0)

	parse(0, 123E1, isbad = 0)
	parse(0, 123.E1, isbad = 0)
	parse(0, 123.0E1, isbad = 0)
	parse(0, 123.00E1, isbad = 0)

	parse(0, 123E+1, isbad = 0)
	parse(0, 123.E+1, isbad = 0)
	parse(0, 123.0E+1, isbad = 0)
	parse(0, 123.00E+1, isbad = 0)

	parse(0, 123E-1, isbad = 1)
	parse(0, 123.E-1, isbad = 1)
	parse(0, 123.0E-1, isbad = 1)
	parse(0, 123.00E-1, isbad = 1)

	parse(0, 1230E-1, isbad = 0)
	parse(0, 1230.E-1, isbad = 0)
	parse(0, 1230.0E-1, isbad = 0)
	parse(0, 1230.00E-1, isbad = 0)

	parse(0, '123E', isbad = 1)
	parse(0, '123.E', isbad = 1)
	parse(0, '123.0E', isbad = 1)
	parse(0, '123.00E', isbad = 1)

	parse(0, '123E+', isbad = 1)
	parse(0, '123.E+', isbad = 1)
	parse(0, '123.0E+', isbad = 1)
	parse(0, '123.00E+', isbad = 1)

	parse(0, '123E-', isbad = 1)
	parse(0, '123.E-', isbad = 1)
	parse(0, '123.0E-', isbad = 1)
	parse(0, '123.00E-', isbad = 1)

	parse(0, '123E0', isbad = 0)
	parse(0, '123.E0', isbad = 0)
	parse(0, '123.0E0', isbad = 0)
	parse(0, '123.00E0', isbad = 0)

	parse(0, '123E+0', isbad = 0)
	parse(0, '123.E+0', isbad = 0)
	parse(0, '123.0E+0', isbad = 0)
	parse(0, '123.00E+0', isbad = 0)

	parse(0, '123E-0', isbad = 0)
	parse(0, '123.E-0', isbad = 0)
	parse(0, '123.0E-0', isbad = 0)
	parse(0, '123.00E-0', isbad = 0)

	parse(0, '123E01', isbad = 0)
	parse(0, '123.E01', isbad = 0)
	parse(0, '123.0E01', isbad = 0)
	parse(0, '123.00E01', isbad = 0)

	parse(0, '123E+01', isbad = 0)
	parse(0, '123.E+01', isbad = 0)
	parse(0, '123.0E+01', isbad = 0)
	parse(0, '123.00E+01', isbad = 0)

	parse(0, '123E-01', isbad = 1)
	parse(0, '123.E-01', isbad = 1)
	parse(0, '123.0E-01', isbad = 1)
	parse(0, '123.00E-01', isbad = 1)

	parse(0, '123E1', isbad = 0)
	parse(0, '123.E1', isbad = 0)
	parse(0, '123.0E1', isbad = 0)
	parse(0, '123.00E1', isbad = 0)

	parse(0, '123E+1', isbad = 0)
	parse(0, '123.E+1', isbad = 0)
	parse(0, '123.0E+1', isbad = 0)
	parse(0, '123.00E+1', isbad = 0)

	parse(0, '123E-1', isbad = 1)
	parse(0, '123.E-1', isbad = 1)
	parse(0, '123.0E-1', isbad = 1)
	parse(0, '123.00E-1', isbad = 1)

	parse(0, '1230E-1', isbad = 0)
	parse(0, '1230.E-1', isbad = 0)
	parse(0, '1230.0E-1', isbad = 0)
	parse(0, '1230.00E-1', isbad = 0)

	parse(0, '123E1.', isbad = 1)
	parse(0, '123.E1.', isbad = 1)
	parse(0, '123.0E1.', isbad = 1)
	parse(0, '123.00E1.', isbad = 1)

	parse(0, '123E1.2', isbad = 1)
	parse(0, '123.E1.2', isbad = 1)
	parse(0, '123.0E1.2', isbad = 1)
	parse(0, '123.00E1.2', isbad = 1)

	val = PRIMEMOD-1
	parse(0, val, isbad = 0)
	parse(0, val, hex(val), isbad = 0)
	parse(0, -val, isbad = 0)
	parse(0, -val, hex(-val), isbad = 0)

	val = PRIMEMOD
	parse(0, val, isbad = 1)
	parse(0, val, hex(val), isbad = 1)
	parse(0, -val, isbad = 1)
	parse(0, -val, hex(-val), isbad = 1)

	val = PRIMEMOD-1
	parse(0, val, str(val) + '.', isbad = 0)
	parse(0, val, str(val) + '.0', isbad = 1)
	parse(0, -val, str(-val) + '.', isbad = 0)
	parse(0, -val, str(-val) + '.0', isbad = 1)

	val = (1<<255)-1
	parse(255, val, isbad = 0)
	parse(255, val, hex(val), isbad = 0)
	parse(255, -val, isbad = 0)
	parse(255, -val, hex(-val), isbad = 0)

	val = (1<<255)
	parse(255, val, isbad = 1)
	parse(255, val, hex(val), isbad = 1)
	parse(255, -val, isbad = 0)
	parse(255, -val, hex(-val), isbad = 0)

	val = (1<<256)+1
	parse(255, val, isbad = 1)
	parse(255, val, hex(val), isbad = 1)
	parse(255, -val, isbad = 1)
	parse(255, -val, hex(-val), isbad = 1)

	val = (1<<256)-1
	parse(256, val, isbad = 0)
	parse(256, val, hex(val), isbad = 0)
	parse(256, -val, isbad = 0)
	parse(256, -val, hex(-val), isbad = 0)

	val = 1<<256
	parse(256, val, isbad = 1)
	parse(256, val, hex(val), isbad = 1)
	parse(256, -val, isbad = 1)
	parse(256, -val, hex(-val), isbad = 1)

	val = (1<<256)-1
	sval = str(val)
	parse(256, val, sval + '.', isbad = 0)
	parse(256, val, sval + '.0', isbad = 1)
	parse(256, -val, '-' + sval + '.', isbad = 0)
	parse(256, -val, '-' + sval + '.0', isbad = 1)

	sval = sval[:-1] + '.' + sval[-1] + 'e1'
	parse(256, val, sval, isbad = 0)
	parse(255, val, sval, isbad = 1)

	val = (1<<256)
	sval = str(val)
	parse(256, val, sval + '.', isbad = 1)
	parse(256, val, sval + '.0', isbad = 1)

	sval = sval[:-1] + '.' + sval[-1] + 'e1'
	parse(256, val, sval, isbad = 1)

t0 = time.time()

if 1:
	print '*** The following three tests should fail:'

	try:
		parse(0, 0, isbad = 1)	# should throw Exception
		exfail = 1
	except:
		exfail = 0
	if exfail:
		print 'Exception test failed'
		raise Exception

	try:
		parse(1, 2, isbad = 0)	# should throw Exception
		exfail = 1
	except:
		exfail = 0
	if exfail:
		print 'Exception test failed'
		raise Exception

	try:
		parse(1, 1, '0', isbad = 0)	# should throw Exception
		exfail = 1
	except:
		exfail = 0
	if exfail:
		print 'Exception test failed'
		raise Exception

	print '-----------------------------------------'

if 1:
	test_static()

if 1:
	test_fracs()

if 1:
	print
	print '### random test start time =', int(time.time() - t0 + 0.5)
	print

	while True:
		test_random()

		seed = random.getrandbits(128)
		print 'random seed', seed
		random.seed(seed)
