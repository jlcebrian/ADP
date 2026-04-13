;  unzx0_68k_fast.s - ZX0 decompressor for 68000, optimized for speed
;
;  Based on the optimized fork by Chris Hodges (platon42):
;  https://git.platon42.de/chrisly42/unzx0_68000
;
;  This local variant keeps the C-callable ABI used by the project:
;
;  in:  4(sp) = start of compressed data
;       8(sp) = start of decompression buffer
;
;  out: a0 = end of compressed data
;       a1 = end of decompression buffer
;
;  preserves: a2/d2
;
;  Note: This fast variant is intended for payloads whose decompressed size
;  does not exceed 64 KB.
;
;  Copyright (C) 2021 Emmanuel Marty
;  Copyright (C) 2023 Emmanuel Marty, Chris Hodges
;  ZX0 compression (c) 2021 Einar Saukas, https://github.com/einar-saukas/ZX0
;
;  This software is provided 'as-is', without any express or implied
;  warranty.  In no event will the authors be held liable for any damages
;  arising from the use of this software.
;
;  Permission is granted to anyone to use this software for any purpose,
;  including commercial applications, and to alter it and redistribute it
;  freely, subject to the following restrictions:
;
;  1. The origin of this software must not be misrepresented; you must not
;     claim that you wrote the original software. If you use this software
;     in a product, an acknowledgment in the product documentation would be
;     appreciated but is not required.
;  2. Altered source versions must be plainly marked as such, and must not be
;     misrepresented as being the original software.
;  3. This notice may not be removed or altered from any source distribution.

	xdef	_zx0_decompress_fast
	xdef	zx0_decompress_fast

_zx0_decompress_fast
zx0_decompress_fast
               move.l 4(sp),a0      ; C ABI: input pointer
               move.l 8(sp),a1      ; C ABI: output pointer
               movem.l a2/d2,-(sp)  ; preserve callee-saved registers
               moveq.l #-128,d1     ; initialize empty bit queue
                                    ; plus bit to roll into carry
               moveq.l #-1,d2       ; initialize rep-offset to 1
               bra.s   .literals

.do_copy_offs2
               move.b  (a1,d2.l),(a1)+
               move.b  (a1,d2.l),(a1)+

               add.b   d1,d1        ; read 'literal or match' bit
               bcs.s   .get_offset

.literals
               moveq.l #1,d0        ; initialize value to 1
.elias_loop1
               add.b   d1,d1        ; shift bit queue, high bit into carry
               bne.s   .got_bit1
               move.b  (a0)+,d1     ; read 8 new bits
               addx.b  d1,d1        ; shift bit queue, high bit into carry
                                    ; and shift 1 from carry into bit queue

.got_bit1
               bcs.s   .got_elias1
               add.b   d1,d1        ; read data bit
               addx.w  d0,d0        ; shift data bit into value in d0
               bra.s   .elias_loop1

.got_elias1
               subq.w  #1,d0        ; dbra loops until d0 is -1
.copy_lits
               move.b  (a0)+,(a1)+
               dbra    d0,.copy_lits

               add.b   d1,d1        ; read 'match or rep-match' bit
               bcs.s   .get_offset

.rep_match
               moveq.l #1,d0        ; initialize value to 1
.elias_loop2
               add.b   d1,d1
               bne.s   .got_bit2
               move.b  (a0)+,d1
               addx.b  d1,d1

.got_bit2
               bcs.s   .got_elias2
               add.b   d1,d1
               addx.w  d0,d0
               bra.s   .elias_loop2

.got_elias2
               subq.w  #1,d0
.do_copy_offs
               move.l  a1,a2        ; calculate backreference address
               add.l   d2,a2
.copy_match
               move.b  (a2)+,(a1)+
               dbra    d0,.copy_match

               add.b   d1,d1        ; read 'literal or match' bit
               bcc.s   .literals

.get_offset
               moveq.l #-2,d0       ; initialize value to $fe

.elias_loop3
               add.b   d1,d1        ; shift bit queue, high bit into carry
               bne.s   .got_bit3
               move.b  (a0)+,d1
               addx.b  d1,d1

.got_bit3
               bcs.s   .got_elias3
               add.b   d1,d1
               addx.w  d0,d0
               bra.s   .elias_loop3

.got_elias3
               addq.b  #1,d0        ; obtain negative offset high byte
               beq.s   .done
               move.b  d0,-(sp)     ; transfer negative high byte to stack
               move.w  (sp)+,d2     ; shift it to make room for low byte

               move.b  (a0)+,d2     ; read low byte of offset + 1 bit of len
               asr.l   #1,d2        ; shift len bit into carry/offset in place
               bcs.s   .do_copy_offs2
               moveq.l #1,d0        ; initialize length value to 1
               add.b   d1,d1        ; read first data bit for length
               addx.w  d0,d0

.elias_loop4
               add.b   d1,d1
               bne.s   .got_bit4
               move.b  (a0)+,d1
               addx.b  d1,d1

.got_bit4
               bcs.s   .do_copy_offs
               add.b   d1,d1
               addx.w  d0,d0
               bra.s   .elias_loop4

.done
               movem.l (sp)+,a2/d2
               rts