;https://www.asm80.com/onepage/asm8080.html

fclk     equ     1777777
freq1    equ     262
counter1 equ     fclk / freq1
freq2    equ     330
counter2 equ     fclk / freq2
freq3    equ     392
counter3 equ     fclk / freq3

        .org 0

        mvi a, 036h
        sta 0EC03h
        mvi a, counter1 % 256
        sta 0EC00h
        mvi a, counter1 / 256
        sta 0EC00h

        mvi a, 076h
        sta 0EC03h
        mvi a, counter2 % 256
        sta 0EC01h
        mvi a, counter2 / 256
        sta 0EC01h

        mvi a, 0B6h
        sta 0EC03h
        mvi a, counter3 % 256
        sta 0EC02h
        mvi a, counter3 / 256
        sta 0EC02h

loop:   jmp loop
