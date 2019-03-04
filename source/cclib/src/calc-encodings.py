 #
 # CredaCash (TM) cryptocurrency and blockchain
 #
 # Copyright (C) 2015-2019 Creda Software, Inc.
 #
 # calc-encodings.py
#

# make base26, base58, base64 and base64url encoding tables

print 'const unsigned char base64[64] ='
outs = '{'
conv = {}
c = 'A'
for i in range(64):
	conv[c] = i
	outs += "'" + c + "'"
	if i < 63:
		outs += ','
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
print outs + '};'
print

print 'const unsigned char base64int[' + str(ord('z') - ord('+') + 3) + '] ='
outs = '{'
outs += "'+',"	# first entry is lowest index
outs += "'z',"	# second entry is highest index
for i in range(ord('+'), ord('z') + 1):
	c = chr(i)
	if c in conv:
		outs += str(conv[c])
	else:
		outs += '255';
	if i < ord('z'):
		outs += ','
print outs + '};'
print

print 'const unsigned char base64url[64] ='
outs = '{'
conv = {}
c = 'A'
for i in range(64):
	conv[c] = i
	outs += "'" + c + "'"
	if i < 63:
		outs += ','
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
print outs + '};'
print

print 'const unsigned char base64urlint[' + str(ord('z') - ord('-') + 3) + '] ='
outs = '{'
outs += "'-',"	# first entry is lowest index
outs += "'z',"	# second entry is highest index
for i in range(ord('-'), ord('z') + 1):
	c = chr(i)
	if c in conv:
		outs += str(conv[c])
	else:
		outs += '255';
	if i < ord('z'):
		outs += ','
print outs + '};'
print

print 'const unsigned char base58[58] ='
outs = '{'
conv = {}
c = 'A'
for i in range(58):
	conv[c] = i
	outs += "'" + c + "'"
	if i < 57:
		outs += ','
	if c == 'H':
		c = 'J'
	elif c == 'N':
		c = 'P'
	elif c == 'Z':
		c = 'a'
	elif c == 'k':
		c = 'm'
	elif c == 'z':
		c = '1'
	else:
		c = chr(ord(c) + 1)
print outs + '};'
print

print 'const unsigned char base58int[' + str(ord('z') - ord('1') + 3) + '] ='
outs = '{'
outs += "'1',"	# first entry is lowest index
outs += "'z',"	# second entry is highest index
for i in range(ord('1'), ord('z') + 1):
	c = chr(i)
	if c in conv:
		outs += str(conv[c])
	else:
		outs += '255';
	if i < ord('z'):
		outs += ','
print outs + '};'
print

print 'const unsigned char base26[26] ='
outs = '{'
conv = {}
c = 'a'
for i in range(26):
	conv[c] = i
	outs += "'" + c + "'"
	if i < 25:
		outs += ','
	c = chr(ord(c) + 1)
print outs + '};'
print

print 'const unsigned char base26int[' + str(ord('z') - ord('A') + 3) + '] ='
outs = '{'
outs += "'A',"	# first entry is lowest index
outs += "'z',"	# second entry is highest index
for i in range(ord('A'), ord('z') + 1):
	if i < ord('a'):
		c = chr(i + ord('a') - ord('A'))
	else:
		c = chr(i)
	if c in conv:
		outs += str(conv[c])
	else:
		outs += '255';
	if i < ord('z'):
		outs += ','
print outs + '};'
print
