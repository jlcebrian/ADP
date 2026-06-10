

	xdef	DecompressRLE


	; Parameters from stack:

	; APTR	Compressed data (must be word aligned)
	; ULONG	Compressed data size
	; APTR	Destination address (plane 0)
	; ULONG	Image width in pixels
	; ULONG Image height in pixels
	; WORD  RLEMask

	; The decoder accumulates 8 pixels in D2 as four 8-bit lanes
	; (plane 0 in bits 24-31 down to plane 3 in bits 0-7). Each
	; nibble is spread to the four lanes with a single OR from a
	; 16-entry table, replacing the lsr/roxl chain of the previous
	; version. Every 8 pixels the four lane bytes are written out
	; through four plane pointers kept in A2-A5.

	; There is no per-pixel X counter: the end of a row is folded
	; into the 8-pixel dump counter held in bits 29-31 of D7. When
	; the last byte of a row begins, (8-width%8)<<29 is added to
	; the counter so the carry fires after width%8 pixels, and the
	; dump handler left-justifies the partial byte and skips the
	; row padding. Row position is tracked by a byte countdown in
	; the high word of D1, updated only inside the dump handler.

DecompressRLE

	; Meaning of registers during main loop:

	; A0	Pointer to compressed data
	; A1	Pointer to end of data buffer
	; A2	Pointer to output plane 0
	; A3	Pointer to output plane 1
	; A4	Pointer to output plane 2
	; A5	Pointer to output plane 3
	; A6	Pointer to nibble spread table

	; D0	Current nibble values
	; D1	LOWORD: Nibble count
	;	HIWORD: Bytes left in row, minus one
	; D2	Plane accumulator (four 8-bit lanes)
	; D3	Contains $20000000 (pixel counter increment)
	; D4	Contains $F
	; D5	Spread pattern of the current RLE run
	; D6	Scratch (color, table index, run count)
	; D7	LOWORD: RLEMask
	;	HIWORD: pixel counter in bits 29-31 (sets C every 8)

	; Dump handler: write the four lane bytes of D2 and continue at
	; \1. Decrements the row byte countdown; at the second-to-last
	; byte of the row it biases the pixel counter in D7 so the next
	; carry comes after width%8 pixels, and at the last byte it
	; left-justifies the partial lanes and skips the row padding.

DUMP	macro
	swap	d1
	subq.w	#1,d1
	beq	.bias\@
	bmi	.eol\@
	swap	d1
.wr\@	move.b	d2,(a5)+
	lsr.w	#8,d2
	move.b	d2,(a4)+
	swap	d2
	move.b	d2,(a3)+
	lsr.w	#8,d2
	move.b	d2,(a2)+
	moveq	#0,d2
	bra	\1

.bias\@	swap	d1
	add.l	BiasValue(pc),d7
	bra	.wr\@

.eol\@	move.w	BytesPerRowM1(pc),d1
	bne	.eolr\@
	add.l	BiasValue(pc),d7	; Single byte rows: re-bias here
.eolr\@	swap	d1
	move.w	d6,-(sp)
	move.w	EOLShift(pc),d6
	lsl.l	d6,d2
	move.w	(sp)+,d6
	move.b	d2,(a5)+
	lsr.w	#8,d2
	move.b	d2,(a4)+
	swap	d2
	move.b	d2,(a3)+
	lsr.w	#8,d2
	move.b	d2,(a2)+
	moveq	#0,d2
	adda.w	PadBytes(pc),a2
	adda.w	PadBytes(pc),a3
	adda.w	PadBytes(pc),a4
	adda.w	PadBytes(pc),a5
	bra	\1
	endm

		movem.l	d2-d7/a2-a6,-(sp)

		move.l	48(sp),a0
		move.l	a0,a1
		add.l	52(sp),a1
		movea.l	56(sp),a2

		move.l	60(sp),d1		; Width in pixels
		move.l	64(sp),d0		; Height in pixels

		move.l	d1,d6			; Plane pointers
		add.l	#15,d6
		lsr.l	#4,d6			; Words per row
		move.w	d6,d5
		add.w	d5,d5			; Row stride in bytes
		mulu.w	d0,d6
		add.l	d6,d6			; Plane stride in bytes
		movea.l	a2,a3
		adda.l	d6,a3
		movea.l	a3,a4
		adda.l	d6,a4
		movea.l	a4,a5
		adda.l	d6,a5

		move.w	d1,d4			; Bytes per row
		add.w	#7,d4
		lsr.w	#3,d4
		sub.w	d4,d5			; Row padding in bytes
		move.w	d5,(PadBytes)
		subq.w	#1,d4
		move.w	d4,(BytesPerRowM1)

		moveq	#8,d5			; EOL shift: (8 - width%8) & 7
		move.w	d1,d6
		and.w	#7,d6
		sub.w	d6,d5
		and.w	#7,d5
		move.w	d5,(EOLShift)
		moveq	#0,d6			; Counter bias: shift << 29
		move.b	d5,d6
		ror.l	#3,d6
		move.l	d6,(BiasValue)

		move.w	d4,d1			; Row byte countdown
		swap	d1

		moveq	#0,d7
		move.w	70(sp),d7		; RLEMask
		tst.w	d4
		bne	.biasok
		add.l	d6,d7			; Single byte rows: bias now
.biasok		lea	NibTable(pc),a6
		moveq	#0,d2
		move.l	#$20000000,d3
		moveq	#15,d4

.nextNibble	move.w	#7,d1
		cmpa.l	a1,a0
		bhs	.end
		move.l	(a0)+,d0

.loop		move.b	d0,d6
		and.w	d4,d6
		btst	d6,d7
		bne	.repeat
		add.w	d6,d6
		add.w	d6,d6
		lsr.l	#4,d0
		add.l	d2,d2
		or.l	(a6,d6.w),d2
		add.l	d3,d7
		bcs	.dump
.next		dbra	d1,.loop
		bra	.nextNibble


.repeat:	; D6: Current color

		lsr.l	#4,d0
		add.w	d6,d6
		add.w	d6,d6
		move.l	(a6,d6.w),d5		; Spread pattern of the color
		add.l	d2,d2			; Draw the flagged pixel
		or.l	d5,d2
		add.l	d3,d7
		bcs	.repdump0

.rep1		dbra	d1,.rep2		; Fetch the count nibble
		move.w	#7,d1
		cmpa.l	a1,a0
		bhs	.end
		move.l	(a0)+,d0

.rep2		move.b	d0,d6
		and.w	d4,d6
		lsr.l	#4,d0
		subq.w	#1,d6
		bmi	.next

.reploop	add.l	d2,d2
		or.l	d5,d2
		add.l	d3,d7
		bcs	.repdump
		dbra	d6,.reploop
		bra	.next

.repcont	dbra	d6,.reploop
		bra	.next

.dump		DUMP	.next
.repdump0	DUMP	.rep1
.repdump	DUMP	.repcont

.end		swap	d1			; Flush pending partial byte.
		move.l	d7,d6			; In the biased last byte of a
		tst.w	d1			; row the counter holds the bias
		bne	.endnb			; even with no pixels pending,
		sub.l	BiasValue(pc),d6	; so ignore it for the test
.endnb		and.l	#$E0000000,d6
		beq	.ret
.endroll	add.l	d2,d2
		add.l	d3,d7
		bcc	.endroll
		tst.w	d1			; In the biased last byte of a
		bne	.endwr			; row, apply the EOL shift too
		move.w	EOLShift(pc),d6
		lsl.l	d6,d2
.endwr		move.b	d2,(a5)+
		lsr.w	#8,d2
		move.b	d2,(a4)+
		swap	d2
		move.b	d2,(a3)+
		lsr.w	#8,d2
		move.b	d2,(a2)+

.ret		movem.l	(sp)+,d2-d7/a2-a6
		rts


	; Each nibble spread into four 8-bit lanes:
	; bit 0 (plane 0) in bits 24-31 ... bit 3 (plane 3) in bits 0-7

		even
NibTable	dc.l	$00000000
		dc.l	$01000000
		dc.l	$00010000
		dc.l	$01010000
		dc.l	$00000100
		dc.l	$01000100
		dc.l	$00010100
		dc.l	$01010100
		dc.l	$00000001
		dc.l	$01000001
		dc.l	$00010001
		dc.l	$01010001
		dc.l	$00000101
		dc.l	$01000101
		dc.l	$00010101
		dc.l	$01010101

BiasValue	dc.l	0
BytesPerRowM1	dc.w	0
EOLShift	dc.w	0
PadBytes	dc.w	0
