
/*
 * FastSPI.S
 *
 * Created: 25.02.2015 21:11:18
 *  Author: main
 */ 

#include <avr/io.h>

.global FastSPI_write

FastSPI_write:
cli
push XL
push XH

movw XL, 24
add r24, r22
adc r25, r23

ld r23, X+
BST r23, 7
brtc Bit0_0
rjmp Bit0_1

Bit0_0:
nop
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
BST r23, 6
brtc Bit1_0
rjmp Bit1_1

Bit0_1:
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
BST r23, 6
brtc Bit1_0
rjmp Bit1_1

Bit1_0:
nop
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
BST r23, 5
brtc Bit2_0
rjmp Bit2_1

Bit1_1:
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
BST r23, 5
brtc Bit2_0
rjmp Bit2_1

Bit2_0:
nop
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
BST r23, 4
brtc Bit3_0
rjmp Bit3_1

Bit2_1:
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
BST r23, 4
brtc Bit3_0
rjmp Bit3_1

Bit3_0:
nop
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
BST r23, 3
brtc Bit4_0
rjmp Bit4_1

Bit3_1:
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
BST r23, 3
brtc Bit4_0
rjmp Bit4_1

Bit4_0:
nop
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
BST r23, 2
brtc Bit5_0
rjmp Bit5_1

Bit4_1:
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
BST r23, 2
brtc Bit5_0
rjmp Bit5_1

Bit5_0:
nop
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
BST r23, 1
brtc Bit6_0
rjmp Bit6_1

Bit5_1:
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
BST r23, 1
brtc Bit6_0
rjmp Bit6_1

Bit6_0:
nop
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
BST r23, 0
brtc Bit7_0
rjmp Bit7_1

Bit6_1:
sbi _SFR_IO_ADDR(PORTD), 5
nop
nop
nop
nop
nop
nop
nop
nop
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
BST r23, 0
brtc Bit7_0
rjmp Bit7_1

Bit7_0:
nop
sbi _SFR_IO_ADDR(PORTD), 5
cp XL, r24
cpc XH, r25
ld r23, X+
cbi _SFR_IO_ADDR(PORTD), 5
brsh Bit_Reset
nop
nop
nop
nop
nop
sbrs r23, 7
rjmp Bit0_0
rjmp Bit0_1

Bit7_1:
sbi _SFR_IO_ADDR(PORTD), 5
cp XL, r24
cpc XH, r25
ld r23, X+
nop
nop
nop
brsh Bit_Reset
nop
nop
cbi _SFR_IO_ADDR(PORTD), 5
sbrs r23, 7
rjmp Bit0_0
rjmp Bit0_1

Bit_Reset:
cbi _SFR_IO_ADDR(PORTD), 5

pop XH
pop XL

ldi r24, 255
Loop:
dec r24
brne Loop

sei
ret

.END