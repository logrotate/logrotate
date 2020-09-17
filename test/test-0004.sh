#!/bin/sh

. ./test-common.sh

cleanup 4

# ------------------------------- Test 4 -------------------------------------
preptest test.log 4 1
preptest test2.log 4 1
$RLR test-config.4 --force

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test2.log 0
test2.log.1 0 zero
scriptout 0 foo
EOF
