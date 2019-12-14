#!/bin/bash

COUNT=$1
shift

for (( i=0; i<$COUNT; i++ ));
do
    # Limit available MIC devices by one device only and run the sample binary
    export OFFLOAD_DEVICES=$i
    echo Run system-level scenario with $i device
    ./multidevice -t acc -c system --instance-count $COUNT --instance-index $i $* >multimic.system-level.acc-$i.out &
done

wait

