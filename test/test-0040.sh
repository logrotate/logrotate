#!/bin/sh

. ./test-common.sh

cleanup 40

# ------------------------------- Test 40 ------------------------------------
# test tabooext and taboopat parsing and implementation, config.v, config.x and
# .config.z should not be loaded.
preptest test.log 40 1
mkdir -p testingdir
echo 1 > ./testingdir/config.v
echo 2 > ./testingdir/config.x
echo 3 > ./testingdir/.config.z

$RLR test-config.40 --force

rm -rf testingdir
