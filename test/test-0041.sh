#!/bin/sh

. ./test-common.sh

cleanup 41

# ------------------------------- Test 41 ------------------------------------
# Test that prerotate and postrotate scripts are called only when the log files
# are actually rotated
preptest test.log 41 1
echo x > test2.log

$RLR test-config.41

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test2.log 0 x
scriptout 0 test.log;test.log;
EOF
