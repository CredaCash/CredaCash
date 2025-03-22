'''
CredaCash(TM) Exchange Common Functions

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors

'''

CREDACASH = 'CredaCash'
FOREIGN = 'Foreign'
BITCOIND = 'Bitcoind'
ELECTRUM = 'Electrum'
ELECTRON = 'Electron Cash'
MINING = 'Mining'

TIMEOUT = 'TIMEOUT'
TEST_ROUNDING = False

import sys
import os
import traceback
import requests
import json
import random
import time
import math
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

def ensure_one_instance(id):
	#print 'script instance id = %s' % id

	if os.name == 'nt':
		import ctypes
		windll = ctypes.cdll.LoadLibrary('Kernel32.dll')
		wincmd = windll.CreateMutexA
		wincmd.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_char_p]
		wincmd(None, 0, id.encode())
		rc = windll.GetLastError()
		#pprint.pprint(rc)
		if rc == 0:
			already_running = False
		elif rc == 183:
			already_running = True
		else:
			print('\nERROR: unrecognized GetLastError return code', rc)
			exit()
	else:
		import fcntl
		global flock
		flock = open('/tmp/' + id, 'wb')
		try:
			fcntl.lockf(flock, fcntl.LOCK_EX | fcntl.LOCK_NB)
			already_running = False
		except (IOError, OSError):
			already_running = True

	if already_running:
		print('\nAn instance of this script appears to be already running.')
		print('To minimize the chance of double payments, only one instance is allowed.\n')
		exit()

def do_rpc(s, c, method, params=(), timeout=600, return_timeout=False, needs_wallet=False, expect_json=True):
	if needs_wallet and c.type == ELECTRUM and c.currency == 'btc':
		if len(params) == 0:
			params = {}
		if isinstance(params, dict):
			params['wallet'] = c.walpath
		else:
			print('do_rpc %s usage error: dict params required with needs_wallet=True')

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

	#print('performing rpc port %d request %s\n' % (c.port, req), end='')
	try:
		r = s.post('http://127.0.0.1:%d%s' % (c.port, c.url), auth=(c.user, c.pwd), data=req, timeout=(20,timeout))
	except Exception as e:
		if return_timeout and isinstance(e, requests.ReadTimeout):
			return TIMEOUT
		print('%d Warning: rpc port %d exception %s "%s" req %s\n' % (time.time(), c.port, type(e), e, req), end='')
		#traceback.print_exc()
		return None
	#print('rpc status code %d response: %s\n' % (r.status_code, r.text), end='')
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
			if not expect_json:
				return r.text
			print('%d Warning: rpc port %d json load failed "%s" req %s\n' % (time.time(), c.port, r.text.encode('ascii', 'backslashreplace'), req), end='')
		else:
			print('%d Warning: rpc port %d no text returned for req %s\n' % (time.time(), c.port, req), end='')
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

def parse_config(conf_file, include_mining=False):
	global Creda, Foreign, Mining
	conf_fp = open(conf_file)
	c = json.load(conf_fp)
	#print c
	Creda = Config(c, conf_file, CREDACASH)
	Foreign = ForeignRPC(c, conf_file, FOREIGN)
	if include_mining:
		Mining = Config(c, conf_file, MINING)
	elif MINING in c:
		del c[MINING]
	Config.del_excluded_keys(c)
	if len(c) > 0:
		print('ERROR: unrecognized elements in configuration file %s:' % conf_file, c)
		exit()

class ForeignRPC(Config):

	def IsBitcoind(self):
		return self.type == BITCOIND

	def IsElectrum(self):
		return self.type == ELECTRUM

	def InvalidTypeError(self):
		print('ERROR: invalid foreign rpc type', self.type)
		return None

	def IsTestnetAddr(self, addr):
		if self.currency == 'btc':
			return addr.startswith('t') \
				or addr.startswith('m') \
				or addr.startswith('n') \
				or addr.startswith('2') \
				or addr.startswith('tb1') \
				or addr.startswith('bcrt1')
		elif self.currency == 'bch':
			return addr.startswith('bchtest:') \
				or addr.startswith('bchreg:') \
				or (not ':' in addr)	# prefixes may be stripped from BCH addresses
		else:
			return False

	def GetNewAddress(self, s):
		if self.IsBitcoind():
			return do_rpc(s, self, 'getnewaddress')
		elif self.IsElectrum():
			if self.currency == 'bch':
				r = do_rpc(s, self, 'addrequest', dict(amount=0, force=True))
			else:
				r = do_rpc(s, self, 'add_request', dict(amount=0, force=True), needs_wallet=True)
			#pprint.pprint(r)
			if 'address' in (r or ()):
				return r['address']
			else:
				print('add_request missing address\n', end='')
				pprint.pprint(r)
			return None
		else:
			return self.InvalidTypeError()

	def Send(self, s, addr, amt):
		if self.IsBitcoind():
			return do_rpc(s, self, 'sendtoaddress', (addr, amt))
		elif self.IsElectrum():
			params = {}
			params['amount'] = amt + 1e-8 # Electrum seems to have a rounding problem, so pay 1 extra satoshi just to make sure
			params['destination'] = addr
			if len(self.walpwd):
				params['password'] = self.walpwd
			tx = do_rpc(s, self, 'payto', params, needs_wallet=True)
			#pprint.pprint(tx)
			if not tx:
				return None
			if 'hex' in tx:
				tx = tx['hex']
			txid = do_rpc(s, self, 'broadcast', (tx, ))
			#pprint.pprint(txid)
			#print type(txid)
			if isinstance(txid, (list, tuple)):
				if len(txid) < 2 or txid[0] is not True:
					print('error attempting to make payment: unexpected result ')
					pprint.pprint(txid)
					return None
				txid = str(txid[1])
			if not isinstance(txid, (str, unicode)):
				print('error attempting to make payment: value for txid is not a string')
				pprint.pprint(txid)
				return None
			return str(txid)
		else:
			return self.InvalidTypeError()

	# finds transaction sent to an address
	def FindPayment(self, s, addr, mintime, maxconf):
		#print int(time.time()),'FindPayment', addr, mintime, maxconf
		if self.IsBitcoind():
			offset = 0
			count = 20
			while True:
				r = do_rpc(s, self, 'listtransactions', ('*', count, offset))
				if r is None:
					print('error retrieving payment info for foreign address', addr)
					return None
				if not len(r):
					#print 'FindPayment found no txs at', offset
					return None
				offset += count - 4
				for e in r:
					#pprint.pprint(e)
					if e['time'] < mintime:
						#print 'FindPayment past mintime at', offset
						return None
					if e['category'] != 'send':
						continue
					if e['abandoned']:
						continue
					if e['address'] == addr:
						return e['txid']
		elif self.IsElectrum():
			r = do_rpc(s, self, 'getinfo')
			if not 'blockchain_height' in (r or ()):
				print('blockchain_height missing from getinfo')
				print(r)
				return None
			height = r['blockchain_height']
			r = do_rpc(s, self, 'getaddresshistory', (addr, ))
			#print 'height', height
			#pprint.pprint(r)
			for t in (r or ()):
				if not 'tx_hash' in t or not 'height' in t:
					print('no tx_hash or height in getaddresshistory')
					print(t)
					continue
				if t['height'] == 0 or height - t['height'] < maxconf:
					return str(t['tx_hash'])
		else:
			return self.InvalidTypeError()

	# retrieves transaction 'confirmations' and 'blockheight'
	def GetTx(self, s, num, addr, txid, conf):
		#print int(time.time()),'GetTx', addr, num, txid
		if self.IsBitcoind():
			r = do_rpc(s, self, 'gettransaction', (txid, ))
			if r is None:
				print('error retrieving transaction info for payment of match', num, 'txid', txid)
				return None
			#pprint.pprint(r)
			if not 'confirmations' in r:
				print('no confirmations for payment of match', num, 'txid', txid)
				print(r)
				return None
			if r['confirmations'] < conf:
				return r
			if 'blockheight' in r:
				return r
			if not 'blockhash' in r:
				print('no blockheight or blockhash for payment of match', num, 'txid', txid)
				print(r)
				return None
			e = do_rpc(s, self, 'getblockheader', (r['blockhash'], ))
			#pprint.pprint(e)
			if not 'height' in (e or ()):
				print('no height for payment of match', num, 'txid', txid, 'blockhash', r['blockhash'])
				print(e)
				return None
			r['blockheight'] = e['height']
			return r
		elif self.IsElectrum():
			r = do_rpc(s, self, 'getaddresshistory', (addr, ))
			#pprint.pprint(r)
			c = None
			for t in (r or ()):
				if not 'tx_hash' in t or not 'height' in t:
					print('no tx_hash or height for payment of match', num, 'address', addr, 'txid', txid)
					print(t)
					return None
				if txid == str(t['tx_hash']) and t['height'] and (not c or c['height'] > t['height']):
					c = t
			if c is None:
				return None
			r = do_rpc(s, self, 'getinfo')
			if not 'blockchain_height' in (r or ()):
				print('no blockchain_height for payment of match', num, 'txid', txid)
				print(r)
				return None
			c['blockheight'] = c['height']
			c['confirmations'] = r['blockchain_height'] - c['blockheight'] + 1
			return c
		else:
			return self.InvalidTypeError()

	def AbandonTx(self, s, txid):
		if self.IsBitcoind():
			return do_rpc(s, self, 'abandontransaction', (txid, ))
		elif self.IsElectrum():
			return False #do_rpc(s, self, 'removelocaltx', (txid, ))
		else:
			return self.InvalidTypeError()

def get_mining_info(s):
	try:
		r = do_rpc(s, Creda, 'cc.exchange_query_mining_info')
		return r['exchange-mining-info-query-results']
	except:
		return None

def get_balance(s, c):
	r = do_rpc(s, c, 'getbalance')
	try:
		return float(r)
	except:
		return None

def get_cc_pledged(s, c, currencies=None):
	cc_pledged = 0
	for cur in (currencies or ('btc', 'bch')):
		pending = do_rpc(s, Creda, 'cc.exchange_requests_pending_totals', (cur, ))
		if not pending:
			return None
		cc_pledged += pending['buy-request-pending-totals']['pledge-amount']
	return cc_pledged

def submit_xreq(s, type, amount, rate, expiration=0):
	if type[0] == 'b':
		foreign_address = ''
	else:
		foreign_address = Foreign.GetNewAddress(s)
		if not foreign_address:
			print('Error obtaining a BCH address\n', end='')
			return None
		if not foreign_address.startswith('bitcoincash:') and not foreign_address.startswith('bch'):
			print('Error: unrecognized BCH address %s\n' % foreign_address, end='')
			return None
	print('Submitting crosschain %s request time %d amount %g rate %g\n' % (type, time.time(), amount, rate), end='')
	txid = do_rpc(s, Creda, 'cc.crosschain_request_create', ('', 's'+type[0], amount, amount, rate, 0, 'bch', foreign_address, expiration))
	if txid:
		return txid
	else:
		print('Crosschain %s request failed time %d amount %g rate %g\n' % (type, time.time(), amount, rate), end='')
		return None

rounding_test = {}

def round_to_power(amount, rounding):
	if not amount:
		return 0

	# round amount to 1, 2, 3, 5 or 7 multiplied by a power of 10
	#print(amount, rounding)
	expon = int(math.log10(amount))
	mant = amount / math.pow(10, expon) + rounding

	while mant < 1:
		mant *= 10
		expon -= 1

	while mant > 10:
		mant /= 10
		expon += 1

	if mant > 3 and mant < 4:
		mant = 3
	elif mant >= 4 and mant < 6:
		mant = 5
	elif mant >= 6 and mant < 8.5:
		mant = 7
	elif mant >= 8.5:
		mant = 10
	else:
		mant = int(mant + 0.5)
		if mant == 0:
			mant = 1
			expon -= 1

	adj_amount = mant * math.pow(10, expon)

	if adj_amount > 0.9:
		adj_amount = int(adj_amount + 0.5)

	#print('%g\t%g' % (adj_amount, amount + 0.5))

	if rounding == 0 and TEST_ROUNDING:
		global rounding_test
		if not adj_amount in rounding_test:
			rounding_test[adj_amount] = [amount, amount]
		elif amount < rounding_test[adj_amount][0]:
			rounding_test[adj_amount][0] = amount
		elif amount > rounding_test[adj_amount][1]:
			rounding_test[adj_amount][1] = amount

	#print('round_to_power %g %g %g\n' % (rounding, amount, adj_amount),)

	return adj_amount

if 0:
	TEST_ROUNDING = True
	for i in range (-1, 2):
		for amount in (0, 1, 2, 3, 5, 7):
			amount *= 10**i
			round_to_power(amount, 0)
	for i in range(10000000):
		amount = 1 + 1300 * random.random()
		amount /= 10
		round_to_power(amount, 0)
	pprint.pprint(rounding_test)
	exit()


def check_if_testnet(s):
	global IsTestnet
	print('CredaCash rpc port:', Creda.port)
	r = do_rpc(s, Creda, 'getinfo')
	#print r
	if not r:
		print('ERROR querying CredaCash wallet at port', Creda.port)
		exit()
	try:
		if r['connections'] < 1:
			print('ERROR: ccnode is not yet synced to the CredaCash network')
			exit()
		IsTestnet = r['testnet']
	except KeyError:
		print('ERROR: unable to determine if the CredaCash wallet is connected to a testnet')
		print(r)
		exit()
	print('IsTestnet', IsTestnet)

def init_bitcoind(s):
	if Foreign.walname is not None:
		walinfo = do_rpc(s, Foreign, 'getwalletinfo')
		if walinfo and 'walletname' in walinfo and walinfo['walletname'] == Foreign.walname:
			print(FOREIGN, 'wallet', Foreign.walname, 'already loaded')
		else:
			print('Loading', FOREIGN.lower(), 'wallet', Foreign.walname)
			do_rpc(s, Foreign, 'loadwallet', (Foreign.walname, ))

stop_electrum = False

def init_electrum(s):
	# Determine Electrum exe name
	global Foreign, stop_electrum
	Foreign.exefile = os.path.expandvars(os.path.expanduser(Foreign.exefile))
	if not os.path.isfile(Foreign.exefile):
		print('ERROR:', ELECTRUM, 'executable file not found: "%s"' % Foreign.exefile)
		exit()
	print(ELECTRUM, 'executable:', Foreign.exefile)
	Foreign.launch = [Foreign.exefile]
	Foreign.launch.extend(Foreign.exeopts.split())
	#pprint.pprint(Foreign.launch)

	if Foreign.currency == 'btc':
		query = Foreign.launch + ['--offline']
	else:
		query = Foreign.launch

	# Set rpcsock = tcp if the electrum default is unix
	import socket
	import subprocess
	if hasattr(socket, 'AF_UNIX'):
		print('setting', ELECTRUM, 'rpc mode...')
		subprocess.Popen(query + ['setconfig', 'rpcsock', 'tcp']).wait()

	# Launch Electrum in GUI or daemon mode
	if Foreign.exemode == 'daemon':
		if not hasattr(os,'fork'):
			print('ERROR: daemon not supported on systems that lack os.fork()')
			exit()
		print('starting', ELECTRUM, 'daemon...')
		stop_electrum = True
		if Foreign.currency == 'bch':
			subprocess.Popen(Foreign.launch + ['daemon', 'start'])
		else:
			subprocess.Popen(Foreign.launch + ['daemon', '-d'])
	else:
		print('starting %s...' % ELECTRUM)
		subprocess.Popen(Foreign.launch)
	time.sleep(10)

	# Determine Electrum data path
	# TODO: better error checking/reporting here:
	print('querying %s...' % ELECTRUM)
	r = subprocess.check_output(Foreign.launch + ['getinfo'])
	#pprint.pprint(r)
	try:
		j = json.loads(makestring(r))
		p = j['path']
	except:
		p = ''
	if not len(p):
		print('ERROR: unable to obtain', ELECTRUM, 'data path')
		raise SystemExit()
	print(ELECTRUM, 'data path: "%s"' % p)

	# Determine Electrum RPC port, username and password
	try:
		f = open(p + '/daemon')
		l = f.read()
		f.close()
	except:
		print('ERROR: unable to read', ELECTRUM, 'daemon file')
		raise SystemExit()
	try:
		c = l.split(')', 1)[0].strip('(').split(',')
		Foreign.rpcsock = c[0].strip().strip("'")
		Foreign.port = c[-1].strip().strip("'")
		#if Foreign.port.isdigit():
		Foreign.port = int(Foreign.port)
		#print Foreign.rpcsock, Foreign.port
	except:
		print('ERROR: unable to parse', ELECTRUM, 'port from string "%s"' % l)
		if Foreign.rpcsock == 'unix':
			print('Unix domain sockets are not supported')
			print('Try completely closing', ELECTRUM, 'and re-running this script')
		raise SystemExit()
	print(ELECTRUM, 'rpc port:', Foreign.port)
	Foreign.user = str(subprocess.check_output(Foreign.launch + ['getconfig', 'rpcuser']).decode()).strip()
	print(ELECTRUM, 'rpc user:', Foreign.user)
	Foreign.pwd = str(subprocess.check_output(Foreign.launch + ['getconfig', 'rpcpassword']).decode()).strip()
	if len(Foreign.pwd):
		print(ELECTRUM, 'rpc password: %c*****' % Foreign.pwd[0])
		#print Foreign.pwd

	# Check if Electrum RPC works
	r = do_rpc(s, Foreign, 'version')
	if not r:
		print('ERROR: unable to get', ELECTRUM, 'version')
		raise SystemExit()
	print(ELECTRUM, 'version', r)

	# Determine Electrum wallet path and password
	walp = {}
	if not len(Foreign.walname):
		Foreign.walname = 'default_wallet'
	walfile = Foreign.waldir
	if not len(walfile):
		walfile = p + '/wallets/'
	walfile += Foreign.walname
	walfile = walfile.replace('\\\\', '/')
	walfile = walfile.replace('\\', '/')
	walp['wallet_path'] = walfile
	Foreign.walpath = walfile
	print(ELECTRUM, 'wallet file: "%s"' % walfile)
	if len(Foreign.walpwd):
		walp['password'] = Foreign.walpwd
		print(ELECTRUM, 'wallet password: %c*****' % Foreign.walpwd[0])
		#print walp['password']

	# Load Electrum wallet
	print('loading', ELECTRUM, 'wallet...')
	if Foreign.currency != 'bch':
		#pprint.pprint(walp)
		r = do_rpc(s, Foreign, 'load_wallet', walp)
		if not r:
			print('ERROR: unable to load', ELECTRUM, 'wallet')
			raise SystemExit()
	else:
		#pprint.pprint(Foreign.launch)
		sp = subprocess.Popen(Foreign.launch + ['--wallet', Foreign.walpath, '--walletpassword', '-', 'daemon', 'load_wallet'], bufsize=4000, stdin=subprocess.PIPE, stdout=subprocess.PIPE) # must have stdin and stdout for wallet password injection to work
		sp.stdin.write(Foreign.walpwd.encode())
		sp.stdin.close()
		for i in range(60):
			time.sleep(1)
			if sp.poll() is not None:
				break
		if sp.poll() is None:
			sp.kill()
		rc = sp.wait()
		if rc:
			print('ERROR:', ELECTRUM, 'returned status code', rc)
			raise SystemExit()
