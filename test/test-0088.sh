#!/bin/sh

. ./test-common.sh

# check that `delaycompress` does not fail with `rotate 0`
cleanup 88

preptest test.log 88 0

$RLR -fv test-config.88 2> stderr || exit $?

if grep 'error:.*No such file or directory' stderr; then
    exit 7
else
    exit 0
fi
