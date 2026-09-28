#!/bin/bash
# Simulates a program on the К1801ВМ1 replica with the ВП1-037 as the RAM
# controller and writes the bus log to log_NAME.txt in the current directory.
#
#   run_sim.sh NAME CPU_HALF_NS C037_HALF_NS FAST_CODE FAST_DATA SKEW_NS MEM [RSYNC [CPU_HIGH_NS]]
#
# CPU_HALF_NS 166.667 is a 3 MHz processor, 125 is 4 MHz, 83.333 is 6 MHz; the
# 037 always runs at 6 MHz (83.333). FAST_CODE / FAST_DATA 1 answer code
# (below 060000) / data (from 060000) without the 037, as a static memory would.
# RSYNC 1 puts the flip-flop the БК has on the RPLY input (see tb_bk.v).
# TRACE=1 in the environment adds the half-clock event trace (needed by
# extract.py); RPLY_ACK1=1 restores rply_ack[1] in dout_start of the model,
# which the author changed to rply_ack[2] (it does not change any time).
# CPU11 and K1801 point at clones of github.com/1801BM1/cpu11 and /k1801;
# iverilog and vvp have to be on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
S=${CPU11:?set CPU11 to a clone of 1801BM1/cpu11}/vm1/hdl
K=${K1801:?set K1801 to a clone of 1801BM1/k1801}
n=$1
QBUS=$S/syn/rtl/vm1_qbus.v
if [ "${RPLY_ACK1:-0}" = 1 ]; then
  sed 's/~rply_ack\[2\]; \/\/ originally ~rply_ack\[1\]/~rply_ack[1];/' $QBUS > vm1_qbus_ack1.v
  QBUS=vm1_qbus_ack1.v
fi
iverilog -g2005 -DGATE037 -o tb_$n.vvp -s tb_bk \
  -P tb_bk.CPU_HALF=$2 -P tb_bk.C037_HALF=$3 -P tb_bk.FAST_CODE=$4 -P tb_bk.FAST_DATA=$5 \
  -P tb_bk.C037_SKEW=$6 -P tb_bk.RSYNC=${8:-0} -P tb_bk.CPU_HIGH=${9:-0} -P tb_bk.TRACE=${TRACE:-0} \
  -I $S/syn/tbe $S/syn/rtl/vm1.v $S/syn/rtl/vm1_simlib.v $QBUS \
  $S/wbc/rtl/vm1_tve.v $S/wbc/rtl/vm1_plm.v $S/syn/tbe/config.v \
  $K/lib/rtl/lib_1801.v $K/037/rtl/vp_037.v "$HERE/tb_bk.v"
vvp -n tb_$n.vvp +mem=$7 > log_$n.txt 2>&1
tail -1 log_$n.txt
