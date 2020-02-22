# CredaCash&trade; Software Changes Log
---
<!--- NOTE: This file is in Markdown format, and is intended to be viewed in a Markdown viewer. -->

## v1.1 2020-02-22

### ccnode

#### - Major Changes

- The transaction server now informs the wallet when it appears that the node is not connected to the network, which allows the wallet to avoid creating transactions that never clear.

#### - Minor Changes

- Version identifier changed to "1.01".
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

- Version identifier changed to "1.01".
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
