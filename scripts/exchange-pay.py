#!/usr/bin/env python

from __future__ import print_function

'''
CredaCash(TM) Exchange Autopay

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors

The script may be used to complete purchases of CredaCash currency on the
CredaCash integrated peer-to-peer exchange. It is used in conjunction with a
CredaCash wallet and a Bitcoin wallet (either the Bitcoin core wallet, or
the Electrum wallet), or a Bitcoin Cash wallet (either the Bitcoin Cash core
wallet, or the Electron Cash wallet).

This script performs the following operations:
- Monitors the CredaCash wallet for CredaCash buy requests that have matched sell requests.
- Determines the amount of bitcoin to pay the seller, and the payment deadline.
- Ensures there should be enough time for a bitcoin payment to be confirmed before the deadline.
- Pays the seller in bitcoin on the bitcoin blockchain.
- Monitors the bitcoin blockchain for payment confirmation.
- Submits a payment claim on the CredaCash blockchain.

Note: The main loop that handles the above operations is called main_loop()

'''

MIN_MINUTES_FOR_PAY_CLAIM = 5		# skip pay claim when too close to deadline, in order to prioritize pay claims that have a better chance of being accepted

exchange_common = 'exchange-common.py'
with open(exchange_common) as f:
    code = compile(f.read(), exchange_common, 'exec')
    exec(code)

import subprocess

def ensure_one_instance():
	id = 'ccap_' + Foreign.currency + '_' + str(Creda.port)
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

def handle_unpaid_matches(s, mi, pi, num, amt, addr, conf, mins):

	# >>> Check to pay bitcoin to seller
	if IsTestnet and not Foreign.IsTestnetAddr(addr):
		# Sanity check: If the CredaCash wallet is connected to testnet, don't send real bitcoin
		print(int(time.time()), 'skipping payment of match', num, 'to non-testnet address', addr)
		do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, 'non-testnet address', -1, 0)) # remove match from cc.crosschain_match_action_list
		return

	if mins < conf * Foreign.blocktime + 10 * (not IsTestnet):
		# >>> Don't have time to safely make the bitcoin payment and get it confirmed, so abandon this match instead
		print(int(time.time()), 'skipping payment of match', num, 'insufficient time', mins)
		do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, 'insufficient time', -1, 0)) # remove match from cc.crosschain_match_action_list
		return

	# >>> Check if the bitcoin wallet already contains a payment for this match.
	# If found, assume this payment was for the exact full amount.
	mt = mi['match-localtime']
	forn_txid = Foreign.FindPayment(s, addr, mt - 10*60, conf) # search in bitcoin wallet up to 10 minutes before the match time, in case of clock differences
	if forn_txid:
		print(int(time.time()), 'found prior payment for match', num, 'foreign address', addr, 'foreign txid', forn_txid)
	try:
		if not forn_txid:
			# Prepare to pay bitcoin to seller.
			# To make this process as foolproof as possible, we want to ensure that if this script is interrrupted
			# in the middle of this code and then later restarted, it will not send a duplicate bitcoin payment.
			# This is accomplished by first checking the bitcoin wallet to see if a payment was already made (the code above),
			# and also by recording the payment in the CredaCash wallet immediately after it is made (the code in the "finally" block below).
			# To reduce the chance of the "finally" code failing, first make sure the CredaCash wallet is still running:
			conn = do_rpc(s, Creda, 'cc.time')
			if not conn:
				print(int(time.time()), 'Error: CredaCash wallet not responding')
			else:
				# >>> Send the bitcoin payment to the seller
				forn_txid = Foreign.Send(s, addr, amt)
				if forn_txid:
					#print 'paid match', num, 'amount', amt, 'address', addr, 'forn_txid', forn_txid, 'deadline', mins
					print(int(time.time()), 'paid match', num, 'amount', amt, 'foreign address', addr, 'foreign txid', forn_txid, 'deadline', mins, 'minutes')
				else:
					print(int(time.time()), 'Error paying for match', num, 'amount', amt, 'foreign address', addr)
					# >>> Error making payment, so set a reminder to retry in 2 minutes
					do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 2, 2))
	finally:
		if forn_txid:
			# >>> Record bitcoin payment in the CredaCash wallet, and set a reminder to check for payment confirmation later
			do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, forn_txid, conf * Foreign.blocktime/8.0, 30)) # start checking for payment confirmation at least 30 minutes before deadline


def handle_paid_matches(s, mi, pi, num, amt, addr, conf, mins, pay_claim_stats):

	# >>> Query number of confirmations that the earlier match payment now has on the bitcoin blockchain
	try:
		forn_txid = pi['foreign-payment-txid']
	except:
		print("missing key 'foreign-payment-txid' in ", pi)
		return False

	r = Foreign.GetTx(s, num, addr, forn_txid, conf)
	try:
		blockheight = r['blockheight']
		confirmations = r['confirmations']
		if confirmations < conf:
			print(int(time.time()), '  match', num, 'payment has', confirmations, 'confirmations; deadline', mins, 'minutes')
	except:
		blockheight = 0
		confirmations = -1

	if confirmations < conf or (confirmations < conf+1 and mins > 40 and mins > MIN_MINUTES_FOR_PAY_CLAIM + 30):
		# >>> Prior payment is not yet confirmed
		# (or, if there's enough time left, wait for an extra confirmation to increase chance of pay claim being accepted on the CredaCash network)
		if mins < 5 and mins < MIN_MINUTES_FOR_PAY_CLAIM:
			# Attempt to abandon the bitcoin transaction since there's probably not enough time left to claim it
			print(int(time.time()), 'Attempting to abandon foreign transaction for payment', num, 'amount', amt, 'foreign txid', forn_txid)
			Foreign.AbandonTx(s, forn_txid)
			do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', -1, 0))		# turn off retries
		if mins < 20 or confirmations >= conf or confirmations < 0:
			# Not yet enough confirmations but the deadline is close, or confirmation is imminent, or there was an error checking confirmations, so set a reminder to recheck in 2 minutes
			do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 2, 2))
		else:
			# Not yet enough confirmations, so set a reminder to recheck in 12 minutes, but at least MIN_MINUTES_FOR_PAY_CLAIM + 2 minutes before the deadline
			do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 12, MIN_MINUTES_FOR_PAY_CLAIM + 2))

		return False

	if mins < MIN_MINUTES_FOR_PAY_CLAIM:
		# >>> Prior payment has been confirmed, but there might not be enough time left to claim it.
		# There's a reason we got to this point, probably because exchange requests are being sent and matches being made
		# faster than this computer can generate payment claims. So instead of having all payment claims fail because
		# they are made after the deadline, skip the ones too close to the deadline and only do the ones that should have
		# enough time left to be successful.
		print(int(time.time()), 'skipping crosschain_payment_claim %d; insufficient time, deadline %d minutes' % (num, mins))
		do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', -1, 0))		# turn off retries
		return False

	if confirmations == conf and mins > MIN_MINUTES_FOR_PAY_CLAIM:
		# >>> Prior payment has been confirmed, but maybe just seconds ago
		# Therefore, sleep a short time to give the bitcoin confirmations a little time to be seen on CredaCash network
		print(int(time.time()), 'pausing 60 seconds before creating crosschain_payment_claim %d with confirmations %d, deadline %d minutes' % (num, confirmations, mins))
		time.sleep(60)

	# >> Prior payment to seller has been confirmed on the bitcoin blockchain, so generate a payment claim.
	# This can take a while and may timeout. It might be tempting to turn off the timeouts and wait forever, but that can
	# result in this script hanging if there is a communication glitch (which happens sometimes).
	# And if it does timeout, we don't want to simply repeat the command, because the wallet will probably timeout
	# again and again and will thrash and never get the command completed.
	# So we can't turn off timeouts, and we can't simply repeat the command, but we can instead take advantage
	# of a feature unique to CredaCash that allows a script to initiate a command, and then reconnect to that
	# same command in progress if communication is interrupted.
	# This is accomplished by invoking the command with a unique refid, and then repeating the command again with the same
	# refid if communication is interrupted before a response is received. Associating the command with a single unique
	# refid ensures the command only happens once.
	# Start by generating a unique refid:
	refid = do_rpc(s, Creda, 'cc.unique_id_generate')
	if not refid:
		print(int(time.time()), 'error obtaining refid')
		txid = None
	else:
		print(int(time.time()), 'generating crosschain_payment_claim %d, deadline %d minutes...' % (num, mins))
		#print(refid, num, blockheight, amt)

		# Now make the payment claim using the unique refid, repeating the command with the same refid if there is a timeout.
		# No matter how many times the refid and command are sent, the CredaCash wallet will only do the command once.
		# After the command completes, the CredaCash wallet will immediately return the result of the command every time
		# the refid and command are resent.
		t0 = time.time()
		while True:
			txid = do_rpc(s, Creda, 'cc.crosschain_payment_claim', (refid, num, blockheight, '', amt), return_timeout=True)
			if txid == TIMEOUT:
				#print('crosschain_payment_claim timeout -- retrying...')
				continue
			break
		dt = time.time() - t0
		mins -= dt/60

		# >> Payment claim command is done.
		# Keep track of total time to generate all successful payment claims:
		if txid:
			pay_claim_stats[0] += dt
			pay_claim_stats[1] += 1

		print(int(time.time()), 'crosschain_payment_claim', num, 'result', txid, 'elapsed time', int(dt+0.5), 'secs; average', int(pay_claim_stats[0]/max(1,pay_claim_stats[1])+0.99), 'secs')

	if not txid:
		# The payment claim was not successful, so set a reminder to retry in 2 minutes
		do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 2, 1))

	# The payment claim was sent, but it could get lost in the network or expire before it is added to a block.
	# Therefore, if there is enough time left, set a reminder to check the match again to make sure the payment claim
	# was recorded, and if not, try sending another payment claim.

	elif mins > 20:
		do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', min(mins-10, 50), 10)) # check again in mins-10 minutes
	elif mins > 10:
		do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 5, 5))   # check again in 5 minutes
	else:
		do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', -1, 0))  # don't check again

	return True # a payment claim was attempted


def main_loop():
	print(int(time.time()), 'start')
	pay_claim_stats = [0, 0]
	s = requests.Session()
	sleep_before_next_check = False

	while True:

		do_paid_matches = True

		if sleep_before_next_check:
			time.sleep(10 + 10 * random.random())
		sleep_before_next_check = True

		# >>> Check if any buy requests need action
		r = do_rpc(s, Creda, 'cc.crosschain_match_action_list')
		#pprint.pprint(r)
		if not r:
			continue

		#print(len(r))
		for e in r:
			#pprint.pprint(e)
			if not 'payment-info' in (e or ()):
				print("missing key 'payment-info' in ", e)
				continue

			mi = e['match-info']
			pi = e['payment-info']
			cur = pi['payment-asset']
			paid = pi['wallet-marked-as-paid']

			num = mi['match-number']
			amt = pi['payment-amount']
			addr = pi['payment-address']
			conf = pi['payment-confirmations-required']
			mins = pi['deadline-minutes']

			if cur != Foreign.currency:
				continue
			#print 'match',num,paid,mins

			if not paid:
				handle_unpaid_matches(s, mi, pi, num, amt, addr, conf, mins)
				sleep_before_next_check = False

			elif do_paid_matches:
				did_pay_claim = handle_paid_matches(s, mi, pi, num, amt, addr, conf, mins, pay_claim_stats)
				if did_pay_claim:
					# Do only one crosschain_payment_claim in each pass through cc.crosschain_match_action_list
					# This prioritizes unpaid matches so they can get paid and confirmed sooner
					do_paid_matches = False
					sleep_before_next_check = False


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

def main(argv):

	if len(argv) > 2:
		print()
		print('Usage: python exchange-pay.py [<config_file>]')
		print()
		exit()

	if len(argv) > 1:
		conf_file = argv[1]
	else:
		conf_file = 'exchange-pay.conf'

	parse_config(conf_file, False, ForeignRPC)

	print(FOREIGN, 'currency:', Foreign.currency)

	ensure_one_instance()

	s = requests.Session()

	check_if_testnet(s)

	try:

		if Foreign.IsBitcoind():
			init_bitcoind(s)

		if Foreign.IsElectrum():
			init_electrum(s)

		print('checking', FOREIGN.lower(), 'wallet balance...')
		r = do_rpc(s, Foreign, 'getbalance', needs_wallet=True)
		if r is None:
			print('ERROR: unable to query', FOREIGN.lower(), 'wallet balance')
			raise SystemExit()
		print(FOREIGN, 'wallet balance:', r)

		print()

		main_loop()

	except Exception:
		traceback.print_exc()
	except KeyboardInterrupt:
		pass
	except SystemExit:
		pass

	if stop_electrum:
		print('stopping', ELECTRUM, 'daemon...')
		subprocess.Popen(Foreign.launch + ['stop'])
		print('done')

if __name__ == '__main__':
	main(sys.argv)
