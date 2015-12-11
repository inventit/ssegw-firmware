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
CONF_NAME = "package.conf"

parser = optparse.OptionParser()
parser.add_option("--version",
    action="store",
    dest="version",
    help="[REQUIRED] version of firmware")
parser.add_option("--prefix",
    action="store",
    dest="prefix",
    help="file name prefix")
parser.add_option("--package",
    action="store",
    dest="debpackage",
    help="path to debian package file of servicesync-raspberrypi")
parser.add_option("--upgrade-disabled",
    action="store_true",
    dest="upgrade_disabled",
    help="disable to execute 'apt-get upgrade'")
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

debpackage_path = None
if options.debpackage is not None:
  debpackage_path = os.path.expandvars(options.debpackage)
  debpackage_path = os.path.expanduser(debpackage_path)
  debpackage_path = os.path.abspath(debpackage_path)
  if not os.path.isfile(debpackage_path):
    error_exit(debpackage_path + " was not found.")
    sys.exit()

if not os.path.isfile(UPDATE_SCRIPT_FILE):
  error_exit(UPDATE_SCRIPT_FILE + " was not found")
if not os.path.isfile(CHECK_SCRIPT_FILE):
  error_exit(CHECK_SCRIPT_FILE + " was not found")

os.makedirs(FW_WORK_DIR)
os.chdir(FW_WORK_DIR)

shutil.copy2(UPDATE_SCRIPT_FILE, os.path.join(FW_WORK_DIR, UPDATE_SCRIPT_NAME))
shutil.copy2(CHECK_SCRIPT_FILE, os.path.join(FW_WORK_DIR, CHECK_SCRIPT_NAME))
debpackage_name = None
if debpackage_path is not None:
  debpackage_name = os.path.basename(debpackage_path)
  shutil.copy2(debpackage_path, os.path.join(FW_WORK_DIR, debpackage_name))

package_prefix = ""
# prefix
if options.prefix:
  package_prefix = options.prefix + NAME_SEPARATOR
# version
package_prefix = package_prefix + options.version

# create conf file
conf_str = ""
if options.upgrade_disabled:
  conf_str = conf_str + "UPGRADE_DISABLED=1\n"
if debpackage_name is not None:
  conf_str = conf_str + "SSGW_DEBPKG=" + debpackage_name + "\n"

if len(conf_str) > 0: 
  f = open(os.path.join(FW_WORK_DIR, CONF_NAME), "w")
  f.write(conf_str)
  f.close()

# archive
zip_name = package_prefix + NAME_SEPARATOR + RANDOM_ID + ".zip"
zip_file = os.path.join(ROOT_DIR, zip_name)
os.chdir(WORK_BASE_DIR)
status, result = commands.getstatusoutput("zip " + zip_file + " ./fw/*")
if status != 0:
  error_exit("failed to archive package")
print "firmware package '" + zip_name + "' has been created."
exit()
