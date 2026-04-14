	SECTION .text

	xdef	_DecompressOldRLEToPlanar8Asm

	; Parameters from stack:

	; APTR	Compressed data
	; ULONG	Compressed data size
	; APTR	Destination address
	; ULONG	Pixel count
	; WORD 	RLEMask

	; Old-RLE decompressor for Atari ST.
	; Emits Planar8 (P0P1P2P3) blocks so the C caller can perform the final
	; Planar8 -> PlanarST conversion.

ARG_DATA		equ	72
ARG_OUT			equ	80
ARG_PIXELS		equ	84
ARG_MASK		equ	90

OLDRLE_READ_COLOR	macro
	move.w	a1,\1		; Check if the shift counter reached 0
	bne.s	.r\@
	move.l	(a2)+,d0	; Fetch next four bytes into D0-D3
	move.l	d0,d1
	lsr.l	#8,d1
	move.l	d0,d2
	swap	d2
	move.l	d1,d3
	swap	d3
	move.w	#8,a1		; A1 <- Shift counter
.r\@	clr.w	\1
	lsl.b	#1,d0		; Fetch the next color from each source byte
	roxl.b	#1,\1
	lsl.b	#1,d1
	roxl.b	#1,\1
	lsl.b	#1,d2
	roxl.b	#1,\1
	lsl.b	#1,d3
	roxl.b	#1,\1		; D4 <- Color
	subq.w	#1,a1		; Update shift counter
	endm

_DecompressOldRLEToPlanar8Asm:

	movem.l	d2-d7/a2-a6,-(sp)
	lea	-24(sp),sp

	move.l	#0,a1			; A1 <- Shift counter
	movea.l	ARG_DATA(sp),a2		; A2 <- Source stream
	movea.l	ARG_OUT(sp),a3		; A3 <- Destination pointer
	move.l	ARG_PIXELS(sp),a4	; A4 <- Number of pixels to copy
	clr.l	d5			; D5 <- Output word
	move.l	#$07010000,d6		; D6 <- Output shift counter (highest byte)
	move.w	ARG_MASK(sp),d6		; D6 <- RLE Mask (lower word)
	move.l	#$01000000,a5		; A5 <- Decrement value for D6 shift counter
	move.l  d6,a6			; A6 <- Reset value for D6
	sub.l	#1,a4

.mainLoop

	OLDRLE_READ_COLOR d4
	clr.l	d7		; D7 <- Repeat count
	btst.l	d4,d6		; Test RLE Mask
	beq.s   .emit
	OLDRLE_READ_COLOR d7
	sub.l	d7,a4		; Subtract repeat count from pixel count

.emit	add.w	d4,d4
	add.w	d4,d4
	move.l	.ColorTable(pc,d4),d4
.loop	add.l	d5,d5
	or.l	d4,d5
	sub.l	a5,d6
	bcc.s	.next
	move.l	a6,d6		; Reset counter
	move.l	d5,(a3)+
	clr.l	d5
.next	dbra	d7,.loop

	sub.l	#1,a4
	move.l	a4,d4		; Has pixel count reached zero?
	bpl.w	.mainLoop

.done	rol.l	#8,d6
	cmp.b	#8,d6
	beq.s	.ret
	lsl.l	d6,d5
	move.l	d5,(a3)+
.ret	moveq	#1,d0
	
	lea.l	24(sp),sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts

.ColorTable

	dl	$00000000
	dl	$01000000
	dl	$00010000
	dl	$01010000
	dl	$00000100
	dl	$01000100
	dl	$00010100
	dl	$01010100
	dl	$00000001
	dl	$01000001
	dl	$00010001
	dl	$01010001
	dl	$00000101
	dl	$01000101
	dl	$00010101
	dl	$01010101