# CredaCash&trade; Software Changes Log
---
<!--- NOTE: This file is in Markdown format, and is intended to be viewed in a Markdown viewer. -->

## v1.1 Beta 2019-10-15

### ccnode

#### - Major Changes

- None

#### - Minor Changes

- Version identifier changed to "1.01 beta".
- Added block timestamp to console output.
- Changed the default port assignments to allow more services in the future.
- To make configuration easier, if a "#" character is used in the value of the command line options "datadir", "rendezvous-file" or "genesis-file", it is now replaced by the blockchain number.
- To decrease disk usage during the initial blockchain sync, the default value of the command line option "db-checkpoint-sec" was reduced from 127 to 21.
- Removed the command line option "db-update-continuously" since setting the command line option "db-checkpoint-sec" to zero now has the same effect.

### ccwallet

#### - Major Changes

- Updated the wallet file schema and added the command line option "update-wallet" to update wallet data files to the most recent schema.
- To reduce timeout errors when sending transactions, the command line option "tx-create-timeout" was added with a default value of 24 hours, and the default of value the command line option "tx-new-billet-wait-sec" was increased from 90 to 300.
- To easily and reliably avoid duplicate payments, the commands "cc.send", "cc.send\_async" and "cc.unique\_id\_generate" were added.
- Added the command "cc.billets\_release\_allocated" which frees all allocated billets, effectively abandoning all uncleared transactions.
- Added the command "cc.billets\_poll\_unspent" which can be used to fix the wallet balance.
- The wallet now detects and automatically handles conflicting (i.e., double-spend) transactions.
- The operation of the wallet is now more robust if the wallet data file is copied and used simultaneously on two computers, however this still remains unsupported and not recommended (instead, a new wallet should be created on the second computer and funds sent between the two wallets as needed).

#### - Minor Changes

- Version identifier changed to "1.01 beta".
- In the command "cc.poll\_destination", added a parameter to generate additional polling addresses, and made the default maximum receive time unlimited.
- Modified the command "listunspent" to no longer list billets with zero amounts, and to remove the polling of unspent billets since that is now performed by the command "cc.billets\_poll\_unspent".
- Added the commands "cc.dump\_transactions", "cc.dump\_billets" and "cc.dump\_tx\_build".
- Changed the default port assignments to allow more services in the future.
- To make configuration easier, if a "#" character is used in the value of the command line options "datadir" or the "transact-tor-hosts-file", it is now replaced by the blockchain number.
- Changed some txrpc\_wallet\_error exceptions to txrpc\_wallet\_db\_error exceptions.
- With wallet data files created using v1.0 of the software and not used for mining, the first transaction received by the wallet will add to the wallet balance and will be spendable, but will not be shown in the output of the commands "listtransactions" or "listsinceblock".  This issue was fixed in this release so that wallet data files created using v1.01 and higher of the software are not affected by this issue.