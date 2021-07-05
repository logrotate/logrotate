#!/bin/sh

. ./test-common.sh

# we don't want any stuff left from previous runs
cleanup 90
rm -Rf real*.log*

# ------------------------------- Test 90 ------------------------------------
# do not rotate file with multiple hard links by default
preptest real.log 90 0
ln real.log test.log

$RLR -f test-config.90 || exit 23

checkoutput <<EOF
test.log 0 zero
EOF

if [ -e test.log.1 ]; then
    echo "test.log.1 exists!"
    exit 3
fi

if [ -e real.log.1 ]; then
    echo "real.log.1 exists!"
    exit 3
fi
