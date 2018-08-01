#!/bin/bash

. ./test-common.sh

cleanup 83

# ------------------------------- Test 83 ------------------------------------
preptest test.log 83 1

if $RLR test-config.83 -v --force; then
    exit 1
else
    exit 0
fi
