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
		do_rpc(s, Creda, 'cc.crosschain_match_mark_paid', (num, '', 10, 10)) # if interrupted, try again in 10 minutes
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
			cc_pledged = get_cc_pledged(s, Creda)
			if cc_pledged is not None:
				print(int(time.time()), '= %g Creda currently locked in pending buy requests' % cc_pledged)
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

	parse_config(conf_file)

	print(FOREIGN, 'currency:', Foreign.currency)

	ensure_one_instance('ccap_' + Foreign.currency + '_' + str(Creda.port))

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
		for iter in range(2):
			while True:
				building = do_rpc(s, Creda, 'cc.dump_tx_build', expect_json=False)
				if not (iter and building):
					break
				if iter < 2:
					print('Hold a second... the CredaCash wallet is in the middle of creating a transaction...')
					iter = 2
				time.sleep(1)
			cc_pledged = get_cc_pledged(s, Creda)
			if cc_pledged:
				print()
				print('There is', cc_pledged, 'Creda currently pledged to exchange buy requests.')
				print('This script should not be stopped until all pending buy requests and matches are fully settled.')
				break
			if not building:
				break # checking pledged can be expensive, so only recheck when building was true
	except SystemExit:
		pass

	if stop_electrum:
		print('stopping', ELECTRUM, 'daemon...')
		subprocess.Popen(Foreign.launch + ['stop'])
		print('done')

if __name__ == '__main__':
	main(sys.argv)
