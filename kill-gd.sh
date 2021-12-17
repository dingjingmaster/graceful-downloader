#!/bin/bash

while
    str=$(pidof graceful-downloader | cut -d " "  -f 1)
    if [[ $str != '' ]]
    then
        kill -9 $str
    fi
    [[ $str != '' ]]
do :; done
