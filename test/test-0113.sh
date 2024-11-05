#!/bin/sh

. ./test-common.sh

cleanup 113

# ------------------------------- Test 113 -------------------------------------
# Skip rotation on checkaction failure

preptest test.log 113 2 1

checkoutput <<EOF
test.log 0 zero
test.log.1.gz 1 first
test.log.2.gz 1 second
EOF

$RLR --force test-config.113 --force || exit 23

checkoutput <<EOF
test.log 0 zero
test.log.1.gz 1 first
test.log.2.gz 1 second
EOF
