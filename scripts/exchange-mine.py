#!/usr/bin/env python

from __future__ import print_function

'''
CredaCash(TM) Exchange Mining Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors

The script may be used to mine CredaCash using the CredaCash integrated peer-to-peer exchange.
It is used in conjunction with the CredaCash Exchange Autopay script (exchange-pay.py),
a CredaCash wallet, and a Bitcoin Cash core wallet. (Note the Electron Cash wallet is not
supported by the script due to concerns about its reliability.)

This script first optionally checks to see that the autopay script is working.
It then makes matching buy and sell requests (wash trading) that meet the mining criteria:
	- Simple trade request of CredaCash and BCH.
	- Requested exchange rate slightly higher than the running average.
	- Match amount between 20% and 180% of the running average.

This script using simple trade requests, which is a request type intended specifically for mining.
It is equivalent to a buy request and sell request at the same rate. However, for the purpose of
mining only (not matching), it is credited with twice its rate, and the request therefore only
required half the rate to mine.

The buy and sell requests created by the simple trade request will either match each other, or one
or both will match a simple buy or sell request submitted by another user. The possible outcomes are:

	1. The buy and sell requests match each other in a wash trade. This qualifies for mining,
			so the net result will be an increase in CredaCash by the amount mined.

	2. The buy and sell requests both match other requests. The result will be a net gain
			of CredaCash, a net gain of BCH, or both. This trade also qualfies for mining, resulting
			in an increase in CredaCash by the amount mined.

	3. The sell request is unmatched, while the buy request matches a different sell request that
			offers a lower rate. The net result will be an exchange of BCH for CredaCash at a rate better
			than the requested match rate. This also qualfies for mining, resulting in an increase in
			CredaCash by the amount mined.

	4. The buy request is unmatched, while the sell request matches a different buy request that
			offers a higher rate. The net result will be an exchange of CredaCash for BCH at a rate better
			than the requested match rate. This trade (an unmatched buy request) does not qualify for mining.

The minimum and maximum acceptable exchange rate, and the number of requests to make each hour
are set in the config file.

The wallet balances and the total amount mined by the CredaCash wallet are reported on the console.
Over time, if more buy requests match than sell requests or vice-versa, the amount of CredaCash
or BCH in the wallet may decrease. Mining will be suspended if either of these balances fall
below the minimums set in the config file.

'''

RATE_WARNING = '''
*** IMPORTANT: The number of mining requests per minute in the config file should
not be set higher than 120 until you are sure your computer can create payment
claims at the chosen rate. This can be determined by examining the average
payment claim time reported by the exchange autopay script during mining.
'''

MINING_ABORT = '''
Mining aborted due to missed exchange match payment. This may be caused by
network connection problems, by the exchange autopay script aborting or being
stopped, or by this computer being unable to generate payment claims fast enough
to keep up with the mining rate. This script may be restarted to continue
mining, but if this problem occurs frequently, it may need to be investigated.
'''

MAX_PAYMENT_BACKLOG_MINUTES = 60	# Mining will be paused if the backlog of matches to be paid or claimed exceeds this value
MIN_PAYMENT_TIME_PERCENTAGE = 60	# Mining will be paused if a match payment is not claimed within this %'age of the allowed time
MIN_ALLOWED_PAYMENT_MINUTES = 4		# Mining will be halted if a match payment is not claimed at least this many minutes before the deadline

BCH_MIN_SEND_AMOUNT = 1e-4					# don't do exchange requests that involve less than this amount of BCH
BCH_TESTNET_MIN_SEND_AMOUNT = 1e-5			# don't do exchange requests that involve less than this amount of tBCH
EXCHANGE_REQUEST_EXPIRATION_SECONDS = 90	# expiration seconds for exchange requests

exchange_common = 'exchange-common.py'
with open(exchange_common) as f:
    code = compile(f.read(), exchange_common, 'exec')
    exec(code)

import threading

CC_TYPE_XCX_SIMPLE_BUY = 6
XMATCH_STATUS_ACCEPTED = 6
XMATCH_STATUS_PAID = 9

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
	# MIN_ALLOWED_PAYMENT_MINUTES prior to the payment deadline.
	# If not, there is a problem making BCH payments and this mining script will stop.

	@staticmethod
	def Check(s):
		r = do_rpc(s, Creda, 'cc.crosschain_match_action_list', (MIN_ALLOWED_PAYMENT_MINUTES, 'true'))
		for m in (r or ()):
			# cc.crosschain_match_action_list was called with following arguments:
			#	minutes_until_deadline = MIN_ALLOWED_PAYMENT_MINUTES and override_reminder_times = True
			# If any results are returned, BCH payments are not being made or dangerously close to not being made
			PayChecker.status_bad = True
			pprint.pprint(m)
			num = m['match-info']['match-number']
			print('BCH Payment Error: match %d not paid %d minutes before deadline\n' % (num, MIN_ALLOWED_PAYMENT_MINUTES), end='')
			do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', -1, 0))		# turn off retries

	@staticmethod
	def StatusIsBad():
		return PayChecker.status_bad

def pay_monitor_thread():
	interval = 120
	interval = int(min(interval, MIN_ALLOWED_PAYMENT_MINUTES * 60 / 2))
	print('pay_monitor_thread interval %d\n' % interval, end='')
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
	print('mine_thread interval %g\n' % interval, end='')
	s = requests.Session()
	backlog = 0
	deadline = 0
	last_time = time.time()
	while True:
		r = do_rpc(s, Creda, 'cc.crosschain_match_action_list')
		#pprint.pprint(r)
		if r is None:
			print('%d mine_thread unable to check match payment backlog\n' % time.time(), end='')
			time.sleep(20)
			continue
		if len(r) * interval > 60 * MAX_PAYMENT_BACKLOG_MINUTES:
			if backlog != len(r):
				backlog = len(r)
				print('%d mine_thread paused mining with match payment backlog of %d entries\n' % (time.time(), len(r)), end='')
			time.sleep(15)
			continue
		backlog = 0

		mins = 0
		allow = 0
		for e in (r or ()):
			#pprint.pprint(e)
			pi = e['payment-info']
			mins = pi['deadline-minutes']
			allow = pi['payment-time'] * MIN_PAYMENT_TIME_PERCENTAGE / (60 * 100)
			if mins < allow:
				break
		if mins < allow:
			if deadline != mins:
				deadline = mins
				print('%d mine_thread paused mining with match payment deadline %d < %d minutes\n' % (time.time(), mins, allow), end='')
			time.sleep(15)
			continue
		deadline = 0

		mine_one(s)

		sleep = interval * (0.5 + random.random())		# randomize to increase privacy
		sleep -= time.time() - last_time
		if sleep > 0:
			time.sleep(sleep)
		last_time = time.time()
		#print '%d mine_thread\n' % last_time,

def mine_one(s):

		# Report wallet balances

		bch_bal = get_balance(s, Foreign)
		pending = do_rpc(s, Creda, 'cc.exchange_requests_pending_totals', ('bch', ))

		mi = get_mining_info(s)

		if bch_bal is None:
			print('%d mine_thread error getting BCH wallet balance\n' % time.time(), end='')
			return
		if not pending:
			print('%d mine_thread error getting wallet pending balances\n' % time.time(), end='')
			return
		if not mi:
			print('%d mine_thread error getting CredaCash mining info\n' % time.time(), end='')
			return

		cc_bal = pending['wallet-balance']
		cc_pending = pending['sell-request-pending-totals']['base-amount']		# CredaCash tied up in sell requests
		cc_pending += pending['buy-request-pending-totals']['pledge-amount']	# CredaCash tied up in buy requests
		cc_net_bal = cc_bal + cc_pending

		market_rate = mi['mining-match-average-rate']

		report_rate = market_rate

		if Mining.min_exchg_rate:
			report_rate = max(report_rate, Mining.min_exchg_rate)
		if Mining.max_exchg_rate:
			report_rate = min(report_rate, Mining.max_exchg_rate)

		mined = mi['wallet-total-mined']
		total_in_cc = cc_net_bal + bch_bal/report_rate
		total_in_bch = total_in_cc * report_rate

		print('Mined %g Balances %g Creda + %g BCH at rate %g = %g Creda or %g BCH\n' \
			% (mined, cc_net_bal, bch_bal, report_rate, total_in_cc, total_in_bch), end='')
		#print 'Mined %g wallet %g pending %g total %g\n' \
		#	% (mined, cc_bal, cc_pending, cc_net_bal),

		# Determine the request rate required to mine.
		# In order to mine, the buy request rate needs to be slightly higher than the average buy req match rate required.
		# This script mines using mining trade requests, which, for the purpose of mining only (not for matching),
		# are counted as having twice the request rate, and therefore only require half the rate in order to mine
		# (this accounts for the "/ 2.0" in the calculation below).

		mining_rate = mi['mining-request-average-match-rate-required'] * 1.001	# slightly higher than the averate rate
		mining_rate /= 2.0														# only half required for trade requests

		# Determine the rate for the exchange requests.
		# The higher of the average exchange rate or the rate required to mine is used,
		# limited by the min and max values optionally set in the config file.

		match_rate = max(market_rate, mining_rate)

		if Mining.min_exchg_rate:
			match_rate = max(match_rate, Mining.min_exchg_rate)
		if Mining.max_exchg_rate:
			match_rate = min(match_rate, Mining.max_exchg_rate)

		# Check if the match_rate is high enough to mine, and if not, submit lower non-mining
		# requests in order to push down the average until it's low enough to mine at the requested rate.

		if match_rate < mining_rate:
			print('Requested match rate %g < required mining rate %g; submitting lower rate requests to push down the mining rate...\n' \
				% (match_rate, mining_rate), end='')

			# The desired match rate is not high enough to mine. Therefore, push down the rate required by submitting
			# requests at half the rate required to mine, limited by the min rate if one is specified in the config file.

			match_rate = mining_rate / 2.0
			if Mining.min_exchg_rate:
				match_rate = max(match_rate, Mining.min_exchg_rate)

		# Determine the exchange request amount

		min_amount = Mining.req_min_amt
		max_amount = Mining.req_max_amt

		if IsTestnet:
			min_amount = max(min_amount, BCH_TESTNET_MIN_SEND_AMOUNT / match_rate)
		else:
			min_amount = max(min_amount, BCH_MIN_SEND_AMOUNT / match_rate)

		min_amount = max(min_amount, 0.2 * mi['mining-match-average-amount'])
		max_amount = min(max_amount, 1.8 * mi['mining-match-average-amount'])

		max_amount = max(1, max_amount)

		if min_amount > max_amount:
			print('%d mine_thread skipping exchange requests; min_amount %g > max_amount %g\n' % (time.time(), min_amount, max_amount), end='')
			return

		max_amount = min(max_amount, (cc_bal - Mining.min_cc_bal) / (1 + mi['mining-request-pledge']/100.0))
		if max_amount < min_amount:
			print('%d mine_thread skipping exchange requests; insufficient CredaCash balance %g, minimum request amount %g\n' % (time.time(), cc_bal, min_amount), end='')
			return

		bch_allocated = pending['buy-request-pending-totals']['quote-amount']
		bch_net_bal = bch_bal - bch_allocated

		max_amount = min(max_amount, (bch_net_bal - Mining.min_bch_bal) / match_rate)
		if max_amount < min_amount:
			print('%d mine_thread skipping exchange requests; insufficient BCH free balance %g, minimum request amount %g rate %g\n' % (time.time(), bch_net_bal, min_amount, match_rate), end='')
			return

		amount = random.random() * (max_amount - min_amount) + min_amount

		rounding = 0
		while True:
			adj_amount = round_to_power(amount, rounding)
			#print(amount, rounding, adj_amount, min_amount, max_amount)
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
			print('%d mine_thread skipping exchange requests; unable to round %g; min request %g max request %g\n' % (time.time(), amount, min_amount, max_amount), end='')
			return

		amount = adj_amount

		# Create exchange request

		expiration = mi['mining-request-minimum-expiration-time'] + EXCHANGE_REQUEST_EXPIRATION_SECONDS

		submit_xreq(s, 'trade', amount, match_rate, expiration)	# send trade request

def check_foreign_wallet(s):
	r = get_balance(s, Foreign)
	if r is None:
		print('ERROR: no BCH wallet loaded; run the exchange-pay.py script before running this script')
		return False
	return True

def pay_monitor_startup(s):
	print('pay_monitor_startup')

	# init match pay check
	PayChecker.Init(s)

	# sanity check config
	mi = get_mining_info(s)
	#pprint.pprint(mi)
	global Mining, EXCHANGE_REQUEST_EXPIRATION_SECONDS, MIN_ALLOWED_PAYMENT_MINUTES

	lim = 10
	if Mining.min_cc_bal < lim:
		print('minimum CredaCash balance adjusted from', Mining.min_cc_bal, 'to', lim)
		Mining.min_cc_bal = lim

	lim = 0.0001
	if Mining.min_bch_bal < lim:
		print('minimum BCH balance adjusted from', Mining.min_bch_bal, 'to', lim)
		Mining.min_bch_bal = lim

	lim = mi['wallet-exchange-request-minimum-amount']
	lim = max(1, lim)
	if Mining.req_min_amt < lim:
		print('exchange request minimum amount adjusted from', Mining.req_min_amt, 'to', lim)
		Mining.req_min_amt = lim

	lim = Mining.req_min_amt
	if Mining.req_max_amt < lim:
		print('exchange request maximum amount adjusted from', Mining.req_max_amt, 'to', lim)
		Mining.req_max_amt = lim

	if Mining.req_min_amt > Mining.req_max_amt:
		print('Error: minimum request amount > maximum request amount')
		exit()

	lim = 90
	if EXCHANGE_REQUEST_EXPIRATION_SECONDS < lim:
		print('EXCHANGE_REQUEST_EXPIRATION_SECONDS adjusted from', EXCHANGE_REQUEST_EXPIRATION_SECONDS, 'to', lim)
		EXCHANGE_REQUEST_EXPIRATION_SECONDS = lim

	lim = 3
	if MIN_ALLOWED_PAYMENT_MINUTES < lim:
		print('MIN_ALLOWED_PAYMENT_MINUTES adjusted from', MIN_ALLOWED_PAYMENT_MINUTES, 'to', lim)
		MIN_ALLOWED_PAYMENT_MINUTES = lim

	if Mining.skip_pay_test:
		print('SKIPPING test of exchange autopay script')
		return True

	# submit matching exchange requests
	print('Testing exchange autopay script...')
	amount = mi['wallet-exchange-request-minimum-amount']
	rate = mi['mining-request-average-match-rate-required']
	rate = max(rate, 1e-5 / amount)
	if not submit_xreq(s, 'buy', amount, rate):
		return False
	for i in range(8):	# submit 8 sell reqs, to maximize chance that the buy req will find a match
		if not submit_xreq(s, 'sell', amount, rate):
			return False

	# wait for match
	print('Waiting for test match...')
	while True:
		time.sleep(30)
		PayChecker.FindTestMatch(s, amount)
		test_matchnum = PayChecker.TestMatchnumFound()
		if test_matchnum:
			break
	print('Found test match matchnum %d' % test_matchnum)

	# wait until match is paid by the CredaCash Exchange Autopay script
	print('Waiting for test match to be paid and confirmed...')
	while True:
		time.sleep(30)
		m = do_rpc(s, Creda, 'cc.exchange_match_info', (test_matchnum, ))
		mi = m['match-info']
		status = mi['status']
		if status == XMATCH_STATUS_PAID:
			print('Test match paid')
			return True
		elif status > XMATCH_STATUS_ACCEPTED:
			print('ERROR: test match was not paid')
			return False

def start_thread(target, args=()):
	t = threading.Thread(target=target, args=args)
	t.daemon = True
	t.start()
	return t

def main(argv):

	if len(argv) > 2:
		print()
		print('Usage: python exchange-mine.py [<config_file>]')
		print()
		exit()

	if len(argv) > 1:
		conf_file = argv[1]
	else:
		conf_file = 'exchange-mine.conf'

	parse_config(conf_file, True)

	if Foreign.currency != 'bch':
		print()
		print('Foreign currency for mining must be bch')
		print()
		exit()

	if Foreign.type != BITCOIND:
		print()
		print('Foreign server type for mining must be', BITCOIND)
		print()
		exit()

	if Mining.reqs_per_hr > 120:
		print
		print(RATE_WARNING)
		print
		time.sleep(20)

	print('maximum exchange rate: ' + ('%g' % Mining.max_exchg_rate, 'none')[Mining.max_exchg_rate == 0])
	print('minimum exchange rate: ' + ('%g' % Mining.min_exchg_rate, 'none')[Mining.min_exchg_rate == 0])
	if Mining.min_exchg_rate and Mining.max_exchg_rate and Mining.min_exchg_rate > Mining.max_exchg_rate:
		print('Error: minimum exchange rate > maximum exchange rate')
		exit()
	print()

	start_time = time.time()
	print('start time %d' % start_time)

	s = requests.Session()

	check_if_testnet(s)

	if not check_foreign_wallet(s):
		exit()

	if not pay_monitor_startup(s):
		print('ERROR: startup failed')
		exit()

	t1 = start_thread(pay_monitor_thread)
	t2 = start_thread(mine_thread)

	if not hasattr(t1,'is_alive'):
		t1.is_alive = t1.isAlive
		t2.is_alive = t2.isAlive

	while True:
		time.sleep(2)
		if not t1.is_alive():
			if PayChecker.StatusIsBad():
				print()
				print(MINING_ABORT)
				print()
			else:
				print('ERROR: pay monitor thread exited\n', end='')
			exit()
		if not t2.is_alive():
			print('ERROR: mining thread exited\n', end='')
			exit()

if __name__ == '__main__':
	main(sys.argv)
