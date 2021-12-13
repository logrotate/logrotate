#!/bin/sh

. ./test-common.sh

cleanup 104

# ------------------------------- Test 104 ------------------------------------
# test config with unknown (new?) keyword
preptest test1.log 104 1
preptest test2.log 104 1

$RLR test-config.104 --force || exit 23

checkoutput <<EOF
test1.log 0
test1.log.1 0 zero
test2.log 0
test2.log.1 0 zero
EOF
