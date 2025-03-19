#!/bin/bash

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

cd $SCRIPTPATH/clang-examples/mpopt-plugin
if [ $1 == "clean" ]; then
    make clean
else
    make $1 && make install
fi