#!/bin/bash

# write the config file
echo "num_workers=4
max_num_files=10
max_storage_size=1000000
enable_compression=0
socketname=./LSOfilestorage.sk
replacement_policy=LRU" > config.txt

# clean the output folder
rm -rf test_out/

./bin/server &

./bin/client -p -f ./LSOfilestorage.sk \
    -W test_data/txt1.txt,test_data/txt2.txt,test_data/txt3.txt,test_data/txt4.txt,test_data/txt5.txt,test_data/txt6.txt \
    -D test_out/ejected -t 200 &


./bin/client -p -f ./LSOfilestorage.sk \
    -W test_data/divina.txt,test_data/simple_text.txt \
    -D test_out/ejected -t 200 &

./bin/client -p -f ./LSOfilestorage.sk \
    -W test_data/A/rand-1k.bin,test_data/A/rand-2k.bin \
    -D test_out/ejected -t 200 &

./bin/client -p -f ./LSOfilestorage.sk \
    -W test_data/B/rand-4k.bin,test_data/B/rand-8k.bin \
    -D test_out/ejected -t 200 &


wait %2
wait %3
wait %4
wait %5

./bin/client -p -f ./LSOfilestorage.sk \
    -W test_data/rand-100k.bin,test_data/rand-200k.bin \
    -D test_out/ejected -t 200 &

./bin/client -p -f ./LSOfilestorage.sk \
    -W test_data/rand-300k.bin,test_data/rand-500k.bin \
    -D test_out/ejected -t 200 &

wait %2
wait %3

sleep 1

kill -s HUP %1

wait %1