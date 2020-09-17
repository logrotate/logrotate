#!/bin/sh

. ./test-common.sh

cleanup 100

# ------------------------------- Test 100 ------------------------------------
# check rotation with extension appended to the filename
preptest test.log 100 1 0
$RLR test-config.100 --force

checkoutput <<EOF
test.log 0
test.log.1.newext 0 zero
EOF
