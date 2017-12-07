#!/bin/bash

. ./test-common.sh

cleanup 77

# ------------------------------- Test 77 ------------------------------------
# ignore empty patterns given by the tabooext directive
preptest test.log 77 1
mkdir -p includedir
echo copytruncate > includedir/test-0077.conf

$RLR test-config.77 --force --verbose

rm -rf includedir

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
