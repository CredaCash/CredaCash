#CredaCash&trade; To Do List

<!--- NOTE: This file is in Markdown format, and is intended to be viewed in a Markdown viewer. -->

###Needed for First Production Release

-	A command line wallet, and possibly a cross-platform desktop GUI wallet.
-	A mechanism to mint the currency supply.  On the current test network, anyone can freely mint currency without limits.
-	A mechanism to collect user donations paid to the witnesses that assemble the blockchain.
-	Currently, the nodes on the network only partially validate transactions that have already been placed into blocks.  This needs to be changed to fully validate transactions inside blocks.
-	A more automated build process, with support for Linux.
-	A final code review.

###Future Items

-	Wallets for Android and iOS smartphones.
-	A wallet-to-wallet communication protocol to help eliminate misdirected payments.
-	Allow anyone to setup their own public or private blockchain with payments seamlessly routed from source to destination blockchain.
-	Additional features to manage the network, such as adding and removing witnesses, and adjusting the witness donation and proof-of-work required for a transaction.
-	Implement a faster blockchain protocol (that has already been developed and tested in simulations) where witnesses vote concurrently instead of sequentially.  This protocol can scale to hundreds or thousands of witnesses.
-	Support for a proof-of-stake blockchain with an automated process to add and remove witnesses.
-	Support for multiple assets, so tokenized stocks, bonds, derivatives, and other assets can be exchanged in a single transaction.
-	Multi-signature transactions with validation scripts.
