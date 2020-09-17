#!/bin/sh

. ./test-common.sh

cleanup 73

# ------------------------------- Test 73 ------------------------------------
# make sure that 'copy' and 'copytruncate' work together
preptest test_copy.log 73 2
# make sure that 'rotate 0' and 'copytruncate' work together
preptest test_rotate.log 73 0

$RLR test-config.73 --force

checkoutput <<EOF
test_copy.log 0
test_copy.log.1 0 zero
test_rotate.log 0
EOF

if [ -f test_rotate.log.1 ]; then
    echo "rotate 0 and copytruncate keep a rotated log file"
    exit 3
fi
