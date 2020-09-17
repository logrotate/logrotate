#!/bin/sh

. ./test-common.sh

cleanup 9

# ------------------------------- Test 9 -------------------------------------
preptest test.log 9 1 1
$RLR test-config.9 --force

checkoutput <<EOF
test.log 0
scriptout 0 foo
EOF

checkmail test.log zero
