#!/bin/sh

. ./test-common.sh

cleanup 124

# ------------------------------- Test 124 -----------------------------------
# Numeric option values are parsed as decimal numbers without sign or base
# prefix and must be within range.  A leading zero triggers a warning for
# options whose value was parsed with base auto-detection in previous versions.

preptest test.log 124 0
createlog 0 test2.log
createlog 0 test3.log
createlog 0 test4.log
createlog 0 test5.log
createlog 0 test6.log
createlog 0 test7.log
createlog 0 test8.log
createlog 0 test9.log
createlog 0 test10.log
createlog 0 test11.log
createlog 0 test12.log
createlog 0 test13.log
createlog 0 test14.log
createlog 0 test15.log
$RLR --force test-config.124 >output.log 2>&1 && exit 23

while read -r expected; do
	if ! grep -F "$expected" output.log >/dev/null; then
		echo "missing error: $expected"
		exit 3
	fi
done <<EOF
bad size '0x10'
bad size ''
bad size '99999999999999999999'
bad size '9007199254740992'
bad shred cycles '2147483648'
bad minutes '0'
bad minutes '4294967296'
bad minimum age '-1'
bad maximum age '+5'
bad start count '4294967296'
bad weekly directive ' 8'
bad weekly directive ' +1'
bad monthly directive ' 32'
EOF

grep -F "maxage value '010' is interpreted as decimal number" output.log >/dev/null || exit 3
if [ "$(grep -c 'is interpreted as decimal number' output.log)" -ne 1 ]; then
	echo "unexpected leading zero warning"
	exit 3
fi
grep -F "old logs are removed after 10 days" output.log >/dev/null || exit 3

rm output.log

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test2.log 0
test2.log.1 0 zero
test3.log 0 zero
test4.log 0 zero
test5.log 0 zero
test6.log 0 zero
test7.log 0 zero
test8.log 0 zero
test9.log 0 zero
test10.log 0 zero
test11.log 0 zero
test12.log 0 zero
test13.log 0 zero
test14.log 0 zero
test15.log 0 zero
EOF
