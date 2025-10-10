#!/bin/bash

if [ $# -lt 1 ]
then
  echo "Internal ERROR mnvcleaning.sh - Not enough arguments were given"
  exit -1
fi
pattern=$@

for name in "$pattern"; do
	echo $pattern
	echo $name
	sed -i 's/ui_cmd_cb (mpiexec\/pmiserv_pmci.c:51): Launch proxy failed./MNV cleaning/g' $name
	sed -i 's/HYDT_dmxu_poll_wait_for_event (lib\/tools\/demux\/demux_poll.c:76): callback returned error status/MNV cleaning/g' $name
	sed -i 's/HYD_pmci_wait_for_completion (mpiexec\/pmiserv_pmci.c:173): error waiting for event/MNV cleaning/g' $name
	sed -i 's/main (mpiexec\/mpiexec.c:260): process manager error waiting for completion/MNV cleaning/g' $name
done
