#!/usr/bin/env python

import time
import os
import subprocess
import sys
import socket
import netifaces

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

fname = './tshark.txt'
content = ''
ipown = ''
ipaddr = '' 
portrtmp = ''
connectUrl =''
tcUrl = ''
playUrl = '' 
swfUrl = 'http://www.bet365.com/home/FlashGen4/MediaPlayerCoreModule/MediaPlayerCoreModule-442.swf'
swfFlags = ' swfVfy=true live=true '

# more deeper dump using ngrep or tcpdump
#sudo timeout 15 ngrep -d em1 -W byline | grep -v -E playing\|display\|player\|playVideo\|_airplay | grep -E swfUrl\|tcUrl\|play
#sudo timeout 15 tcpdump -A -s 0 -n -i em1 -c 5000 | grep -E tcUrl > tcpdump.txt

ownaddr = _getownip()
iface = _getif(ownaddr)

tstime = 10 
grepcmd = "sudo tshark -i " + iface + " -f " + " 'ip src " +  ownaddr + "' -Y 'rtmpt and (frame contains \"swfUrl\" or frame contains \"play\")' -T fields -e tcp.dstport -T fields -e ip.dst -T fields -e col.Info -a duration:" + str(tstime) + " > tshark.txt &"
p = subprocess.Popen(grepcmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(tstime + 1) 
p.stdout.close()

with open(fname) as f:
	dumpdata = f.readlines()	
	content = ''.join(dumpdata)
	content = content[content.find ('Handshake C2') + len ('Handshake C2'):]	
	content  = content.replace("\n","\t")	
	print 'content = ' + content

	if content.find ('\t') > -1:
		splitted = content.split ('\n')
		splitted = content.split ('\t')
		ipaddr = splitted[2]			
		connect = splitted[0]
		portrtmp = splitted[1]
		print 'port = ' + portrtmp						
		print 'ipaddr = ' + ipaddr		
		print 'connect = ' + connect	

		if connect.find ('connect')  > -1:
			 connectUrl = connect[connect.find ('connect') + len ('connect'):]
			 connectUrl = connectUrl.replace("'","")
			 connectUrl = connectUrl.replace("(","")
			 connectUrl = connectUrl.replace(")","")				 
			 connectUrl = connectUrl.replace("\n","")	
			 connectUrl = connectUrl.replace(portrtmp,"")	
#			 print 'connectUrl = ' + connectUrl
			 tcUrl = 'rtmp://' + ipaddr + ':' + portrtmp + '/'+ connectUrl
			 print 'tcUrl = ' + tcUrl
#	content = f.readline()		
	if content.find ('play')  > -1:
		 playUrl = content[content.find ('play') + len ('play'):]
		 playUrl = playUrl.replace("'","")
		 playUrl = playUrl.replace("(","")
		 playUrl = playUrl.replace(")","")				 
		 playUrl = playUrl.replace("\n","")	
#		 print connectUrl

ffargs = '"'+ tcUrl + '/' + playUrl + ' playpath=' + playUrl + ' swfUrl=' + swfUrl +  swfFlags + '"' 
#p.stdout.close()
ffgrab = 'ffplay ' + ffargs
print ffgrab + '\n'
p2 = subprocess.Popen(ffgrab, shell=True)
