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
cd source/3rdparty/Release
make all
cd ../../..
cd source/cccommon/Release
make all
cd ../../..
cd source/ccdll/Release
make all
cd ../../..
#export CPPFLAGS="-pthread -Wno-misleading-indentation $cppx"
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
find source -type f \( -name \*.exe -o -name \*.dll \) -exec cp {} . \;
echo CredaCash build done.
