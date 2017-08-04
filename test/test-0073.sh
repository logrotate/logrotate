#!/bin/bash

. ./test-common.sh

cleanup 73

# ------------------------------- Test 73 ------------------------------------
# make sure that 'copy' and 'copytruncate' work together
preptest test.log 73 2

$RLR test-config.73 --force

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
