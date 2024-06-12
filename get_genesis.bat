curl -O https://credacash.s3.dualstack.us-east-1.amazonaws.com/genesis.zip
unzip genesis.zip
move genesis\* .
del genesis.zip
rmdir genesis
