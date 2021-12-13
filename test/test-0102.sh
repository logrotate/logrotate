#!/bin/sh

. ./test-common.sh

cleanup 102

# ------------------------------- Test 102 ------------------------------------
# test invalid config file with binary content
preptest test.log 102 1

$RLR test-config.102 --force

if [ $? -eq 0 ]; then
   echo "No error, but there should be one."
   exit 3
fi

checkoutput <<EOF
test.log 0 zero
test.log.1 0 first
EOF
