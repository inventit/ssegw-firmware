#!/usr/bin/env python
import optparse
import os
import sys
import shutil
import commands

ORG_DIR = os.getcwd()
ROOT_DIR = os.path.abspath(os.path.dirname(__file__))
RANDOM_ID = os.urandom(16).encode('hex')
WORK_BASE_DIR = os.path.join(ROOT_DIR, RANDOM_ID)
FW_DIR_NAME = "fw"
FW_WORK_DIR = os.path.join(WORK_BASE_DIR, FW_DIR_NAME)
NAME_SEPARATOR = "_"
SCRIPT_DIR = os.path.join(ROOT_DIR, "scripts")
UPDATE_SCRIPT_NAME = "fw_upgrade.sh"
UPDATE_SCRIPT_FILE = os.path.join(SCRIPT_DIR, UPDATE_SCRIPT_NAME)
CHECK_SCRIPT_NAME = "check_result.sh"
CHECK_SCRIPT_FILE = os.path.join(SCRIPT_DIR, CHECK_SCRIPT_NAME)

parser = optparse.OptionParser()
parser.add_option("--version",
    action="store",
    dest="version",
    help="[REQUIRED] version of firmware")
parser.add_option("--prefix",
    action="store",
    dest="prefix",
    help="file name prefix")
(options, args) = parser.parse_args()

if not options.version:
  print 'version is required'
  parser.print_help()
  sys.exit()

def exit():
  if os.path.isdir(WORK_BASE_DIR):
    shutil.rmtree(WORK_BASE_DIR)
  os.chdir(ORG_DIR)
  sys.exit()

def error_exit(msg, help=False):
  if msg:
    print 'Error: ' + msg
  if help:
    parser.print_help()
  exit()

if not os.path.isfile(UPDATE_SCRIPT_FILE):
  error_exit(UPDATE_SCRIPT_FILE + " was not found")
if not os.path.isfile(CHECK_SCRIPT_FILE):
  error_exit(CHECK_SCRIPT_FILE + " was not found")

os.makedirs(FW_WORK_DIR)
os.chdir(FW_WORK_DIR)

shutil.copy2(UPDATE_SCRIPT_FILE, os.path.join(FW_WORK_DIR, UPDATE_SCRIPT_NAME))
shutil.copy2(CHECK_SCRIPT_FILE, os.path.join(FW_WORK_DIR, CHECK_SCRIPT_NAME))

package_prefix = ""
# prefix
if options.prefix:
  package_prefix = options.prefix + NAME_SEPARATOR
# version
package_prefix = package_prefix + options.version

# archive
zip_name = package_prefix + NAME_SEPARATOR + RANDOM_ID + ".zip"
zip_file = os.path.join(ROOT_DIR, zip_name)
os.chdir(WORK_BASE_DIR)
status, result = commands.getstatusoutput("zip " + zip_file + " ./fw/*")
if status != 0:
  error_exit("failed to archive package")
print "firmware package '" + zip_name + "' has been created."
exit()
