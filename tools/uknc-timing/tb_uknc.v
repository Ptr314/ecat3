// КМ1801ВМ2 (синхронная модель cpu11) на памяти с окнами процессора: каждое
// обращение занимает несколько окон подряд, RPLY - через заданное время после
// последнего. Запись может быть отложенной: RPLY сразу, окна - потом, и
// следующее обращение ждёт, пока они не пройдут.
//
// Плюс-аргументы (времена в нс, остальное - в тактах контроллера):
//   clc, mclk, mph - период CLC, период такта контроллера, сдвиг его фазы
//   slots, mask    - цикл расписания и окна процессора в нём (разряды)
//   nr, nw, nwb    - окон на чтение, запись слова, запись байта
//   cr, cw, cwb    - память занята после начала цикла (чтение, запись слова,
//                    байта): следующий цикл - не раньше
//   cm, cmb        - то же у записи в цикле чтение-модификация-запись
//                    (слово, байт); по умолчанию cw, cwb
//   rd, wr         - от последнего окна до RPLY (чтение / запись)
//   post           - 1: запись отложенная, RPLY через wr от строба
//   latch, rdl     - 1: повторное чтение того же слова без окон, RPLY через rdl
//   setup          - строб не позже, чем за столько нс до окна
//   fast, fd       - адреса от fast (восьмеричный) отвечают без окон, через
//                    fd тактов контроллера: регистр
`timescale 1ns / 10ps

module tb_uknc();

parameter TRACE = 0;
parameter LIMIT = 5000000;

real clc_half, mclk_half, mph, setup;
integer slots, mask, rd, wr, nr, nw, nwb, post, latch, rdl, cr, cw, cwb, cm, cmb, fast, fd;
initial begin
   if (!$value$plusargs("clc=%f", clc_half)) clc_half = 160;
   clc_half = clc_half / 2;
   if (!$value$plusargs("mclk=%f", mclk_half)) mclk_half = 80;
   mclk_half = mclk_half / 2;
   if (!$value$plusargs("mph=%f", mph)) mph = 0;
   if (!$value$plusargs("setup=%f", setup)) setup = 0;
   if (!$value$plusargs("slots=%d", slots)) slots = 1;
   if (!$value$plusargs("mask=%d", mask)) mask = 1;
   if (!$value$plusargs("rd=%d", rd)) rd = 0;
   if (!$value$plusargs("wr=%d", wr)) wr = 0;
   if (!$value$plusargs("nr=%d", nr)) nr = 1;
   if (!$value$plusargs("nw=%d", nw)) nw = 1;
   if (!$value$plusargs("nwb=%d", nwb)) nwb = 1;
   if (!$value$plusargs("post=%d", post)) post = 0;
   if (!$value$plusargs("latch=%d", latch)) latch = 0;
   if (!$value$plusargs("rdl=%d", rdl)) rdl = 0;
   if (!$value$plusargs("cr=%d", cr)) cr = 0;
   if (!$value$plusargs("cw=%d", cw)) cw = 0;
   if (!$value$plusargs("cwb=%d", cwb)) cwb = 0;
   if (!$value$plusargs("fast=%o", fast)) fast = 0;
   if (!$value$plusargs("fd=%d", fd)) fd = 0;
   if (!$value$plusargs("cm=%d", cm)) cm = cw;
   if (!$value$plusargs("cmb=%d", cmb)) cmb = cwb;
end

reg clc = 0;
initial begin #1; forever #(clc_half) clc = ~clc; end
reg mclk = 0;
initial begin #1; #(mph); forever #(mclk_half) mclk = ~mclk; end

reg f1 = 0, f2 = 0;
always @(negedge clc) f1 <= ~f2;
always @(posedge clc) f2 <= f1;

integer nclc = 0;
always @(posedge clc) nclc = nclc + 1;
`define HT ($realtime / (2 * clc_half))

reg dclo = 0, aclo = 0, ar = 1;
tri1 [15:0] ad;
tri1 din, dout, wtbt, sync, init, iako, sel;
wire dmgo, wrq, clko;

reg [15:0] mem [0:32767];
reg [8*64:1] memfile;
initial begin
   if (!$value$plusargs("mem=%s", memfile)) memfile = "test.mem";
   $readmemh(memfile, mem);
end

always @(negedge sync) ar = 0;
always @(posedge sync) ar = 1;

reg [15:0] addr;
always @(negedge sync) addr = ~ad;

reg drive = 0;
reg [15:0] rdata;
assign ad = drive ? ~rdata : 16'hZZZZ;
always @(negedge din) if (~sync) begin
   rdata = mem[addr[15:1]];
   drive = 1;
end
always @(posedge din) drive = 0;

// метки: запись в 177700 - конец, в 177710 - отметка времени
always @(negedge dout) if (~sync) begin
   if (addr == 16'o177700) begin
      $display("END %0d", nclc);
      $finish;
   end
   if (addr == 16'o177710) $display("M %0.3f", `HT);
   if (addr < 16'o160000) begin
      if (~wtbt) begin
         if (addr[0]) mem[addr[15:1]][15:8] = ~ad[15:8];
         else         mem[addr[15:1]][7:0]  = ~ad[7:0];
      end else
         mem[addr[15:1]] = ~ad;
   end
end

// ---------------------------------------------------------------- окна
wire strobe = (~din | ~dout) & ~sync;
real t_req;
reg pend = 0, served = 0;
reg [15:0] last_rd = 16'hFFFF;
reg had_din = 0;            // в этом цикле уже было чтение: запись - половина RMW
always @(negedge sync) had_din = 0;
always @(negedge din) if (~sync) had_din = 1;
always @(negedge din or negedge dout) if (~sync) begin t_req = $realtime; pend = 1; end
integer mcnt = 0, left = -1, need = 0, debt = 0, busy = 0;
reg rply = 0, isw = 0, waiting = 0, own = 0;
always @(posedge mclk) begin
   if (left > 0) begin
      left = left - 1;
      if (left == 0) begin rply <= #2 1; left = -1; end
   end
   if (pend && !served && strobe && ($realtime - t_req >= setup)) begin
      served = 1;
      isw = ~dout;
      if (fast != 0 && addr >= fast) begin
         left = fd;
         if (left == 0) begin rply <= #2 1; left = -1; end
      end else if (!isw && latch && addr[15:1] == last_rd[15:1]) begin
         left = rdl;
         if (left == 0) begin rply <= #2 1; left = -1; end
      end else begin
         need = !isw ? nr : (~wtbt ? nwb : nw);
         if (isw) last_rd = 16'hFFFF; else last_rd = addr;
         if (isw && post) begin
            debt = debt + need;
            left = wr;
            if (left == 0) begin rply <= #2 1; left = -1; end
         end else waiting = 1;
      end
   end
   own = ((mask >> (mcnt % slots)) & 1) && mcnt >= busy;
   if (own) begin
      if (debt > 0) begin
         debt = debt - 1;
         if (debt == 0) busy = mcnt + (~wtbt ? cwb : cw);
         if (TRACE) $display("E Z 000001 %0.2f", `HT);
      end else if (waiting) begin
         need = need - 1;
         if (TRACE) $display("E Z %06o %0.2f", addr, `HT);
         if (need == 0) begin
            waiting = 0;
            busy = mcnt + (!isw ? cr : had_din ? (~wtbt ? cmb : cm) : (~wtbt ? cwb : cw));
            left = isw ? wr : rd;
            if (left == 0) begin rply <= #2 1; left = -1; end
         end
      end
   end
   mcnt = mcnt + 1;
end
always @(posedge din or posedge dout) if (din && dout) begin
   rply <= 0; pend = 0; served = 0; left = -1; waiting = 0;
end
wire rply_n = ~rply;

// ---------------------------------------------------------------- трасса
always @(negedge din)  if (TRACE && ~sync) $display("E I %06o %0.2f", addr, `HT);
always @(posedge din)  if (TRACE) $display("E i %06o %0.2f", addr, `HT);
always @(negedge dout) if (TRACE) $display("E O %06o %0.2f", addr, `HT);
always @(posedge dout) if (TRACE) $display("E o %06o %0.2f", addr, `HT);
always @(negedge sync) if (TRACE) $display("E S %06o %0.2f", ~ad, `HT);

vm2 cpu(
   .pin_clk_p(f1), .pin_clk_n(~f1),
   .pin_init_n(init), .pin_dclo_n(dclo), .pin_aclo_n(aclo),
   .pin_halt_n(1'b1), .pin_evnt_n(1'b1), .pin_virq_n(1'b1),
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
