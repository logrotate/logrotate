# We test if the ACLs tests should be done or not

if [ -z "$ACL_TESTS" ]; then
  # not yet checked

  ACL_TESTS=1
  echo 1 > test.x
  setfacl -m u:nobody:rwx test.x 2>/dev/null
  if [ $? != 0 ]; then
    ACL_TESTS=0
    echo "setfacl failed on this system. ACL tests will not be executed."
  fi
  rm -f test.x
  if [ $ACL_TESTS = 1 ]; then
    # It seems we can run the ACL tests, but was logrotate compiled WITH_ACL=yes ?
    # See the Makefile, "pretest" part, for more information
    import "test.ACL"
    if [ -f ./test.ACL ]; then
      ACL_TESTS=`cat ./test.ACL`
      if [ $ACL_TESTS = 0 ]; then
        echo "logrotate was NOT compiled with 'WITH_ACL=yes'. ACL tests will not be executed."
      fi
    fi
  fi
fi
