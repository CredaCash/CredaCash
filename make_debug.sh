if [ "$1" = "" ]
then
    cppx="-DBOOST_ALL_DYN_LINK"
elif [ "$1" != "static-boost" ]
then
    echo "usage: make_release.sh [static-boost]"
    exit 1
fi
export CREDACASH_BUILD=`pwd`
export CPPFLAGS="-Og -g3 -pthread -fPIC -fstack-protector-strong -Wno-misleading-indentation -Wno-deprecated-declarations $cppx"
export CXXFLAGS="-Wno-deprecated-copy"
export LDFLAGS="-fstack-protector"
export LDLIBS="-lpthread -ldl"
rm -f ccnode.exe ccwallet.exe ccwallet.dll cctracker.exe cctx64.dll
find source -type f \( -name \*.exe -o -name \*.dll -o -name \*.a \) -delete
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
find source -type f \( -name \*.exe -o -name \*.dll \) -exec cp {} . \;
echo CredaCash build done.
