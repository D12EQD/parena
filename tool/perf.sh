#!/bin/fish

./build/nob build test/single/test_parena_slot_special_size.c

sudo perf record -o test/data/perf-test.data -g -F 99 ./build/main && sudo perf report -i test/data/perf-test.dat
a --stdio --sort comm,dso,symbol --percent-limit 1 > test/data/perf-report.txt
