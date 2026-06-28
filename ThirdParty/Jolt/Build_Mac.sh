#!/bin/sh
set -e

cd "$(dirname "$0")"
python3 main.py --build-tests-and-demos
