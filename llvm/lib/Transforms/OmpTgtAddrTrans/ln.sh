#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
ln -sf $DIR/OmpTgtAddrTrans.cpp ../IPO/OmpTgtAddrTrans.cpp
