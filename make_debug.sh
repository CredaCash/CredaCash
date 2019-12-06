if [ "$1" = "" ]
then
    cppx="-DBOOST_ALL_DYN_LINK"
elif [ "$1" != "static-boost" ]
then
    echo "usage: make_release.sh [static-boost]"
    exit 1
fi
export CREDACASH_BUILD=`pwd`
export CPPFLAGS="-pthread -fPIC -fstack-protector-strong -Wno-misleading-indentation $cppx"
export LDLIBS="-lpthread -ldl"
rm -f ccnode.exe ccwallet.exe cctracker.exe cctx64.dll
find source -type f \( -name \*.exe -o -name \*.dll \) -delete
cd source/3rdparty/Debug
make all
cd ../../..
cd source/cccommon/Debug
make all
cd ../../..
cd source/ccdll/Debug
make all
cd ../../..
#export CPPFLAGS="-pthread -Wno-misleading-indentation $cppx"
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
