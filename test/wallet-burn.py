'''
CredaCash(TM) Wallet Test Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2019 Creda Software, Inc.

This script runs "burn-in" tests against the CredaCash wallet

Note: when doing automated testing, it is helpful to:
	set the donation params to zero, so no funds are lost and balances can easily be checked
		or alternately, set TEST_FAIL_ALL_TXS to 1 so balance should not change (after all failed tx's are reverted)
	for debugging, compile with TEST_LOG_BALANCE = 1
	for more speed, compile with TEST_SKIP_ZKPROOFS = 1 and *_work_difficulty = 1
	for more speed, store the databases on a ram disk
	to test more complex tx's, compile with proof_params.outvalmin = 0
	test with and without TEST_RANDOM_TX_ERRORS
	when TEST_RANDOM_TX_ERRORS is enabled:
			set tx-polling-addresses = 200 so no payments are missed
			set polling_table[SECRET_TYPE_POLL_ADDRESS nothing received] to clear faster
			the simulated error in CreateTxMint can be disabled so the wallets get their full intended balances
	set tx-polling-threads = 20
	set wallet-rpc-conns = 40 to 200
	set transact-conns on the tx server to the sum over all wallets of the polling threads and rpc conns
	set witness-test-block-random-ms = 3000
	set trace = 4 for both the wallet and tx server
	set trace-polling = 1
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

# CRITICAL, ERROR, WARNING, INFO, DEBUG
logging.basicConfig(level=logging.WARNING, format='[%(levelname)s] (%(threadName)-10s) %(message)s')

rpc_user = 'rpc'
rcp_pass = 'pwd'

min_exponent = -30

def do_rpc(s, method, params):
	req = '{"method":"' + method + '","params":['
	for i in range(len(params)):
		if i: req += ','
		req += params[i]
	req += ']}'
	logging.info('performing rpc request %s' % req)
	r = s.post('http://127.0.0.1:%d' % rpc_port, auth=(rpc_user, rcp_pass), data=req)
	#logging.debug('rpc done')
	if r.status_code != 200:
		logging.critical('rpc status code %d' % r.status_code)
	logging.debug('rpc response %s' % r.text)

def wallet_thread():
	logging.debug('thread start port %s' % rpc_port)
	s = requests.Session()
	n = 0
	while n < nmint:
		do_rpc(s, 'cc.mint', ())
		n += 1
	if prob_tx < 0:
		return
	while True:
		method = random.randrange(3)
		if random.random() < prob_tx:
			destination = destinations[random.randrange(len(destinations))]
			amount = random.randrange(15*1000) + 1
			#amount = random.randrange(10) + 1		# for testing
			amount = str(amount) + '.'
			digits = random.randrange(40) + 1
			#digits = random.randrange(10) + 1		# for testing
			for i in range(digits):
				amount += str(random.randrange(10))
			exponent = random.randrange(1 - min_exponent)
			amount += 'e-' + str(exponent)
			#amount = '5000'						# for testing
			do_rpc(s, 'sendtoaddress', (destination, amount))
		elif method == 0:
			do_rpc(s, 'getbalance', ())
		elif method == 1:
			do_rpc(s, 'getblockcount', ())
		else:
			do_rpc(s, 'listransactions', ())
	logging.debug('thread end')

def main(argv):
	global rpc_port, nthreads, nmint, prob_tx, destinations

	if len(argv) < 6:
		print
		print 'Usage: wallet-burn.py <rpc_port> <nthreads> <nmint> <prob_tx> <pay_destination_1> <pay_destination_2> ...'
		print
		print 'Note: This script will only work if ccwallet is stsrted with --wallet-rpc-password=pwd'
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

	for i in range(nthreads):
		t = threading.Thread(target=wallet_thread)
		t.daemon = True
		t.start()

	try:	
		while True:
			time.sleep(2)
	except KeyboardInterrupt:
		exit()

if __name__ == '__main__':
	main(sys.argv)
