#!/bin/sh

. ./test-common.sh

cleanup 103

# ------------------------------- Test 103 ------------------------------------
# test invalid config file with unknown keywords
preptest test.log 103 1

$RLR test-config.103 --force

if [ $? -eq 0 ]; then
   echo "No error, but there should be one."
   exit 3
fi

checkoutput <<EOF
test.log 0 zero
test.log.1 0 first
EOF
