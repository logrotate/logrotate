#!/bin/sh

. ./test-common.sh

cleanup 52

# ------------------------------- Test 52 ------------------------------------
# sharedscripts are not run if the first log file does not exist
preptest test.log 52 1 0

$RLR test-config.52

checkoutput <<EOF
test.log 0
test.log.1 0 zero
scriptout 0 foo
EOF
