#!/bin/bash
# see https://github.com/chrippa/livestreamer/issues/303
echo "usage ./lstream.sh src rtmp://dst"
livestreamer -a="" -p="ffmpeg -i -  -codec:a libfdk_aac -ar 44100 -c:v libx264 -filter:v scale=640:-1  -f flv  $2 "  -v  $1 "480p,720p,best"


