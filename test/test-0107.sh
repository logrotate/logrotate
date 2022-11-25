#!/bin/sh

. ./test-common.sh

cleanup 107

# ------------------------------- Test 107 ------------------------------------
preptest test.log 107 1
preptest zzzz.log 107 1
$RLR test-config.107 --force || exit 23

checkoutput <<EOF
test.log.1 0 zero
zzzz.log.1 0 zero
EOF
