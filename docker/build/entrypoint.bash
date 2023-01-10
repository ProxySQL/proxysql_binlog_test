#!/bin/bash
#set -x

set -eu

echo "==> Build environment:"
env

echo -e "==> C compiler: ${CC} -> $(readlink -e $(which ${CC}))\n$(${CC} --version)"
echo -e "==> C++ compiler: ${CXX} -> $(readlink -e $(which ${CXX}))\n$(${CXX} --version)"

echo "==> Dependecies"
cd /opt/proxysql_binlog_test

echo "==> Cleaning"
make cleanbuild

echo "==> Building"
make -j $(ncpu)
