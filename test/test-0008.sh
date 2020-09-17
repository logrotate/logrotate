#!/bin/sh

. ./test-common.sh

cleanup 8

# ------------------------------- Test 8 -------------------------------------
preptest test.log 8 1 1
$RLR test-config.8 --force

checkoutput <<EOF
test.log 0
test.log.1.gz 1 zero
scriptout 0 foo
EOF

checkmail test.log zero
