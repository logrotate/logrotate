#!/bin/sh

. ./test-common.sh

cleanup 82

# ------------------------------- Test 82 ------------------------------------
preptest test.log 82 0

for i in $(seq 32); do
    $RLR test-config.82 --force
done

test "$(ls test.log.* | wc -l)" -eq 32
