#!/bin/sh

. ./test-common.sh

cleanup 2

# ------------------------------- Test 2 -------------------------------------
preptest test.log 2 3
$RLR test-config.2 --force

checkoutput <<EOF
test.log.1 0 zero
test.log.2 0 first
EOF

checkmail test.log.3 second

if [ -f test.log ]; then
    echo "erroneously created test.log"
fi
