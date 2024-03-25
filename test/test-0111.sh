#!/bin/sh

. ./test-common.sh

cleanup 111
rm -fr dir symlink 2>/dev/null || true

# ------------------------------- Test 111 ------------------------------------
# Test behavior when having symlinked directories

mkdir dir
ln -s dir symlink
preptest dir/test.log 111 1

$RLR -fv test-config.111  2> stderr

if [ ! -e dir/test.log.1.gz ]; then
	echo "File dir/test.log.1.gz didn't get rotated, but should."
	exit 3
fi
