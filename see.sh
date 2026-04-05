#!/usr/bin/env bash

set -e

mkdir -p img
python3 ./see.py "samples/sample$1.txt" "results/sample$1_solution.txt" "img/sample$1.png"
