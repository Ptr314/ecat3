// Стенд для снятия времени команд КМ1801ВМ3 по синхронной модели кристалла
// (github 1801BM1/cpu11, vm3/hdl/syn). Один период clk - один такт CLC: модель
// работает на обоих фронтах (pin_clk_p / pin_clk_n), ВМ3 вход CLC пополам не
// делит.
//
// Память - 32К слов ниже 0100000, образ из prog.mem ($readmemh, слова). Ответ
// RPLY приходит через RDLY тактов после DIN или DOUT (параметр +RDLY=n,
// по умолчанию 1) и снимается, когда снят строб. Пуск - по вектору 024/026
// (вход BSEL снят). Стенд пишет журнал шины: такт спада SYNC с адресом,
// такты DIN/DOUT и снятия строба. Конец - команда HALT (спад HLTM) или
// предел тактов.
`timescale 1ns/1ps

module tb_vm3();

reg         clk;
reg         dclo, aclo;
tri1        init;
reg         init_drv;
assign      init = init_drv ? 1'b0 : 1'bz;
tri1 [15:0] ad;
tri1 [21:16] a;
tri1        sync, din, dout, wtbt, iako, bs, umap;
wire        hltm, sel;
reg         rply;
tri1        xrply;
assign      xrply = rply ? 1'bz : 1'b0;

reg  [15:0] mem [0:32767];
reg  [15:0] addr;
reg  [15:0] ad_reg;
reg         ad_oe;
integer     cyc, rdly, limit, i;
integer     t_sync;
reg  [8*256-1:0] memfile;
reg         byte_w;

assign ad = ad_oe ? ~ad_reg : 16'hZZZZ;

initial begin
   for (i = 0; i < 32768; i = i + 1) mem[i] = 16'o000000;
   if (!$value$plusargs("mem=%s", memfile)) memfile = "prog.mem";
   $readmemh(memfile, mem);
   // Программы форм пишутся для ВМ2 (пуск по 000000); ВМ3 берёт PC и PSW из
   // 024/026
   if ($test$plusargs("vm2start")) begin
      mem[10] = mem[0];
      mem[11] = 16'o000340;
      // Ловушки (нечётный адрес, резервная команда) - на
      // RTI по 057700: случай с ловушкой отбраковывается, а программа идёт
      // дальше, а не теряется до конца части
      mem[16'o27740] = 16'o000002;
      mem[2] = 16'o057700;  mem[3] = 16'o000340;
      mem[4] = 16'o057700;  mem[5] = 16'o000340;
      // Вектора 250 (диспетчер памяти) нет: с 000100 идёт код форм, а
      // диспетчер не включается
   end
   if (!$value$plusargs("RDLY=%d", rdly)) rdly = 1;
   if (!$value$plusargs("LIMIT=%d", limit)) limit = 2000000;
end

initial begin
   clk = 1;
   forever begin
      #10 clk = 0;
      #10 clk = 1;
   end
end

initial cyc = 0;
always @(posedge clk) begin
   cyc = cyc + 1;
   if (cyc > limit) begin
      $display("LIMIT %0d", cyc);
      $finish;
   end
end

always @(negedge sync) begin
   addr   = ~ad;
   t_sync = cyc;
   $display("S %0d %06o", cyc, ~ad);
end

always @(negedge din) begin
   if (~sync & iako) begin
      $display("I %0d", cyc);
      ad_reg = (addr < 16'o100000) ? mem[addr[15:1]] : 16'o000000;
      ad_oe  = 1'b1;
      repeat (rdly) @(posedge clk);
      #2 rply = 1'b0;
   end
end

always @(negedge dout) begin
   if (addr == 16'o177700) begin
      $display("END %0d", cyc);
      $finish;
   end
   // Данные записи модель выставляет после спада DOUT: берутся на следующем
   // фронте CLC, ответ - через RDLY тактов от DOUT, как и раньше
   @(posedge clk);
   #1;
   $display("O %0d %06o", cyc, ~ad);
   byte_w = ~wtbt;
   // Данные форм (060000-075777) - указатели 070000, только для чтения:
   // формы пишут туда #1 и прочее, и указатель следующей формы стал бы
   // нечётным - у ВМ3 это ловушка по 4, у ВМ2, под который писались
   // программы, нет. Стек (076000-077777) и код пишутся как обычно
   if (addr < 16'o060000 || (addr >= 16'o076000 && addr < 16'o100000)) begin
      if (byte_w) begin
         if (addr[0]) mem[addr[15:1]][15:8] = ~ad[15:8];
         else         mem[addr[15:1]][7:0]  = ~ad[7:0];
      end else
         mem[addr[15:1]] = ~ad;
   end
   if (rdly > 1) repeat (rdly - 1) @(posedge clk);
   #2 rply = 1'b0;
end

always @(posedge din or posedge dout) begin
   $display("E %0d", cyc);
   #2;
   ad_oe = 1'b0;
   rply  = 1'b1;
end

always @(negedge hltm) begin
   if (cyc > 100) begin
      $display("HALT %0d", cyc);
      $finish;
   end
end

initial begin
   // INIT держится, пока держится DCLO: без него внутренний признак ответа
   // (irply) модели остаётся неопределённым и шина не начинает работать
   dclo = 0; aclo = 0; rply = 1; ad_oe = 0; ad_reg = 0; init_drv = 1;
   repeat (10) @(negedge clk);
   dclo = 1;
   init_drv = 0;
   repeat (12) @(negedge clk);
   #1 aclo = 1;
end

vm3 cpu
(
   .pin_clk_p(clk),
   .pin_clk_n(~clk),
   .pin_init_n(init),
   .pin_dclo_n(dclo),
   .pin_aclo_n(aclo),
   .pin_halt_n(1'b1),
   .pin_evnt_n(1'b1),
   .pin_virq_n(4'b1111),
   .pin_ad_n(ad),
   .pin_sync_n(sync),
   .pin_wtbt_n(wtbt),
   .pin_dout_n(dout),
   .pin_din_n(din),
   .pin_iako_n(iako),
   .pin_rply_n(xrply),
   .pin_a_n(a),
   .pin_umap_n(umap),
   .pin_bs_n(bs),
   .pin_hltm_n(hltm),
   .pin_sel_n(sel),
   .pin_bsel_n(1'b1)
);

endmodule
