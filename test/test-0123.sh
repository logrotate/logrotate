#!/bin/sh

. ./test-common.sh

cleanup 123

# ------------------------------- Test 123 -----------------------------------
# The rotation count is parsed as a decimal number, i.e. a leading zero does
# not denote an octal number and hexadecimal numbers are rejected, and values
# outside the range of -1 to INT_MAX are rejected instead of truncated.

preptest test.log 123 0
createlog 0 anothertest.log
createlog 0 differenttest.log
createlog 0 differenttest2.log
createlog 0 differenttest3.log
createlog 0 differenttest4.log
createlog 0 differenttest5.log
$RLR --force test-config.123 >output.log 2>&1 && exit 23

for value in 0x1 4294967297 2147483648 -2; do
	if ! grep "bad rotation count '$value'" output.log >/dev/null; then
		echo "rotation count '$value' was not rejected"
		exit 3
	fi
done

for value in 010 08; do
	if grep "bad rotation count '$value'" output.log >/dev/null; then
		echo "rotation count '$value' was rejected"
		exit 3
	fi
done

if [ "$(grep -c '(10 rotations)' output.log)" -ne 1 ] ||
   [ "$(grep -c '(8 rotations)' output.log)" -ne 1 ]; then
	echo "rotation count not parsed as decimal number"
	exit 3
fi

rm output.log

checkoutput <<EOF2
test.log 0
test.log.1 0 zero
anothertest.log 0
anothertest.log.1 0 zero
differenttest.log 0 zero
differenttest2.log 0 zero
differenttest3.log 0 zero
differenttest4.log 0 zero
differenttest5.log 0
differenttest5.log.1 0 zero
EOF2
