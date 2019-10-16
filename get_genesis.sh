curl -O https://credacash.s3.amazonaws.com/genesis.zip
unzip genesis.zip
mv genesis/* .
rm genesis.zip
rmdir genesis
cp ccnode.conf.orig ccnode.conf
cp ccwallet.conf.orig ccwallet.conf
cp tor.conf.orig tor.conf
