#!/bin/sh

. ./test-common.sh

cleanup 24

# ------------------------------- Test 24 ------------------------------------
# symlinks 2 - now copytruncate is used, but symlinks rotation is not allowed for
# security reasons.
# since logrotate-3.8.2, we don't support symlinks rotation officially.
preptest test.log.original 24 1
ln -s test.log.original test.log
$RLR test-config.24 --force 2>error.log

checkoutput <<EOF
test.log 0 zero
test.log.original 0 zero
EOF

rm -f test.log 2>/dev/null || true
