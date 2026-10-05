// КМ1801ВМ2 (синхронная модель github 1801BM1/cpu11, vm2/hdl/syn) на шине со
// статической памятью или с видеопамятью КЦГД. Пишет лог обращений и, с
// TRACE = 1, трассу событий шины во времени в тактах CLC.
//
// Кристалл делит вход CLC пополам на фазы f1/f2 (vm2/hdl/org: f1 <= ~f2 по
// спаду CLC); синхронная модель тактируется самой f1. Поэтому половина такта
// модели - это один такт CLC, и все времена ниже - в тактах CLC.
//
// Память КЦГД (WINDOW = 1): D33 делит 30,8 МГц, CLC - его разряд QB, при
// переполнении он загружает 4 * CLCO (CLCO = ~f1), что подстраивает
// знакоместо (16 тактов кварца, 4 такта CLC) под фазу процессора. Обращение
// к адресу ниже 0100000 ждёт конца знакоместа 1 из каждых 3 (D61, D70), RPLY
// встаёт в конце следующего знакоместа (D30, D19) и держится до снятия строба.
`timescale 1ns / 10ps

module tb_vm2();

parameter real OSC_HALF = 16.25;    // ~30,8 МГц, точно в шаге 10 пс: CLC = 7,69 МГц
parameter WINDOW = 0;               // 1 - видеопамять КЦГД ниже 0100000, 2 - только данные
                                    // (060000-077777), 3 - только код (ниже 060000)
parameter TRACE = 0;
parameter LIMIT = 400000000;

reg osc = 0;
initial forever #(OSC_HALF) osc = ~osc;

reg [3:0] cnt = 0;
wire clko;
wire clc = cnt[1];
reg f1 = 0, f2 = 0;
always @(negedge clc) f1 <= ~f2;
always @(posedge clc) f2 <= f1;

integer nclc = 0;
always @(posedge clc) nclc = nclc + 1;
real clc_period = OSC_HALF * 8;
`define HT ($realtime / clc_period)

reg dclo = 0, aclo = 0, ar = 1;
tri1 [15:0] ad;
tri1 din, dout, wtbt, sync, init, iako, sel;
wire dmgo, wrq;

// ---------------------------------------------------------------- память
reg [15:0] mem [0:32767];
reg [15:0] mem0 [0:32767];
integer ri;
reg [8*64:1] memfile;
initial begin
   if (!$value$plusargs("mem=%s", memfile)) memfile = "test.mem";
   $readmemh(memfile, mem);
   for (ri = 0; ri < 32768; ri = ri + 1) mem0[ri] = mem[ri];
end

always @(negedge sync) ar = 0;
always @(posedge sync) ar = 1;

reg [15:0] addr;
always @(negedge sync) addr = ~ad;

function slow(input [15:0] a);
   slow = (WINDOW == 1) ? (a < 16'o100000) :
          (WINDOW == 2) ? (a >= 16'o060000 && a < 16'o100000) :
          (WINDOW == 3) ? (a < 16'o060000) : 0;
endfunction

reg drive = 0;
reg [15:0] rdata;
assign ad = drive ? ~rdata : 16'hZZZZ;
reg virq_n = 1, evnt_n = 1;
always @(negedge din) begin
   if (~sync) begin
      rdata = (addr == 16'o177716) ? 16'o000000 : mem[addr[15:1]];
      drive = 1;
   end
end
// IAKO приходит после DIN: вектор VIRQ и ответ - по нему
always @(negedge iako) if (~din) begin
   rdata = 16'o000340; drive = 1;
   virq_n = 1;
end
always @(posedge din) drive = 0;

always @(negedge dout) if (~sync) begin
   if (addr == 16'o177700) begin
      $display("END %0d", nclc);
      $finish;
   end
   if (addr == 16'o177702)
      for (ri = 16'o060000 >> 1; ri < 32768; ri = ri + 1) mem[ri] = mem0[ri];
   if (addr == 16'o177706) virq_n = 0;
   // код не пишется никогда: случайный указатель не должен портить программу
   if (addr >= 16'o060000 && addr < 16'o160000) begin
      if (~wtbt) begin
         if (addr[0]) mem[addr[15:1]][15:8] = ~ad[15:8];
         else         mem[addr[15:1]][7:0]  = ~ad[7:0];
      end else
         mem[addr[15:1]] = ~ad;
   end
end
// импульс EVNT на 100 тактов f1 - отдельно, чтобы не пропустить запись за ним
always @(negedge dout) if (~sync && addr == 16'o177704) begin
   evnt_n = 0; repeat (100) @(negedge f1); evnt_n = 1;
end

// ---------------------------------------------------------------- ответ
wire strobe = ~din | ~dout;
// быстрая память: сразу (как стенд автора модели)
reg rply_f = 0;
always @(negedge din or negedge dout) if (~sync && !slow(addr)) #2 rply_f = 1;
always @(negedge iako) if (~din) #2 rply_f = 1;
always @(posedge din or posedge dout) if (din && dout) rply_f = 0;

// КЦГД: D33 и окно
integer nslot = 0;
reg u1 = 0, rply_w = 0;
wire u37 = (nslot % 3 == 1) && strobe && ~sync && slow(addr);
always @(posedge osc) begin
   if (cnt == 15) begin
      if (TRACE && WINDOW != 0 && (nslot % 3 == 1)) $display("E Z %06o %0.2f", nslot % 3, `HT);
      if (u1 && !u37) rply_w <= #2 1;
      u1 <= u37;
      nslot = nslot + 1;
      cnt <= {1'b0, (clko === 1'b1), 2'b00};
   end else cnt <= cnt + 1;
end
always @(posedge din or posedge dout) if (din && dout) rply_w <= 0;

wire rply_n = ~(rply_f | rply_w);

// ---------------------------------------------------------------- лог
always @(negedge din)  if (~sync && iako) $display("R %06o %0d", addr, nclc);
always @(negedge dout) if (~sync)         $display("W %06o %0d", addr, nclc);
always @(negedge sync) if (TRACE) $display("E S %06o %0.2f", ~ad, `HT);
always @(posedge sync) if (TRACE) $display("E s %06o %0.2f", addr, `HT);
always @(negedge din)  if (TRACE) begin if (sync) $display("E Q 000000 %0.2f", `HT); else $display("E I %06o %0.2f", addr, `HT); end
always @(posedge din)  if (TRACE) $display("E i %06o %0.2f", addr, `HT);
always @(negedge dout) if (TRACE) $display("E O %06o %0.2f", addr, `HT);
always @(posedge dout) if (TRACE) $display("E o %06o %0.2f", addr, `HT);
always @(negedge rply_n) if (TRACE) $display("E P %06o %0.2f", addr, `HT);
always @(posedge rply_n) if (TRACE) $display("E p %06o %0.2f", addr, `HT);
always @(negedge iako) if (TRACE) $display("E A 000000 %0.2f", `HT);

// ---------------------------------------------------------------- процессор
vm2 cpu(
   .pin_clk_p(f1), .pin_clk_n(~f1),
   .pin_init_n(init), .pin_dclo_n(dclo), .pin_aclo_n(aclo),
   .pin_halt_n(1'b1), .pin_evnt_n(evnt_n), .pin_virq_n(virq_n),
   .pin_ar_n(ar), .pin_dmr_n(1'b1), .pin_sack_n(1'b1), .pin_rply_n(rply_n),
   .pin_waki_n(1'b0), .pin_wrq_n(wrq), .pin_clko(clko), .pin_dmgo_n(dmgo),
   .pin_ad_n(ad), .pin_sel_n(sel), .pin_sync_n(sync), .pin_wtbt_n(wtbt),
   .pin_dout_n(dout), .pin_din_n(din), .pin_iako_n(iako));

initial begin
   repeat (20) @(negedge clc);
   dclo = 1;
   repeat (20) @(negedge clc);
   aclo = 1;
   #(LIMIT) $display("TIMEOUT"); $finish;
end
endmodule
