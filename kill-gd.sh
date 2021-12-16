#!/bin/bash

kill -9 $(ps aux | grep graceful-downloader | awk '{print $2}' | head -1)
