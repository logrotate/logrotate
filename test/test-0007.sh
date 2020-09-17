#!/bin/sh

. ./test-common.sh

cleanup 7

# ------------------------------- Test 7 -------------------------------------
preptest test.log 7 1
preptest anothertest.log 7 1

$RLR test-config.7 --force

checkoutput <<EOF
test.log 0
test.log.6 0 zero
anothertest.log 0
anothertest.log.6 0 zero
scriptout 0 foo
EOF
