#!/bin/bash

# write the config file
echo "num_workers=4
max_num_files=10
max_storage_size=1000000
socketname=./LSOfilestorage.sk
replacement_policy=LRU" > config.txt

# clean the output folder
rm -rf test_out/

./bin/server &

./bin/client -p -f ./LSOfilestorage.sk \
    -W $(realpath test_data/txt1.txt),$(realpath test_data/txt2.txt),$(realpath test_data/txt3.txt),$(realpath test_data/txt4.txt),$(realpath test_data/txt5.txt),$(realpath test_data/txt6.txt) \
    -D $(realpath .)/test_out/ejected -t 200 &


./bin/client -p -f ./LSOfilestorage.sk \
    -W $(realpath test_data/divina.txt),$(realpath test_data/simple_text.txt) \
    -D $(realpath .)/test_out/ejected -t 200 &

./bin/client -p -f ./LSOfilestorage.sk \
    -W $(realpath test_data/A/rand-1k.bin),$(realpath test_data/A/rand-2k.bin) \
    -D $(realpath .)/test_out/ejected -t 200 &

./bin/client -p -f ./LSOfilestorage.sk \
    -W $(realpath test_data/B/rand-4k.bin),$(realpath test_data/B/rand-8k.bin) \
    -D $(realpath .)/test_out/ejected -t 200 &


wait %2
wait %3
wait %4
wait %5

./bin/client -p -f ./LSOfilestorage.sk \
    -W $(realpath test_data/rand-100k.bin),$(realpath test_data/rand-200k.bin) \
    -D $(realpath .)/test_out/ejected -t 200 &

./bin/client -p -f ./LSOfilestorage.sk \
    -W $(realpath test_data/rand-300k.bin),$(realpath test_data/rand-500k.bin) \
    -D $(realpath .)/test_out/ejected -t 200 &

wait %2
wait %3

sleep 1

kill -s HUP %1

wait %1