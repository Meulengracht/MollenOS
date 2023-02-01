#!/bin/bash

# find all test programs. They are postfixed with _test
TESTS=$(find . -name "*_test" | sort -V)

# execute all discovered tests
for TEST in $TESTS
do
  echo "Running $TEST"
  $TEST
done
