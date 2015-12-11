# !/bin/sh

SSEGW_HOME=/opt/inventit/ssegw
SSEGW_PATH=${SSEGW_HOME}/bin
START_SCRIPT=${SSEGW_PATH}/start_servicesyncd.sh
STOP_SCRIPT=${SSEGW_PATH}/stop_servicesyncd.sh

#### functions ####
error_exit () {
  echo "Failure: $1" > result.txt;
  exit 1;
}
#### functions ####

cd $(dirname $0)

[ -f ${STOP_SCRIPT} ] || { error_exit "'${STOP_SCRIPT}' was not found."; }
[ -f ${START_SCRIPT} ] || { error_exit "'${START_SCRIPT}' was not found."; }

${STOP_SCRIPT} || { error_exit "Failed to stop servicesyncd"; }
echo "Success" > result.txt

${START_SCRIPT}
exit 0
