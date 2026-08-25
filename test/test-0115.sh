#!/bin/sh

. ./test-common.sh

cleanup 115

# ------------------------------- Test 115 ------------------------------------
# a log file definition spanning several lines must not throw off the line
# numbers reported by the diagnostics that follow it
preptest test.log 115 1 0

$RLR test-config.115 --force 2>&1 | tee output.log

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF

# 'bogusdirective' sits on line 13 of test-config.115, below the
# multi-line log file definition consumed by parseGlobString()
if ! grep -q "test-config.115:13 unknown option 'bogusdirective'" output.log; then
	echo "wrong line number reported for a directive that follows a"
	echo "multi-line log file definition; logrotate said:"
	grep "unknown option 'bogusdirective'" output.log
	exit 3
fi
