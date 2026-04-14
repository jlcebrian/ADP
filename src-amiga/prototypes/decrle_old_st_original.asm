	xdef	DecompressOldRLEToSTAsmOriginal

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

DecompressOldRLEToSTAsmOriginal

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
