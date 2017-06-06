"""
FDKUtils.py v 1.2 April 13 6 2016
 A module of functions that are needed by several of the FDK scripts.
"""

from __future__ import print_function, absolute_import

__copyright__ = """Copyright 2016 Adobe Systems Incorporated (http://www.adobe.com/). All Rights Reserved.
"""

import subprocess
import traceback
import shlex

def runShellCmd(cmd):
	try:
		args = shlex.split(cmd)
		p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		log = p.stdout.read()
		return log
	except :
		traceback.print_exc()
		print("Error executing command '%s'." % cmd)
		return ""
