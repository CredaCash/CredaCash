set CREDACASH_BUILD=%cd:\=/%
set CPPFLAGS=-O3 -g1 -m64 -mthreads -fstack-protector-strong -Wno-misleading-indentation -Wno-deprecated-declarations
set CXXFLAGS=-Wno-deprecated-copy
set LDFLAGS=-static -fstack-protector
set LDLIBS=-lWs2_32 -lMswsock -lssp
cd source/3rdparty/Release
make all
cd ../../..
cd source/cccommon/Release
make all
cd ../../..
cd source/ccdll/Release
make all
cd ../../..
cd source/cclib/Release
make all
cd ../../..
cd source/ccnode/Release
make all
cd ../../..
cd source/cctracker/Release
make all
cd ../../..
cd source/ccwallet/Release
make all
cd ../../..
echo CredaCash build done.
