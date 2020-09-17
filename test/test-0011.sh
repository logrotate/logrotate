#!/bin/sh

. ./test-common.sh

cleanup 11

# ------------------------------- Test 11 ------------------------------------
preptest test.log 11 2 1
$RLR test-config.11 --force

checkoutput <<EOF
test.log 0
scriptout 0 foo
EOF

checkmail test.log.2.gz first
