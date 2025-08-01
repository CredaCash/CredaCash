#!/usr/bin/env python2

'''
CredaCash(TM) Exchange Autopay Burn-in Test

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors

'''

import sys
import os
import subprocess
import traceback
import requests
import json
import random
import time
import pprint

if not sys.version.startswith('2.7.') or not ('GCC' in sys.version or '64 bit' in sys.version or 'AMD64' in sys.version):
	print 'ERROR: This script requires Python 2.7.x (64 bit version).'
	exit()

NOM_RATE = 0.0001
FORN_SELL_COSTS = 0*0.00003
FORN_BUY_COSTS  = 0.00002 + FORN_SELL_COSTS
NOM_INTERVAL = 30
XREQ_EXPIRATION = 10*60
BUY_THREADS = 1

if 0: # !!! normally 0; for testing
	NOM_INTERVAL = 1
	XREQ_EXPIRATION = 2*60

rpc_user = 'rpc'
rcp_pass = 'pwd'

def rand_amount():
	amounts = (10, 30)
	amount = amounts[random.randrange(len(amounts))]
	#print amount
	return amount

def do_rpc(s, method, params=()):
	req = '{"method":"' + method + '","params":'
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
	req += '}'
	#print 'performing rpc request', req
	try:
		r = s.post('http://127.0.0.1:%d' % rpc_port, auth=(rpc_user, rcp_pass), data=req)
	except Exception as e:
		print int(time.time()), 'Warning: rpc port', rpc_port, 'exception', type(e), 'req', req
		return None
	#print 'rpc status code', r.status_code, 'response:', r.text
	if r.status_code != 200 and method != 'sendtoaddress':
		print int(time.time()), 'Warning: rpc port', rpc_port, 'status code', r.status_code, 'req', req
	if method.startswith('cc.dump'):
		return None
	try:
		j = json.loads(r.text)
		rv = j['result']
		if rv is None:
			print int(time.time()), 'Warning: rpc port', rpc_port, 'result "%s"' % j['error']['message'],'req', req
		return rv
	except:
		#pprint.pprint(r)
		if hasattr(r, 'text'):
			print int(time.time()), 'Warning: rpc port', rpc_port, 'json load failed "%s"' % r.text.encode('ascii', 'backslashreplace'), 'req', req
		else:
			print int(time.time()), 'Warning: rpc port', rpc_port, 'no text returned for req', req
		return None

class Pauser:
	def __init__(self):
		self.t0 = time.time()
	def rand_pause(self, interval):
		duration = interval - (time.time() - self.t0)
		if duration > 0:
			self.t0 += duration
			time.sleep(duration * 2*random.random())
		self.t0 = time.time()

def buy_thread():
	s = requests.Session()
	buy_pauser = Pauser()
	while True:
		buy_pauser.rand_pause(NOM_INTERVAL * BUY_THREADS)
		min_amount = rand_amount()
		max_amount = rand_amount()
		if min_amount > max_amount:
			min_amount, max_amount = max_amount, min_amount
		rate = NOM_RATE * (1.10 + random.random() / 20)
		# cc.crosschain_request_create reference_id \\\"simple_buy|simple_sell|naked_buy|naked_sell\\\" min_amount max_amount rate costs cryptoasset ( unique_foreign_address expiration wait_discount )
		txid = do_rpc(s, 'cc.crosschain_request_create', ('', 'nb', min_amount, max_amount, rate, FORN_BUY_COSTS, 'btc', '', XREQ_EXPIRATION))
		print int(time.time()), 'nb', min_amount, max_amount, rate, FORN_BUY_COSTS, 'btc', '', XREQ_EXPIRATION
		#print 'cc.crosschain_request_create buy txid', txid

def main(argv):
	global rpc_port

	if len(argv) != 2:
		print
		print 'Usage: python exchange-pay-burn.py <CredaCash_wallet_port>'
		print
		exit()

	rpc_port = int(argv[1])

	buy_thread()

if __name__ == '__main__':
	main(sys.argv)
