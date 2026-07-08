# Toomey May 2026
# Moved the CUnit tests here.  Need CUnit-devel in say Oracle 9
# Then with the ccmake . test option on
# ctest -V
# ./testall.sh
# We could run everything with ctest or everything by script.
# Keeping it separate for the moment.  Typically in your build
# folder you can run ctest -V to run the 20+ unit tests.  Then
# to run the binary/dynamic tests use testall.sh, or you can
# run them individually.
