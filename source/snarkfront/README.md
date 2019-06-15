snarkfront: a C++ embedded domain specific language for zero knowledge proofs
================================================================================

--------------------------------------------------------------------------------
Introduction
--------------------------------------------------------------------------------

The name "snarkfront" is homage to Cfront - the original C++ compiler
implementation developed at AT&T Bell Laboratories through the 1980s. Cfront
was a front-end over the native C toolchain, literally a source translator.
Somewhat analogously, snarkfront is a C++ embedded domain specific language
(EDSL) over the underlying snarklib template library.

The author suspects zero knowledge cryptography may be the new public-key, a
technology that opens up entirely new classes of applications and systems.
Public-key cryptography makes asymmetric trust relationships possible.
Zero knowledge proofs make anonymous trust relationships possible.

The author is grateful to the [SCIPR Lab] for the [GitHub libsnark project].
This project is built on top of the [GitHub snarklib project] which was made
possible by the libsnark source code release.

Modifications for the CredaCash&trade; project Copyright &copy; 2015-2019 Creda Software, Inc.
CredaCash is a trademark of Creda Software, Inc.

--------------------------------------------------------------------------------
[TOC]

<!---
  NOTE: the file you are reading is in Markdown format, which is is fairly readable
  directly, but can be converted into an HTML file with much nicer formatting.
  To do so, run "make doc" (this requires the python-markdown package) and view
  the resulting file README.html.
-->

--------------------------------------------------------------------------------
Language summary
--------------------------------------------------------------------------------

Six types:

- Boolean
- 8-bit unsigned integer octets
- 32-bit unsigned integer words
- 64-bit unsigned integer words
- 128-bit unsigned integer scalars
- underlying elliptic curve finite scalar field

The usual operators:

- logical and bitwise complement
- AND, OR, XOR
- addition, subtraction, multiplication
- modulo addition, modulo multiplication
- inverse and exponentiation (finite fields)
- shift and rotate
- comparisons: == != < <= > >=
- ternary conditional
- array subscript
- type conversion between Boolean, 8-bit, 32-bit, 64-bit, and 128-bit

Cryptographic algorithms:

- [FIPS PUB 180-4]: SHA-1, SHA-224, SHA-256, SHA-384, SHA-512, SHA-512/224, SHA-512/256
- [FIPS PUB 197]: AES-128, AES-192, AES-256
- binary Merkle tree using SHA-256 or SHA-512

Elliptic curve pairings:

- Barreto-Naehrig at 128 bits of security
- Edwards at 80 bits of security

Use both API and CLI toolchain:

- programmatic API with cryptographic structures in C++ templates
- map-reduce cryptographic structures in files from command line

--------------------------------------------------------------------------------
Build instructions
--------------------------------------------------------------------------------

The following libraries are required.

- [GNU Multiple Precision Arithmetic Library]
- [GitHub snarklib project]
- [GitHub cryptl project]

Note that both snarklib and cryptl are C++ template libraries. They are
entirely header files.

To build everything as statically linked:

    $ make archive tools tests PREFIX=/usr/local

To build the shared library:

    $ make lib PREFIX=/usr/local

When building, the $(PREFIX)/include directory is added to the compiler header
file path.

To install the library files and tools:

    $ make install PREFIX=/usr/local

When installing, library files are copied to $(PREFIX)/include/snarkfront,
$(PREFIX)/lib and $(PREFIX)/bin .

--------------------------------------------------------------------------------
test_proof (zero knowledge proof for SHA-256)
--------------------------------------------------------------------------------

Just run the shell script:

    $ ./test_proof.sh 

Equivalently:

(generate the proving and verification keys)

    $ ./test_proof -m keygen > keygen.txt

(public input)

    $ ./test_proof -m input > input.txt

(generate a proof using the secret)

    $ cat keygen.txt input.txt | ./test_proof -m proof > proof.txt

(verify the proof)

    $ cat keygen.txt input.txt proof.txt | ./test_proof -m verify

Note key pair generation is expensive in comparison with the proof and
verification. This is typical. The proving key will be very large and take a
long time to generate. However, it only has to be done once at the very
beginning and then is reused forever by all parties.

*The entity or multi-party protocol generating the key pair is trusted!*

This test is realistic in the sense that each stage is properly blinded to
inadvertant knowledge which it can not know. Each of the four stages only
has the information it should possess.

--------------------------------------------------------------------------------
test_aes (zero knowledge AES)
--------------------------------------------------------------------------------

The usage message:

    $ ./test_aes 
    encrypt: ./test_aes -p BN128|Edwards -b 128|192|256 -k key_in_hex -e -i hex_text
    decrypt: ./test_aes -p BN128|Edwards -b 128|192|256 -k key_in_hex -d -i hex_text

Examples from the NIST standards document:

(AES-128 encrypt with Edwards curve)

    $ ./test_aes -p Edwards -b 128 -e -k 2b7e151628aed2a6abf7158809cf4f3c -i 3243f6a8885a308d313198a2e0370734

(AES-128 encrypt with Barreto-Naehrig curve)

    $ ./test_aes -p BN128 -b 128 -e -k 000102030405060708090a0b0c0d0e0f -i 00112233445566778899aabbccddeeff

(AES-128 decrypt with Edwards curve)

    $ ./test_aes -p Edwards -b 128 -d -k 000102030405060708090a0b0c0d0e0f -i 69c4e0d86a7b0430d8cdb78070b4c55a

(AES-192 encrypt with Barreto-Naehrig curve)

    $ ./test_aes -p BN128 -b 192 -e -k 000102030405060708090a0b0c0d0e0f1011121314151617 -i 00112233445566778899aabbccddeeff

(AES-192 decrypt with Barreto-Naehrig curve)

    $ ./test_aes -p BN128 -b 192 -d -k 000102030405060708090a0b0c0d0e0f1011121314151617 -i dda97ca4864cdfe06eaf70a0ec0d7191

(AES-256 encrypt with Edwards curve)

    $ ./test_aes -p Edwards -b 256 -e -k 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f -i 00112233445566778899aabbccddeeff

(AES-256 decrypt with Barreto-Naehrig curve)

    $ ./test_aes -p BN128 -b 256 -d -k 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f -i 8ea2b7ca516745bfeafc49904b496089

The test_aes process uses about 5.5 GB for the AES-256 examples during key pair
generation. Memory requirements could be lower if the CLI were used instead of
the API (which holds all cryptographic structures in RAM).

--------------------------------------------------------------------------------
test_sha (zero knowledge SHA-2)
--------------------------------------------------------------------------------

The usage message:

    $ ./test_sha 
    usage: ./test_sha -b 1|224|256|384|512|512_224|512_256 [-p BN128|Edwards] [-r] [-d hex_digest] [-e equal_pattern] [-n not_equal_pattern]

    text from standard input:
    echo "abc" | ./test_sha -p BN128|Edwards -b 1|224|256|384|512|512_224|512_256

    pre-image pattern and hash:
    echo "abc" | ./test_sha -p BN128|Edwards -b 1|224|256|384|512|512_224|512_256 -d digest -e pattern -n pattern

    hash only, skip zero knowledge proof:
    echo "abc" | ./test_sha -b 1|224|256|384|512|512_224|512_256

    random data:
    ./test_sha -p BN128|Edwards -b 1|224|256|384|512|512_224|512_256 -r

Two elliptic curves are supported.

- Barreto-Naehrig at 128 bits, use option: "-p BN128"
- Edwards at 80 bits, use option: "-p Edwards"

All SHA-2 variants are supported, use option: "-b number" which selects algorithm
SHA-number.

The "-r" switch fills the message with random bytes drawn from /dev/urandom.
Otherwise, the message input is read from standard input. Note the random data
fills the entire message block and is intentionally not padded. It is often useful
to use the SHA-2 compression function without padding in zero knowledge proofs.

The "-d hex_digest" option sets the message digest hash value.
The "-e equal_pattern" and "-n not_equal_pattern" apply constraints on the preimage.
See the example below for details.

Some examples:

(SHA-256 hash of "abc" using Barreto-Naehrig elliptic curve)

    $ echo "abc" | ./test_sha -p BN128 -b 256

(SHA-512 hash of random data using Edwards elliptic curve)

    $ ./test_sha -p Edwards -b 512 -r

(SHA-1 hash with specified digest and preimage constraints)

    1. calculate message digest
    $ echo "hello" | ./test_sha -b 1
    f572d396fae9206628714fb2ce00f72e94f2258f

    2. zero knowledge proof with satisfied constraints (should pass)
    $ echo "hello" | ./test_sha -b 1 -p BN128 -d f572d396fae9206628714fb2ce00f72e94f2258f -e ??ll -n a?a

    3. zero knowledge proof with violated constraints (should fail)
    $ echo "hello" | ./test_sha -b 1 -p BN128 -d f572d396fae9206628714fb2ce00f72e94f2258f -e ??ll -n h?a

Note the "variable count" is shown. This is not the same as the number of gates
in the circuit. There are always more variables than gates. For instance, a
binary AND gate has two input wires and one output. Each wire has an associated
variable. In this case, there are three variables and one gate.

--------------------------------------------------------------------------------
test_merkle (zero knowledge Merkle tree)
--------------------------------------------------------------------------------

The usage message:

    $ ./test_merkle 
    usage: ./test_merkle -p BN128|Edwards -b 256|512 -d tree_depth -i leaf_number

The binary Merkle tree uses either SHA-256 or SHA-512. The test fills the tree
while maintaining all authentication paths from leaves to the root. When the
tree is full, a zero knowledge proof is generated for the authentication path
corresponding to leaf_number (zero indexed so the first leaf is 0). This proves
membership of the leaf in the Merkle tree without revealing the path. The leaf
remains secret, known only to the entity which generates the proof.

Here is an example:

    $ ./test_merkle -p Edwards -b 256 -d 8 -i 123

Note the index convention runs in the opposite direction of tree depth. The
root element at the top of the tree is index 7. The index 0 is at the leaves
of the tree. This reversed indexing is consistent with how the proof works.
The proof follows the path from the leaf upwards to the root.

--------------------------------------------------------------------------------
test_cli.sh (command line toolchain)
--------------------------------------------------------------------------------

An example script exercises the command line toolchain end-to-end.

    $ ./test_cli.sh
    usage: ./test_cli.sh BN128|Edwards 256|512 <tree_depth> <vector_blocks> <window_blocks> [clearonly]

It uses test_bundle to generate a Merkle tree, add a commitment leaf, then
write the constraint system, input, and witness to files.

    $ ./test_bundle 
    new tree:      ./test_bundle -p BN128|Edwards -b 256|512 -t merkle_tree_file -d tree_depth
    add leaf:      ./test_bundle -p BN128|Edwards -b 256|512 -t merkle_tree_file -c hex_digest [-k]
    constraints:   ./test_bundle -p BN128|Edwards -b 256|512 -t merkle_tree_file -s constraint_system_file -n constraints_per_file
    proof input:   ./test_bundle -p BN128|Edwards -b 256|512 -t merkle_tree_file -i proof_input_file
    proof witness: ./test_bundle -p BN128|Edwards -b 256|512 -t merkle_tree_file -w proof_witness_file

These files are inputs to the four toolchain executables which perform the
zero knowledge cryptographic calculations.

1. randomness - sample entropy from /dev/urandom and save in files (dangerous!)
2. qap - map constraint system to query vectors
3. ppzk - map query vectors and randomness to generate key pair, reduce proving key and witness to generate proof
4. verify - check that verification key, input, and proof are consistent

Here is an easy example. This creates a Merkle tree of depth one using the
80 bit Edwards curve and SHA-256. The map-reduce index space is trivial with
a single partition for the query vectors and windowed exponentiation table.

    $ ./test_cli.sh Edwards 256 1 1 1 clearonly

A more realistic example is a Merkle tree of depth 64 using the 128 bit
Barreto-Naehrig curve. As before, the SHA-256 compression function is used.
Query vectors are partitioned into 16 blocks. The windowed exponentiation
table is partitioned into 8 blocks.

    $ ./test_cli.sh BN128 256 64 16 8 clearonly

Note this may take hours to run and writes 6 GB of files to disk. However,
RAM use remains between 500 MB and 2 GB. A laptop with 4 GB RAM and a slow
x86-64 bit CPU running at 1 GHz can generate the key pair in under eight hours
using a single core without stressing itself (getting hot or thrashing disk).

--------------------------------------------------------------------------------
References
--------------------------------------------------------------------------------

[SCIPR Lab]: http://www.scipr-lab.org/ (Succinct Computational Integrity and Privacy Research Lab)

[GitHub libsnark project]: https://github.com/scipr-lab/libsnark

[GitHub snarklib project]: https://github.com/jancarlsson/snarklib

[GitHub cryptl project]: https://github.com/jancarlsson/cryptl

[GNU Multiple Precision Arithmetic Library]: https://gmplib.org/

[FIPS PUB 180-4]: http://csrc.nist.gov/publications/fips/fips180-4/fips-180-4.pdf

[FIPS PUB 197]: https://csrc.nist.gov/publications/fips/fips197/fips-197.pdf
