"""
FDKUtils.py v 1.2 April 13 6 2016
 A module of functions that are needed by several of the FDK scripts.
"""

__copyright__ = """Copyright 2016 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
"""

import subprocess
import traceback

def runShellCmd(cmd):
	try:
		p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout
		log = p.read()
		return log
	except :
		msg = "Error executing command '%s'. %s" % (cmd, traceback.print_exc())
		print(msg)
		return ""
