curl -O https://credacash.s3.dualstack.us-east-1.amazonaws.com/genesis.zip
unzip genesis.zip
mv genesis/* .
rm genesis.zip
rmdir genesis
