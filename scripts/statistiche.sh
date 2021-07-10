#!/bin/bash

if [ $# -ne 1 ]
  then
    echo "usage: $0 logfile"
    exit 1
fi

if ! [ -e $1 ]
  then
    echo "$1 is not a file"
    exit 1
fi

for op in open close write read read_n append lock unlock remove
do
  n_req=$(grep -o ".${op}. REQUEST" log.txt | wc -l)
  n_succ=$(grep -o ".${op}. SUCCESS" log.txt | wc -l)
  echo "Number of $op operations {requests:$n_req, successes:$n_succ}"
done

n_rep=$(grep -o "REPLACEMENT" log.txt | wc -l)
echo "Number of times the replacement algorithm ran $n_rep"

n=$(grep -o "lock:1" log.txt | wc -l)
echo "Number of open-lock requests: $n"

written=$(grep -o "written_bytes:[0-9]*" log.txt | awk -F ':' '{print $2}')
if [ -z "$written" ]
then
  written="0"
fi
written_sum=$( echo $written | sed -r 's/ /+/g' | bc)
written_len=$(echo $written | wc -w)
written_avg=$(echo "scale=2;$written_sum/$written_len" | bc)

echo "Average size of write operations: $written_avg byte"

read=$(grep -o "sent_bytes:[0-9]*" log.txt | awk -F ':' '{print $2}')
if [ -z "$read" ]
then
  read="0"
fi
read_sum=$( echo $read | sed -r 's/ /+/g' | bc)
read_len=$(echo $read | wc -w)
read_avg=$(echo "scale=2;$read_sum/$read_len" | bc)


echo "Average size of read operations:  $read_avg byte"

echo ""
echo "REQUESTS SERVED PER WORKER"
grep -o "worker [0-9]* requests served: [0-9]*" log.txt | sort