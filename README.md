#CredaCash&trade; Cryptocurrency and Blockchain##

<!--- NOTE: This file is in Markdown format, and is intended to be viewed in a Markdown viewer. -->

CredaCash&trade; is a next generation cryptocurrency and blockchain that features:

-	Privacy: Transactions are encrypted using Zero Knowledge Proofs to keep the source of funds, destination of funds, and the transaction amounts completely private.  CredaCash can create a private transaction in about 3 to 6 seconds, much faster than any other cryptocurrency that uses zero knowledge proofs.

-	Speed: Transactions clear in just a few seconds.

-	Finality: Cleared transactions are final and cannot be reversed.

-	Scalability: A single blockchain can scale to thousands of transactions per second.  In the future, multiple blockchains will be interconnected and payments routed seamlessly between them, similar to routing data on the internet.

##Software Overview

This repository contains the source code for the CredaCash network node (server) software.  This server can:

-	Join the network to relay blocks and transactions.
-	Track the blockchain to record cleared transactions and spent bills.
-	Serve as a network gateway for wallets and other payment applications.
-	Optionally, serve as a blockchain witness by assembling transactions into blocks and sending them across the network.

##Project Status

This software is currently in beta. It runs reliably, has been extensively tested, and has no known issues although some may be uncovered during beta testing.  A test script is provided that sends transactions to the network, but a user wallet has not yet been written.  For a full list of the items left to be completed for the first release, see the [TODO](https://github.com/CredaCash/network-node/blob/master/TODO.md) file.

##Quick Start

A Windows executable with instructions is available at [CredaCash.com](https://credacash.com/software/)

##Building

The CredaCash network node software is cross-platform, but the most recent release has only been built and tested under Windows.  For build instructions, see the [BUILDING](https://github.com/CredaCash/network-node/blob/master/BUILDING.md) file.

##License

This software is licensed under the [Creda Software Licensing Agreement](https://credacash.com/legal/software-license-agreement/). This license is designed to allow anyone to freely run, modify, and redistribute the CredaCash software as long as they are using it with the CredaCash currency.

CredaCash is a trademark of Creda Software, Inc.  US and worldwide patents pending.
