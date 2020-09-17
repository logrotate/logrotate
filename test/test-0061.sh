#!/bin/sh

. ./test-common.sh

cleanup 61

# ------------------------------- Test 61 ------------------------------------
preptest test.log 61 1 0

$RLR test-config.61 --force

DATESTRING=$(/bin/date +%Y-%m-%d-%H)

checkoutput <<EOF
test.log 0
test.log.$DATESTRING 0 zero
EOF
