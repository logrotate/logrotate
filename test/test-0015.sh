#!/bin/sh

. ./test-common.sh

# shred test
cleanup 15

# ------------------------------- Test 15 ------------------------------------
preptest test.log 15 1 0
$RLR test-config.15 --force

# this rotation should use shred
$RLR test-config.15 --force

checkoutput <<EOF
test.log 0
test.log.1 0
EOF
