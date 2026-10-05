#!/bin/bash
# Прогон программы на модели К1801ВМ2; лог шины - в log_NAME.txt текущего каталога.
#
#   run_sim.sh NAME MEM [WINDOW]
#
# WINDOW 1 - память ниже 0100000 отвечает в окне КЦГД, 0 (по умолчанию) - сразу.
# TRACE=1 в окружении добавляет трассу событий шины (её читает extract.py).
# CPU11 - клон github.com/1801BM1/cpu11; iverilog и vvp должны быть в PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
S=${CPU11:?set CPU11 to a clone of 1801BM1/cpu11}/vm2/hdl
n=$1
iverilog -g2005 -o tb_$n.vvp -s tb_vm2 \
  -P tb_vm2.WINDOW=${3:-0} -P tb_vm2.TRACE=${TRACE:-0} \
  $S/syn/rtl/vm2.v $S/syn/rtl/vm2_qbus.v $S/wbc/rtl/vm2_plm.v "$HERE/tb_vm2.v"
vvp -n tb_$n.vvp +mem=$2 > log_$n.txt 2>&1
rm -f tb_$n.vvp
