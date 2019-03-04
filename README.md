# CredaCash&trade; Cryptocurrency and Blockchain

<!--- NOTE: This file is in Markdown format, and is intended to be viewed in a Markdown viewer. -->

CredaCash&trade; is a next generation cryptocurrency and blockchain that is fast, final, highly scalable and completely private.  It features:

-	Privacy: Transactions are encrypted using Zero Knowledge Proofs to keep the source of funds, destination of funds, and the transaction amounts completely private.

-	Advanced Privacy Features: CredaCash Zero Knowledge Proofs also provide advanced privacy features such as completely private hierarchical M-of-N multi-secrets, completely private hash-locked tokens, completely private token lock times, completely private restricted output addresses, completely private multi-asset transactions, complete private escrow transactions, and completely private swap transactions.

-	Speed: Transactions clear in just seconds.

-	Finality: Cleared transactions are final and cannot be reversed.

-	Scalability: A single blockchain can scale to hundreds of transactions per second.  In the future, multiple blockchains may be interconnected and payments routed seamlessly between them, similar to routing data on the internet.

## Software Overview

This repository contains the source code for the CredaCash network node and wallet.  The network node can:

-	Join the network to relay blocks and transactions.
-	Track the blockchain to record cleared transactions and spent bills.
-	Serve as a network gateway for wallets and other payment applications.
-	Optionally, serve as a blockchain witness by assembling transactions into blocks and sending them across the network.

The wallet:

-	Tracks user funds and spend secrets.
-	Sends and receives transactions.
-	Can be run interactively from the console, or used by other programs through a JSON-RPC interface.

## Project Status

This software is currently in beta. It runs reliably, has been extensively tested, and has no known issues although some may be uncovered during beta testing.  For a full list of the items left to be completed for the first release, see the [TODO](https://github.com/CredaCash/network-node/blob/master/TODO.md) file.

## Quick Start

A Windows executable with instructions is available at [CredaCash.com](https://credacash.com/software/)

## Building

The CredaCash network node software is cross-platform, and has been built and tested under Windows and Linux.  For build instructions, see the [BUILDING](https://github.com/CredaCash/network-node/blob/master/BUILDING.md) file.

## License

This software is licensed under the [Creda Software License Agreement](https://credacash.com/legal/software-license-agreement/). This license is designed to allow anyone to freely run, modify, and redistribute the CredaCash software as long as they support the CredaCash currency on a non-discriminatory basis.

CredaCash is a trademark of Creda Software, Inc.  US and worldwide patents pending.
