#!/bin/sh

. ./test-common.sh

cleanup 84

# ------------------------------- Test 84 ------------------------------------
preptest test.log 84 1

mkdir -p log/dir
ln -s XXX log/sym
touch log/dir/file

$RLR test-config.84 --force
