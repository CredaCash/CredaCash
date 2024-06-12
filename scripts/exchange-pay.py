#!/usr/bin/env python2

'''
CredaCash(TM) Exchange Autopay

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors

The script may be used to complete purchases of CredaCash currency on the
CredaCash integrated peer-to-peer exchange. It is used in conjunction with a
CredaCash wallet and either a Bitcoin wallet (either the Bitcoin core wallet, or
the Electrum wallet), or a Bitcoin Cash wallet (either the Bitcoin Cash core
wallet, or the Electron Cash wallet).  It has been specifically tested with:

	bitcoin-24.1-win64 from https://bitcoincore.org
	bitcoin-24.1-aarch64-linux-gnu from https://bitcoincore.org
	electrum-4.1.5.exe from https://electrum.org

	bitcoin-cash-node-27.0.0-win64 from https://bitcoincashnode.org
	bitcoin-cash-node-27.0.0-aarch64-linux-gnu from https://bitcoincashnode.org
	Electron-Cash-4.3.1.exe from https://electroncash.org

This script performs the following operations:
- Monitors the CredaCash wallet for CredaCash buy requests that have matched sell requests.
- Determines the amount of bitcoin to pay the seller, and the payment deadline.
- Ensures there should be enough time for a bitcoin payment to be confirmed before the deadline.
- Pays the seller in bitcoin on the bitcoin blockchain.
- Monitors the bitcoin blockchain for payment confirmation.
- Submits a payment claim on the CredaCash blockchain.

Note: The main payment loop is in pay_foreign()

Note: This script is written for python v2.7 and requires the requests module.
The requests module for python v2.7 can be obtained as follows:
	$ wget https://bootstrap.pypa.io/pip/2.7/get-pip.py
	$ sudo python2 get-pip.py
	$ python2 -m pip install requests

'''

import sys
import os
import subprocess
import traceback
import socket
import ssl
import requests
import json
import random
import time
import pprint

if not sys.version.startswith('2.7.') or not ('GCC' in sys.version or '64 bit' in sys.version or 'AMD64' in sys.version):
	print 'ERROR: This script requires Python 2.7.x (64 bit version).'
	exit()

CREDACASH = 'CredaCash'
FOREIGN = 'Foreign'
BITCOIND = 'Bitcoind'
ELECTRUM = 'Electrum'
ELECTRON = 'Electron Cash'

def ensure_one_instance():
	id = 'ccap_' + Foreign.currency + '_' + str(Creda.port)
	#print 'script instance id = %s' % id

	if os.name == 'nt':
		import ctypes
		windll = ctypes.cdll.LoadLibrary('Kernel32.dll')
		wincmd = windll.CreateMutexA
		wincmd.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_char_p]
		wincmd(None, 0, id)
		rc = windll.GetLastError()
		#pprint.pprint(rc)
		if rc == 0:
			already_running = False
		elif rc == 183:
			already_running = True
		else:
			print '\nERROR: unrecognized GetLastError return code', rc
			exit()
	else:
		import fcntl
		lock = open('/tmp/' + id, 'wb')
		try:
			fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
			already_running = False
		except IOError:
			already_running = True

	if already_running:
		print '\nAn instance of this script appears to be already running.'
		print 'To minimize the chance of double payments, only one instance is allowed.\n'
		exit()

def do_rpc(s, c, method, params=(), needs_wallet=False):
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
			if isinstance(p, basestring) and p != 'true' and p != 'false':
				req += '"' + (params[i]) + '"'
			else:
				req += str(params[i])
		req += ']'
	req += '}\n'

	#print 'performing rpc port %d request %s\n' % (c.port, req),
	try:
		r = s.post('http://127.0.0.1:%d' % c.port, auth=(c.user, c.pwd), data=req, timeout=360)
	except Exception as e:
		print '%d Warning: rpc port %d exception %s req %s\n' % (time.time(), c.port, type(e), req),
		#traceback.print_exc()
		return None
	#print 'rpc status code %d response: %s\n', (r.status_code, r.text),
	if r.status_code != 200: # and method not in ('sendtoaddress', 'payto', 'broadcast'):
		print '%d Warning: rpc port %d status code %d req %s\n' % (time.time(), c.port, r.status_code, req),

	if method == 'unloadwallet' or method.startswith('cc.dump'):
		return None
	try:
		j = json.loads(r.text)
		if 'result' in (j or ()):
			rv = j['result']
		else:
			rv = None
		if rv is None:
			print '%d Warning: rpc port %d result "%s" req %s\n' % (time.time(), c.port, j['error']['message'], req),
		return rv
	except:
		#pprint.pprint(r)
		if hasattr(r, 'text'):
			print '%d Warning: rpc port %d json load failed "%s" req %s\n' % (time.time(), c.port, r.text.encode('ascii', 'backslashreplace'), req),
		else:
			print '%d Warning: rpc port % d no text returned for req %s\n' % (time.time(), c.port, req),
		return None

class Config:
	def __init__(self, c, conf_file, s):
		try:
			c = c.pop(s)
		except KeyError:
			print 'ERROR: missing required section "%s" in config file %s' % (s, conf_file)
			exit()
		if s == FOREIGN:
			self.type		= self.getkey(c, conf_file, s, 'type', validvals=[BITCOIND, ELECTRUM])
			self.currency	= self.getkey(c, conf_file, s, 'currency')
		else:
			self.type		= s
		if self.type == CREDACASH or self.type == BITCOIND:
			self.port		= self.getkey(c, conf_file, s, 'port')
			self.user		= self.getkey(c, conf_file, s, 'user')
			self.pwd		= self.getkey(c, conf_file, s, 'password')
		if self.type == ELECTRUM:
			self.exefile	= self.getkey(c, conf_file, s, 'exe-file')
			self.exemode	= self.getkey(c, conf_file, s, 'exe-mode', defval='gui', validvals=['gui', 'daemon'])
			self.exeopts	= self.getkey(c, conf_file, s, 'exe-options', defval='')
			self.waldir		= self.getkey(c, conf_file, s, 'wallet-dir', defval='')
			self.walpwd		= self.getkey(c, conf_file, s, 'wallet-password', defval='')
		if self.type != CREDACASH:
			self.walname	= self.getkey(c, conf_file, s, 'wallet-name', defval=None)
			self.blocktime	= self.getkey(c, conf_file, s, 'blocktime')
		self.del_excluded_keys(c)
		if len(c) > 0:
			print 'ERROR: unrecognized key in section "%s" of config file %s:' % (s, conf_file), c
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
			print 'ERROR: key "%s" in section "%s" of config file %s must be one of these values:' % (k, s, conf_file), validvals
			exit()
		except KeyError:
			if defval is not None:
				return defval
			if not required:
				return None
			else:
				print 'ERROR: missing required key "%s" in section "%s" of config file %s' % (k, s, conf_file)
				exit()

	@staticmethod
	def del_excluded_keys(c):
		for k in c.keys():
			if k.startswith('x-'):
				del c[k]

	def IsBitcoind(self):
		return self.type == BITCOIND

	def IsElectrum(self):
		return self.type == ELECTRUM

	def InvalidTypeError(self):
		print 'ERROR: invalid foreign rpc type', self.type
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
					print 'error attempting to make payment: unexpected result '
					pprint.pprint(txid)
					return None
				txid = str(txid[1])
			if not isinstance(txid, (str, unicode)):
				print 'error attempting to make payment: value for txid is not a string'
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
					print 'error retrieving payment info for foreign address', addr
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
				print 'blockchain_height missing from getinfo'
				print r
				return None
			height = r['blockchain_height']
			r = do_rpc(s, self, 'getaddresshistory', (addr, ))
			#print 'height', height
			#pprint.pprint(r)
			for t in (r or ()):
				if not 'tx_hash' in t or not 'height' in t:
					print 'no tx_hash or height in getaddresshistory'
					print t
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
				print 'error retrieving transaction info for payment of match', num, 'txid', txid
				return None
			#pprint.pprint(r)
			if not 'confirmations' in r:
				print 'no confirmations for payment of match', num, 'txid', txid
				print r
				return None
			if r['confirmations'] < conf:
				return r
			if 'blockheight' in r:
				return r
			if not 'blockhash' in r:
				print 'no blockheight or blockhash for payment of match', num, 'txid', txid
				print r
				return None
			e = do_rpc(s, self, 'getblockheader', (r['blockhash'], ))
			#pprint.pprint(e)
			if not 'height' in (e or ()):
				print 'no height for payment of match', num, 'txid', txid, 'blockhash', r['blockhash']
				print e
				return None
			r['blockheight'] = e['height']
			return r
		elif self.IsElectrum():
			r = do_rpc(s, self, 'getaddresshistory', (addr, ))
			#pprint.pprint(r)
			c = None
			for t in (r or ()):
				if not 'tx_hash' in t or not 'height' in t:
					print 'no tx_hash or height for payment of match', num, 'address', addr, 'txid', txid
					print t
					return None
				if txid == str(t['tx_hash']) and t['height'] and (not c or c['height'] > t['height']):
					c = t
			if c is None:
				return None
			r = do_rpc(s, self, 'getinfo')
			if not 'blockchain_height' in (r or ()):
				print 'no blockchain_height for payment of match', num, 'txid', txid
				print r
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

def parse_config(conf_file):
	global Creda, Foreign
	conf_fp = open(conf_file)
	c = json.load(conf_fp)
	#print c
	Creda = Config(c, conf_file, CREDACASH)
	Foreign = Config(c, conf_file, FOREIGN)
	Config.del_excluded_keys(c)
	if len(c) > 0:
		print 'ERROR: unrecognized elements in configuration file %s:' % conf_file, c
		exit()

def pay_foreign():
	print int(time.time()), 'start'
	s = requests.Session()
	while True:
		time.sleep(10 + 10 * random.random())
		# >>> Check if any buy requests need action
		r = do_rpc(s, Creda, 'cc.crosschain_match_action_list')
		#pprint.pprint(r)
		for e in (r or ()):
			#pprint.pprint(e)
			if not 'payment-info' in (e or ()):
				print "missing key 'payment-info' in ", e
				continue
			mi = e['match-info']
			pi = e['payment-info']
			num = mi['match-number']
			mt = mi['match-localtime']
			cur = pi['payment-asset']
			amt = pi['payment-amount']
			addr = pi['payment-address']
			conf = pi['payment-confirmations-required']
			mins = pi['deadline-minutes']
			paid = pi['wallet-marked-as-paid']
			if cur != Foreign.currency:
				continue
			#print 'match',num,paid,mins
			if not paid:
				# >>> Check to pay bitcoin to seller
				if IsTestnet and not Foreign.IsTestnetAddr(addr):
					print int(time.time()), 'skipping payment of match', num, 'to non-testnet address', addr
					do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, 'non-testnet address', -1, 0))
				elif mins < conf * Foreign.blocktime + 10 * (not IsTestnet):
					# >>> Don't have time to safely pay, so mark as skipped
					print int(time.time()), 'skipping payment of match', num, 'insufficient time', mins
					do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, 'insufficient time', -1, 0))
				else:
					# >>> Check if the bitcoin wallet already contains a payment for this match
					# if found, assume this payment was for the exact full amount
					forn_txid = Foreign.FindPayment(s, addr, mt - 10*60, conf) # search in bitcoin wallet up to 10 minutes before the match time, in case of clock differences
					if forn_txid:
						print int(time.time()), 'found prior payment for match', num, 'foreign address', addr, 'foreign txid', forn_txid
					try:
						if not forn_txid:
							# pay the match
							# to reduce the chance of the "finally" code below failing, first make sure the CredaCash wallet is still running
							conn = do_rpc(s, Creda, 'cc.time')
							if not conn:
								print int(time.time()), 'Error: CredaCash wallet not responding'
							else:
								# >>> Pay bitcoin to seller
								forn_txid = Foreign.Send(s, addr, amt)
								if forn_txid:
									#print 'paid match', num, 'amount', amt, 'address', addr, 'forn_txid', forn_txid, 'deadline', mins
									print int(time.time()), 'paid match', num, 'amount', amt, 'foreign address', addr, 'foreign txid', forn_txid, 'minutes to deadline', mins
								else:
									print int(time.time()), 'Error paying for match', num, 'amount', amt, 'foreign address', addr
									# >>> Error making payment, so set a reminder to retry
									do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 2, 2))
					finally:
						if forn_txid:
							# >>> Record bitcoin paymnet in the CredaCash wallet, and set reminder to check for payment confirmation
							do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, forn_txid, conf * Foreign.blocktime/8.0, 10))
			else:
				# >>> Check to see if earlier payment to seller has been confirmed on the bitcoin blockchain
				try:
					forn_txid = pi['foreign-payment-txid']
				except:
					print "missing key 'foreign-payment-txid' in ", e
					continue
				e = Foreign.GetTx(s, num, addr, forn_txid, conf)
				if not 'confirmations' in (e or ()) or e['confirmations'] < conf:
					if mins < 5:
						do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 1, 2))
						# >>> Attempt to abandon the foreign transaction since there's probably not enough time left to claim it
						print int(time.time()), 'Attempting to abandon foreign transaction for payment', num, 'amount', amt, 'foreign txid', forn_txid
						Foreign.AbandonTx(s, forn_txid)
					if not e or mins < 12:
						# >>> Error querying payment, so set a reminder to recheck in 2 minutes
						do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 2, 2))
					else:
						# >>> Not yet enough confirmations, so set a reminder to recheck in 5 minutes
						do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 5, 6))
				else:
					# >>> Payment to seller has been confirmed on the bitcoin blockchain
					txid = do_rpc(s, Creda, 'cc.crosschain_payment_claim', ('', num, e['blockheight'], '', amt))
					print int(time.time()), 'crosschain_payment_claim', num, 'result', txid, 'dl', mins
					# >>> Set a reminder to claim the payment again just to be sure (multiple duplicate claims are ok)
					if not txid:
						do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 2, 2))
					elif mins > 20:
						do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 120, 10))
					elif mins > 10:
						do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 120, 5))

def check_if_testnet(s):
	global IsTestnet
	print 'CredaCash rpc port:', Creda.port
	r = do_rpc(s, Creda, 'getinfo')
	#print r
	if not r:
		print 'ERROR querying CredaCash wallet at port', Creda.port
		exit()
	try:
		if r['connections'] < 1:
			print 'ERROR: ccnode is not yet synced to the CredaCash network'
			exit()
		IsTestnet = r['testnet']
	except KeyError:
		print 'ERROR: unable to determine if the CredaCash wallet is connected to a testnet'
		print r
		exit()
	print 'IsTestnet', IsTestnet

stop_electrum = False

def init_bitcoind(s):
	if Foreign.walname is not None:
		walinfo = do_rpc(s, Foreign, 'getwalletinfo')
		if walinfo and 'walletname' in walinfo and walinfo['walletname'] == Foreign.walname:
			print 'Wallet %s already loaded' % Foreign.walname
		else:
			print 'Loading wallet %s' % Foreign.walname
			do_rpc(s, Foreign, 'unloadwallet', ('', ))
			do_rpc(s, Foreign, 'loadwallet', (Foreign.walname, ))

def init_electrum(s):
	# Determine Electrum exe name
	global Foreign, stop_electrum
	Foreign.exefile = os.path.expandvars(os.path.expanduser(Foreign.exefile))
	if not os.path.isfile(Foreign.exefile):
		print 'ERROR:', ELECTRUM, 'executable file not found: "%s"' % Foreign.exefile
		exit()
	print ELECTRUM, 'executable:', Foreign.exefile
	Foreign.launch = [Foreign.exefile]
	Foreign.launch.extend(Foreign.exeopts.split())
	#pprint.pprint(Foreign.launch)

	if Foreign.currency == 'btc':
		query = Foreign.launch + ['--offline']
	else:
		query = Foreign.launch

	# Set rpcsock = tcp if the electrum default is unix
	if hasattr(socket, 'AF_UNIX'):
		print 'setting', ELECTRUM, 'rpc mode...'
		subprocess.Popen(query + ['setconfig', 'rpcsock', 'tcp']).wait()

	# Launch Electrum in GUI or daemon mode
	if Foreign.exemode == 'daemon':
		if not hasattr(os,'fork'):
			print 'ERROR: daemon not supported on systems that lack os.fork()'
			exit()
		print 'starting', ELECTRUM, 'daemon...'
		stop_electrum = True
		if Foreign.currency == 'bch':
			subprocess.Popen(Foreign.launch + ['daemon', 'start'])
		else:
			subprocess.Popen(Foreign.launch + ['daemon', '-d'])
	else:
		print 'starting %s...' % ELECTRUM
		subprocess.Popen(Foreign.launch)
	time.sleep(10)

	# Determine Electrum data path
	# TODO: better error checking/reporting here:
	print 'querying %s...' % ELECTRUM
	r = subprocess.check_output(Foreign.launch + ['getinfo'])
	#pprint.pprint(r)
	try:
		j = json.loads(r)
		p = j['path']
	except:
		p = ''
	if not len(p):
		print 'ERROR: unable to obtain', ELECTRUM, 'data path'
		raise SystemExit()
	print ELECTRUM, 'data path: "%s"' % p

	# Determine Electrum RPC port, username and password
	try:
		f = open(p + '/daemon')
		l = f.read()
		f.close()
	except:
		print 'ERROR: unable to read', ELECTRUM, 'daemon file'
		raise SystemExit()
	try:
		c = l.split(')', 1)[0].strip('(').split(',')
		Foreign.rpcsock = c[0].strip().strip("'")
		Foreign.port = c[-1].strip().strip("'")
		#if Foreign.port.isdigit():
		Foreign.port = int(Foreign.port)
		#print Foreign.rpcsock, Foreign.port
	except:
		print 'ERROR: unable to parse', ELECTRUM, 'port from string "%s"' % l
		if Foreign.rpcsock == 'unix':
			print 'Unix domain sockets are not supported'
			print 'Try completely closing', ELECTRUM, 'and re-running this script'
		raise SystemExit()
	print ELECTRUM, 'rpc port:', Foreign.port
	Foreign.user = subprocess.check_output(query + ['getconfig', 'rpcuser']).strip()
	print ELECTRUM, 'rpc user:', Foreign.user
	Foreign.pwd = subprocess.check_output(query + ['getconfig', 'rpcpassword']).strip()
	if len(Foreign.pwd):
		print ELECTRUM, 'rpc password: %c*****' % Foreign.pwd[0]
		#print Foreign.pwd

	# Check if Electrum RPC works
	r = do_rpc(s, Foreign, 'version')
	if not r:
		print 'ERROR: unable to get', ELECTRUM, 'version'
		raise SystemExit()
	print ELECTRUM, 'version', r

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
	print ELECTRUM, 'wallet file: "%s"' % walfile
	if len(Foreign.walpwd):
		walp['password'] = Foreign.walpwd
		print ELECTRUM, 'wallet password: %c*****' % Foreign.walpwd[0]
		#print walp['password']

	# Load Electrum wallet
	print 'loading', ELECTRUM, 'wallet...'
	if Foreign.currency != 'bch':
		#pprint.pprint(walp)
		r = do_rpc(s, Foreign, 'load_wallet', walp)
		if not r:
			print 'ERROR: unable to load', ELECTRUM, 'wallet'
			raise SystemExit()
	else:
		#pprint.pprint(Foreign.launch)
		sp = subprocess.Popen(Foreign.launch + ['--wallet', Foreign.walpath, '--walletpassword', '-', 'daemon', 'load_wallet'], bufsize=4000, stdin=subprocess.PIPE, stdout=subprocess.PIPE) # must have stdin and stdout for wallet password injection to work
		sp.stdin.write(Foreign.walpwd)
		sp.stdin.close()
		for i in range(60):
			time.sleep(1)
			if sp.poll() is not None:
				break
		if sp.poll() is None:
			sp.kill()
		rc = sp.wait()
		if rc:
			print 'ERROR:', ELECTRUM, 'returned status code', rc
			raise SystemExit()

def main(argv):

	if len(argv) > 2:
		print
		print 'Usage: python exchange-pay.py [<config_file>]'
		print
		exit()

	if len(argv) > 1:
		conf_file = argv[1]
	else:
		conf_file = 'exchange-pay.conf'

	parse_config(conf_file)

	ensure_one_instance()

	s = requests.Session()

	check_if_testnet(s)

	print FOREIGN, 'currency:', Foreign.currency

	try:

		if Foreign.IsBitcoind():
			init_bitcoind(s)

		if Foreign.IsElectrum():
			init_electrum(s)

		print 'checking wallet balance...'
		r = do_rpc(s, Foreign, 'getbalance', needs_wallet=True)
		if r is None:
			print 'ERROR: unable to query wallet balance'
			raise SystemExit()
		print 'wallet balance:', r

		print

		pay_foreign()

	except Exception:
		traceback.print_exc()
	except KeyboardInterrupt:
		pass
	except SystemExit:
		pass

	if stop_electrum:
		print 'stopping', ELECTRUM, 'daemon...'
		subprocess.Popen(Foreign.launch + ['stop'])
		print 'done'

if __name__ == '__main__':
	main(sys.argv)
