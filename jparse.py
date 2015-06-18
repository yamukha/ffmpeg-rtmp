#!/usr/bin/env python
import time
import zmq
import json
import os
import subprocess
import psutil
import sys
import signal
import pexpect

stnumber=1
flconf = 'conf'+ str(stnumber) +'.sh'
fldelay = 'dly'+ str(stnumber)+ '.sh'
flgrab =  'grab'+ str(stnumber)+ '.sh'
flkill =  'kill'+ str(stnumber)+ '.sh'

confFile  = './' + flconf
delayFile = './'+ fldelay
grabFile  = './' + flgrab
killFile  = './' + flkill

f1 = open('./log.txt', 'w+')
json_file='fconf.json'

ffdelay_dst = ''

#part1 ='#!/bin/bash \n sleep 1\n  \n file=$1\n name=$(cat "$file") \n echo $name \n ' 
part1 ='#!/bin/bash\n sleep 1\n ' 
part2 ='\necho "PID" \n PID=$! \n echo "$$" \n on_die() \n { \n echo "Dying transcoder..." \n kill -15 $PID \n TIME=$(date +"%d-%m-%Y %H:%M:%S") \n echo "PID Monitor $PID exit $TIME" >>  "./log.txt" \n exit 0 \n }\n' 
part3 ='trap \'on_die\' TERM \n SEC=0 \n while true ; do \n TIME=$(date +"%d-%m-%Y %H:%M:%S") \n RUNNED=$(echo $(ps -p $PID | grep $PID ))\n'
part4 = 'if [ -n "$RUNNED" ]; then \n echo "Alive transcoder..."  \n else \n echo "Killed transcoder $TIME "  >>  "./log.txt" \n'
part5 = '\nsleep 1\n PID=$! \n fi\n sleep 1 \n	SEC=$((SEC+1)) \n done \n exit 0 \n'

part1d ='#!/bin/bash\n sleep 1\n ' 
part2d ='\necho "PID" \n PID=$! \n echo "$$" \n on_die() \n { \n echo "Dying delayer..." \n kill -15 $PID \n TIME=$(date +"%d-%m-%Y %H:%M:%S") \n echo "PID Monitor $PID $TIME" >>  "./log.txt" \n exit 0 \n }\n' 
part3d ='trap \'on_die\' TERM \n SEC=0 \n while true ; do \n TIME=$(date +"%d-%m-%Y %H:%M:%S") \n RUNNED=$(echo $(ps -p $PID | grep $PID ))\n'
part4d = 'if [ -n "$RUNNED" ]; then \n echo "Alive delayer..."  \n else \n echo "Killed delayer $TIME "  >>  "./log.txt" \n'
part5d = '\nsleep 1\n PID=$! \n fi\n sleep 1 \n	SEC=$((SEC+1)) \n done \n exit 0 \n'

part1g ='#!/bin/bash\n sleep 1\n ' 
part2g ='\necho "PID" \n PID=$! \n echo "$$" \n on_die() \n { \n echo "Dying grubber..." \n kill -15 $PID \n TIME=$(date +"%d-%m-%Y %H:%M:%S") \n echo "PID Monitor $PID $TIME" >>  "./log.txt" \n exit 0 \n }\n' 
part3g ='trap \'on_die\' TERM \n SEC=0 \n while true ; do \n TIME=$(date +"%d-%m-%Y %H:%M:%S") \n RUNNED=$(echo $(ps -p $PID | grep $PID ))\n'
part4g = 'if [ -n "$RUNNED" ]; then \n echo "Alive grabber..."  \n else \n echo "Killed grabber $TIME "  >>  "./log.txt" \n'
part5g = '\nsleep 1\n PID=$! \n fi\n sleep 1 \n	SEC=$((SEC+1)) \n done \n exit 0 \n'

part1k ='#!/bin/bash\n' 
part2k = '\nTIME=$(date +"%d-%m-%Y %H:%M:%S")\necho "Run killer $TIME" >>  "./log.txt" \necho "Run killer $TIME" \nexit 0 \n'

ffproxy =''
fftype = ''
ffsrcid = ''
ffapk = 'ffmpeg'
ffprocess = 'ffplay';
ffsrc = ''
ffdst = ''
fflogo = ''
ffstart =  0
ffstop =  0
ffdelay =  0
ffin =  '-i'
ffinlogo = '-i'
fffg =  '-filter_complex'
ffout =  '-f'
fffmt=  'flv'
ffvc = '-c:v libx264'
ffac = '-c:a copy'
ffqu = '"'
ffcrop = ''
ffoverlogo=' main_w-overlay_w-10'

fgout =  'fg'
ffmap =  ' -map \'['+  fgout  +']\' -map 0:a'

blur_count = 0
logo_count = 0
filter_count = 0

filter_list = []
logo_list = []

cmdkill = ''

def crop (x,y,w,h):
	return str(x)+':' + str(y) +':'+ str(w) +':'+ str(h)
def place (x,y):
	return str(x)+':' + str(y)
	
def makesh (fname, p1,p2,p3,p4, command ,p5):
	f = open(fname , 'w+')
	bash = p1 + command + p2 + p3 + p4 + command  + p5	
	print >> f, bash
	os.system( 'chmod +x ' +  fname)
	f.close()	

def killsh (fname, p1, command ,p5):
	f = open(fname , 'w+')
	bash = p1 + command  + p5	
	print >> f, bash
	os.system( 'chmod +x ' +  fname)
	f.close()	
	
json_data=open(json_file)
j = json.load (json_data);
for key in j:
		value = j[key]
		ffsrc = j['source'] ['url']
		fftype = j['source'] ['type']
		ffsrcid = j['source'] ['data']
		ffproxy = j['source'] ['proxy']
		ffdst = j['destination'] ['url']
		ffstart = j['time']['start']
		ffstop = j['time']['end']				
		ffdelay = j['delayer']['delay']	
		ffdelay_dst = j['delayer'] ['url']
		if key  == 'blurs':
			blur_count = len(j['blurs'])
			print ("numb. of blurs = " +  str(blur_count) )			
			print ("blur coordinates are:" + str (value))						
			for bcount in range ( 0, blur_count):
				print ("blur # " +  str(bcount) + " is "  + str (j['blurs'] [bcount]) )	
				x = j['blurs'] [bcount] ['x'] 
				y = j['blurs'] [bcount] ['y'] 
				w = j['blurs'] [bcount] ['width'] 
				h = j ['blurs'] [bcount] ['height'] 	
				filter_list.append([ "blur"+str(bcount),x,y,w,h])			
				print ("x=" + str(x) + " y=" + str (y) + " w=" + str(w) + " h=" + str (h)) 				
			pass
		pass			
		
		if key  == 'logo':
			logo_count = 1
			fflogo = j['logo'] ['file'] 
			print ("logo " +  fflogo )				
			x = j['logo']  ['x'] 
			y = j['logo']  ['y'] 
			w = j['logo']  ['width'] 
			h = j['logo']  ['height'] 				
			logo_list.append([ "logo",x,y,w,h])
			print ("x=" + str(x) + " y=" + str (y) + " w=" + str(w) + " h=" + str (h)) 										
pass

cmdgrab = ''
ffinfo =''
defwidth = 640
inwidth = defwidth
logowidth = 126 
beresized = True

if (fftype == 'twitch' or fftype=='livestream' or fftype=='dailymotion' or fftype == 'ustream' or fftype == 'youtube'):		
	ffpath = '~/ffmpeg_opt/ffmpeg-2.6.3/ffmpeg ' 
	ffprobe = '~/ffmpeg_opt/ffmpeg-2.6.3/ffprobe '
	if (fftype == 'twitch' or fftype=='livestream' or fftype=='dailymotion' or fftype == 'ustream'):		
		cmdgrab =  'livestreamer -a="" -p="ffmpeg -i -  -codec:a libfdk_aac -ar 44100  -c:v libx264 -filter:v scale=640:-1 -f flv ' + ffproxy + ' " -v ' + ffsrc +  ' "480p,720p,best"' + ' & '
	ffsrc = ffproxy
	print >> f1, 'grab: ' +  ffsrc + '\n' + cmdgrab
	if (fftype == 'youtube'): 
		ffsrc = ffproxy
		print >> f1, 'run youtubedl' 		
		
		youtubedl = 'YOUTUBE_DL_COMMAND="youtube-dl https://www.youtube.com/watch?v='+ffsrcid + ' ' + '--format=mp4 -g" \n'				
		echodl = 'echo $($YOUTUBE_DL_COMMAND) >> log.txt\n'
		
		if (beresized == False):
			ytdl = ' '
			yturl = './ytdl.sh  ' + ffsrcid
			f=os.popen(yturl)
			for i in f.readlines():
				ytdl +=i
			
			print >> f1, 'youtubedl =' + ytdl		
		
			ffprobe = ffprobe + ' -v quiet -print_format json -show_entries stream=width,height  -show_entries stream=width,height' + ytdl
		
			print >> f1, 'ffprobe json =' + ffprobe
		
			f=os.popen(ffprobe)
			for i in f.readlines():
				ffinfo +=i
			
			jinfo = json.loads (ffinfo);
			inwidth = jinfo ['streams'] [0] ['width'] 
			print >> f1, 'ffprobe w_in =' + str (inwidth )					
			
			cmdgrab =   youtubedl + echodl+  ffpath + ' -i $($YOUTUBE_DL_COMMAND) -acodec libmp3lame  -c:v libx264 -ar 44100  -f flv ' + ffproxy  +' & \n'	
		else: 
			cmdgrab =   youtubedl + echodl+  ffpath + ' -i $($YOUTUBE_DL_COMMAND) -acodec libmp3lame  -c:v libx264 -ar 44100 -vf scale=640:-1 -f flv ' + ffproxy  +' & \n'		
		
	makesh (grabFile, part1g, part2g, part3g, part4g, cmdgrab, part5g)	
	time.sleep(1) 	
	cmdkill += 'killall ' + flgrab + ' \n' 
	os.system( grabFile + ' &')

lscaled = inwidth / defwidth * logowidth

print filter_list
print logo_list

filter_count = blur_count + logo_count
print "filter_count = " + str(filter_count)

if logo_count == 0 and blur_count == 0: 	
	print "no blur no logo"
	fffg = ''	
	fffilter = ''
	fflogo = ''
	ffinlogo = ''
	fffg = ''

if logo_count == 1 and blur_count == 0: 		
	print "logo no blur"
	posxl = logo_list[0] [1]
	posyl = logo_list[0] [2]
#	fffilter =  '['+ 'v0' +'][1:v]overlay=' +  ffoverlogo  + ':' + str (posyl) +'[' + fgout + ']'
	fffilter =  '[1:v] scale=' + str (lscaled ) + ':' + '-1'+'[lg];' + '['+ 'v0' +'][lg]overlay=' +  ffoverlogo  + ':' + str (posyl) +'[' + fgout + ']'
	fffg +=  ' '+ '\'' + fffilter +  '\'' + ffmap

# '[v0]crop=160:100:50:90,boxblur=5[fg0];[v0]crop=160:90:60:290,boxblur=5[fg1];[v0]crop=160:90:60:190,boxblur=5[fg2];
# [v0][fg0]overlay=50:50[vo1];[vo1][fg1]overlay=60:290[vo2];[vo2][fg2]overlay=60:190[fg]'
if logo_count == 0 and blur_count > 0: 		
	print "blur no logo"
	fflogo = ''
	ffinlogo = ''	
	cropped = ''
	for idx in  range(0, blur_count):
		cropped += '[v0]crop=' + crop (filter_list[idx] [3], filter_list[idx] [4] ,filter_list[idx] [1] , filter_list[idx] [2]) + ',boxblur=' + '3' + '[fg' + str(idx) +'];'
	overlayed = ''	
	for idx in  range(0, blur_count):	
		overlayed += '[v'+ str (idx) +']' + '[fg'+ str(idx)+ ']' + 'overlay=' +  place (filter_list[idx] [1], filter_list[idx] [2]) +'[v'+ str (idx +1) +'];'
			
	logolay = overlayed.replace('[v'+str (blur_count)+'];', '['+fgout+']')
	fffilter = cropped + logolay		
	fffg +=  ' '+ '\'' + fffilter +  '\'' + ffmap			
	
#'[v0]crop=160:90:50:20,boxblur=3[fg0];[v0]crop=160:90:60:240,boxblur=3[fg1];[v0]crop=160:90:60:130,boxblur=3[fg2];
# [v0][fg0]overlay=50:20[v1];[v1][fg1]overlay=60:240[v2];[v2][fg2]overlay=60:130[v3];[1:v] scale=160:-1[lg];[v3][lg]overlay= main_w-overlay_w-10:0[fg]'	
if logo_count == 1 and blur_count > 0: 
	print "logo and blur"
	posxl = logo_list[0] [1]
	posyl = logo_list[0] [2]
	cropped = ''
	for idx in  range(0, blur_count):
		cropped += '[v0]crop=' + crop (filter_list[idx] [3], filter_list[idx] [4] ,filter_list[idx] [1] , filter_list[idx] [2]) + ',boxblur=' + '3' + '[fg' + str(idx) +'];'
	overlayed = ''	
	for idx in  range(0, blur_count):	
		overlayed += '[v'+ str (idx) +']' + '[fg'+ str(idx)+ ']' + 'overlay=' +  place (filter_list[idx] [1], filter_list[idx] [2]) +'[v'+ str (idx +1) +'];'
	lout0 = 'v'+str (blur_count)	
	logolay =  '[1:v] scale=' + str (lscaled ) + ':' + '-1'+'[lg];' + '['+ lout0 +'][lg]overlay=' +  ffoverlogo  + ':' + str (posyl) +'[' + fgout + ']'	
	fffilter = cropped + overlayed + 	logolay		
	fffg +=  ' '+ '\'' + fffilter +  '\'' + ffmap	
		
fflist = [ffapk, ffin, ffsrc, ffinlogo, fflogo, fffg, ffac, ffvc, ffout, fffmt, ffdst ];

print fffg 
print fffilter 

print fflogo 
print ffsrc
print ffdst		
print ffstart
print ffstop
print ffdelay	

print fflist;
		
cmd = '' 	
for idx, word in enumerate(fflist):
	cmd += word + ' '
cmd += ' &'     
print cmd	
print >> f1, cmd 

epoc = int(round (time.time() ))
print epoc

currtime = time.localtime (time.time())
logtime = time.strftime("%d-%m-%Y %H:%M:%S", currtime ) 

print >> f1, logtime 
makesh (confFile, part1, part2, part3, part4, cmd, part5)
time.sleep(1) 

os.system( confFile + " &")
time.sleep(1) 

dlypath="~/ffmpeg_opt/ffmpeg-2.6.3/ffdelayer"
ffdelay_src = ffdst
cmddly= dlypath + ' -f ' + ffdelay_src + ' -t ' + ffdelay_dst + ' -d ' + str (ffdelay) + ' &'

if (ffdelay_dst != ''):	
	makesh (delayFile, part1d, part2d, part3d, part4d, cmddly, part5d)
	print >> f1, 'run delayer' 
	cmdkill += 'killall ' + fldelay + ' \n' 
	time.sleep(1) 	
	os.system( delayFile + ' &')
		
cmdkill += 'killall ' + flconf	+ '\n'
killsh (killFile, part1k, cmdkill ,part2k)	
f1.close() 	

sys.exit(1)
