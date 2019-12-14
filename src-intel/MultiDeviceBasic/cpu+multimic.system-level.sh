#!/bin/bash

COUNT=$1
shift

echo Run system-level scenario with CPU device
./multidevice -t cpu -c system --instance-count $((COUNT+1)) --instance-index 0 $* >cpu+multimic.system-level.cpu.out &

for (( i=0; i<$COUNT; i++ ));
do
    # Limit available MIC devices by one device only and run the sample binary
    export OFFLOAD_DEVICES=$i
    echo Run system-level scenario with $i-th accelerator device
    ./multidevice -t acc -c system --instance-count $((COUNT+1)) --instance-index $((i+1)) $* >cpu+multimic.system-level.acc-$i.out &
done

wait

