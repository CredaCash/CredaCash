#!/usr/bin/env python2

'''
CredaCash(TM) Exchange Mining Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors

The script may be used to mine CredaCash using the CredaCash integrated peer-to-peer exchange.
It is used in conjunction with the CredaCash Exchange Autopay script (exchange-pay.py),
a CredaCash wallet, and a Bitcoin Cash core wallet. (Note the Electron Cash wallet is not
supported by the script due to concerns about its reliability.)

This script first checks to see that the autopay script is working.
It then makes matching buy and sell requests (wash trading) that meet the mining criteria:
	- Simple buy requests of CredaCash for BCH.
	- Requested exchange rate slightly higher than the running average.
	- Match amount between 20% and 180% of the running average.

The number of requests it makes per hour is set in the config file.

This script will suspend requests if either the CredaCash of BCH balance falls below the minimum
set in the config file.  It will also stop if at any time the autopay script appears to fail.

The wallet balances and the total amount mined by the CredaCash wallet are reported on the console.

'''

Minimum_Allowed_Payment_Minutes = 4	# It is considered a payment failure if a payment is not made
									#   at least this many minutes before the payment deadline

BCH_MIN_SEND_AMOUNT = 1e-4

test_rounding = False
rounding_test = {}

import sys
import os
import threading
import traceback
import requests
import json
import random
import math
import time
import pprint

if not sys.version.startswith('2.7.') or not ('GCC' in sys.version or '64 bit' in sys.version or 'AMD64' in sys.version):
	print 'ERROR: This script requires Python 2.7.x (64 bit version).'
	exit()

MINING = 'Mining'
CREDACASH = 'CredaCash'
FOREIGN = 'BCH'
BITCOIND = 'Bitcoind'

CC_TYPE_XCX_SIMPLE_BUY = 6
XMATCH_STATUS_ACCEPTED = 6
XMATCH_STATUS_PAID = 9

def do_rpc(s, c, method, params=()):
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
	if method.startswith('cc.dump'):
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
			self.type			= self.getkey(c, conf_file, s, 'type', validvals=[BITCOIND, ])
		else:
			self.type			= s
		if self.type == MINING:
			self.reqs_per_hr	= self.getkey(c, conf_file, s, 'exchange requests per hour')
			self.req_min_amt	= self.getkey(c, conf_file, s, 'exchange request minimum amount')
			self.req_max_amt	= self.getkey(c, conf_file, s, 'exchange request maximum amount')
			self.min_cc_bal		= self.getkey(c, conf_file, s, 'minimum CredaCash balance', False, 20)
			self.min_bch_bal	= self.getkey(c, conf_file, s, 'minimum BCH balance', False, 20)
			self.skip_pay_test	= self.getkey(c, conf_file, s, 'skip autopay test', False, False)
		if self.type == CREDACASH or self.type == BITCOIND:
			self.port			= self.getkey(c, conf_file, s, 'port')
			self.user			= self.getkey(c, conf_file, s, 'user')
			self.pwd			= self.getkey(c, conf_file, s, 'password')
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

def parse_config(conf_file):
	global Creda, Foreign, Mining
	conf_fp = open(conf_file)
	c = json.load(conf_fp)
	#print c
	Creda = Config(c, conf_file, CREDACASH)
	Foreign = Config(c, conf_file, FOREIGN)
	Mining = Config(c, conf_file, MINING)
	Config.del_excluded_keys(c)
	if len(c) > 0:
		print 'ERROR: unrecognized elements in configuration file %s:' % conf_file, c
		exit()

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

class PayChecker:
	# this class is a singleton, so everything is static
	status_bad = False
	start_matchnum = 0
	test_matchnum = 0

	# The thread calling this function will submit exchange test requests that generate a test match.
	# It will then wait for that test match to be paid by the CredaCash Exchange Autopay script (exchange-pay.py).
	# These three functions Init(), FindTestMatch() and TestMatchFound() help this process as follows:
	# Init() finds the highest existing matchnum -- the eventual test match will have a higher matchnum
	# FindTestMatch() finds the test match; this function needs to be called repeatedly until the test match is found
	# TestMatchnumFound() returns the test match matchnum, or zero if not yet found

	@staticmethod
	def Init(s):
		r = do_rpc(s, Creda, 'cc.crosschain_match_action_list', (0, 'true'))
		for m in (r or ()):
			mi = m['match-info']
			matchnum = mi['match-number']
			if matchnum > PayChecker.start_matchnum:
				PayChecker.start_matchnum = matchnum

	@staticmethod
	def FindTestMatch(s, test_amount):
		r = do_rpc(s, Creda, 'cc.crosschain_match_action_list', (0, 'true'))
		for m in (r or ()):
			# look for test request match
			#pprint.pprint(m)
			mi = m['match-info']
			pi = m['payment-info']
			matchnum = mi['match-number']
			if matchnum <= PayChecker.start_matchnum:
				#print 'not matchnum'
				continue
			if not mi['wallet-is-buyer']:
				#print 'not buyer'
				continue
			if mi['type'] != CC_TYPE_XCX_SIMPLE_BUY:
				#print 'not type'
				continue
			if mi['base-amount'] != test_amount:
				#print 'not amount'
				continue
			if pi['payment-asset'] != 'bch':
				#print 'bch'
				continue
			# test match found
			PayChecker.test_matchnum = matchnum

	@staticmethod
	def TestMatchnumFound():
		return PayChecker.test_matchnum

	# This function checks that all of this wallet's BCH buy request matches have been paid at least
	# Minimum_Allowed_Payment_Minutes prior to the payment deadline.
	# If not, there is a problem making BCH payments and this mining script will stop.

	@staticmethod
	def Check(s):
		r = do_rpc(s, Creda, 'cc.crosschain_match_action_list', (Minimum_Allowed_Payment_Minutes, 'true'))
		for m in (r or ()):
			# cc.crosschain_match_action_list was called with following arguments:
			#	minutes_until_deadline = Minimum_Allowed_Payment_Minutes and override_reminder_times = True
			# If any results are returned, BCH payments are not being made or dangerously close to not being made
			PayChecker.status_bad = True
			pprint.pprint(m)
			print 'BCH Payment Error: match not paid %d minutes before deadline\n' % Minimum_Allowed_Payment_Minutes,

	@staticmethod
	def StatusIsBad():
		return PayChecker.status_bad

def pay_monitor_thread():
	interval = 120
	interval = int(min(interval, Minimum_Allowed_Payment_Minutes * 60 / 2))
	print 'pay_monitor_thread interval %d\n' % interval,
	last_time = time.time()
	s = requests.Session()
	while True:
		time.sleep(interval + last_time - time.time())
		last_time = time.time()
		#print '%d pay_monitor_thread check\n' % last_time,
		PayChecker.Check(s)
		if PayChecker.StatusIsBad():
			return

def mine_thread():
	interval = 3600.0 / Mining.reqs_per_hr
	print 'mine_thread interval %g\n' % interval,
	s = requests.Session()
	last_time = time.time()
	while True:
		sleep = interval * (0.5 + random.random())		# randomize to increase privacy
		sleep -= time.time() - last_time
		if sleep > 0:
			time.sleep(sleep)
		last_time = time.time()
		#print '%d mine_thread\n' % last_time,
		mine_one(s)

def mine_one(s):
		bch_bal = get_balance(s, Foreign)
		pending = do_rpc(s, Creda, 'cc.exchange_requests_pending_totals', ('bch', ))
		mi = get_mining_info(s)

		if bch_bal is None:
			print '%d mine_thread error getting BCH wallet balance\n' % time.time(),
			return
		if not pending:
			print '%d mine_thread error getting wallet pending balances\n' % time.time(),
			return
		if not mi:
			print '%d mine_thread error getting CredaCash mining info\n' % time.time(),
			return

		cc_bal = pending['wallet-balance']
		cc_pending = pending['sell-request-pending-totals']['base-amount']		# CredaCash tied up in sell requests
		cc_pending += pending['buy-request-pending-totals']['pledge-amount']	# CredaCash tied up in buy requests
		cc_net_bal = cc_bal + cc_pending

		rate = mi['mining-match-average-rate']
		mined = mi['wallet-total-mined']
		total_in_cc = cc_net_bal + bch_bal/rate
		total_in_bch = total_in_cc * rate
		print 'Mined %g Balances %g Creda + %g BCH at rate %g = %g Creda or %g BCH\n' \
			% (mined, cc_net_bal, bch_bal, rate, total_in_cc, total_in_bch),
		#print 'Mined %g wallet %g pending %g total %g\n' \
		#	% (mined, cc_bal, cc_pending, cc_net_bal),

		# buy request at slightly higher than average buy req match rate reqired, to ensure mining
		rate = mi['mining-request-average-match-rate-required'] * 1.001

		# Note: this script is setting the request estimated costs to zero, and as a result,
		# the request net_rate_required = the request match rate required.
		# If the req costs were not zero, then the buyer and seller net_rate_required
		# would need to be computed using the formulas:
		#  buyer net_rate = (match_base_amount * match_rate + quote_costs) / (match_base_amount - base_costs)
		# seller net_rate = (match_base_amount * match_rate - quote_costs) / (match_base_amount + base_costs)

		min_amount = Mining.req_min_amt
		max_amount = Mining.req_max_amt

		min_amount = max(min_amount, BCH_MIN_SEND_AMOUNT / rate)

		min_amount = max(min_amount, 0.2 * mi['mining-match-average-amount'])
		max_amount = min(max_amount, 1.8 * mi['mining-match-average-amount'])

		if min_amount > max_amount:
			print '%d mine_thread skipping exchange requests; min_amount %g > max_amount %g\n' % (time.time(), min_amount, max_amount),
			return

		do_sell = 1

		max_amount = min(max_amount, cc_bal - Mining.min_cc_bal)
		if max_amount < min_amount:
			do_sell = 0

		max_amount = min(max_amount, (cc_bal - Mining.min_cc_bal) / (do_sell + 0.5))
		if max_amount < min_amount:
			print '%d mine_thread skipping exchange requests; insufficient CredaCash balance %g, minimum request amount %g\n' % (time.time(), cc_bal, min_amount),
			return

		if not do_sell:
			print '%d mine_thread skipping exchange sell request; insufficient CredaCash balance %g, minimum request amount %g\n' % (time.time(), cc_bal, min_amount),

		bch_allocated = pending['buy-request-pending-totals']['quote-amount']
		bch_net_bal = bch_bal - bch_allocated

		max_amount = min(max_amount, (bch_net_bal - Mining.min_bch_bal) / rate)
		if max_amount < min_amount:
			print '%d mine_thread skipping exchange requests; insufficient BCH free balance %g, minimum request amount %g rate %g\n' % (time.time(), bch_net_bal, min_amount, rate),
			return

		amount = random.random() * (max_amount - min_amount) + min_amount

		rounding = 0
		while True:
			adj_amount = round_to_power(amount, rounding)
			if adj_amount < min_amount:
				if rounding < 0:
					adj_amount = None
					break
				rounding += 0.5
				continue
			if adj_amount > max_amount:
				if rounding > 0:
					adj_amount = None
					break
				rounding -= 0.5
				continue
			break

		if not adj_amount:
			print '%d mine_thread skipping exchange requests; unable to round %g; min request %g max request %g\n' % (time.time(), amount, min_amount, max_amount),
			return

		amount = adj_amount

		expiration = 60 + mi['mining-request-minimum-expiration-time']

		if not submit_xreq(s, 0, amount, rate, expiration):
			return
		if do_sell:
			submit_xreq(s, 1, amount, rate, expiration)

def round_to_power(amount, rounding):
	# amount must be 1, 2, 3, 5 or 7 multiplied by a power of 10
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

	#print '%g\t%g' % (adj_amount, amount + 0.5)

	if rounding == 0 and test_rounding:
		global rounding_test
		if not adj_amount in rounding_test:
			rounding_test[adj_amount] = [amount, amount]
		elif amount < rounding_test[adj_amount][0]:
			rounding_test[adj_amount][0] = amount
		elif amount > rounding_test[adj_amount][1]:
			rounding_test[adj_amount][1] = amount

	#print 'round_to_power %g %g %g\n' % (rounding, amount, adj_amount),

	return adj_amount

if 0:
	test_rounding = True
	for i in range(10000000):
		amount = 1 + 130 * random.random()
		round_to_power(amount, 0)
	pprint.pprint(rounding_test)
	exit()

def submit_xreq(s, type, amount, rate, expiration = 0):
	if type > 1:
		type = 1
	req = ('buy ','sell')[type]
	if req[0] == 'b':
		foreign_address = ''
	else:
		foreign_address = do_rpc(s, Foreign, 'getnewaddress')
		if not foreign_address:
			print 'Error obtaining a BCH address\n',
			return False
		if not foreign_address.startswith('bitcoincash:') and not foreign_address.startswith('bch'):
			print 'Error: unrecognized BCH address %s\n' % foreign_address,
			return False
	txid = do_rpc(s, Creda, 'cc.crosschain_request_create', ('', 's'+req[0], amount, amount, rate, 0, 'bch', foreign_address, expiration))
	if txid:
		print 'Submitted crosschain %s request time %d amount %g rate %g\n' % (req, time.time(), amount, rate),
		return True
	else:
		print 'Crosschain %s request failed time %d amount %g rate %g\n' % (req, time.time(), amount, rate),
		return False

def check_foreign_wallet(s):
	r = get_balance(s, Foreign)
	if r is None:
		print 'ERROR: no BCH wallet loaded; run the exchange-pay.py script before running this script'
		return False
	return True

def pay_monitor_startup(s):
	print 'pay_monitor_startup'

	# init match pay check
	PayChecker.Init(s)

	# sanity check config
	mi = get_mining_info(s)
	#pprint.pprint(mi)
	global Mining, Minimum_Allowed_Payment_Minutes

	lim = 10
	if Mining.min_cc_bal < lim:
		print 'minimum CredaCash balance adjusted from', Mining.min_cc_bal, 'to', lim
		Mining.min_cc_bal = lim

	lim = 0.001
	if Mining.min_bch_bal < lim:
		print 'minimum BCH balance adjusted from', Mining.min_bch_bal, 'to', lim
		Mining.min_bch_bal = lim

	lim = mi['wallet-exchange-request-minimum-amount']
	if Mining.req_min_amt < lim:
		print 'exchange request minimum amount adjusted from', Mining.req_min_amt, 'to', lim
		Mining.req_min_amt = lim

	lim = Mining.req_min_amt
	if Mining.req_max_amt < lim:
		print 'exchange request maximum amount adjusted from', Mining.req_max_amt, 'to',lim
		Mining.req_max_amt = lim

	lim = 3
	if Minimum_Allowed_Payment_Minutes < lim:
		print 'Minimum_Allowed_Payment_Minutes adjusted from', Minimum_Allowed_Payment_Minutes, 'to', lim
		Minimum_Allowed_Payment_Minutes = lim

	if Mining.skip_pay_test:
		print 'SKIPPING test of exchange autopay script'
		return True

	# submit matching exchange requests
	print 'Testing exchange autopay script...'
	amount = mi['wallet-exchange-request-minimum-amount']
	rate = mi['mining-request-average-match-rate-required']
	for i in range(9):	# submit 8 sell reqs, to maximize chance that the buy req will find a match
		if not submit_xreq(s, i, amount, rate):
			return False

	# wait for match
	print 'Waiting for test match...'
	while True:
		time.sleep(30)
		PayChecker.FindTestMatch(s, amount)
		test_matchnum = PayChecker.TestMatchnumFound()
		if test_matchnum:
			break
	print 'Found test match matchnum %d' % test_matchnum

	# wait until match is paid by the CredaCash Exchange Autopay script
	print 'Waiting for test match to be paid and confirmed...'
	while True:
		time.sleep(30)
		m = do_rpc(s, Creda, 'cc.exchange_match_info', (test_matchnum, ))
		mi = m['match-info']
		status = mi['status']
		if status == XMATCH_STATUS_PAID:
			print 'Test match paid'
			return True
		elif status > XMATCH_STATUS_ACCEPTED:
			print 'ERROR: test match was not paid'
			return False

def start_thread(target, args=()):
	t = threading.Thread(target=target, args=args)
	t.daemon = True
	t.start()
	return t

def main(argv):

	if len(argv) > 2:
		print
		print 'Usage: python exchange-mine.py [<config_file>]'
		print
		exit()

	if len(argv) > 1:
		conf_file = argv[1]
	else:
		conf_file = 'exchange-mine.conf'

	parse_config(conf_file)

	start_time = time.time()
	print 'start time %d' % start_time

	s = requests.Session()

	if not check_foreign_wallet(s):
		exit()

	if not pay_monitor_startup(s):
		print 'ERROR: startup failed'
		exit()

	t1 = start_thread(pay_monitor_thread)
	t2 = start_thread(mine_thread)

	while True:
		time.sleep(2)
		if not t1.isAlive():
			if not PayChecker.StatusIsBad():
				print 'ERROR: pay monitor thread exited\n',
			exit()
		if not t2.isAlive():
			print 'ERROR: mining thread exited\n',
			exit()

if __name__ == '__main__':
	main(sys.argv)
