#!/usr/bin/env python2

'''
CredaCash(TM) Wallet Test Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors

This script runs "burn-in" tests against the CredaCash wallet

Note: when doing automated testing, it is helpful to:
	for debugging, compile with TEST_LOG_BALANCE = 1
	for more speed, compile with TEST_SKIP_ZKPROOFS = 1 and *_work_difficulty = 0
	for more speed, store the databases on a ram disk
	when RTEST_TX_ERRORS is enabled, the simulated error in CreateTxMint can be disabled so the wallets get their full balances
	set tx-polling-threads = 20
	set wallet-rpc-conns = 40 to 200
	set transact-conns on the tx server to the sum over all wallets of the polling threads and rpc conns
	set witness-test-block-random-ms = 3000
	set trace = 4 for both the wallet and tx server
	set trace-polling = 1

When testing, check that no funds are created or lost (total amount minted - total donations = total wallet balances)
	Note, total donations can be found with:
		tail -n 1000 ccnode.out | grep donation | tail

Testing scenarios:
(A) One wallet with one destination, wallet data file copied to make a second wallet.  Both wallets sending payments to the
single destinations, with poll_thread and tnum sleep enabled in this script.
(B) Two wallets, each with one destination, both wallets sending payments to both destinations, with poll_thread enabled in
this script. Note: this script must be started close to simultaneously on both wallets to get past the destination reuse check.
Also, to recover the full wallet balances, it may be necessary to use the command cc.billets_release_allocated and then
poll both destinations with cc.destination_poll.
(C) Two wallets, each with two destinations, both wallets sending payments of random amounts to themselves and the other wallet,
without duplicating destinations.  Do this with donations enabled and disabled, with outvalmin set to 0 and 23.
- Above tests with and without cc.billets_release_allocated and RTEST_ALLOW_DOUBLE_SPENDS enabled.
- Above tests with RTEST_TX_ERRORS enabled, RTEST_CUZZ enabled, AppVerifier enabled.
- Above tests with witness-test-block-random-ms=3000, with nwitnesses=1 and nwitnesses=2.
- Above tests with and without TEST_RANDOM_POLLING 16.
- Above tests with and without donations enabled.
- Above test with an without TEST_FEWER_RETRIES (especially when outvalmin=0).
Note: if RTEST_TX_ERRORS is enabled and poll_thread is not enabled, set tx-polling-addresses=100
Note: run tests with minimal logging, and check logs for errors and warnings.
'''

import sys
import requests
import json
import random
import codecs
import time
import threading
import logging
import pprint

if not sys.version.startswith('2.7.') or not ('GCC' in sys.version or '64 bit' in sys.version or 'AMD64' in sys.version):
	print 'ERROR: This script requires Python 2.7.x (64 bit version).'
	exit()

TEST_THREAD_SLEEP = False
TEST_POLL_DESTINATION = False

# Using the below test options may require manual polling to recover the full wallet balances.
# The manual polling commands are:
#		cc.billets_release_allocated
#		cc.billets_poll_unspent
#		cc.destination_poll <dest1>
#		cc.destination_poll <dest2>
TEST_RELEASE_ALLOCATED = False
TEST_ABANDON_TXS = 0 #True
TEST_CANCEL_TXS = 0 #True

# CRITICAL, ERROR, WARNING, INFO, DEBUG
logging.basicConfig(level=logging.WARNING, format='[%(levelname)s] (%(threadName)-10s) %(message)s')

rpc_user = 'rpc'
rcp_pass = 'pwd'

min_exponent = -30

def do_rpc(s, method, params=()):
	req = '{"method":"' + method + '","params":['
	for i in range(len(params)):
		if i: req += ','
		req += params[i]
	req += ']}'
	logging.info('performing rpc request %s' % req)
	try:
		r = s.post('http://127.0.0.1:%d' % rpc_port, auth=(rpc_user, rcp_pass), data=req)
	except:
		logging.critical('rpc post failed req %s' % req)
		return None
	#logging.debug('rpc done')
	if r.status_code != 200:
		logging.critical('rpc status code %d' % r.status_code)
	logging.debug('rpc response %s' % r.text.encode('ascii', 'backslashreplace'))
	if method.startswith('cc.dump'):
		return None
	try:
		j = json.loads(r.text)
		rv = j['result']
		if rv is None:
			e = j['error']['message']
			if e != 'Insufficient funds':
				print 'rpc port', rpc_port, 'result "%s"' % e, 'req', req
		return rv
	except:
		#pprint.pprint(r)
		if hasattr(r, 'text'):
			print 'rpc port', rpc_port, 'json load failed "%s"' % r.text.encode('ascii', 'backslashreplace'), 'req', req
		else:
			print 'rpc port', rpc_port, 'no text returned for req', req
		return None

def poll_thread(tnum):
	global stime
	logging.debug('poll_thread start tnum %d tgroup %d port %s' % (tnum, tgroup, rpc_port))
	s = requests.Session()
	while True:
		stime = time.time() + 600
		time.sleep(600)
		dt = stime - time.time()
		if dt > 0:
			time.sleep(dt)
		destination = destinations[0]
		print 'time', int(time.time()+0.5), 'thread', tnum, 'polling destination'
		do_rpc(s, 'cc.destination_poll', (destination, '200'))

def wallet_thread(tnum):
	logging.debug('wallet_thread start tnum %d tgroup %d port %s' % (tnum, tgroup, rpc_port))
	s = requests.Session()
	for iter in range(nmint):
		do_rpc(s, 'cc.mint')
	if prob_tx < 0:
		print 'thread', tnum, 'done'
		return
	sleep_num = 1
	sleep_period = 500
	iter = 0
	while True:
		iter += 1
		now = time.time()
		sleep_start = start_time + (sleep_num*2 - tgroup) * sleep_period
		if tnum > 1 and now >= sleep_start and TEST_THREAD_SLEEP:
			sduration = sleep_period
			if tgroup and sleep_num < 2:
				sduration /= 2
			if tnum == 2:
				print 'time', int(now+0.5),'thread', tnum, 'group', tgroup, 'sleep_num', sleep_num, 'sleeping', sduration
			time.sleep(sduration)
			sleep_num += 1
			if tnum == 2:
				print 'time', int(time.time()),'thread', tnum, 'group', tgroup, 'done sleeping'
		method = random.randrange(7)
		if not random.randrange(50 * nthreads) and tgroup and TEST_RELEASE_ALLOCATED:
			print 'time', int(time.time()),'thread', tnum, 'group', tgroup, 'cc.billets_release_allocated'
			do_rpc(s, 'cc.billets_release_allocated')	# will cause "BilletSpendInsert constraint violation" warnings
		elif random.random() < prob_tx:
			destination = destinations[random.randrange(len(destinations))]
			if random.randrange(8):
				amount = random.randrange(15*1000) + 1
			else:
				amount = random.randrange(7500*1000) + 1
			#amount = random.randrange(10) + 1	# for testing
			#amount = random.randrange(100)		# for testing
			amount = str(amount) + '.'
			digits = random.randrange(40) + 1
			#digits = random.randrange(10) + 1	# for testing
			#digits = 4							# for testing
			for i in range(digits):
				amount += str(random.randrange(10))
			exponent = random.randrange(1 - min_exponent)
			#exponent = 0						# for testing
			amount += 'e-' + str(exponent)
			if random.random() < 0.5:
				amount = '1'
			#amount = '1' # for testing
			#amount = str((iter*10 + tnum) / 1000000.)
			#print amount
			txid = do_rpc(s, 'sendtoaddress', (destination, amount))
			#if txid: do_rpc(s, 'cc.transaction_cancel', ('"' + txid + '"', )) # for testing
			for i in range(10):
				if not txid or random.random() < 0.4:
					break
				if txid and random.random() < 0.5 and TEST_ABANDON_TXS:
					do_rpc(s, 'abandontransaction', ('"' + txid + '"', ))
				if txid and random.random() < 0.5 and TEST_CANCEL_TXS:
					do_rpc(s, 'cc.transaction_cancel', ('"' + txid + '"', ))
		elif method == 0:
			do_rpc(s, 'getbalance')
		elif method == 1:
			do_rpc(s, 'getblockcount')
		elif method == 2:
			do_rpc(s, 'listtransactions')
		elif method == 3:
			do_rpc(s, 'cc.billets_poll_unspent')
		elif method == 4:
			do_rpc(s, 'cc.dump_transactions', ('0', '100', 'true'))
		elif method == 5:
			do_rpc(s, 'cc.dump_billets', ('0', '100', 'true'))
		elif method == 6:
			do_rpc(s, 'cc.dump_tx_build')
		else:
			raise Exception
	logging.debug('thread end')

def main(argv):
	global rpc_port, nthreads, nmint, prob_tx, destinations, start_time, tgroup

	if len(argv) < 6:
		print
		print 'Usage: wallet-burn.py <rpc_port> <nthreads> <nmint> <prob_tx> <pay_destination_1> <pay_destination_2> ...'
		print
		print 'Note: This script will only work if ccwallet is started with --wallet-rpc=1 --wallet-rpc-password=pwd'
		print
		exit()

	rpc_port = int(argv[1])

	nthreads = int(argv[2])

	nmint = int(argv[3])

	prob_tx = float(argv[4])

	destinations = []
	for i in range(len(argv)-5):
		destinations.append('"' + argv[i+5] + '"')

	#print 'destinations =', destinations
	#exit()

	tgroup = rpc_port <= 9999

	start_time = int(time.time())
	print 'start_time', start_time

	for i in range(nthreads + 2 * TEST_POLL_DESTINATION):
		if i < nthreads:
			t = threading.Thread(target=wallet_thread, args=(i,))
		else:
			t = threading.Thread(target=poll_thread, args=(i,))
		t.daemon = True
		t.start()

	try:
		while True:
			time.sleep(2)
	except KeyboardInterrupt:
		exit()

if __name__ == '__main__':
	main(sys.argv)
