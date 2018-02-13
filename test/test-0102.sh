#!/bin/bash

. ./test-common.sh

cleanup 102

# ------------------------------- Test 102 ------------------------------------
preptest test1.log 102 0 0
preptest test2.log 102 0 0
preptest test3.log 102 0 0

$RLR test-config.102 --force

checkoutput <<EOF
test1.log.1 0
test1.log.1.a 0 zero
test2.log.1 0
test2.log.1.b 0 zero
test3.log.1 0
test3.log.1.b 0 zero
EOF
