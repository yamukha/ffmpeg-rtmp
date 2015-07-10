#!/usr/bin/env python

import time
import os
import subprocess
import sys
import socket
import netifaces

ffargs = ''
ffgrab = ''
ffmpeg = 'ffmpeg -i - '
ffplay = 'ffplay - &'
ffstream = ' -acodec libmp3lame  -c:v libx264 -ar 44100 -vf scale=640:-1 -f flv rtmp://gstream1.com:1935/live/test1 &  '
grabScript = 'php AdobeHDS.php --manifest '    
grabOptions = ' --delete --play | '

def _getownip():
	s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	s.connect(("gmail.com",80))
	ownip = s.getsockname()[0]
	s.close()	
	return ownip
	
def _getif(ownaddr):
	iface = "eth0"
	iflist = netifaces.interfaces()
	for ifitem in iflist: 
		ifresult = str( netifaces.ifaddresses(ifitem))
		if ownaddr in ifresult: 			
			iface = ifitem
	return iface	
	
content = ''
ipown = ''
connectUrl =''
tcUrl = ''
playUrl = '' 

ownaddr = _getownip()
iface = _getif(ownaddr)
		
tstime = 10 		
grepcmd = "sudo tshark -i " + iface + " -f " + " 'ip src " +  ownaddr + "' -Y 'http.request and (frame contains \"f4m\")' -T fields -e col.Info -e http.host -e http.request.uri -a duration:" + str(tstime) + " &"
	
p = subprocess.Popen(grepcmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

dumped  = p.communicate("\n")[0]
content = str (dumped)
patternGet = 'GET' 
print 'dumped  = ' + content
if content.find (patternGet) < 0: 
	res = "grab hds: no f4m"
	print res
	exit (-1)

else:	
	content = content[content.find (patternGet) + len (patternGet):]	
	content = content.replace("\\t","\t")
	content = content.replace("\\n","\t")	
	content = content.replace("\\","")

	splitted = content.split ('\t')
	connect = splitted[0]
	hostUrl = splitted[1]

	request = connect.split (' ')		
	playUrl = request [1]			
	ffargs = 'http://' + hostUrl + playUrl 			

	if playUrl == "":
		res = 'can not get url for flash'
		print res
		
	ffgrab = grabScript + '"' + ffargs + '"' + grabOptions + ffplay
#	ffgrab = grabScript + '"' + ffargs + '"' + grabOptions + ffmpeg + ffstream
	print ffgrab + '\n'
	p2 = subprocess.Popen(ffgrab, shell=True)
			
