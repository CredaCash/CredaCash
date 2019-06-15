set CREDACASH_BUILD=%cd:\=/%
set CPPFLAGS=-m64 -mthreads -fstack-protector-strong
set LDFLAGS=-static
set LDLIBS=-lWs2_32 -lMswsock
cd source/3rdparty/Debug
make all
cd ../../..
cd source/cccommon/Debug
make all
cd ../../..
cd source/ccdll/Debug
make all
cd ../../..
cd source/cclib/Debug
make all
cd ../../..
cd source/ccnode/Debug
make all
cd ../../..
cd source/cctracker/Debug
make all
cd ../../..
cd source/ccwallet/Debug
make all
cd ../../..
