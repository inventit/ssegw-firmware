# !/bin/sh

#### functions ####
error_reboot_exit () {
  echo "Failure: $1" > result.txt;
  reboot;
  exit 1;
}
#### functions ####

hw=`cat /proc/cpuinfo | grep Hardware | cut -d: -f2`
echo ${hw} | grep -e "BCM" || { echo "${hw} is unexpected hardware type."; exit 1; }

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
