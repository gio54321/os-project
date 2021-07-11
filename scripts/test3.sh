#!/bin/bash

# write the config file
echo "num_workers=8
max_num_files=100
max_storage_size=32000000
enable_compression=0
socketname=./LSOfilestorage.sk
replacement_policy=FIFO" > config.txt

# clean the output folder
rm -rf test_out/

echo "spawning the server"
./bin/server &

function run_client() {
    while true; do
        ./bin/client -f ./LSOfilestorage.sk \
            -w test_data -R \
            -D test_out/ejected -d test_out/read \
            -l test_data/divina.txt -u test_data/divina.txt
    done
}

echo "spawning the clients"
for i in {1..10};
do
    run_client &
done

sleep 30

echo "killing the server"

kill -s INT %1
wait %1

echo "killing the clients"
for i in {2..11};
do
    kill %$i
done

for i in {2..11};
do
    wait %$i 2>/dev/null
done

exit 0