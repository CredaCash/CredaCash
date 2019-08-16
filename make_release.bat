set CREDACASH_BUILD=%cd:\=/%
set CPPFLAGS=-m64 -mthreads -fstack-protector-strong
set LDFLAGS=-static
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
