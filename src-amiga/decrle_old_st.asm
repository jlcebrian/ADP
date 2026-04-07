	xdef	DecompressOldRLEToSTAsm
	xref	DMGOldRLEPackedColorBits0
	xref	DMGOldRLEPackedColorBits1
	xref	DMGOldRLEPackedColorBits2
	xref	DMGOldRLEPackedColorBits3
	xref	DMGOldRLESpanMask16
	xref	DMGOldRLEFullWordPlanes16

	; Parameters from stack:

	; APTR	Compressed data
	; ULONG	Compressed data size
	; APTR	Destination address
	; ULONG	Pixel count
	; WORD 	RLEMask

	; Register-remap old-RLE planar-ST decoder matching the compiler-emitted C path.
	; Returns 1 unconditionally and leaves swap/truncation handling to the caller.
	;
	; Persistent state:
	; A0  remaining logical pixels to decode
	; A1  bits already used in current 16-pixel output word
	; A2  output pointer
	; A3  compressed input pointer
	; A4  remaining packed nibbles currently buffered in D0
	; A5  lookup-table base / indexed scratch
	; A6  packedColorBits0 base
	;
	; D0  packedColors shift register (next nibble in top 4 bits)
	; D4  plane0 word being built
	; D5  plane1 word being built
	; D6  plane2 word being built
	; D7  plane3 word being built
	;
	; Transient scratch:
	; D1  current color, then current span
	; D2  run count / remaining count in span loop
	; D3  scratch, mask-index scratch, plane scratch
	

LOCAL_MASK		equ	0
LOCAL_PLANE0FLAG	equ	4
LOCAL_COLORFLAGS	equ	8

ARG_DATA		equ	72
ARG_OUT			equ	80
ARG_PIXELS		equ	84
ARG_MASK		equ	90

	; The original data argument lives in A3 after entry.
	; Reuse its old stack slot for a cached 32-bit copy of the 16-bit RLE mask.
	
LOCAL_RLEMASK		equ	ARG_DATA

	; Prologue: load arguments, cache the RLE mask, and clear decoder state.
	; This variant writes Atari ST interleaved plane words directly through A2.

DecompressOldRLEToSTAsm

	movem.l	d2-d7/a2-a6,-(sp)
	lea	-24(sp),sp

	movea.l	ARG_DATA(sp),a3
	movea.l	ARG_OUT(sp),a2
	movea.l	ARG_PIXELS(sp),a0
	moveq	#0,d2
	move.w	ARG_MASK(sp),d2

	move.l	d2,LOCAL_RLEMASK(sp)
	suba.l	a1,a1
	suba.l	a4,a4
	clr.w	d4
	clr.w	d5
	clr.w	d6
	clr.w	d7
	moveq	#0,d0
	lea	DMGOldRLEPackedColorBits0,a6

	; Main loop: stop when all logical pixels are consumed.
	; If a partial 16-pixel word is pending, flush it once before returning.

.nextRun

	move.l	a0,d3
	bgt.s	.refillPacked
	move.l	a1,d3
	beq	.done

	; Final partial flush in Atari ST interleaved order: P0, P1, P2, P3.

	move.w	d4,(a2)+
	move.w	d5,(a2)+
	move.w	d6,(a2)+
	move.w	d7,(a2)+
	bra	.done

	; Refill the packed-color FIFO when no decoded nibbles remain.
	; Four bitplane bytes are expanded into eight packed color nibbles in D0.

.refillPacked

	move.l	a4,d3
	bne.s	.decodeColor
	moveq	#0,d1
	move.b	(a3)+,d1
	add.w	d1,d1
	add.w	d1,d1
	moveq	#0,d0
	move.b	(a3)+,d0
	add.w	d0,d0
	add.w	d0,d0
	lea	DMGOldRLEPackedColorBits1,a5
	move.l	(0,a5,d0.l),d0
	or.l	(0,a6,d1.l),d0
	moveq	#0,d1
	move.b	(a3)+,d1
	add.w	d1,d1
	add.w	d1,d1
	lea	DMGOldRLEPackedColorBits2,a5
	or.l	(0,a5,d1.l),d0
	moveq	#0,d1
	move.b	(a3)+,d1
	add.w	d1,d1
	add.w	d1,d1
	lea	DMGOldRLEPackedColorBits3,a5
	or.l	(0,a5,d1.l),d0
	moveq	#8,d3
	movea.l	d3,a4

	; Pull the next color nibble from the top of D0.
	; A4 tracks how many packed nibbles remain buffered.

.decodeColor

	rol.l	#4,d0
	move.l	d0,d1
	andi.l	#15,d1
	subq.w	#1,a4
	move.l	LOCAL_RLEMASK(sp),d3
	moveq	#1,d2
	btst	d1,d3
	beq.s	.applyRun
	move.w	a4,d3
	bne.s	.decodeRepeatCount

	; Repeat-count nibble needs another packed refill before it can be read.

	move.b	(a3)+,d2
	add.w	d2,d2
	add.w	d2,d2
	moveq	#0,d0
	move.b	(a3)+,d0
	add.w	d0,d0
	add.w	d0,d0
	lea	DMGOldRLEPackedColorBits1,a5
	move.l	(0,a5,d0.l),d0
	or.l	(0,a6,d2.l),d0
	moveq	#0,d2
	move.b	(a3)+,d2
	add.w	d2,d2
	add.w	d2,d2
	lea	DMGOldRLEPackedColorBits2,a5
	or.l	(0,a5,d2.l),d0
	moveq	#0,d2
	move.b	(a3)+,d2
	add.w	d2,d2
	add.w	d2,d2
	lea	DMGOldRLEPackedColorBits3,a5
	or.l	(0,a5,d2.l),d0
	moveq	#8,d3
	movea.l	d3,a4

	; Decode the repeat nibble for colors present in the RLE mask.
	; D2 is explicitly zero-extended because later code treats it as a long.

.decodeRepeatCount

	move.l	d0,d3

	swap	d3
	lsr.w	#8,d3
	lsr.w	#4,d3

	moveq	#0,d2
	move.b	d3,d2
	addq.l	#1,d2
	lsl.l	#4,d0
	subq.w	#1,a4

	; The following code can be commented out if you assume the
	; incoming data is well formed and won't go past the remaining pixels,
	; but we didn't measure any gains when doing so
	
	move.l	a0,d3
	cmp.l	d3,d2
	ble.s	.applyRun
	move.l	a0,d2

	; Apply the run length to the remaining-pixel counter.
	; Either emit a whole 16-pixel word directly or fall back to span assembly.

.applyRun

	suba.l	d2,a0
	move.l	a1,d3
	bne.s	.emitPartial
	cmpi.b	#16,d2
	bne.s	.emitPartial

	; Fast path for a complete 16-pixel word with no partial accumulators live.
	; The lookup table already contains the four final plane words for this color.

	lsl.l	#3,d1
	lea	DMGOldRLEFullWordPlanes16,a5
	adda.l	d1,a5
	move.w	(a5)+,(a2)+
	move.w	(a5)+,(a2)+
	move.w	(a5)+,(a2)+
	move.w	(a5)+,(a2)+
	bra	.nextRun

	; Partial-run setup: cache the plane flags once so the span loop only has
	; to test bits and OR the current span mask into the live accumulators.

.emitPartial

	moveq	#1,d3
	and.l	d1,d3
	neg.w	d3
	move.w	d3,LOCAL_PLANE0FLAG(sp)
	move.w	d1,LOCAL_COLORFLAGS(sp)

	; Emit this run as one or more spans inside the current 16-pixel word.
	; D1 becomes the span width, A1 tracks bits already filled in the word.

.emitSpan

	moveq	#16,d1
	move.l	a1,d3
	sub.w	d3,d1
	cmp.w	d1,d2
	bcc.s	.spanReady
	move.w	d2,d1

.spanReady

	move.w	a1,d3
	lsl.w	#4,d3
	add.w	a1,d3
	add.w	d1,d3
	add.w	d3,d3
	lea	DMGOldRLESpanMask16,a5
	move.w	(0,a5,d3.l),LOCAL_MASK(sp)

	; Plane0 uses a pre-negated mask flag so we can AND directly with the span mask.
	; Merge the current span mask into each live plane accumulator selected by color.

	move.w	LOCAL_PLANE0FLAG(sp),d3
	and.w	LOCAL_MASK(sp),d3
	or.w	d3,d4

	move.w	LOCAL_MASK(sp),d3
	btst	#1,1+LOCAL_COLORFLAGS(sp)
	beq.s	.skipPlane1
	or.w	d3,d5
.skipPlane1
	btst	#2,1+LOCAL_COLORFLAGS(sp)
	beq.s	.skipPlane2
	or.w	d3,d6
.skipPlane2
	btst	#3,1+LOCAL_COLORFLAGS(sp)
	beq.s	.skipPlane3
	or.w	d3,d7
.skipPlane3
	adda.w	d1,a1
	sub.w	d1,d2
	move.l	a1,d3
	cmpi.w	#16,d3
	bne.s	.nextSpan

	; Completed one full 16-pixel word from the accumulators: write it in ST
	; interleaved order and clear the accumulators for the next word.

	move.w	d4,(a2)+
	move.w	d5,(a2)+
	move.w	d6,(a2)+
	move.w	d7,(a2)+
	suba.l	a1,a1
	clr.l	d4
	clr.l	d5
	clr.l	d6
	clr.l	d7

	; Keep consuming spans from this run until its count reaches zero.

.nextSpan

	tst.w	d2
	bne	.emitSpan
	bra	.nextRun

	; Epilogue: return success unconditionally.
.done
	moveq	#1,d0
	lea	24(sp),sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts