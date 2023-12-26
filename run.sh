#!/bin/bash
set -e
mkdir -p test_out
./sim 16 1024 1 0 0 0 0 gcc_trace.txt     > test_out/val1.16_1024_1_0_0_0_0_gcc.txt
./sim 32 1024 2 0 0 0 0 gcc_trace.txt     > test_out/val2.32_1024_2_0_0_0_0_gcc.txt
./sim 16 1024 1 8192 4 0 0 gcc_trace.txt  > test_out/val3.16_1024_1_8192_4_0_0_gcc.txt
./sim 32 1024 2 12288 6 0 0 gcc_trace.txt > test_out/val4.32_1024_2_12288_6_0_0_gcc.txt
./sim 16 1024 1 0 0 1 4 gcc_trace.txt     > test_out/val5.16_1024_1_0_0_1_4_gcc.txt
./sim 32 1024 2 0 0 3 1 gcc_trace.txt     > test_out/val6.32_1024_2_0_0_3_1_gcc.txt
./sim 16 1024 1 8192 4 3 4 gcc_trace.txt  > test_out/val7.16_1024_1_8192_4_3_4_gcc.txt
./sim 32 1024 2 12288 6 7 6 gcc_trace.txt > test_out/val8.32_1024_2_12288_6_7_6_gcc.txt
diff -ruiw ref_out test_out > test.diff