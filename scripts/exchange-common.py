'''
CredaCash(TM) Exchange Common Functions

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors

'''

import sys
import os
import traceback
import requests
import json
import random
import time
import pprint

sysver = sys.version.split('.')
#pprint.pprint(sysver)
if int(sysver[0]) < 2 or (int(sysver[0]) == 2 and int(sysver[1]) < 7) or (int(sysver[0]) == 3 and int(sysver[1]) < 8) or int(sysver[0]) > 3:
	print('ERROR: This script requires Python v2.7, or Python v3.8 or higher.)')
	exit()

if type(u'').__name__ != 'unicode':
	unicode = str

def makestring(r):
	if not isinstance(r, (str, unicode)) and hasattr(r, 'decode'):
		return str(r.decode())
	return r

CREDACASH = 'CredaCash'
FOREIGN = 'Foreign'
BITCOIND = 'Bitcoind'
ELECTRUM = 'Electrum'
ELECTRON = 'Electron Cash'
MINING = 'Mining'

TIMEOUT = 'TIMEOUT'

def do_rpc(s, c, method, params=(), timeout=600, return_timeout=False, needs_wallet=False):
	if needs_wallet and c.type == ELECTRUM and c.currency == 'btc':
		if len(params) == 0:
			params = {}
		if isinstance(params, dict):
			params['wallet'] = c.walpath

	req = '{"id":0,"method":"' + method + '","params":'
	if isinstance(params, dict):
		params = json.dumps(params)
		#print params
		req += params
	else:
		req += '['
		for i in range(len(params)):
			if i: req += ','
			p = params[i]
			if isinstance(p, (str, unicode)) and p != 'true' and p != 'false':
				req += '"' + (params[i]) + '"'
			else:
				req += str(params[i])
		req += ']'
	req += '}\n'

	#print 'performing rpc port %d request %s\n' % (c.port, req),
	try:
		r = s.post('http://127.0.0.1:%d%s' % (c.port, c.url), auth=(c.user, c.pwd), data=req, timeout=(20,timeout))
	except Exception as e:
		if return_timeout and isinstance(e, requests.ReadTimeout):
			return TIMEOUT
		print('%d Warning: rpc port %d exception %s "%s" req %s\n' % (time.time(), c.port, type(e), e, req), end='')
		#traceback.print_exc()
		return None
	#print 'rpc status code %d response: %s\n', (r.status_code, r.text),
	if r.status_code != 200: # and method not in ('sendtoaddress', 'payto', 'broadcast'):
		print('%d Warning: rpc port %d status code %d req %s\n' % (time.time(), c.port, r.status_code, req), end='')

	try:
		j = json.loads(makestring(r.text))
		if 'result' in (j or ()):
			rv = j['result']
		else:
			rv = None
		if rv is None:
			if method == 'load_wallet':
				return True
			print('%d Warning: rpc port %d result "%s" req %s\n' % (time.time(), c.port, j['error']['message'], req), end='')
		return rv
	except:
		#pprint.pprint(r)
		if hasattr(r, 'text'):
			print('%d Warning: rpc port %d json load failed "%s" req %s\n' % (time.time(), c.port, r.text.encode('ascii', 'backslashreplace'), req), end='')
		else:
			print('%d Warning: rpc port % d no text returned for req %s\n' % (time.time(), c.port, req), end='')
		return None

class Config:
	def __init__(self, c, conf_file, s):
		try:
			c = c.pop(s)
		except KeyError:
			print('ERROR: missing required section "%s" in config file %s' % (s, conf_file))
			exit()

		self.url				= ''
		if s == FOREIGN:
			self.type			= self.getkey(c, conf_file, s, 'type', validvals=[BITCOIND, ELECTRUM])
			self.currency		= self.getkey(c, conf_file, s, 'currency')
			self.walname		= self.getkey(c, conf_file, s, 'wallet-name', defval='')
			self.blocktime		= self.getkey(c, conf_file, s, 'blocktime')
		else:
			self.type			= s
		if self.type == MINING:
			self.min_exchg_rate	= self.getkey(c, conf_file, s, 'minimum exchange rate', False, 0)
			self.max_exchg_rate	= self.getkey(c, conf_file, s, 'maximum exchange rate', False, 0)
			self.reqs_per_hr	= self.getkey(c, conf_file, s, 'exchange requests per hour', False, 120)
			self.req_min_amt	= self.getkey(c, conf_file, s, 'exchange request minimum amount', False, 100)
			self.req_max_amt	= self.getkey(c, conf_file, s, 'exchange request maximum amount', False, 1000)
			self.min_cc_bal		= self.getkey(c, conf_file, s, 'minimum CredaCash balance', False, 10)
			self.min_bch_bal	= self.getkey(c, conf_file, s, 'minimum BCH balance', False, 0.001)
			self.skip_pay_test	= self.getkey(c, conf_file, s, 'skip autopay test', False, False)
		if self.type == CREDACASH or self.type == BITCOIND:
			self.port			= self.getkey(c, conf_file, s, 'port')
			self.user			= self.getkey(c, conf_file, s, 'user')
			self.pwd			= self.getkey(c, conf_file, s, 'password')
		if self.type == ELECTRUM:
			self.exefile		= self.getkey(c, conf_file, s, 'exe-file')
			self.exemode		= self.getkey(c, conf_file, s, 'exe-mode', defval='gui', validvals=['gui', 'daemon'])
			self.exeopts		= self.getkey(c, conf_file, s, 'exe-options', defval='')
			self.waldir			= self.getkey(c, conf_file, s, 'wallet-dir', defval='')
			self.walpwd			= self.getkey(c, conf_file, s, 'wallet-password', defval='')
		if self.type == BITCOIND:
			self.url			= '/wallet/'
			if self.walname:
				self.url += requests.utils.quote(self.walname, safe='')

		self.del_excluded_keys(c)
		if len(c) > 0:
			print('ERROR: unrecognized key in section "%s" of config file %s:' % (s, conf_file), c)
			exit()

	@staticmethod
	def getkey(c, conf_file, s, k, required=True, defval=None, validvals=()):
		try:
			val = c.pop(k)
			if not len(validvals):
				return val
			for v in validvals:
				if val == v:
					return val
			print('ERROR: key "%s" in section "%s" of config file %s must be one of these values:' % (k, s, conf_file), validvals)
			exit()
		except KeyError:
			if defval is not None:
				return defval
			if not required:
				return None
			else:
				print('ERROR: missing required key "%s" in section "%s" of config file %s' % (k, s, conf_file))
				exit()

	@staticmethod
	def del_excluded_keys(c):
		for k in list(c):
			if k.startswith('x-'):
				del c[k]

def parse_config(conf_file, include_mining=True, ForeignClass=Config):
	global Creda, Foreign, Mining
	conf_fp = open(conf_file)
	c = json.load(conf_fp)
	#print c
	Creda = Config(c, conf_file, CREDACASH)
	Foreign = ForeignClass(c, conf_file, FOREIGN)
	if include_mining:
		Mining = Config(c, conf_file, MINING)
	Config.del_excluded_keys(c)
	if len(c) > 0:
		print('ERROR: unrecognized elements in configuration file %s:' % conf_file, c)
		exit()
