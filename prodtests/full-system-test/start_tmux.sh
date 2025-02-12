#!/bin/bash

if [ "0$1" == "0" ]; then
  echo Please indicate whether to start with raw-reader [rr] or with DataDistribution [dd]
  exit 1
fi

MYDIR="$(dirname $(readlink -f $0))"
source $MYDIR/setenv.sh

# This sets up the hardcoded configuration to run the full system workflow on the EPN
export NGPUS=4
export GPUTYPE=HIP
export SHMSIZE=$(( 128 << 30 ))
export DDSHMSIZE=$(( 128 << 10 ))
export GPUMEMSIZE=$(( 24 << 30 ))
export NUMAGPUIDS=1
export EXTINPUT=1
export EPNPIPELINES=1
export SYNCMODE=1
export SHMTHROW=0
export SEVERITY=error

if [ $1 == "dd" ]; then
  export CMD=datadistribution.sh
else
  export CMD=raw-reader.sh
fi

if [ ! -f matbud.root -a -f ctf_dictionary.root ]; then
  echo matbud.root or ctf_dictionary.root missing
  exit 1
fi

if [ "0$FST_TMUX_NOWAIT" != "01" ]; then
  ENDCMD="echo END; sleep 1000"
fi

if [ "0$FST_TMUX_KILLCHAINS" == "01" ]; then
  KILLCMD="sleep 60; ps aux | grep 'o2-dpl-run --session' | grep -v grep | awk '{print \$2}' | xargs kill -s INT --;"
fi

if [ "0$FST_TMUX_LOGPREFIX" != "0" ]; then
  LOGCMD0=" &> ${FST_TMUX_LOGPREFIX}_0.log"
  LOGCMD1=" &> ${FST_TMUX_LOGPREFIX}_1.log"
fi

rm -f /dev/shm/*fmq*

tmux \
    new-session  "sleep  0; NUMAID=0 $MYDIR/dpl-workflow.sh $LOGCMD0; $ENDCMD" \; \
    split-window "sleep 45; NUMAID=1 $MYDIR/dpl-workflow.sh $LOGCMD1; $ENDCMD" \; \
    split-window "sleep 90; SEVERITY=debug numactl --interleave=all $MYDIR/$CMD; $KILLCMD $ENDCMD" \; \
    select-layout even-vertical
