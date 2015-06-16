#!/bin/bash
YOUTUBE_DL_COMMAND="youtube-dl https://www.youtube.com/watch?v=$1 --format=mp4 -g"
echo $($YOUTUBE_DL_COMMAND)


