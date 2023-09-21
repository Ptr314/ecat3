;https://www.asm80.com/onepage/asm8080.html
;https://86rk.ru/disassm/
;http://dunfield.classiccmp.org//r/8080.txt

        .org 0xF800

        jmp start
start:  xra a               ;A=0
        sta 0xF900
        mvi a, 0x55         ;A=55
        mvi b, 0xAA         ;B=AA
        mov c, a            ;C=55
        mov a, b            ;A=AA
        lxi h, 0x1234       ;HL=1234, H=12, L=34
        lxi b, 0x4567       ;BC=4567, B=45, C=67
        lxi sp, 0x89AB      ;SP=89AB
        nop
        mvi a, 0x55
        sta 0x1000          ;[1000]=55
        mvi a, 0            ;A=0
        lda 0x1000          ;A=55
        nop
        lxi h, 0x1234       ;HL=1234
        shld 0x1000         ;[1000]=34, [1001]=12
        lxi h, 0            ;HL=0
        lhld 0x1000         ;HL=1234
        nop
        lxi h, 0x1234       ;HL=1234
        lxi d, 0x4567       ;DE=4567, D=45, E=67
        xchg                ;HL=4567, DE=1234
        nop
        mvi a, 0xFF
        mvi b, 0x01
        add b               ;A=00, z=1, s=0, p=1, c=1
        add b               ;A=01, z=0, s=0, p=0, c=0
        adi 0x7F            ;A=80, z=0, s=1, p=0, c=0
        nop
        stc                 ;c=1
        cmc                 ;c=0
        nop
        mvi a, 0xFF
        mvi c, 0x00
        stc                 ;c=1
        adc c               ;A=00, z=1, s=0, p=1, c=1
        cmc                 ;c=0
        mvi d, 0xFF
        adc d               ;A=FF, z=0, s=1, p=0, c=0
        stc
        aci 0x01            ;A=01, z=0, s=0, p=0, c=0
        nop
        mvi a, 0xFF         ;A=FF
        mvi e, 0xFF         ;E=FF
        sub e               ;A=0, z=1, s=0, p=1, c=0
        sui 0x01            ;A=FF, z=0, s=1, p=0, c=1
        mvi h, 0x01
        sbb h               ;A=FD, z=0, s=1, p=0, c=0
        stc                 ;c=1
        sbi 0xFC            ;A=00, z=1, s=0, p=1, c=0
        nop
        stc
        mvi l, 0xFF         ;L=FF
        inr l               ;L=00, z=1, s=0, p=1, c=1!
        inr l               ;L=01, z=0, s=0, p=0, c=1!
        cmc
        mvi a, 0xFF         ;A=FF
        inr a               ;A=00, z=1, s=0, p=1, c=0!
        inr a               ;A=01, z=0, s=0, p=0, c=0!
        stc
        cmc
        mvi a, 0x7F         ;A=7F
        inr a               ;A=80, z=0, s=1, p=1, c=0
        nop
        stc
        mvi a, 0x00
        dcr a               ;A=FF, z=0, s=1, p=0, c=1!
        cmc
        mvi a, 0x00
        dcr a               ;A=FF, z=0, s=1, p=0, c=0!
        mvi b, 0x01
        dcr b               ;B=00, z=1, s=0, p=1, c=0
        nop
        lxi sp, 0x1100      ;SP=1000
        lxi b, 0x1234
        lxi h, 0x5678
        mvi a, 0x55
        cpi 0x55            ;z=1, s=0, p=1, c=0
        stc                 ;c=1
        push b              ;SP=10FE, [10FE]=34, [10FF]=12
        push h              ;SP=10FC, [10FC]=78, [10FD]=56
        push psw            ;SP=10FA, [10FA]=45?, [10FB]=55
        pop d               ;DE=5545
        pop psw             ;A=56, F=78
        nop
        lxi h, 0x1234       ;HL=1234
        inx h               ;HL=1235
        lxi d, 0x0000       ;DE=0000
        dcx d               ;DE=FFFF
        nop
        stc
        cmc                 ;c=0
        lxi h, 0xFFFF       ;HL=FFFF
        lxi b, 0x0001
        dad b               ;HL=0, c=1
        nop
        mvi a, 0x55
        mvi b, 0xAA
        ana b               ;A=0, z=1, s=0, p=1, c=0
        mvi a, 0xAA
        ani 0xAA            ;A=AA, z=0, s=1, p=1, c=0
        nop
        mvi a, 0x55
        mvi b, 0xAA
        ora b               ;A=FF, z=0, s=1, p=0, c=0
        mvi a, 0xAA
        ori 0x55            ;A=FF, z=0, s=1, p=0, c=0
        nop
        mvi a, 0x55
        mvi b, 0xAB
        xra b               ;A=FE, z=0, s=1, p=1, c=0
        mvi a, 0xAA
        xri 0x55            ;A=FF, z=0, s=1, p=0, c=0
        nop
        mvi a, 0xFF         ;A=FF
        mvi e, 0xFF         ;E=FF
        cmp e               ;A=FF, z=1, s=0, p=1, c=0
        mvi a, 0x01
        cpi 0x02            ;A=01, z=0, s=1, p=0, c=1
        nop
        mvi a, 0xAA
        rlc                 ;A=55, c=1
        rlc                 ;A=AA, c=0
        rrc                 ;A=55, c=0
        rrc                 ;A=AA, c=1
        ral                 ;A=55, c=1
        ral                 ;A=AB, c=0
        rar                 ;A=55, c=1
        rar                 ;A=AA, c=1
        nop
        mvi a, 0xAA
        cma                 ;A=55
        nop
        jmp label
        nop
label:  nop                 ;PC is here
        lxi h, label2
        nop
        pchl
        nop
label2: nop                 ;PC is here
        lxi sp, 0           ;SP=0
        lxi h, 0x1234
        sphl                ;SP=1234
        nop
        lxi sp, 0x1200      ;SP=1200
        lxi h, 0x1234
        lxi b, 0x5678
        push b              ;SP=11FE
        xthl                ;HL=5678, [11FE]=34, [11FF]=12

cycle:  jmp cycle

;Inst      Encoding          Flags   Description
;----------------------------------------------------------------------
;+MOV D,S   01DDDSSS          -       Move register to register
;+MVI D,#   00DDD110 db       -       Move immediate to register
;+LXI RP,#  00RP0001 lb hb    -       Load register pair immediate
;+LDA a     00111010 lb hb    -       Load A from memory
;+STA a     00110010 lb hb    -       Store A to memory
;+LHLD a    00101010 lb hb    -       Load H:L from memory
;+SHLD a    00100010 lb hb    -       Store H:L to memory
;+LDAX RP   00RP1010 *1       -       Load indirect through BC or DE
;+STAX RP   00RP0010 *1       -       Store indirect through BC or DE
;+XCHG      11101011          -       Exchange DE and HL content
;+ADD S     10000SSS          ZSPCA   Add register to A
;+ADI #     11000110 db       ZSCPA   Add immediate to A
;+ADC S     10001SSS          ZSCPA   Add register to A with carry
;+ACI #     11001110 db       ZSCPA   Add immediate to A with carry
;+SUB S     10010SSS          ZSCPA   Subtract register from A
;+SUI #     11010110 db       ZSCPA   Subtract immediate from A
;+SBB S     10011SSS          ZSCPA   Subtract register from A with borrow
;+SBI #     11011110 db       ZSCPA   Subtract immediate from A with borrow
;+INR D     00DDD100          ZSPA    Increment register
;+DCR D     00DDD101          ZSPA    Decrement register
;+INX RP    00RP0011          -       Increment register pair
;+DCX RP    00RP1011          -       Decrement register pair
;+DAD RP    00RP1001          C       Add register pair to HL (16 bit add)
;DAA       00100111          ZSPCA   Decimal Adjust accumulator
;+ANA S     10100SSS          ZSCPA   AND register with A
;+ANI #     11100110 db       ZSPCA   AND immediate with A
;+ORA S     10110SSS          ZSPCA   OR  register with A
;+ORI #     11110110          ZSPCA   OR  immediate with A
;+XRA S     10101SSS          ZSPCA   ExclusiveOR register with A
;+XRI #     11101110 db       ZSPCA   ExclusiveOR immediate with A
;+CMP S     10111SSS          ZSPCA   Compare register with A
;+CPI #     11111110          ZSPCA   Compare immediate with A
;+RLC       00000111          C       Rotate A left
;+RRC       00001111          C       Rotate A right
;+RAL       00010111          C       Rotate A left through carry
;+RAR       00011111          C       Rotate A right through carry
;+CMA       00101111          -       Compliment A
;+CMC       00111111          C       Compliment Carry flag
;+STC       00110111          C       Set Carry flag
;+JMP a     11000011 lb hb    -       Unconditional jump
;Jccc a    11CCC010 lb hb    -       Conditional jump
;CALL a    11001101 lb hb    -       Unconditional subroutine call
;Cccc a    11CCC100 lb hb    -       Conditional subroutine call
;RET       11001001          -       Unconditional return from subroutine
;Rccc      11CCC000          -       Conditional return from subroutine
;RST n     11NNN111          -       Restart (Call n*8)
;+PCHL      11101001          -       Jump to address in H:L
;+PUSH RP   11RP0101 *2       -       Push register pair on the stack
;+POP RP    11RP0001 *2       *2      Pop  register pair from the stack
;+XTHL      11100011          -       Swap H:L with top word on stack
;+SPHL      11111001          -       Set SP to content of H:L
;IN p      11011011 pa       -       Read input port into A
;OUT p     11010011 pa       -       Write A to output port
;EI        11111011          -       Enable interrupts
;DI        11110011          -       Disable interrupts
;HLT       01110110          -       Halt processor
;NOP       00000000          -       No operation
