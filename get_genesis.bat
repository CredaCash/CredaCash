curl -O https://credacash.s3.amazonaws.com/genesis.zip
unzip genesis.zip
move genesis\* .
del genesis.zip
rmdir genesis
copy ccnode.conf.orig ccnode.conf
copy ccwallet.conf.orig ccwallet.conf
copy tor.conf.orig tor.conf
