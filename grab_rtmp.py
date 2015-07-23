#!/usr/bin/python3

import json
import sys
import subprocess
#sys.path.append('..')
import rtmpg
import time

ffargs = ''
ffgrab = ''
ffpath = '' 
ffplay = 'ffplay -v 99 '
ffplay = 'ffplay '
ffmpeg = 'ffmpeg -i '

epoc = epoch_time = int(time.time())
stream = str (epoc) 
dstUrl = 'rtmp://gstream1.com:1935/live/' + stream
print ("stream ID = " +  stream)
print ("restream to : "  +  dstUrl)

ffstream = ' -acodec libmp3lame  -c:v libx264 -ar 44100 -vf scale=640:-1 -f flv ' + dstUrl + ' &  '

try:
	ffargs = rtmpg.rtmpUrl()
	ffgrab = ffplay + ffargs
#	ffgrab = ffmpeg + ffargs + ffstream
	p2 = subprocess.Popen(ffgrab, shell=True)
	print (ffargs)

except Exception as e:
    res = str(e)
    print (res)

