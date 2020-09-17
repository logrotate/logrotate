#!/bin/sh

. ./test-common.sh

cleanup 58

# ------------------------------- Test 58 ------------------------------------
# Test renamecopy
preptest test.log 58 1 0
$RLR test-config.58 --force

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
