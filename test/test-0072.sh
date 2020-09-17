#!/bin/sh

. ./test-common.sh

cleanup 72

# ------------------------------- Test 72 ------------------------------------
preptest test.log 72 2

$RLR test-config.72 --force

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test.log.2.gz 1 first
EOF

echo 'unexpected' > test.log.1.gz

$RLR test-config.72 --force
dt="$(date +%Y%m%d%H)"

checkoutput <<EOF
test.log 0
test.log.1 0
test.log.1.gz-$dt.backup 0 unexpected
test.log.2.gz 1 zero
test.log.3.gz 1 first
EOF
