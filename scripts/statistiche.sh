#!/bin/bash

# check if the user provided an argument to the script
if [ $# -ne 1 ]
  then
    echo "usage: $0 logfile"
    exit 1
fi

# check if the argument is a file
if ! [ -e $1 ]
  then
    echo "$1 is not a file"
    exit 1
fi

# calculate the requests and the successes of each operation
for op in open close write read read_n append lock unlock remove
do
  n_req=$(grep -o ".${op}. REQUEST" log.txt | wc -l)
  n_succ=$(grep -o ".${op}. SUCCESS" log.txt | wc -l)
  echo "Number of $op operations {requests: $n_req, successes: $n_succ}"
done

# calculate the number of opens with the flag O_LOCK set to 1
n=$(grep -o "lock:1" log.txt | wc -l)
echo "Number of open-lock requests: $n"

echo ""
# calculate the number of replacements occurred
n_rep=$(grep -o "REPLACEMENT" log.txt | wc -l)
echo "Number of times the replacement algorithm ran: $n_rep"

# calculate the avergae write size
written=$(grep -o "written_bytes:[0-9]*" log.txt | awk -F ':' '{print $2}')
if [ -z "$written" ]
then
  written="0"
fi
written_sum=$( echo $written | sed -r 's/ /+/g' | bc)
written_len=$(echo $written | wc -w)
written_avg=$(echo "scale=2;$written_sum/$written_len" | bc)
echo "Average size of write operations: $written_avg byte"

# calculate the avergae read size
read=$(grep -o "sent_bytes:[0-9]*" log.txt | awk -F ':' '{print $2}')
if [ -z "$read" ]
then
  read="0"
fi
read_sum=$( echo $read | sed -r 's/ /+/g' | bc)
read_len=$(echo $read | wc -w)
read_avg=$(echo "scale=2;$read_sum/$read_len" | bc)
echo "Average size of read operations:  $read_avg byte"

# calculate the maximum clients connected
echo "Maximum concurrent $(grep -o "clients connected:[0-9]*" log.txt | sed -r 's/:/: /' | sort -r -k 3 -n | head -n 1 )"

# show the number of requests served by each worker
echo ""
echo "REQUESTS SERVED PER WORKER"
grep -o "worker [0-9]* requests served: [0-9]*" log.txt | sort -k2 -n