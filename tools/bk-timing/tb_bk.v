// VM1 (1801BM1 synchronous replica) + 1801VP1-037 as the DRAM controller.
// Logs every bus transaction with the CPU clock number it started at.
`timescale 1ns / 100ps

module tb_bk();

// Base tick: 1 unit. Half periods are parameters, set from the command line.
parameter real CPU_HALF  = 4;     // 3 MHz CPU: period 8 units
parameter real C037_HALF = 2;     // 6 MHz 037: period 4 units
parameter real C037_SKEW = 0;     // phase offset of the 037 clock, units
parameter FAST_CODE = 0;     // addresses below 060000 answered without the 037
parameter FAST_DATA = 0;     // addresses from 060000 answered without the 037
parameter real RDELAY    = 0;
parameter RSYNC = 0;
parameter TRACE = 0;
parameter real ODELAY = 0;   // delay of the CPU bus outputs as the 037 sees them, ns
parameter DUMP_FROM = 0, DUMP_TO = 0;          // 0 none, 1 posedge, 2 negedge, 3 negedge then posedge, 4 posedge then negedge     // rise time of the released RPLY line, units
parameter LIMIT     = 400000000;

reg clk = 0, clk037 = 0;
always #(CPU_HALF) clk = ~clk;
initial begin #(C037_SKEW); forever #(C037_HALF) clk037 = ~clk037; end

integer ncpu = 0;
always @(posedge clk) ncpu = ncpu + 1;

reg         dclo = 0, aclo = 0;
tri1 [15:0] ad;
tri1        din, dout, wtbt, sync, rply, dmr, sack, iako, bsy, init;
tri1 [2:1]  sel;
wire        dmgo;

// ---------------------------------------------------------------- memory
reg [15:0] mem [0:32767];
reg [15:0] mem0 [0:32767];     // the data area is restored from here on a marker write
integer ri;
reg [8*64:1] memfile;
initial begin
   if (!$value$plusargs("mem=%s", memfile)) memfile = "test.mem";
   $readmemh(memfile, mem);
   for (ri = 0; ri < 32768; ri = ri + 1) mem0[ri] = mem[ri];
end

reg  [15:0] addr;
reg         drive = 0;
reg  [15:0] rdata;
assign ad = drive ? ~rdata : 16'hZZZZ;

always @(negedge sync) addr = ~ad;

function is_fast(input [15:0] a);
   is_fast = (a >= 16'o100000) ? 1 :
             (a <  16'o060000) ? FAST_CODE : FAST_DATA;
endfunction

// read data: present as soon as DIN is asserted, for any memory
always @(negedge din) if (~sync) begin
   rdata = (addr == 16'o177716) ? 16'o000000 : mem[addr[15:1]];
   drive = 1;
end
always @(posedge din) drive = 0;

always @(negedge dout) if (~sync) begin
   if (addr == 16'o177700) begin
      $display("END %0d", ncpu);
      $finish;
   end
   if (addr == 16'o177702)
      for (ri = 16'o060000 >> 1; ri < 32768; ri = ri + 1) mem[ri] = mem0[ri];
   // the code area is never written: a stray pointer must not destroy the program
   if (addr >= 16'o060000 && addr < 16'o160000) begin
      if (~wtbt) begin
         if (addr[0]) mem[addr[15:1]][15:8] = ~ad[15:8];
         else         mem[addr[15:1]][7:0]  = ~ad[7:0];
      end else
         mem[addr[15:1]] = ~ad;
   end
end

// fast reply: next falling CPU clock edge after the strobe, released after it
reg frply = 1;
// interrupt requests driven by marker writes: 177704 pulses IRQ2 (vector 100),
// 177706 raises VIRQ until the processor acknowledges it (vector from IAKO)
reg [3:1] irq_n = 3'b111;
reg virq_n = 1;
always @(negedge dout) if (~sync) begin
   if (addr == 16'o177704) begin
      irq_n[2] = 0; repeat (100) @(negedge clk); irq_n[2] = 1;
   end
   if (addr == 16'o177706) virq_n = 0;
end
always @(negedge iako) if (~din) begin
   rdata = 16'o000340; drive = 1;
   @(negedge clk); frply = 0; virq_n = 1;
end
always @(negedge din or negedge dout) if (~sync && is_fast(addr)) begin
   @(negedge clk); frply = 0;
end
always @(posedge din or posedge dout) begin
   @(negedge clk); frply = 1;
end

// ---------------------------------------------------------------- 037
// The 037 sees A15 set for any address the testbench answers itself
wire [15:0] ad037_in = is_fast(~sync ? addr : ~ad) ? (ad & 16'h7FFF) : ad;
wire        nrply037;
wire [6:0]  ra;
wire [1:0]  ncas;
wire        nras, nwe, ne, nbs, wti, wtd, nvs;
reg         r037 = 1;
wire sync_d = sync;
wire #(ODELAY) din_d = din;
wire #(ODELAY) dout_d = dout;
tri1 [15:0] ad037_bus;
assign ad037_bus = ad037_in;

`ifdef GATE037
vp_037 c037(
`else
va_037 c037(
`endif
   .PIN_nSYNC(sync_d), .PIN_nDIN(din_d), .PIN_nDOUT(dout_d),
   .PIN_CLK(clk037), .PIN_R(r037), .PIN_C(1'b0),
   .PIN_nAD(ad037_bus), .PIN_nWTBT(wtbt), .PIN_nRPLY(nrply037),
   .PIN_A(ra), .PIN_nCAS(ncas), .PIN_nRAS(nras), .PIN_nWE(nwe),
   .PIN_nE(ne), .PIN_nBS(nbs), .PIN_WTI(wti), .PIN_WTD(wtd), .PIN_nVSYNC(nvs));

reg rply037_d = 1;
always @(negedge nrply037) rply037_d = 0;
always @(posedge nrply037) if (RDELAY == 0) rply037_d = 1; else rply037_d <= #(RDELAY) 1'b1;
reg rs1 = 1, rs2 = 1;
always @(posedge clk) begin if (RSYNC == 1) rs1 <= rply037_d; if (RSYNC == 3) rs2 <= rs1; if (RSYNC == 4) rs1 <= rply037_d; end
always @(negedge clk) begin if (RSYNC == 2) rs1 <= rply037_d; if (RSYNC == 3) rs1 <= rply037_d; if (RSYNC == 4) rs2 <= rs1; end
wire rply037_s = (RSYNC == 0) ? rply037_d : (RSYNC >= 3) ? rs2 : rs1;
assign rply = (frply & rply037_s) ? 1'bz : 1'b0;

// ---------------------------------------------------------------- log
always @(negedge din)  if (~sync && iako) $display("R %06o %0d", addr, ncpu);
always @(negedge dout) if (~sync)         $display("W %06o %0d", addr, ncpu);

`ifndef GATE037
always @(clk037) if (ncpu >= DUMP_FROM && ncpu < DUMP_TO)
   $display("T %0d clk=%b c037=%b PC=%0d sync=%b din=%b dout=%b rply=%b rasel=%b trply=%b a=%06o", ncpu, clk, clk037,
      c037.PC, sync, din, dout, rply, c037.RASEL, c037.TRPLY, addr);
`endif
// half-clock resolution event trace: time in CPU half periods
`define HT ($realtime / CPU_HALF)
always @(negedge sync) if (TRACE) $display("E S %06o %0.2f", ~ad, `HT);
always @(posedge sync) if (TRACE) $display("E s %06o %0.2f", addr, `HT);
always @(negedge din)  if (TRACE) begin if (sync) $display("E Q 000000 %0.2f", `HT); else $display("E I %06o %0.2f", addr, `HT); end
always @(posedge din)  if (TRACE) $display("E i %06o %0.2f", addr, `HT);
always @(negedge dout) if (TRACE) $display("E O %06o %0.2f", addr, `HT);
always @(posedge dout) if (TRACE) $display("E o %06o %0.2f", addr, `HT);
always @(negedge rply) if (TRACE) $display("E P %06o %0.2f", addr, `HT);
always @(posedge rply) if (TRACE) $display("E p %06o %0.2f", addr, `HT);
// ---------------------------------------------------------------- cpu
vm1 cpu0(
`ifdef ORGVM1
   .pin_clk(clk), .pin_pa_n(2'b11),
`else
   .pin_clk_p(clk), .pin_clk_n(~clk), .pin_ena(1'b1), .pin_pa_n(2'b11),
`endif
   .pin_init_n(init), .pin_dclo_n(dclo), .pin_aclo_n(aclo),
   .pin_irq_n(irq_n), .pin_virq_n(virq_n),
   .pin_ad_n(ad), .pin_dout_n(dout), .pin_din_n(din), .pin_wtbt_n(wtbt),
   .pin_sync_n(sync), .pin_rply_n(rply), .pin_dmr_n(dmr), .pin_sack_n(sack),
   .pin_dmgi_n(1'b1), .pin_dmgo_n(dmgo), .pin_iako_n(iako), .pin_sp_n(1'b1),
   .pin_sel_n(sel), .pin_bsy_n(bsy));

initial begin
   repeat (4) @(negedge clk);
   r037 = 0;
   dclo = 1;
   repeat (8) @(negedge clk);
   aclo = 1;
   #(LIMIT) $display("TIMEOUT"); $finish;
end
endmodule
