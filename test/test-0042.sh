#!/bin/sh

. ./test-common.sh

cleanup 42

# ------------------------------- Test 42 ------------------------------------
# Test that script is called only once when sharedscripts is defined
preptest test.log 42 1
echo number2 > test2.log

$RLR test-config.42

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test2.log 0
test2.log.1 0 number2
scriptout 0 "test*.log ;test*.log ;"
EOF
