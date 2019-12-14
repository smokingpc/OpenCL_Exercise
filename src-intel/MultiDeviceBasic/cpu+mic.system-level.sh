#!/bin/bash

./multidevice -t cpu -c system --instance-count 2 --instance-index 0 $* > cpu+mic.system-level.cpu.out &
./multidevice -t acc -c system --instance-count 2 --instance-index 1 $* > cpu+mic.system-level.acc.out &

wait

