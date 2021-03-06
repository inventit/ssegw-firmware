# !/bin/sh

#### functions ####
check_image_file () {
  [ -n "$1" ] || { return 0; }
  [ -f $1 ] || { echo "'$1' was not found."; exit 1; }
  md5sum -c $1.md5 || { echo "'$1': md5sum check failed"; exit 1 ; }
}
flash_image_file () {
  [ -n "$2" ] || { return 0; }
  netflash -b -k -n -u -s -r  $1 $2 || { echo "'$2:' netflash failed"; exit 1 ; }
}
#### functions ####

cd $(dirname $0)

conf=firmware.conf
[ -f ${conf} ] || { echo "'${conf}' was not found."; exit 1; }
. ${conf}


check_image_file ${KERNEL}
check_image_file ${USERLAND}

flash_image_file /dev/flash/kernel ${KERNEL}
flash_image_file /dev/flash/userland ${USERLAND}

#### successful ####
reboot
exit 0

