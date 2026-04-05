#!/usr/bin/env bash

set -e

make eval S="samples/sample$1.txt" R="results/sample$1_solution.txt"
