# !/bin/sh

#### functions ####
error_reboot_exit () {
  echo "Failure: $1" > result.txt;
  reboot;
  exit 1;
}
#### functions ####

img_pkg=kernel-img-obs*
dpkg -l "${img_pkg}" || { echo "failed to find '${img_pkg}' package."; exit 1; }

cd $(dirname $0)
pwd
conf=package.conf
if [ -f ${conf} ]; then
  . ./${conf}
fi

[ -n "${UPGRADE_DISABLED}" ] || { UPGRADE_DISABLED=0; }
if [ ${UPGRADE_DISABLED} != "1" ]; then
  apt-get -y update
  apt-get -y upgrade
fi

service servicesync stop
if [ -n "${SSGW_DEBPKG}" ]; then
  dpkg -i ${SSGW_DEBPKG} || { error_reboot_exit "failed to install '${SSGW_DEBPKG}'"; }
fi

echo "Success" > result.txt
reboot
exit 0
