# !/bin/sh

RESULT=result.txt

cd $(dirname $0)

[ -f ${RESULT} ] || { echo "'${RESULT}' was not found."; exit 1; }

cat ${RESULT} | grep -e "^Success" || { cat ${RESULT}; exit 1; }
exit 0
