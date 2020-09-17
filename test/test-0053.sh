#!/bin/sh

. ./test-common.sh

cleanup 53

# ------------------------------- Test 53 ------------------------------------
# test if --force works
preptest test.log 53 1 0

$RLR test-config.53 --force

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
