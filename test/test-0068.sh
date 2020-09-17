#!/bin/sh

. ./test-common.sh

cleanup 68

# ------------------------------- Test 68 ------------------------------------
# Old state file entries should be removed when not used. Logrotate should
# not freeze on big state file.
preptest test.log 68 1 0

cat > state << EOF
logrotate state -- version 1
"$PWD/test.log" 2000-1-1
EOF

for i in $(seq 1 200000)
do
   echo "\"$PWD/removed.log$i\" 2000-1-1" >> state
done

$RLR test-config.68 --force

cat state|grep test.log >/dev/null
if [ $? != 0 ]; then
	echo "state file should contain 'test.log'"
	exit 3
fi

cat state|grep removed.log >/dev/null
if [ $? = 0 ]; then
	echo "state file should not contain 'removed.log'"
	exit 3
fi
