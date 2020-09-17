#!/bin/sh

. ./test-common.sh

cleanup 59

# ------------------------------- Test 59 ------------------------------------
# Test renamecopy in debug mode, nothing should happen
preptest test.log 59 1 0
touch test.log.1
touch test.log.2
$RLR test-config.59 --force -d 2>/dev/null

checkoutput <<EOF
test.log 0 zero
EOF

rm -f test.log.1
rm -f test.log.2
