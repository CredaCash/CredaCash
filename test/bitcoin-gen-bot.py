#!/usr/bin/env python2

'''
CredaCash(TM) Exchange Test Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors

Generate blocks for bitcoin/bitcoincash regtest network
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
TEST_MINING_ONLY = False
TEST_SKIP_PAYMENT = 0
TEST_PARTIAL_PAYMENT = True

# CRITICAL, ERROR, WARNING, INFO, DEBUG
logging.basicConfig(level=logging.WARNING, format='[%(levelname)s] (%(threadName)-10s) %(message)s')

rpc_user = 'rpc'
rcp_pass = 'pwd'

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

def forn_gen_thread(forn_port, block_secs, gen_addr):
	logging.debug('forn_gen_thread')
	s = requests.Session()
	do_rpc(s, forn_port, 'unloadwallet', ('', ))
	do_rpc(s, forn_port, 'createwallet', ('test', ))
	do_rpc(s, forn_port, 'loadwallet', ('test', ))
	mine_addr = do_rpc(s, forn_port, 'getnewaddress')
	print 'mine_addr', mine_addr
	if gen_addr is None:
		gen_addr = mine_addr
	bal = do_rpc(s, forn_port, 'getbalance')
	nblocks = int((2000-bal)/50)
	print 'balance', bal, 'nblocks', nblocks
	if nblocks > 0:
		txid = do_rpc(s, forn_port, 'generatetoaddress', (nblocks, mine_addr))
		txid = do_rpc(s, forn_port, 'generatetoaddress', (100, gen_addr))
	last_time = time.time()
	last_bal = bal
	while True:
		while time.time() - last_time < block_secs:
			time.sleep(1)
		last_time += block_secs
		bal = do_rpc(s, forn_port, 'getbalance')
		if bal != last_bal:
			print 'balance', bal
			last_bal = bal
		txid = do_rpc(s, forn_port, 'generatetoaddress', (1, gen_addr))
		#print 'new block result', txid

def main(argv):
	global rpc_port, nthreads, nmint, prob_tx, start_time, tgroup

	if len(argv) < 3 or len(argv) > 4:
		print
		print 'Usage: bitcoin-gen-bot.py <btc_port> <block_secs> [ <address> ]'
		print
		exit()

	btc_port = int(argv[1])
	block_secs = int(argv[2])

	if len(argv) > 3:
		gen_addr = argv[3]
	else:
		gen_addr = None

	forn_gen_thread(btc_port, block_secs, gen_addr)

if __name__ == '__main__':
	main(sys.argv)
