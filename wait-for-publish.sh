#!/bin/bash

if [ $# -lt 5 ]; then
  echo "Usage: $0 <api-host> <project-name> <package-name> <repo> <arch>"
  exit 1
fi

apihost=https://$1
proj=$2
pkg=$3
repo=$4
arch=$5

blockwait=60
buildwait=20

echo `date -u`
echo "$0 $apihost $proj $pkg $repo $arch"
export OSC_CONFIG=$HOME/.osc/oscrc-virtbugs
export XDG_STATE_HOME="/dev/shm/.osc_cookiejar.virtbugs.${host}"
rm -f ~/.osc_cookiejar ~/.local/state/osc/cookiejar

# Get the latest rev of sources
source_rev=`osc -A $apihost --config=$OSC_CONFIG api /source/$proj/$pkg | xmllint --xpath "string(//directory/@rev)" -`
echo "source rev: $source_rev"

# Check if build is blocked
blocked=1
for (( i = 0; i < $blockwait; i++ )); do
    status=`osc -A $apihost --config=$OSC_CONFIG api /build/$proj/$repo/$arch/$pkg/_status | xmllint --xpath "string(//status/@code)" -`
    if [ $status != "blocked" ]; then
	blocked=0
	break
    fi
    echo "The build is blocked, waiting..."
    sleep 60
done

# Give up if still blocked
if [ $blocked -eq 1 ]; then
    echo `date -u`
    echo "Build has been blocked for more than $blockwait minutes"
    exit 1
fi

# Wait for the package to build and publish by comparing the source rev to the
# latest rev in build history
bhist_rev=''
for (( i = 0; i < $buildwait; i++ )); do
    if [ $i -ne 0 ]; then
        echo "$i: waiting for OBS/IBS to build rev $source_rev"
        sleep 60
    fi

    bhist_rev=`osc -A $apihost --config=$OSC_CONFIG api /build/$proj/$repo/$arch/$pkg/_history | xmllint --xpath "string(//buildhistory/entry[last()]/@rev)" -`
    if [ $bhist_rev -ge $source_rev ]; then
        echo `date -u`
        echo "Build is complete and published"
        exit 0
    fi
done

echo `date -u`
echo "Timed out waiting for build: source rev $source_rev, latest buildhist rev $bhist_rev"
exit 1
