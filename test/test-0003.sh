#!/bin/sh

. ./test-common.sh

cleanup 3

# ------------------------------- Test 3 -------------------------------------

preptest test.log 3 1
$RLR test-config.3 --force

checkoutput <<EOF
test.log 0
test.log.1 0 zero
scriptout 0 foo
EOF

cleanup

preptest test.log 3 1
preptest test2.log 3 1
$RLR test-config.3 --force

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test2.log 0
test2.log.1 0 zero
scriptout 0 foo foo
EOF
