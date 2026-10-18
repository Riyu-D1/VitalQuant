#!/usr/bin/env python3
"""Launch a command fully detached (double-fork + setsid), stdio -> logfile.
usage: daemon_run.py <logfile> <cmd> [args...]"""
import os, sys

out = sys.argv[1]
cmd = sys.argv[2:]
if os.fork() > 0:
    sys.exit(0)
os.setsid()
if os.fork() > 0:
    os._exit(0)
fd = os.open(out, os.O_WRONLY | os.O_CREAT | os.O_APPEND)
os.dup2(fd, 1)
os.dup2(fd, 2)
dn = os.open(os.devnull, os.O_RDONLY)
os.dup2(dn, 0)
os.execvp(cmd[0], cmd)
