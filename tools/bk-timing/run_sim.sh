#!/bin/bash
# Simulates a program on the К1801ВМ1 replica with the ВП1-037 as the RAM
# controller and writes the bus trace to log_NAME.txt in the current directory.
#
#   run_sim.sh NAME CPU_HALF_NS C037_HALF_NS FAST_CODE FAST_DATA SKEW_NS MEM
#
# CPU_HALF_NS 166.667 is a 3 MHz processor, 125 is 4 MHz, 83.333 is 6 MHz; the
# 037 always runs at 6 MHz (83.333). FAST_CODE / FAST_DATA 1 answer code
# (below 060000) / data (from 060000) without the 037, as a static memory would.
# CPU11 and K1801 point at clones of github.com/1801BM1/cpu11 and /k1801;
# iverilog and vvp have to be on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
S=${CPU11:?set CPU11 to a clone of 1801BM1/cpu11}/vm1/hdl
K=${K1801:?set K1801 to a clone of 1801BM1/k1801}
n=$1
iverilog -g2005 -DGATE037 -o tb_$n.vvp -s tb_bk \
  -P tb_bk.CPU_HALF=$2 -P tb_bk.C037_HALF=$3 -P tb_bk.FAST_CODE=$4 -P tb_bk.FAST_DATA=$5 \
  -P tb_bk.C037_SKEW=$6 -P tb_bk.TRACE=1 \
  -I $S/syn/tbe $S/syn/rtl/vm1.v $S/syn/rtl/vm1_simlib.v $S/syn/rtl/vm1_qbus.v \
  $S/wbc/rtl/vm1_tve.v $S/wbc/rtl/vm1_plm.v $S/syn/tbe/config.v \
  $K/lib/rtl/lib_1801.v $K/037/rtl/vp_037.v "$HERE/tb_bk.v"
vvp -n tb_$n.vvp +mem=$7 > log_$n.txt 2>&1
tail -1 log_$n.txt
