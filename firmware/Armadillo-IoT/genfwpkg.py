#!/usr/bin/env python
import optparse
import os
import sys
import shutil
import commands

ORG_DIR = os.getcwd()
ROOT_DIR = os.path.abspath(os.path.dirname(__file__))
IMAGES_DIR = os.path.join(ROOT_DIR, "images")
LINUX_IMAGE_NAME = "linux.bin.gz"
LINUX_IMAGE_FILE = os.path.join(IMAGES_DIR, LINUX_IMAGE_NAME)
ROMFS_IMAGE_NAME = "romfs.img.gz"
ROMFS_IMAGE_FILE = os.path.join(IMAGES_DIR, ROMFS_IMAGE_NAME)
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
CONF_NAME = "firmware.conf"

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

def process_image_file(image_prefix, image_name, src_path):
  dest_name = image_prefix + NAME_SEPARATOR + image_name
  dest_path = os.path.join(FW_WORK_DIR, dest_name) 
  shutil.copy2(src_path, dest_path)
  status, result = commands.getstatusoutput("md5sum " + dest_name)
  if status != 0:
    error_exit("failed to 'md5sum'")
  f = open(dest_path + ".md5", 'w')
  f.write(result)
  f.close()
  return "\"${PREFIX}${VERSION}" + NAME_SEPARATOR + image_name + "\""

if not os.path.isfile(UPDATE_SCRIPT_FILE):
  error_exit(UPDATE_SCRIPT_FILE + " was not found")
if not os.path.isfile(CHECK_SCRIPT_FILE):
  error_exit(CHECK_SCRIPT_FILE + " was not found")

linux_img_exists = False
romfs_img_exists = False
if os.path.isfile(LINUX_IMAGE_FILE):
  linux_img_exists = True
if os.path.isfile(ROMFS_IMAGE_FILE):
  romfs_img_exists = True

if not linux_img_exists and not romfs_img_exists:
  print 'kernel and/or userland images are not exist.'
  sys.exit()

os.makedirs(FW_WORK_DIR)
os.chdir(FW_WORK_DIR)

shutil.copy2(UPDATE_SCRIPT_FILE, os.path.join(FW_WORK_DIR, UPDATE_SCRIPT_NAME))
shutil.copy2(CHECK_SCRIPT_FILE, os.path.join(FW_WORK_DIR, CHECK_SCRIPT_NAME))

conf_str = "\n"
image_prefix = ""
# prefix
if options.prefix:
  conf_str = conf_str + "PREFIX=\"" + \
    options.prefix + NAME_SEPARATOR + "\"\n"
  image_prefix = options.prefix + NAME_SEPARATOR
# version
conf_str = conf_str + "VERSION=\"" + options.version + "\"\n"
image_prefix = image_prefix + options.version
# kernel image
if linux_img_exists:
  value = process_image_file(image_prefix, LINUX_IMAGE_NAME, LINUX_IMAGE_FILE)
  conf_str = conf_str + "KERNEL=" + value + "\n"
# userland image
if romfs_img_exists:
  value = process_image_file(image_prefix, ROMFS_IMAGE_NAME, ROMFS_IMAGE_FILE)
  conf_str = conf_str + "USERLAND=" + value + "\n"

# create conf file
f = open(os.path.join(FW_WORK_DIR, CONF_NAME), "w")
f.write(conf_str)
f.close()

# archive
zip_name = image_prefix + NAME_SEPARATOR + RANDOM_ID + ".zip"
zip_file = os.path.join(ROOT_DIR, zip_name)
os.chdir(WORK_BASE_DIR)
status, result = commands.getstatusoutput("zip " + zip_file + " ./fw/*")
if status != 0:
  error_exit("failed to archive package")
print "firmware package '" + zip_name + "' has been created."
exit()
