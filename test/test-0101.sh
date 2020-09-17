#!/bin/sh

. ./test-common.sh

# check rotation with extension moved after the number
cleanup 101

preptest test.log 101 1 0
$RLR test-config.101 --force

checkoutput <<EOF
test.log 0
test.1.log 0 zero
EOF
