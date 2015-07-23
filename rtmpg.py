import time
import os
import subprocess
import sys
import socket
import netifaces
import re
    
class RtmpError(Exception):
    pass

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
	
def _getUrl ( strIn, pattern, separator):
	if strIn.find (pattern)  > -1:
		connectUrl = strIn[strIn.find (pattern) + len (pattern):]
		connectUrl = connectUrl.replace("'","")
		connectUrl = connectUrl.replace("(","")
		connectUrl = connectUrl.replace(")","")				 
		connectUrl = connectUrl.replace("\n","")		
		splitted = connectUrl.split (separator)		
		parsedUrl = splitted [0]
		parsedUrl = parsedUrl.lstrip(".") 
		parsedUrl = parsedUrl.rstrip(".") 
		pattern = pattern.replace(".","")			
#		print ( pattern + ' = '+ parsedUrl )		
		return parsedUrl
	else:	
		return ''		
	
def rtmpUrl():
	dumpfile = "tcpdump.txt"
	content = ''
	ipown = ''
	ipaddr = '' 
	portrtmp = ''
	connectUrl =''
	tcUrl = ''
	playUrl = '' 	
	swfFlags = ' swfVfy=true live=true '

	ownaddr = _getownip()
	iface = _getif(ownaddr)
			
	tstime = 25 		
	grepcmd = "sudo timeout " + str(tstime) + " tcpdump -A -s 0 -n -i " + iface + " -c 5000 | grep -e '\.pageUrl\.' -e '\.play\.'" + " & "
	print ("cmd = " + grepcmd)
        
	try: 		
		p = subprocess.Popen(grepcmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

		dumped  = p.communicate("\n")[0]
		content = str (dumped)
		print (	'dumped  = ' + content)
		tofind = 'swfUrl'
		if content.find (tofind) < 0: 
			res = "grab rtmp: no Handshake "
			raise RtmpError(res)

		else:	
			tofind = 'swfUrl'
			strNext = 'tcUrl'
			pattern = 'http'
			swfUrl =  _getUrl (content, tofind, strNext )		
			strIn = swfUrl
			if strIn.find (pattern)  > -1:
				swfUrl = pattern + strIn[strIn.find (pattern) + len (pattern):]
			print (	'swfUrl = ' + swfUrl)
			
			tofind = 'tcUrl'				
			strNext = 'pageUrl'
			separator = '..'
			tcUrl =  _getUrl (content, tofind, strNext )
			splitted = tcUrl.split (separator)		
			tcUrl = splitted [0]	
			if len (tcUrl) < 10:
				tcUrl = splitted [0] + '.' + splitted [1] 
			cnt = tcUrl.count('.')	
			if cnt != 3:				
				tcUrl =  tcUrl.replace(".","", 1)	
			print (	'tcUrl = ' + tcUrl)
			
			tofind = 'pageUrl'				
			strNext = 'object'
			pageUrl =  _getUrl (content, tofind, strNext )				
			k = pageUrl.rfind("/")
			rightUrl = pageUrl[k:]
			leftUrl = pageUrl[:k]			
			rightUrl = rightUrl.replace(".","")			
			pageUrl = leftUrl + rightUrl	
			pattern = ':'
			strhttp = 'http'						
			strIn = pageUrl
			
			if strIn.find (pattern)  > -1:
				pageUrl = strhttp + pattern + strIn[strIn.find (pattern) + len (pattern):]
			print (	'pageUrl  = ' + pageUrl)
	
			tofind = '.play.'				
			strNext = '\n'
			playUrl =  _getUrl (content, tofind, strNext )			
			playUrl =  playUrl.replace(".","")		
			playUrl =  playUrl.replace("\\n","")	
			print (	'playUrl  = ' + playUrl)
			playUrl =  playUrl.replace("\"","")	
			k = re.search("\d", playUrl)			
			playUrl = playUrl[k.start():]
				
			ffargs = '"'+ tcUrl + ' playpath=' + playUrl + ' swfUrl=' + swfUrl +  ' pageUrl=' + pageUrl + swfFlags + '"' 			

			if playUrl == "":
				res = 'can not get url for flash'
				raise RtmpError(res)
				
			return ffargs
			
	except  RtmpError as e:
			res = repr(e)
			raise RtmpError(res)		
	except ValueError as e:
			res = repr(e)
			raise RtmpError(res)
	except Exception as e:
			res = repr(e)
			raise RtmpError(res)
