#!/bin/sh

. ./test-common.sh

cleanup 114

# ------------------------------- Test 114 ------------------------------------
# test that 'addextension' specified in the global section is inherited by the
# log file definitions that follow it, exactly like 'extension' is
preptest test.log 114 1 0

$RLR test-config.114 --force || exit 23

checkoutput <<EOF
test.log 0
test.log.1.foo 0 zero
EOF

if [ -f test.log.1 ]; then
    echo "addextension from the global section was ignored:"
    echo "rotated to test.log.1 instead of test.log.1.foo"
    exit 2
fi
