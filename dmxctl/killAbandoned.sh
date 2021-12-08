#!/bin/sh
ps aux | awk '/dmxctl-child/ && !/grep/ && !/awk/ { print $2 }' | xargs -L1 kill
