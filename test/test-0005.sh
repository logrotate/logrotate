#!/bin/sh

. ./test-common.sh

cleanup 5

# ------------------------------- Test 5 -------------------------------------
preptest test.log 5 1
preptest anothertest.log 5 1
$RLR test-config.5 --force

checkoutput <<EOF
test.log 0
test.log.1 0 zero
anothertest.log 0
anothertest.log.1 0 zero
scriptout 0 foo
EOF
