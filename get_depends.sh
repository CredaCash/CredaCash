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
rm -rf snarklib
rm -rf snarkfront
unzip snarklib.zip
unzip snarkfront.zip
rm *.zip
mv snarklib-master snarklib
mv snarkfront-master snarkfront
cd ..
cd depends
rm -rf blake2
rm -rf ed25519
rm -rf jsoncpp
rm -rf siphash
rm -rf sqlite
unzip blake2.zip
unzip ed25519.zip
unzip jsoncpp.zip
unzip siphash.zip
unzip sqlite.zip
rm *.zip
mv blake2_mjosref-master blake2
mv ed25519-donna-master ed25519
mv siphash-c-master siphash
mv sqlite-amalgamation-3490200 sqlite
cd jsoncpp-master
python amalgamate.py
mv dist ..
cd ..
mv dist jsoncpp
rm -rf jsoncpp-master
cd ..
