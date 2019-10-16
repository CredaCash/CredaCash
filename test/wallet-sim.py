'''
CredaCash(TM) Wallet Simulation Script

Part of the CredaCash (TM) cryptocurrency and blockchain

Copyright (C) 2015-2019 Creda Software, Inc.

This program exercises the CredaCash network by simulating a wallet that sends
and receives transactions. The differences between what this script does and
what a real wallet might do are noted, and if not noted, it is safe to assume a
real wallet would work identically.

For further information on the how a wallet interacts with the CredaCash
network, and the API it can use, see:

	The CredaCash Wallet Developer's Guide
	The CredaCash Transaction API Reference Manual
	The CredaCash Transaction Protocol

This script can also be used to randomly double-spend a valid output, and verifies
that the double-spend attempt never clears.

This script can also be used to submit transactions in which one bit has been
randomly flipped, and verifies these are always rejected and do not crash the server.

'''

import collections
import hashlib

from cclib import *

cclib.server_hostname = 'lbua2etw6sgkmtqtmtoubhkthragv7gh6lraephmqor6vmlmw5rkvpad.onion'		# hostname of the Tx server @@!

#cclib.server_allows_bad_txs = True

####################################################################################
#
# Simulation Parameters (these can be changed)
#

# Maximum number of input and output bills in each transaction

txmaxout = 5
txmaxin = 4

if 0: # !!! normally 0; for testing; values below must match zkproof key capacity limits
	print '*** WARNING: OVERRIDING TX MAX IN/OUT ***'
	txmaxout = 2
	txmaxin = 1

# This is the target number of unspent bills in the wallet for simulation purposes

unspent_target = 10

# More than one payment can be sent to each destination
# This is the probability the simulation will create a new destination;
#	otherwise, it will reuse the last destination

prob_create_new_destination = 0.2

# After a transaction in submitted to the transaction server, this is the minimum amount of time
# in seconds that will pass before the simulation checks to see if the transaction has cleared.
# In the meantime, it will create more transactions.
# Set this to zero to see how long it takes transactions to clear.
# Set this to a higher number to spend more time creating transactions and less time waiting.

cleared_check_time_lag = 20

# These parameters are passed directly to the "tx-create" function

skip_tx_precheck = 0
test_use_larger_zkkey = 0

# These are for testing

extra_on_wire = 0				# must match TEST_EXTRA_ON_WIRE in code
bad_tx_probability = 0.5		# probability of creating an invalid tx, if test_bad_txs is set (note: faster testing with mal witness if code compiled with TEST_VALIDATION_FAIL_NO_STOP)
double_spend_probability = 0.2	# probability of attempting to double-spend a bill, if test_double_spends is set
bad_spend_wait_time = 120		# seconds that must elapse before deciding an invalid tx did not clear
test_nfuzz = 20					# number of times to fuzz a transaction, if the test_fuzz_txs is set and server_allows_bad_txs is clear

bill_commithashes = {}			# to prevent duplicate commitments; a real wallet does not need this
spent_serialnums = {}			# to check for doubles-spends; a real wallet does not need this

# Notes on double-spend testing:
#	txmaxin must be at least 2 to test double-spends inside the same transaction
#	The witness block rate must be slower than this scipt's transaction rate to test double-spends in the same block
#	Set witness-test-mal = 1 for at least one witness so it will generate blocks that contain double-spends

####################################################################################
#
# Bill class -- stores all attributes associated with a bill
#

class Bill:
	# For the purpose of debugging and reporting, this simulation assigns a sequential billnum to each bill
	# A real wallet doesn't have to do this, but might find it helpful.
	next_billnum = 0

	def __init__(self, wallet, destnum, default_pool, amount, ismint):
		self.wallet = wallet		# store a reference to the wallet because it generates the spend-secret
		self.billnum = Bill.next_billnum
		self.already_spent = False
		self.ismint = int(ismint)
		Bill.next_billnum += 1

		# The destination number is chosen by the Payor to get different bill spend secrets which results in different payment destinations
		#	which in turn results in different payment addresses. The payment address is the value that is publicly published in the blockchain.
		# Once a payment destination is sent to another user, she could make multiple payments to the same address.  In this simulation,
		# the destination number and payment number are sometimes reused so we can test and demonstrate handling multiple payments to the same address.
		self.destnum = destnum

		# The payment number should start at 0 and increment for subsequent payments, up to a maximum of 2^TX_PAYNUM_BITS
		# This simulation tests the range by chosing a random number.
		if HasStaticAddress(self.Destination()) or random.getrandbits(1):
			self.paynum = 0		# half of bills will have paynum = 0, to stress test multiple payments to same address
		else:
			self.paynum = random.getrandbits(TX_PAYNUM_BITS)

		# Test setting the pool attribute of the bills
		if extra_on_wire:
			self.pool = hex(random.getrandbits(TX_POOL_BITS))
		else:
			self.pool = default_pool

		# The bill amount would normally be chosen by the Payor, possibly using the number suggested by the Payee and included in the payspec
		self.amount = amount
		self.amount_fp = Amounts.Encode(amount, 0, False)

		# Also test transactions in which the output amount is publicly published
		if ismint:
			self.encrypted = 0
		elif extra_on_wire:
			self.encrypted = random.getrandbits(1)
		else:
			self.encrypted = 1

		# Since we already know the payment-number, we can compute the address and save them for later
		self.ComputeAddress()

	def Billnum(self):
		return self.billnum

	def Destnum(self):
		return self.destnum

	def Destination(self):
		# The payment destination depend on the bill's spend-secret and the destination-number
		# Normally, the Payee would use the spend-secret to generate a payspec and send it to the Payor who would decode it to determine the payment destination
		# This simulation is both the Payee and Payor, so it both generates a payspec and decodes it to extract the payment desination.
		jstr = '{"payspec-encode" :'
		jstr += ' {"spend-secret" : "' + self.wallet.spend_secret + '"'
		jstr += ', "destination-number" : "' + hex(self.Destnum()) + '"'
		jstr += '}}'
		payspec = DoJsonCmd(jstr)

		# decode payspec and extract destination
		jstr = '{"payspec-decode" : ' + payspec + '}'
		payspec = DoJsonCmd(jstr)
		payspec = json.loads(payspec)
		payspec = payspec['payspec']
		return payspec['destination']		# this function only returns the destination; a real wallet would also need the requested-amount

	def Paynum(self):
		return self.paynum

	def ComputeAddress(self):
		# The address depends on the destination and the payment-number
		# The Payee can compute the address using the "compute-address" function, or it can be provided by the Payor.
		# The Payor can get the address from "tx-to-json" after calling "tx-create".
		# In this simulation, we do both and make sure they match
		jstr = '{"compute-address" :'
		jstr += ' {"destination" : "' + self.Destination() + '"'
		jstr += ', "destination-chain" : "' + NetParams.blockchain + '"'
		jstr += ', "payment-number" : "' + hex(self.Paynum()) + '"'
		jstr += '}}'
		results = DoJsonCmd(jstr)

		results = json.loads(results)
		#print results
		self.address = results['address']
		#print 'address', self.address

	def ComputeAmountXor(self):
		# The amount-xor depends on the commitment_iv, the destination and the payment-number
		# The Payee can compute the amount-xor using the "compute-amount-encryption" function, or it can be provided by the Payor.
		jstr = '{"compute-amount-encryption" :'
		jstr += ' {"commitment-iv" : "' + self.CommitIV() + '"'
		jstr += ', "destination" : "' + self.Destination() + '"'
		jstr += ', "payment-number" : "' + hex(self.Paynum()) + '"'
		jstr += '}}'
		results = DoJsonCmd(jstr)

		results = json.loads(results)
		#print results
		self.asset_xor = toint(results['asset-encrypt-xor'])
		self.amount_xor = toint(results['amount-encrypt-xor'])
		#print 'address', self.address

	def Address(self):
		return self.address

	def Amount(self):
		return self.amount

	def AmountFP(self):
		return self.amount_fp

	def Pool(self):
		return self.pool

	# Construct json string to publish this bill in a transaction
	def OutputJson(self):
		jstr = '{"destination" : "' + self.Destination() + '"'
		jstr += ', "payment-number" : "' + hex(self.Paynum()) + '"'
		jstr += ', "pool" : "' + self.Pool() + '"'
		amount_fp = self.AmountFP()
		jstr += ', "amount" : "' + hex(amount_fp) + '"'
		if self.encrypted:
			jstr += ', "asset-mask" : "' + hex((1 << Amounts.asset_bits) - 1) + '"'
			jstr += ', "amount-mask" : "' + hex((1 << Amounts.amount_bits) - 1) + '"'
		else:
			jstr += ', "asset-mask" : 0'
			jstr += ', "amount-mask" : 0'
		jstr += '}'
		return jstr

	# These values are needed to spend the bill
	def SetCommitment(self, address, commitment_iv, commitment):
		#print 'SetCommitment bill', self.billnum, 'address', address, 'commitment-iv', commitment_iv, 'commitment', commitment
		# check the address output by "compute-address" against the value from "tx-create"
		if address != self.address:
			print 'tx-create address mismatch', address, self.address
			raise Exception
		self.commitment_iv = commitment_iv
		self.commitment = commitment

	def CommitIV(self):
		return self.commitment_iv

	def Commitment(self):
		return self.commitment

	def SetCommitnum(self, commitnum):
		#print 'SetCommitnum bill', self.billnum, 'commitnum', commitnum
		self.commitnum = commitnum

	def Commitnum(self):
		return self.commitnum

####################################################################################
#
# TxOuts class
#
# This class stores an address, commitment and serial-number from a transaction, so the wallet can look them up and see if the transaction has cleared
# Note, the wallet could look up all of the commitments and/or all of the serial numbers in a transaction, but looking up just one is enough

class TxOuts:
	def __init__(self):
		self.bills = []
		self.next_query_commitnum = 0
	def AddBill(self, bill):
		self.bills.append(bill)
	def AddOutput(self, i, address, commitment_iv, commitment):
		self.bills[i].SetCommitment(address, commitment_iv, commitment)
	def AddInputs(self, serialnums):
		self.serialnums = serialnums
	def NBills(self):
		return len(self.bills)
	def FirstBillnum(self):
		return self.bills[0].Billnum()
	def Bills(self):
		return self.bills
	def SetTime(self):
		self.time = time.time()
	def Time(self):
		return self.time
	def SetNextQueryCommitnum(self, n):
		self.next_query_commitnum = n
	def NextQueryCommitnum(self):
		return self.next_query_commitnum

####################################################################################
#
# Wallet class
#

class Wallet:
	def __init__(self):
		self.wallet_next_destnum = 0
		self.txs_unconfirmed = collections.deque()
		self.bills_unspent = {}

		passphrase_hash_milliseconds = 4000
		passphrase_hash_memory_mb = 100

		print
		print 'Master secret target generation time =', passphrase_hash_milliseconds/1000.0, 'seconds using', passphrase_hash_memory_mb, 'MB memory'

		jstr = '{"master-secret-generate" :'
		jstr += '{"milliseconds" : ' + str(passphrase_hash_milliseconds)
		jstr += ',"memory" : ' + str(passphrase_hash_memory_mb)
		jstr += '}}'
		result = DoJsonCmd(jstr)
		result = json.loads(result)
		result = result['encrypted-master-secret']
		self.encrypted_master_secret = result

		print 'The scrambled master secret =', self.encrypted_master_secret

	def DecodeMasterSecret(self):
		passphrase = "this is a test"		# this would normally be input by the user
		print 'The wallet passphrase =', '"' + passphrase +'"'

		jstr = '{"master-secret-decrypt" :'
		jstr += '{"encrypted-master-secret" : "' + self.encrypted_master_secret + '"'
		jstr += ',"passphrase" : "' + passphrase + '"'
		jstr += '}}'
		result = DoJsonCmd(jstr)
		result = json.loads(result)
		result = result['master-secret']
		self.master_secret = result

		print 'The wallet master secret =', self.master_secret

		jstr = '{"compute-spend-secret" : '
		jstr += '{"master-secret" : "' + self.master_secret + '"'
		jstr += '}}'
		result = DoJsonCmd(jstr)
		result = json.loads(result)
		result = result['spend-secret']
		self.spend_secret = result

		print 'The wallet spend secret =', self.spend_secret

	# Simulate a transation
	# return True if this function should be called again to retry
	def SimulateTx(self):

		print '\n'

		inbills = []
		insum = 0
		invals = []			# used only for trace/debugging output
		inbillnums = []		# used only for trace/debugging output

		unspent = len(self.bills_unspent)

		nin = random.randrange(txmaxin + 1)
		if unspent < unspent_target:
			nin -= random.randrange(unspent_target - unspent)
			nin = max(nin, 0)
		elif unspent > unspent_target:
			nin += random.randrange(unspent - unspent_target)
			nin = min(nin, txmaxin)
		nin = min(nin, unspent)

		#print 'nin', nin, 'bills_unspent', unspent, 'unspent_target', unspent_target, 'txmaxin', txmaxin

		# figure out the transaction inputs
		unspent_keys = self.bills_unspent.keys()
		unspent_keys.sort()
		inputs = ''
		for i in range(nin):
			key = unspent_keys[0]
			bill = self.bills_unspent[key]
			del unspent_keys[0]
			del self.bills_unspent[key]
			if test_double_spends and random.random() < double_spend_probability and len(self.bills_unspent) < max(7, 2 * unspent_target):
				# requeue the bill to be spent again at a random time, possibly even in this same transaction
				print 'Requeuing bill for a double-spend attempt'
				spend_order = random.getrandbits(64)
				#spend_order = 1							# spend this bill next
				self.bills_unspent[spend_order] = bill
				unspent_keys = self.bills_unspent.keys()
				#unspent_keys.insert(0, 0)					# spend this bill in same tx
				unspent_keys.sort()
			val = bill.Amount()
			invals.append(val)
			insum += val

			if not bill.already_spent:
				bill.already_spent = 1
			inbills.append(bill)
			inbillnums.append(bill.billnum)

			if i:
				inputs += ', '
			inputs += '"' + hex(bill.Commitnum()) + '"'

		inputs = QueryInputs(inputs)

		param_level = inputs['parameter-level']
		default_output_pool = inputs['default-output-pool']

		for i in range(nin):
			input = inputs['inputs'][i]
			bill = inbills[i]
			if toint(input['commitment-number']) != toint(bill.Commitnum()):
				print 'tx-input-query commitment-number mismatch', bill.Billnum(), input['commitment-number'], bill.Commitnum()
				raise Exception
			# the wallet needs to make some additions to the results returned by QueryInputs:
			input['spend-secret'] = self.spend_secret
			input['destination-number'] = hex(bill.Destnum())
			input['payment-number'] = hex(bill.Paynum())
			input['pool'] = bill.Pool()
			input['amount'] = hex(bill.AmountFP())
			input['commitment-iv'] = bill.CommitIV()
			input['commitment'] = bill.Commitment()
		inputs = json.dumps(inputs)
		inputs = inputs[1:-1]	# strip off outer brackets
		#print inputs

		outsum = None

		for attempt in range(20):
			if outsum == insum:
				break

			# chose random number of outputs
			if extra_on_wire:
				nout = random.randrange(txmaxout + 1)
			else:
				nout = random.randrange(txmaxout) + 1
			if len(self.bills_unspent) < unspent_target:
				nout += random.randrange(unspent_target - len(self.bills_unspent))
				nout = min(nout, txmaxout)
			elif len(self.bills_unspent) > unspent_target:
				nout -= random.randrange(len(self.bills_unspent) - unspent_target)
				nout = max(nout, int(not extra_on_wire))
			if nin == 0:
				nout = TX_MINT_NOUT

			est_txsize, donation = Amounts.ComputeDonation(nout, nin)
			if donation is None:
				raise Exception

			if nin == 0:
				insum = TX_CC_MINT_AMOUNT
				donation = TX_CC_MINT_DONATION

			suggested_donation = donation

			if 0 and not random.getrandbits(3):	# quick test to make sure small donations are rejected by server
				print '*** Reducing donation -- tx may fail'
				donation = int(donation * 0.999)

			print 'Trying transaction with', nout, 'output',
			if nout == 1:
				print 'bill',
			else:
				print 'bills',
			print 'and', nin, 'input',
			if nin == 1:
				print 'bill',
			else:
				print 'bills',
			print 'from unspent pool of', unspent,
			if unspent == 1:
				print 'bill;',
			else:
				print 'bills;',
			print 'suggested donation', suggested_donation

			# chose random amounts for output bills

			for attempt2 in range(200):
				outsum = donation
				outvals = []

				for i in range(nout):
					if outsum >= insum:
						r = 0
					elif nout - i < 2:
						r = insum - outsum
					else:
						r = random.randrange(insum - outsum + 1)
						r = random.randrange(r + 1)
					val = Amounts.Truncate(r, 0, False)
					#print 'round up', insum - outsum, r, val
					if val > insum - outsum:
						val = Amounts.Truncate(r, 0, False)
						#print 'round down', insum - outsum, r, val
					outvals.append(val)
					outsum += val

				# adjust donation to balance tx
				#print 'pre-adjusted', insum, outsum, insum - outsum, donation
				if outsum <= insum:
					new_donation = Amounts.Truncate(insum - (outsum - donation), 0, True)
					#print 'adjusted', insum, outsum, insum - outsum, donation
					if new_donation is not None and new_donation <= 4 * suggested_donation:
						outsum += new_donation - donation
						donation = new_donation
						if outsum == insum:
							break

		if outsum != insum:
			makebad = True
		else:
			makebad = (test_bad_txs and random.random() < bad_tx_probability)

		if makebad and test_bad_txs:
			print 'Testing an invalid transaction...'

		if makebad:
			# return input billets to the unspent pool
			for bill in inbills:
				if bill.already_spent == 1:
					bill.already_spent = False
				spend_order = random.getrandbits(64)
				self.bills_unspent[spend_order] = bill

		if makebad and not test_bad_txs:
			return False

		double_spend_attempt = False
		for bill in inbills:
			if bill.already_spent:
				bill.already_spent = True
				double_spend_attempt = True

		if not makebad and double_spend_attempt:
			print 'This transaction is a double-spend attempt'

		print
		if nout == 1:
			print 'Creating bill numbered', Bill.next_billnum
		elif nout > 1:
			print 'Creating bills numbered', Bill.next_billnum, '-', Bill.next_billnum + nout - 1
		else:
			print 'Creating transaction with no outputs'

		print 'The estimated transaction size is', est_txsize, 'bytes and the suggested donation is', suggested_donation

		# create transaction in json format
		jstr = '{"tx-create" : {'
		if nin == 0:
			jstr += '"mint" : {'
		else:
			jstr += '"tx-pay" : {'
		jstr += '"no-precheck" : "' + str(random.getrandbits(1) | skip_tx_precheck) + '"'	# either skip randomly or skip always
		jstr += ', "test-use-larger-zkkey" : "' + str(random.getrandbits(1) * test_use_larger_zkkey * (nout > 0)) + '"'
		jstr += ', "source-chain" : "' + NetParams.blockchain + '"'
		jstr += ', "destination-chain" : "' + NetParams.blockchain + '"'
		jstr += ', "donation" : "' + hex(Amounts.Encode(donation, 0, True)) + '"'

		jstr += ', ' + inputs
		jstr += ', "outputs" : ['

		# figure out the transaction outputs
		outsum = donation
		if nout:
			# note: this script only creates TxOuts and adds them to txs_unconfirmed when nout > 0
			# that means tx's with no outputs will not be queried to see if/when they clear
			txouts = TxOuts()
			txouts.is_bad_tx = makebad
			txouts.double_spend = None
			txouts.dont_requeue = False
			for i in range(nout):
				val = outvals[i]
				while True:
					for num in range(2):
						if num or not self.wallet_next_destnum or random.random() < prob_create_new_destination:
							destnum = self.wallet_next_destnum
							self.wallet_next_destnum += 1
						else:
							destnum = random.randrange(self.wallet_next_destnum)	# use any of prior destination numbers as a stress test
						# if two tx's contain an identical output commitment, it is not possible to tell which of the two cleared
						# to prevent this, compute a commitment hash and use it to avoid duplicate commitments
						# note: M_commitment = zkhash(M_commitment_iv, #dest, #paynum, M_pool, #asset, #amount)
						hasher = hashlib.new('md5')
						hasher.update(str(param_level) + '/' + str(destnum) + '/' + str(val))
						commit_hash = hasher.digest()
						#print 'commit_hash', num, hasher.hexdigest()
						if commit_hash in bill_commithashes:
							if num:
								raise Exception
							continue
						bill_commithashes[commit_hash] = True
					bill = Bill(self, destnum, default_output_pool, val, nin == 0)
					if HasAcceptanceRequired(bill.Destination()):
						# this destination requires acceptance, so pick a different one
						if self.wallet_next_destnum < 20:
							self.wallet_next_destnum += 1
					else:
						break

				txouts.AddBill(bill)

				if i:
					jstr += ', '
				jstr += bill.OutputJson()
		jstr += ']'

		jstr += '}}}'

		print 'Input bill numbers:', inbillnums
		print 'Input bill amounts:', invals
		print 'Output bill amounts:', outvals
		print 'Witness donation:', donation

		print 'Generating transaction...'
		output = SubmitTx(jstr, makebad = makebad, test_nfuzz = test_fuzz_txs * test_nfuzz)
		#pprint.pprint(output)
		#DumpTx()

		if isinstance(output, str):
			tx_ok = False
		elif double_spend_attempt and (output['submit-response'] == 'INVALID:already spent' or output['submit-response'] == 'INVALID:duplicate serial number'):
			tx_ok = True	# keep double-spend attempts and make sure they don't clear
			print 'Double-spend attempt submitted'
		elif output['tx-accepted']:
			tx_ok = True
			print 'Transaction accepted'
		else:
			tx_ok = False

		if not tx_ok:
			if makebad:
				print 'Invalid tx correctly rejected'
				return False
			else:
				print output
				DumpTx()
				raise Exception

		if makebad and not cclib.server_allows_bad_txs:
			print 'Invalid tx should have been rejected'
			raise Exception

		# double check the size computation
		if est_txsize != output['wire-size'] and not extra_on_wire:
			print 'ERROR: the transaction size estimate was wrong', est_txsize, '!=', output['wire-size']
			raise Exception

		if donation < suggested_donation:
			raise Exception	# server should have rejected this tx

		commitment_iv = output['commitment-iv']
		next_commitnum = output['next-commitment-number']

		if nout:	# this wallet tracks outputs, so it needs an output in which to store the input serialnums
			serialnums = []
			for i in range(nin):
				serialnum = output['inputs'][i]['serial-number']
				for j in serialnums:
					if j == serialnum and not txouts.double_spend:
						print 'This is a double-spend attempt with duplicate serial numbers'
						txouts.double_spend = True
				serialnums.append(serialnum)
			txouts.AddInputs(serialnums)		# note: a real wallet doesn't need to track the input serial numbers

		output = output['outputs']
		for i in range(nout):
			address = output[i]['address']
			#commitment_iv = output[i]['commitment-iv']
			commitment = output[i]['commitment']
			#print address, commitment_iv, commitment
			txouts.AddOutput(i, address, commitment_iv, commitment)

		if nout:
			txouts.SetTime()
			txouts.SetNextQueryCommitnum(next_commitnum)
			self.txs_unconfirmed.append(txouts)

		#time.sleep(10)	# for testing

		return False

	# Query the next entry in the self.txs_unconfirmed list to see if it has cleared
	# return True if this function should be called again to check the next txouts
	def UpdateCleared(self):
		if not len(self.txs_unconfirmed):
			return False
		txouts = self.txs_unconfirmed[0]
		if not txouts.is_bad_tx and txouts.double_spend is None:
			# a double-spend attempt could clear before the initial spend, so we need to check a table of serialnums to make sure only one clears
			for i in txouts.serialnums:
				if i in spent_serialnums:
					txouts.double_spend = True
					break
		txtime = txouts.Time()
		dt = time.time() - txtime
		#print 'txtime', txtime, 'cleared_check_time_lag', cleared_check_time_lag, 'elapsed time', dt
		if dt < cleared_check_time_lag:
			return False
		if (txouts.is_bad_tx or txouts.double_spend) and dt < bad_spend_wait_time:
			return False
		self.txs_unconfirmed.popleft()
		check_billnum = txouts.FirstBillnum()
		proxyuser = RandomString(12)
		qcount = 0
		print
		while True:
			print
			print 'Querying status of',
			if txouts.is_bad_tx:
				print 'invalid',
			elif txouts.double_spend:
				print 'double-spent',
			if txouts.NBills() > 1:
				print 'bills numbered', check_billnum, '-', check_billnum + txouts.NBills() - 1
			else:
				print 'bill number', check_billnum
			cleared = QueryTxOutputs(txouts, proxyuser)
			#print 'QueryTxOutputs result', cleared
			if cleared:
				break
			if txouts.is_bad_tx:
				print 'Invalid tx did not clear'
				return True
			if txouts.double_spend:
				print 'Double-spend did not clear'
				return True
			dt = time.time() - txtime
			if test_double_spends and dt < bad_spend_wait_time:
				# requeue to check again later
				self.txs_unconfirmed.append(txouts)
				return False
			if dt >= bad_spend_wait_time and not txouts.dont_requeue:
				# requeue to check again later
				txouts.dont_requeue = True
				self.txs_unconfirmed.append(txouts)
				return False
			if 0: # qcount == 40: # for debugging
				print vars(txouts)
				print vars(txouts.Bills()[0])
			qcount += 1
			if qcount > 20:
				time.sleep(20)
			else:
				time.sleep(0.5)
			if qcount > 70:
				print
				print 'Timeout waiting for the transaction to clear'
				print
				raise Exception
		if txouts.NBills() > 1:
			print 'Bills numbered', check_billnum, '-', check_billnum + txouts.NBills() - 1, 'have cleared;',
		else:
			print 'Bill number', check_billnum, 'has cleared;',
		print 'elapsed time', round(time.time() - txouts.Time(), 1), 'seconds'
		if txouts.is_bad_tx:
			print 'Invalid tx cleared'
			raise Exception
		if txouts.double_spend:
			print 'Double-spend cleared'
			raise Exception
		#time.sleep(10)	# for testing
		for i in txouts.serialnums:
			spent_serialnums[i] = True
		if len(txouts.serialnums):
			# Make sure the serialnum query works, too.  A real wallet doesn't need to do this.
			print
			print 'Verifying the transaction inputs are recorded as spent...'
			for i in txouts.serialnums:
				if QuerySerialnum(i, False, proxyuser) != 'indelible':	# !!! TODO: check all serialnums in one query
					print i
					raise Exception
			print 'ok'
		# queue the outputs to be spent
		for bill in txouts.Bills():
			spend_order = random.getrandbits(64)
			self.bills_unspent[spend_order] = bill
		return True

####################################################################################
#
# Functions to check if a transaction has cleared
#

# This is the query the Payee would normally use to see if it has received any payments
def QueryTxOutputs(txouts, proxyuser):
	bill = txouts.Bills()[0]
	reply = QueryAddress(bill.Address(), txouts.NextQueryCommitnum(), proxyuser)
	if isinstance(reply, str):
		print 'Server reply to address query:', '"' + reply + '"'
		return False
	for entry in reply:
		commitnum = entry['commitment-number']
		commitnum = toint(commitnum)

		# Next time we query, skip past any bills already seen by setting NextQueryCommitnum to the last seen commitnum + 1
		# In a real wallet, NextQueryCommitnum would be associated with each address.  In this simulation, it is associated with the
		# transaction and used in conjunction with the address of the first output bill in the transaction.
		txouts.SetNextQueryCommitnum(commitnum + 1)

		#print 'looking for', bill.Commitment()
		#print 'have       ', entry['commitment']
		if toint(entry['commitment']) == toint(bill.Commitment()):
			#print 'match'
			# This wallet's bill was found in the results returned by the server
			# Let's check the values returned by the query
			if entry['commitment-iv'] != bill.CommitIV():
				print 'tx-address-query commitment-iv mismatch', bill.Billnum(), entry['commitment-iv'], bill.CommitIV()
				raise Exception
			if toint(entry['pool']) != toint(bill.Pool()):
				print 'tx-address-query pool mismatch', bill.Billnum(), toint(entry['pool']), bill.Pool()
				raise Exception
			if toint(entry['encrypted']) != toint(bill.encrypted):
				print 'tx-address-query encrypted mismatch', entry['encrypted'], bill.encrypted
				raise Exception
			if bill.encrypted:
				bill.ComputeAmountXor()
				asset = toint(entry['encrypted-asset']) ^ (((1 << toint(entry['asset-bits'])) - 1) & bill.asset_xor)
				amount = toint(entry['encrypted-amount']) ^ (((1 << toint(entry['amount-bits'])) - 1) & bill.amount_xor)
				amount = Amounts.Decode(amount, False)
				if asset != 0:
					print 'tx-address-query non-zero asset', asset, entry['encrypted-asset'], entry['asset-bits'], bill.asset_xor
					raise Exception
				if toint(amount) != toint(bill.Amount()):
					print 'tx-address-query amount mismatch', amount, entry['encrypted-amount'], entry['amount-bits'], bill.amount_xor, bill.Amount()
					raise Exception
			else:
				asset = toint(entry['asset'])
				amount = toint(entry['amount'])
				amount = Amounts.Decode(amount, False)
				if asset != 0:
					print 'tx-address-query non-zero asset', asset
					raise Exception
				if toint(amount) != toint(bill.Amount()):
					print 'tx-address-query amount mismatch', amount, bill.Amount()
					raise Exception
			# The bill has cleared.	At this point, we want to store the commitment-number with the bill which is required to spend it.
			# A real wallet that receives payments would have to monitor all of its payment addresses for incoming bills, and record the
			# commitment-numbers.  Here however this simulation cheats. If one bill in the transaction has cleared, then they have all
			# cleared and they have sequential commitment-numbers. So instead of taking the time to lookup all the bills, the simulation
			# simply sets all of the commitment-numbers in the transaction, which it can do because it sent the payment to itself and
			# knows the output bills in the transaction.
			for bill in txouts.Bills():
				bill.SetCommitnum(commitnum)
				commitnum += 1		# all output bills in a transaction have sequential commitment-number's
			return True		# found
	return False	# not found

####################################################################################
#
# main
#

def main(argv):
	global test_bad_txs, test_double_spends, test_fuzz_txs, wallet

	if len(argv) < 2 or len(argv) > 5:
		print
		print 'Usage: python wallet-sim.py <port> [<test_bad_txs=0>] [<show_queries=0>] [<use_tor_proxy>]'
		print
		print ' Note: The standalone Tor proxy by default listens at port 9050'
		print '       The Tor Browser bundle   by default listens at port 9150'
		print '       Tx server on localhost   by default listens at port 9220'
		print
		print ' test_bad_txs:'
		print '       0 = test only valid txs'
		print '       1 = test valid and invalid txs, including double-spends'
		print
		print ' show_queries:'
		print '       0 = normal script output'
		print '       1 = log messages to/from tx server/network'
		print
		print ' use_tor_proxy:'
		print '       Defaults to 1 if port is 9050, 9150, 9226, or 29206; otherwise defaults to 0'
		exit()

	cclib.net_port = int(argv[1])

	# !!! TODO: make sure double-spend test tries serialnums that are old enough to no longer be in tempdb

	test_bad_txs = False
	test_double_spends = False
	test_fuzz_txs = False
	if len(argv) > 2 and int(argv[2]):
		test_bad_txs = True			# test invalid tx's
		test_double_spends = True	# test double-spending a bill
		test_fuzz_txs = not cclib.server_allows_bad_txs	# test sending random tx data to server (TEST_BREAK_ON_ASSERT in code must be false)

	if len(argv) > 3:
		cclib.show_queries = int(argv[3])
		cclib.show_activity = int(argv[3])

	if cclib.net_port == 9050 or cclib.net_port == 9150 or cclib.net_port == 9226 or cclib.net_port == 29206:
		cclib.use_tor_proxy = True
	else:
		cclib.use_tor_proxy = False

	if len(argv) > 4:
		cclib.use_tor_proxy = int(argv[4])

	seed = int(time.time())
	#seed = 0	# for repeatibility
	print 'random seed', seed
	random.seed(seed)

	wallet = Wallet()
	wallet.DecodeMasterSecret()

	NetParams.Query()					# get network parameters

	while True:
		while wallet.SimulateTx():		# simulate one transaction
			pass
		while wallet.UpdateCleared():	# possibly check if a transaction has cleared, and if one was found, check for next
			pass
		time.sleep(0.20)

if __name__ == '__main__':
	main(sys.argv)
