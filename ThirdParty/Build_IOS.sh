#!/bin/sh
set -e

cd "$(dirname "$0")"

: "${VE_IOS_DEPLOYMENT_TARGET:=26.5}"
export VE_IOS_DEPLOYMENT_TARGET

python3 main.py ios
