#!/bin/bash

# convenience script to build all of the FreeRTOS targets

cmd=${1:-exe}

# exit when any command fails
set -e


echo ----------------------------- building rtos_kUser ------------------------------
make TARGET=rtos_kUser $cmd

echo --------------------------- building rtos_kUser.xMR ----------------------------
make TARGET=rtos_kUser.xMR $cmd

echo ------------------------- building rtos_kUser.app.xMR --------------------------
make TARGET=rtos_kUser.app.xMR $cmd

echo ------------------------------- building rtos_mm -------------------------------
make TARGET=rtos_mm $cmd

echo ----------------------------- building rtos_mm.xMR -----------------------------
make TARGET=rtos_mm.xMR $cmd

echo ----------------------------- building rtos_mm.app.xMR -----------------------------
make TARGET=rtos_mm.app.xMR $cmd

echo ---------------------------- building rtos_fat_demo ----------------------------
make TARGET=rtos_fat_demo $cmd

echo -------------------------- building rtos_fat_demo.xMR --------------------------
make TARGET=rtos_fat_demo.xMR $cmd

echo -------------------------- building rtos_fat_demo.app.xMR --------------------------
make TARGET=rtos_fat_demo.app.xMR $cmd
