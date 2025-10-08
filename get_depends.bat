mkdir source
curl -L https://github.com/CredaCash/snarklib/archive/master.zip -o source/snarklib.zip
curl -L https://github.com/CredaCash/snarkfront/archive/master.zip -o source/snarkfront.zip
mkdir depends
curl -L https://github.com/CredaCash-depends/blake2_mjosref/archive/master.zip -o depends/blake2.zip
curl -L https://github.com/CredaCash-depends/ed25519-donna/archive/master.zip -o depends/ed25519.zip
curl -L https://github.com/CredaCash-depends/jsoncpp/archive/master.zip -o depends/jsoncpp.zip
curl -L https://github.com/CredaCash-depends/siphash-c/archive/master.zip -o depends/siphash.zip
curl -L https://sqlite.org/2025/sqlite-amalgamation-3490200.zip -o depends/sqlite.zip
cd source
rmdir/s/q snarklib
rmdir/s/q snarkfront
unzip snarklib.zip
unzip snarkfront.zip
del *.zip
ren snarklib-master snarklib
ren snarkfront-master snarkfront
cd ..
cd depends
rmdir/s/q blake2
rmdir/s/q ed25519
rmdir/s/q jsoncpp
rmdir/s/q siphash
rmdir/s/q sqlite
unzip blake2.zip
unzip ed25519.zip
unzip jsoncpp.zip
unzip siphash.zip
unzip sqlite.zip
del *.zip
ren blake2_mjosref-master blake2
ren ed25519-donna-master ed25519
ren siphash-c-master siphash
ren sqlite-amalgamation-3490200 sqlite
cd jsoncpp-master
python amalgamate.py
move dist ..
cd ..
ren dist jsoncpp
rmdir/s/q jsoncpp-master
cd ..
