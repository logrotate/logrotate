#!/bin/bash

. ./test-common.sh

cleanup 102

# ------------------------------- Test 102 -----------------------------------
# size option and the time interval option are mutually exclusive, so the user
# should be warned.
preptest test.log 102 1 0

$RLR -d test-config.102 2>&1 | \
    grep -q "size option is mutually exclusive with the time interval options."
