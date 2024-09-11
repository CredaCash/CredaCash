#!/usr/bin/env python2

'''
CredaCash(TM) Exchange Test Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors

This script runs "burn-in" tests against the CredaCash exchange functions

Note: when doing automated testing, it is helpful to:
	for more speed, compile with TEST_SKIP_ZKPROOFS = 1 and *_work_difficulty = 0
	for more speed, store the databases on a ram disk
	set tx-polling-threads = 20
	set exchange-poll-interval = 30
	set wallet-rpc-conns = 40 to 200
	set transact-conns on the tx server to the sum over all wallets of the polling threads and rpc conns

Testing baseline scenario:
One CredaCash node/tx server. Bitcoin and Bitcoin Cash core nodes with wallets enabled.
Two CredaCash wallets.  Two instances of this test script, each creating exchange requests on one of the wallets.
	initial script launch:
			exchange-burn.py 29234 18332 18333 2 200 0.9
	stop and relaunch without minting:
			exchange-burn.py 29234 18332 18333 10 0 0.9
	stop and relaunch only to finish paying pending matches:
			exchange-burn.py 29234 18332 18333 0 0 0.9

	initial script launch with one currency only to test foreign address reuse:
		exchange-burn.py 29234 0 18333 2 200 0.9

Check logs for errors and warnings:
	sed -i -e "s/,\"error\":null//" tempw.out & attrib -r tempw.out
	grep -E "Error|error|Warn|warn" tempw.out > errw.out
	sed -i -e "s/.*\]  *//" errw.out & attrib -r errw.out
	sort -u -b errw.out > errws.out
Check that no funds are created or lost (total amount minted + total mined - total donations = total wallet balances).
	Note, total donations can be found with:
		tail -n 1000 ccnode.out | grep donation | tail
Check that total mined as tracked by node software = sum wallet mined as tracked by each wallet.
Check request dispositions:
	sqlite3 Z:\node\CCNode.db
	select distinct Type,Disposition from Exchange_Match_Reqs order by Type,Disposition;

Other testing scenarios:
- TEST_PARTIAL_PAYMENT = 0, check status of matches:
	sqlite3 Z:\node\CCNode.db
	select distinct Xmatchnum%10,Status from Exchange_Matches order by Xmatchnum%10,Status;
- TEST_PULSE_DURATION = 4, and one wallet with TEST_PULSE_PERIOD = 27, the other with TEST_PULSE_PERIOD = 28
- retest with wallet compiled with TEST_RANDOM_POLLTIMES 600
- run second CredaCash node with intermittent stop, restart and resync enabled as follows:
	enable this source code line: new thread(ShutdownTestThreadProc);
	batch file to run second node:
		:restart
		timeout /t 8 > nul
		ccnode --config ccnode.conf --datadir z:\node1 --baseport 9333 --transact 1 --relay-in 6 --relay-out 4 --witness-index -1 --privrelay-host-index 0 %1 %2 %3 %4 %5 %6 %7 %8 %9
		if not exist ccnode.kill goto restart
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

# CRITICAL, ERROR, WARNING, INFO, DEBUG
logging.basicConfig(level=logging.WARNING, format='[%(levelname)s] (%(threadName)-10s) %(message)s')

TEST_WIDE_RANGE = False
TEST_MINING_ONLY = False
TEST_SKIP_PAYMENT = 0
TEST_PARTIAL_PAYMENT = True
TEST_PULSE_DURATION = 0 # 4
TEST_PULSE_PERIOD = 27

PROB_CROSSCHAIN_QUERY_REQUESTS = 0.05
PROB_FOREIGN_ADDR_REUSE = 0.1 # note: doesn't work with two blockchains

# regtest
#EXPIRATION_MIN = 3*60
#EXPIRATION_RANGE = 0
#foreign_block_time = 5 

#testnet
EXPIRATION_MIN = 5*60	# POW gen time + hold time + minimum active time
EXPIRATION_RANGE = 5*60 + 60
foreign_block_time = 10*60

rpc_user = 'rpc'
rcp_pass = 'pwd'

def rand_amount():
	#return 10
	amts = (1,2,3,5,7)
	amount = amts[random.randrange(len(amts))]
	amount *= random.randrange(2) * 9 + 1
	amount *= random.randrange(2) * 9 + 1
	if TEST_WIDE_RANGE:
		amount *= random.randrange(2) * 9 + 1
		amount *= random.randrange(2) * 9 + 1
	#print "\n%g\n" % amount,
	return amount

def do_rpc(s, port, method, params=()):
	req = '{"id":0,"method":"' + method + '","params":['
	for i in range(len(params)):
		if i: req += ','
		p = params[i]
		if isinstance(p, basestring) and p != 'true' and p != 'false':
			req += '"' + (params[i]) + '"'
		else:
			req += str(params[i])
	req += ']}'
	logging.info('performing rpc request %s' % req)
	try:
		r = s.post('http://127.0.0.1:%d' % port, auth=(rpc_user, rcp_pass), data=req)
	except:
		logging.critical('rpc port %d post failed req %s' % (port, req))
		return None
	#logging.debug('rpc done')
	if r.status_code != 200:
		logging.critical('rpc port %d status code %d req %s' % (port, r.status_code, req))
	logging.debug('rpc response %s' % r.text.encode('ascii', 'backslashreplace'))
	if method.startswith('cc.dump'):
		return None
	try:
		j = json.loads(r.text)
		rv = j['result']
		if rv is None:
			e = j['error']['message']
			if e != 'Insufficient funds':
				print 'rpc port', port, 'result "%s"' % e, 'req', req
		return rv
	except:
		#pprint.pprint(r)
		if hasattr(r, 'text'):
			print 'rpc port', port, 'json load failed "%s"' % r.text.encode('ascii', 'backslashreplace'), 'req', req
		else:
			print 'rpc port', port, 'no text returned for req', req
		return None

def wallet_thread(tnum, btc_port, bch_port):
	logging.debug('wallet_thread start tnum %d tgroup %d port %s' % (tnum, tgroup, rpc_port))
	s = requests.Session()
	for iter in range(nmint):
		do_rpc(s, rpc_port, 'cc.mint')
	if prob_tx < 0:
		print 'thread', tnum, 'done'
		return
	if nmint:
		time.sleep(10)
	foreign_address_reuse = []
	#for iter in range(8000 / nthreads):
	#for iter in range(2000 / nthreads):
	#for iter in range(800 / nthreads):
	while True:
		if TEST_PULSE_DURATION:
			toffset = time.time() % TEST_PULSE_PERIOD
			if toffset > TEST_PULSE_DURATION:
				time.sleep(TEST_PULSE_PERIOD - toffset)
		method = random.randrange(10)
		if random.random() < prob_tx:
			if TEST_MINING_ONLY:
				type = 's'
			else:
				type = 'sn'[random.randrange(2)]
			req = 'bsst'[random.randrange(4)]
			#if rpc_port < 9999:
			#	req = 's' # for testing
			#else:
			#	req = 'b' # for testing
			min_amount = rand_amount()
			max_amount = rand_amount()
			if min_amount > max_amount:
				min_amount, max_amount = max_amount, min_amount
			#if req == 'b': min_amount = max_amount = 10	# test big buys with small sells, and vice-versa
			#if req == 's': min_amount = max_amount = 100
			#rate = random.random() * (random.randrange(2) * 10 + 1)
			rate = random.random() * 0.1
			costs = max_amount * rate * random.random()
			if req == 't' and random.randrange(8):
				type = 'm'
			if req == 't' and random.randrange(8):
				costs = 0
			if req == 't' and random.randrange(8):
				min_amount = max_amount
			if not TEST_WIDE_RANGE:
				costs /= 100.0
			if TEST_MINING_ONLY or not btc_port or (bch_port and random.randrange(2)):
				asset = 'bch'
				forn_port = bch_port
			else:
				asset = 'btc'
				forn_port = btc_port
			if random.random() < PROB_CROSSCHAIN_QUERY_REQUESTS:
				count = random.randrange(40)
				offset = random.randrange(40)
				incl = ('true', 'false')[random.randrange(2)]
				# cc.crosschain_query_requests \\\"simple_buy|simple_sell|naked_buy|naked_sell\\\" min_amount max_amount rate costs cryptoasset ( count offset include_pending_matched ) - query crosschain exchange requests
				do_rpc(s, rpc_port, 'cc.crosschain_query_requests', (type+req, min_amount, max_amount, rate, costs, asset, count, offset, incl))
				continue
			i = len(foreign_address_reuse)
			if req == 'b' and random.randrange(8) or not random.randrange(8):
				foreign_address = ''
			elif i and random.random() < PROB_FOREIGN_ADDR_REUSE:
				if random.random() < PROB_FOREIGN_ADDR_REUSE:
					i -= 1
				else:
					i = random.randrange(i)
				foreign_address = foreign_address_reuse.pop(i)
				#print 'reusing foreign_address', foreign_address
			else:
				while True:
					foreign_address = do_rpc(s, forn_port, 'getnewaddress')
					if not foreign_address:
						continue
					lc = foreign_address.lower()
					if not 'err' in lc and not 'warn' in lc and not 'fail' in lc:
						break
			if len(foreign_address) and random.random() < PROB_FOREIGN_ADDR_REUSE:
				foreign_address_reuse.append(foreign_address)
			expiration = EXPIRATION_MIN
			if EXPIRATION_RANGE:
				expiration += random.randrange(EXPIRATION_RANGE)
			#expiration = expiration + time.time() # for testing
			#expiration = int(expiration - time.time() - (expiration % 100)) # for testing
			#print int(expiration + time.time())
			wait_discount = random.random()
			if req == 't' and random.randrange(8):
				wait_discount = 1
			# cc.crosschain_request_create reference_id \\\"simple_buy|simple_sell|naked_buy|naked_sell\\\" min_amount max_amount rate costs cryptoasset ( unique_foreign_address expiration wait_discount )
			txid = do_rpc(s, rpc_port, 'cc.crosschain_request_create', ('', type+req, min_amount, max_amount, rate, costs, asset, foreign_address, expiration, wait_discount))
			print '%d cc.crosschain_request_create txid %s %s %d %d %f %f %s %s %d %f' % (time.time(), txid, type+req, min_amount, max_amount, rate, costs, asset, foreign_address, expiration, wait_discount)
			if not txid:
				time.sleep(1)
		elif method == 0:
			do_rpc(s, rpc_port, 'getbalance')
		elif method == 1:
			do_rpc(s, rpc_port, 'getblockcount')
		elif method == 2:
			do_rpc(s, rpc_port, 'listtransactions')
		elif method == 3:
			do_rpc(s, rpc_port, 'cc.billets_poll_unspent')
		elif method == 4:
			do_rpc(s, rpc_port, 'cc.dump_transactions', (0, 100, 'true'))
		elif method == 5:
			do_rpc(s, rpc_port, 'cc.dump_billets', (0, 100, 'true'))
		elif method == 6:
			do_rpc(s, rpc_port, 'cc.dump_tx_build')
		elif method == 7:
			do_rpc(s, rpc_port, 'cc.dump_exchange_requests', (0, 100, 'true'))
		elif method == 8:
			do_rpc(s, rpc_port, 'cc.dump_exchange_matches', (0, 100, 'true', 'true'))
		elif method == 9:
			do_rpc(s, rpc_port, 'cc.exchange_query_mining_info')
		else:
			raise Exception
	logging.critical('thread %d ended' % tnum)

def forn_gen_thread(forn_port):
	if not forn_port:
		return
	logging.debug('forn_gen_thread')
	s = requests.Session()
	# bitcoin-cli createwallet test
	# bitcoin-cli getnewaddress
	# bitcoin-cli generatetoaddress 200 <addr>
	do_rpc(s, forn_port, 'unloadwallet', ('', ))
	do_rpc(s, forn_port, 'createwallet', ('test', ))
	do_rpc(s, forn_port, 'loadwallet', ('test', ))
	last_time = time.time()
	nblocks = 400
	while True:
		time.sleep(1)
		if time.time() - last_time > foreign_block_time:
			last_time += foreign_block_time
			addr = do_rpc(s, forn_port, 'getnewaddress')
			if not addr:
				continue
			txid = do_rpc(s, forn_port, 'generatetoaddress', (nblocks, addr))
			nblocks = 1
			#print 'new block result', txid
			#print 'new block'

def forn_pay_thread(btc_port, bch_port):
	logging.debug('forn_pay_thread')
	s = requests.Session()
	while True:
		time.sleep(1)
		#t0 = time.clock()
		r = do_rpc(s, rpc_port, 'cc.crosschain_match_action_list')
		#t1 = time.clock()
		if r and len(r):
			print '%d cc.crosschain_match_action_list %d entries' % (time.time(), len(r))
		for e in (r or ()):
			#pprint.pprint(e)
			if not 'payment-info' in e:
				print 'missing payment-info in', e
			else:
				# cc.crosschain_match_mark_paid match_number ( foreign_txid reminder_minutes minimum_advance_minutes )
				# cc.crosschain_payment_claim reference_id match_number ( foreign_block_id foreign_payment_identifier amount reminder_minutes minimum_advance_minutes )
				num = e['match-info']['match-number']
				bc   = e['payment-info']['payment-asset']
				amt  = e['payment-info']['payment-amount']
				addr = e['payment-info']['payment-address']
				conf = e['payment-info']['payment-confirmations-required']
				line = e['payment-info']['deadline']
				paid = e['payment-info']['wallet-marked-as-paid']
				full_amt = amt
				if bc == 'btc':
					forn_port = btc_port
				elif bc == 'bch':
					forn_port = bch_port
				else:
					raise Exception
				if TEST_SKIP_PAYMENT and not num % TEST_SKIP_PAYMENT:
					amt = 0
				elif TEST_PARTIAL_PAYMENT:
					frac = num % 10
					if frac > 5:
						frac += 3
					amt = round(amt*frac/10.0, 8)
				if not amt:
					do_rpc(s, rpc_port, 'cc.crosschain_match_mark_paid', (num, 'skip', -1, 0))
				elif not paid:
					forn_txid = do_rpc(s, forn_port, 'sendtoaddress', (addr, amt))
					if not forn_txid:
						print 'error: sendtoaddress', addr, amt, 'forn_txid = ', forn_txid
					else:
						#print 'paid', num, 'amount', amt, 'address', addr, 'forn_txid', forn_txid, 'deadline', int(line - time.time())
						print '%d paid %d %g %s %s %d\n' % (time.time(), num, amt, addr, forn_txid, line - time.time()),
						do_rpc(s, rpc_port, 'cc.crosschain_match_mark_paid', (num, forn_txid, ((conf + 1) * foreign_block_time + 20)/60.0, 1))
				else:
					forn_txid = e['payment-info']['foreign-payment-txid']
					e = do_rpc(s, forn_port, 'gettransaction', (forn_txid, ))
					if not 'confirmations' in (e or ()):
						print 'error: unable to get blockheight for payment', num, 'forn_txid', forn_txid
						print e
					else:
						#pprint.pprint(e)
						if e['confirmations'] < conf:
							#print 'payment for', num, 'forn_txid', forn_txid, 'confirmations', e['confirmations'], 'required', conf, 'deadline', int(line - time.time())
							do_rpc(s, rpc_port, 'cc.crosschain_match_mark_paid', (num, '', 5, 1))
						elif time.time() > line:
							do_rpc(s, rpc_port, 'cc.crosschain_match_mark_paid', (num, '', -1, 0))
						else:
							blockheight = None
							if 'blockheight' in e:
								blockheight = e['blockheight']
							else:
								e = do_rpc(s, forn_port, 'getblockheader', (e['blockhash'], ))
								#pprint.pprint(e)
								if 'height' in e:
									blockheight = e['height']
								else:
									print 'error: unable to get height for payment', num, 'forn_txid', forn_txid
									print e
							if blockheight:
								txid = do_rpc(s, rpc_port, 'cc.crosschain_payment_claim', ('', num, blockheight))
								if amt < full_amt:
									if txid:
										raise Exception
									txid = do_rpc(s, rpc_port, 'cc.crosschain_payment_claim', ('', num, blockheight, '', amt))
								print '%d cc.crosschain_payment_claim %d result %s dl %d\n' % (time.time(), num, txid ,line - time.time()),
								if not txid:
									do_rpc(s, rpc_port, 'cc.crosschain_match_mark_paid', (num, '', 1, 1))
								else:
									do_rpc(s, rpc_port, 'cc.crosschain_match_mark_paid', (num, '', -1, 0))

def start_thread(target, args=()):
	t = threading.Thread(target=target, args=args)
	t.daemon = True
	t.start()

def main(argv):
	global rpc_port, nthreads, nmint, prob_tx, start_time, tgroup

	if len(argv) != 7:
		print
		print 'Usage: exchange-burn.py <rpc_port> <btc_port> <bch_port> <nthreads> <nmint> <prob_tx>'
		print
		print 'Note: This script will only work if ccwallet is started with --wallet-rpc=1 --wallet-rpc-password=pwd'
		print
		exit()

	rpc_port = int(argv[1])

	btc_port = int(argv[2])

	bch_port = int(argv[3])

	nthreads = int(argv[4])

	nmint = int(argv[5])

	prob_tx = float(argv[6])

	tgroup = rpc_port <= 9999

	global PROB_FOREIGN_ADDR_REUSE
	if btc_port and bch_port and PROB_FOREIGN_ADDR_REUSE:
		print "Using two blockchains --> PROB_FOREIGN_ADDR_REUSE set to zero"
		PROB_FOREIGN_ADDR_REUSE = 0;

	start_time = int(time.time())
	print 'start_time', start_time

	for i in range(nthreads):
		start_thread(wallet_thread, (i, btc_port, bch_port))

	if btc_port:
		start_thread(forn_gen_thread, (btc_port, ))

	if bch_port:
		start_thread(forn_gen_thread, (bch_port, ))

	if TEST_SKIP_PAYMENT != 1:
		start_thread(forn_pay_thread, (btc_port, bch_port))

	try:
		while True:
			time.sleep(2)
	except KeyboardInterrupt:
		exit()

if __name__ == '__main__':
	main(sys.argv)
