#!/bin/sh

. ./test-common.sh

cleanup 105

# ------------------------------- Test 105 ------------------------------------
# test config with garbage keyword bails out
preptest test1.log 105 1
preptest test2.log 105 1

$RLR test-config.105 --force

if [ $? -eq 0 ]; then
   echo "No error, but there should be one."
   exit 3
fi


checkoutput <<EOF
test1.log 0 zero
test1.log.1 0 first
test2.log 0
test2.log.1 0 zero
EOF
