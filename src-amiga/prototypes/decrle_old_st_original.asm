	xdef	DecompressOldRLEToSTAsmOriginalPrototype
	xdef	DecompressOldRLEToSTAsmExperimental

	; Parameters from stack:

	; APTR	Compressed data
	; ULONG	Compressed data size
	; APTR	Destination address
	; ULONG	Pixel count
	; WORD 	RLEMask

	; Original Atari ST old-RLE decompressor extracted from the disassembly.
	; The only adaptation is the entry wrapper: load A2/A3/D4/D7 from the C-call
	; convention and return a boolean success value in D0.

ARG_DATA		equ	72
ARG_OUT			equ	80
ARG_PIXELS		equ	84
ARG_MASK		equ	90

DecompressOldRLEToSTAsmOriginalPrototype

	movem.l	d2-d7/a2-a6,-(sp)
	lea	-24(sp),sp

	movea.l	ARG_DATA(sp),a2
	movea.l	ARG_OUT(sp),a3
	move.l	ARG_PIXELS(sp),d4
	moveq	#0,d7
	move.w	ARG_MASK(sp),d7

	; The original caller passed width / 2 * height bytes in D4.
	; Our interface passes width * height pixels, so convert once here.

	lsr.l	#1,d4
	beq.s	.done

.decompress
	lea.l	(a3,d4.l),a4
	movea.l	a3,a0
	move.l	d4,d0
	lsr.l	#2,d0
	subq.l	#1,d0
.clear
	clr.l	(a0)+
	dbf		d0,.clear
	move.w	#8,d2
	move.w	d2,d3
.nextColor
	bsr.w	.readColor
	cmpa.l	a3,a4
	bcs.s	.done
	bsr.w	.emitPixel
	btst.l	d0,d7
	beq.s	.nextColor
	move.w	d0,-(sp)
	bsr.w	.readColor
	move.w	d0,d5
	move.w	(sp)+,d0
.repeatCheck
	tst.w	d5
	beq.s	.nextColor
	cmpa.l	a3,a4
	bcs.s	.done
	bsr.w	.emitPixel
	subq.w	#1,d5
	bra.s	.repeatCheck

.done
	moveq	#1,d0
	lea	24(sp),sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts

.readColor

	clr.w	d0
	move.b	3(a2),d1
	lsr.b	d2,d1
	roxl.w	#1,d0
	move.b	2(a2),d1
	lsr.b	d2,d1
	roxl.w	#1,d0
	move.b	1(a2),d1
	lsr.b	d2,d1
	roxl.w	#1,d0
	move.b	(a2),d1
	lsr.b	d2,d1
	roxl.w	#1,d0
	subq.w	#1,d2
	bne.s	.readDone
	move.w	#8,d2
	adda.l	#4,a2
.readDone
	rts

.emitPixel
	subq.w	#1,d3
	btst.l	#3,d0
	beq.s	.skipPlane3
	bset.b	d3,3(a3)
.skipPlane3
	btst.l	#2,d0
	beq.s	.skipPlane2
	bset.b	d3,2(a3)
.skipPlane2
	btst.l	#1,d0
	beq.s	.skipPlane1
	bset.b	d3,1(a3)
.skipPlane1
	btst.l	#0,d0
	beq.s	.emitDone
	bset.b	d3,(a3)
.emitDone
	tst.w	d3
	bne.s	.emitReturn
	move.w	#8,d3
	adda.l	#4,a3
.emitReturn
	rts


; --------------------------------------------------
;  Experimental version
; --------------------------------------------------
	
; Parameters from stack:

; APTR	Compressed data
; ULONG	Compressed data size
; APTR	Destination address
; ULONG	Pixel count
; WORD 	RLEMask

; Uses the original routine's call signature for easier comparison.
; The compressed size is currently unused by the experimental path.

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
	move.w	#8,a1		; A1 ← Shift counter
.r\@	clr.w	\1
	lsl.b	#1,d0		; Fetch the next color from each source byte
	roxl.b	#1,\1
	lsl.b	#1,d1
	roxl.b	#1,\1
	lsl.b	#1,d2
	roxl.b	#1,\1
	lsl.b	#1,d3
	roxl.b	#1,\1		; D4 ← Color
	subq.w	#1,a1		; Update shift counter
	endm

DecompressOldRLEToSTAsmExperimental:

	movem.l	d2-d7/a2-a6,-(sp)
	lea	-24(sp),sp

	move.l	#0,a1			; A1 ← Shift counter
	movea.l	ARG_DATA(sp),a2		; A2 ← Source stream
	movea.l	ARG_OUT(sp),a3		; A3 ← Destination pointer
	move.l	ARG_PIXELS(sp),a4	; A4 ← Number of pixels to copy
	clr.l	d5			; D5 ← Output word
	move.l	#$07010000,d6		; D6 ← Output shift counter (highest byte)
	move.w	ARG_MASK(sp),d6		; D6 ← RLE Mask (lower word)
	move.l	#$01000000,a5		; A5 ← Decrement value for D6 shift counter
	move.l  d6,a6			; A6 ← Reset value for D6

	; Fetch next four bitplanes into D0-D3

.mainLoop

	OLDRLE_READ_COLOR d4
	clr.l	d7		; D7 ← Repeat count
	btst.l	d4,d6		; Test RLE Mask
	beq.s   .emit
	OLDRLE_READ_COLOR d7
	sub.l	d7,a4		; Subtracts repeat count from pixel count

.emit	add.w	d4,d4
	add.w	d4,d4
	move.l	.ColorTable(pc,d4),d4
	sub.l	#1,a4
.loop	add.l	d5,d5
	or.l	d4,d5
	sub.l	a5,d6
	bcc.s	.next
	move.l	a6,d6		; Reset counter
	move.l	d5,(a3)+
	clr.l	d5
.next	dbra	d7,.loop

	move.l	a4,d4		; Has pixel count reached zero?
	bne.w	.mainLoop

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
