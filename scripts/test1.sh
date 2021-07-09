#!/bin/bash

# write the config file
echo "num_workers=1
max_num_files=1000
max_storage_size=128000000
enable_compression=0
socketname=./LSOfilestorage.sk" > config.txt

# clean the output folder
rm -rf test_out/

valgrind --leak-check=full ./bin/server &

valgrind --leak-check=full ./bin/client -p -f ./LSOfilestorage.sk \
    -W test_data/divina.txt,test_data/simple_text.txt -w test_data \
    -r test_data/divina.txt,test_data/simple_text.txt -R n=0 -D test_out/ejected \
    -d test_out/read -t 200 -l test_data/divina.txt,test_data/simple_text.txt \
    -u test_data/divina.txt,test_data/simple_text.txt \
    -c test_data/divina.txt,test_data/simple_text.txt

sleep 1

kill -s HUP %1