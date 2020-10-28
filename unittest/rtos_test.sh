#!/bin/bash

# Script to test all of the RTOS benchmarks

# normal timeout is 8 seconds, but can make it longer if needed
runTime=${1:-10}

# exit when any command fails
set -e

# save current dir
this_dir=$(pwd)

# cool printing code
# https://unix.stackexchange.com/a/267730
center() {
#   termwidth="$(tput cols)"
  termwidth=80
  padding="$(printf '%0.1s' -{1..200})"
  printf '%*.*s %s %*.*s\n' 0 "$(((termwidth-2-${#1})/2))" "$padding" "$1" 0 "$(((termwidth-1-${#1})/2))" "$padding"
}

# First build everything using the convenience script
cd rtos/pynq
./make_all.sh

# Then we try running everything
# When testing manually, have to press 'ctrl+a, ctrl+x' to exit
# So we have to kill each test, but still see if it worked right
target_list=(rtos_kUser rtos_kUser.xMR rtos_kUser.app.xMR rtos_mm rtos_mm.xMR rtos_mm.app.xMR)

for tgt in "${target_list[@]}"; do
    # why does this not work?
    # timeout -s 'INT' "$runTime" make TARGET="$tgt" qemu-dbg
    center "running $tgt"
    # just start it in the background instead
    make TARGET="$tgt" qemu &
    LAST_PID=$!
    sleep "$runTime"
    kill -s 'TERM' $LAST_PID
done

cd $this_dir
echo Done!
