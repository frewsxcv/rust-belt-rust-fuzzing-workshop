#!/usr/bin/python

from __future__ import print_function

import io
import math
import random
import subprocess
import sys


if len(sys.argv) != 3:
    print("fuzz.py <executable> <input-file>")
    sys.exit()
_, executable, input_file = sys.argv
with open(input_file) as file_:
    buf = bytearray(file_.read())
FuzzFactor = 5

#####################################
numwrites = random.randrange(
    math.ceil((float(len(buf)) / FuzzFactor))) + 1
for j in range(numwrites):
  rbyte = random.randrange(256)
  rn = random.randrange(len(buf))
  buf[rn] = "%c" % (rbyte)
#####################################

print("Calling '{}' with {}".format(executable, repr(buf)))
process = subprocess.Popen(
    [executable, '-'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE)
stdout, stderr = process.communicate(buf)
if stdout:
    print(stdout)
if stderr:
    print(stderr, file=sys.stderr)
if process.returncode != 0:
    print("Encountered non-zero return code")
