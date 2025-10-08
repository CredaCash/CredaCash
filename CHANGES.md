# CredaCash&trade; Software Changes Log
---
<!--- NOTE: This file is in Markdown format, and is intended to be viewed in a Markdown viewer. -->

## v2.1 2025-10-08

### ccnode

#### - Minor Changes

- Added support for standalone GUI wallet.
- Fixed a bug that could cause blocksync to hang, requiring the node to be restarted to continue.

### ccwallet

#### - Minor Changes

- Enabled ccwallet to be loaded as a dynamic library.
- Released standalone, self-contained GUI Wallet app for Android and Windows that runs without server software.

## v2.0.5 2025-07-25

### ccnode

#### - Major Changes

- Changed foreign payment accounting from fractional BTC/BCH to integral satoshi, to make verification more reliable.

#### - Minor Changes

- Fixed a regression that prevented strict validation of historical blocks.

### ccwallet

#### - Major Changes

- Added the command "cc.transactions_list" to provide more detailed transaction info.
- Added an option for remote wallet access via a Tor hidden service.

### Exchange scripts

#### - Minor Changes

- Minor improvements.

## v2.0.4 2025-03-22

### ccwallet

#### - Minor Changes

- Fixed rare assert condition that could cause wallet to stop.

### Exchange scripts

#### - Minor Changes

- Minor improvements.

## v2.0.3 2024-11-04

### ccnode

#### - Major Changes

- Fixed handling of partial exchange payments.

### ccwallet

#### - Minor Changes

- Fixed handling of partial exchange payments.

### Exchange scripts

#### - Minor Changes

- Minor improvements.

## v2.0.2 2024-09-30

### ccnode

#### - Major Changes

- Fixed a floating point roundoff error that could occur when processing an exchange request with a fractional amount. This roundoff error triggered a rate comparison assertion that caused the blockchain to halt at block level 1299246 on an exchange request with amount 0.1.

#### - Minor Changes

- Fixed the command "cc.crosschain_query_requests" to only return selected request types.
- Added the configuration option "tor-port" to set the tor proxy port.

### ccwallet

#### - Minor Changes

- Added the configuration option "tor-port" to set the tor proxy port.

### Exchange scripts

#### - Minor Changes

- Various improvements.

## v2.0.01 2024-09-10

### ccnode

#### - Minor Changes

- Added support for exchange mining request types.
- Fixed problems connecting to the network on some platforms due to issues launching Tor.
- Fixed rare assert that could cause program to halt, requiring restart.

### ccwallet

#### - Minor Changes

- Added the "mining_trade" exchange request type, which simplifies mining using the crosschain exchange.
- Improved exchange request generation on slower or heavily-loaded computers.
- Added the commands "cc.crosschain_request_create_local" and "cc.broadcast", which allows exchange requests to be created locally and then broadcast to the network at a later time.
- "gettransaction" now accepts wallet internal transaction id's.
- Fixed memory leak in the asynchronous transaction functions.
- Fixed rare assert that could cause program to halt, requiring restart.

### Exchange scripts

#### - Major Changes

- Increased robustness.
- Updated for compatibility with Python v2.7 and v3.8+

## v2.0 2024-06-12

### ccnode

#### - Major Changes

- Added integrated peer-to-peer crosschain exchange supporting CredaCash, Bitcoin (BTC), and Bitcoin Cash (BCH).
- Added mining of CredaCash currency based on the use of the exchange.

#### - Minor Changes

- Faster initial blockchain sync.

### ccwallet

#### - Major Changes

- Added support for integrated peer-to-peer crosschain exchange.
- Added support for mining CredaCash currency based on use of the exchange.

#### - Minor Changes

- Added various "cc.crosschain\_" and "cc.exchange\_" commands.
- Added command "cc.donation\_estimate".
- Added command "cc.billets_list_unspent" that can filter output by pending, cleared and allocated statuses.
- "listunspent" output now includes pending and allocated billets.
- Commands that provide transaction information now output several CredaCash-specific fields prefixed "cc."
- Commands that require a txid now accept both universal txid's (prefixed "CCTX\_") and wallet internal txid's (prefixed "CCTX_Internal\_").

## v1.1 2020-02-22

### ccnode

#### - Major Changes

- The transaction server now informs the wallet when it appears that the node is not connected to the network, which allows the wallet to avoid creating transactions that never clear.

#### - Minor Changes

- Added block timestamp to console output.
- Changed the default port assignments to allow more services in the future.
- To make configuration easier, if a "#" character is used in the value of the command line options "datadir", "rendezvous-file" or "genesis-file", it is now replaced by the blockchain number.
- To decrease disk usage during the initial blockchain sync, the default value of the command line option "db-checkpoint-sec" was reduced from 127 to 21.
- Removed the command line option "db-update-continuously" since setting the command line option "db-checkpoint-sec" to zero now has the same effect.
- To make Tor troubleshooting easier, Tor console output is now included with ccnode console output. (This previously worked under Linux but not Windows.)
- The transaction server now stores transaction output commitment numbers with the transaction input serial numbers, and returns them in response to a "tx-serial-number-query". This feature is currently unused, but may eventually be used by a block explorer serial number query, or to allow the wallet to associate billet spends with the complete transaction.

### ccwallet

#### - Major Changes

- Updated the wallet file schema and added the command line option "update-wallet" to update wallet data files to the most recent schema.
- Added the command "backupwallet" to reliably backup the wallet file while the wallet is running.
- To avoid creating transactions that never clear, the wallet now refuses to create transactions when ccnode detects that it is not connected to the network.
- To easily and reliably avoid duplicate payments, the commands "cc.send", "cc.send\_async" and "cc.unique\_id\_generate" were added.
- The wallet now detects and automatically handles conflicting (i.e., double-spend) transactions.
- To reduce timeout errors when sending transactions, the command line option "tx-create-timeout" was added with a default value of 24 hours, and the default of value the command line option "tx-new-billet-wait-sec" was increased from 90 to 300.
- The operation of the wallet is now more robust if the wallet data file is copied and used simultaneously on two computers, however this still remains unsupported and not recommended (instead, a new wallet should be created on the second computer and funds sent between the two wallets as needed).
- Added the bitcoin-compatible command "abandontransaction".
- Added the command "cc.transaction\_cancel" which attempts to cancel a transaction by creating a conflicting transaction.
- Added the command "cc.billets\_release\_allocated" which frees all allocated billets, effectively abandoning all uncleared transactions, and then resets the balance to match the total of the unspent billets.
- Added the command "cc.billets\_poll\_unspent" which can be used to adjust the wallet balance for billets spent externally, for example, by another wallet that has control over the same funds.

#### - Minor Changes

- Fixed the command "gettransaction" to report a transaction amount of zero when the amount is sent to one of the wallet's own destinations.
- In bitcoin, some functions such as "gettransaction" and "getreceivedbyaddress" will return the same error message for an invalid transaction id or address, and for a transaction id or address that is valid but not found in the wallet. In order to make troubleshooting easier, these functions have been changed in the CredaCash wallet to return different error codes and messages for an invalid transaction id or address, as opposed to a transaction id or address that is valid but not in the wallet.
- Renamed the command "cc.poll\_destination" to "cc.destination\_poll", and added a parameter to generate additional polling addresses and made the default maximum receive time unlimited.
- Added the command "cc.list\_change\_destinations" to get a list of change destinations used by the wallet, allowing these destinations to be manually polled if necessary.
- Modified the command "listunspent" to no longer list billets with zero amounts, and to remove the polling of unspent billets since that is now performed by the command "cc.billets\_poll\_unspent".
- Added the commands "cc.dump\_transactions", "cc.dump\_billets" and "cc.dump\_tx\_build".
- Changed the default port assignments to allow more services in the future.
- To make configuration easier, if a "#" character is used in the value of the command line options "datadir" or "transact-tor-hosts-file", it is now replaced by the blockchain number.
- Changed some txrpc\_wallet\_error exceptions to txrpc\_wallet\_db\_error exceptions.
- With wallet data files created using v1.0 of the software and not used for mining, the first transaction received by the wallet will add to the wallet balance and will be spendable, but will not be shown in the output of the commands "listtransactions" or "listsinceblock".  This issue was fixed in this release so that wallet data files created using v1.01 and higher of the software are not affected by this issue.
