#!/bin/bash

. ./test-common.sh

cleanup 70

# ------------------------------- Test 70 ------------------------------------
# No rotation should occur because file is too young
preptest test.log 70 2

$RLR test-config.70

checkoutput <<EOF
test.log 0 zero
test.log.1 0 first
test.log.2 0 second
EOF
