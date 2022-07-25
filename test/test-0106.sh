#!/bin/sh

. ./test-common.sh

cleanup 106

# ------------------------------- Test 106 ------------------------------------
# test ~ replacement in include and olddir directives
preptest test.log 106 1

export HOME="$(pwd)/homedir"
rm -rf homedir/
mkdir -p homedir/includedir
cat > homedir/includedir/test-0106-included.conf << EOF
$(pwd)/test.log {
  rotate 1
  create
  olddir ~/old
  createolddir 700
}
EOF
chmod go-w homedir/includedir/test-0106-included.conf

$RLR test-config.106 --force || exit 23

checkoutput <<EOF
test.log 0
homedir/old/test.log.1 0 zero
EOF

rm -r homedir/
