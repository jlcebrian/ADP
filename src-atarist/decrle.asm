	SECTION .text

	xdef	_DecompressRLE


	; Parameters from stack:

	; APTR	Compressed data
	; ULONG	Compressed data size
	; APTR	Destination address
	; ULONG	Image width in pixels
	; WORD  RLEMask

_DecompressRLE
	
	; Meaning of registers during main loop:

	; A0	Pointer to compressed data
	; A1	Pointer to end of data buffer
	; A2	Pointer to output
	; A4	Contains $00010000
	; A5	Contains -width in the high word
	; A6    Contains $40000000

	; D0	Current nibble values
	; D1	LOWORD: Nibble count
	;	HIWORD: X counter
	; D2	Plane 0 bits
	; D3	Plane 1 bits
	; D4	Plane 2 bits
	; D5	Plane 3 bits
	; D6	Scratch
	; D7	LOWORD: RLEMask 
	;	HIWORD: byte counter (A6 added every pixel, sets C every 16)

		movem.l	d2-d7/a2-a6,-(sp)

		move.l	48(sp),a0
		move.l	a0,a1
		add.l	52(sp),a1
		move.l  56(sp),a2
		move.l	60(sp),d1		; Line width in pixels
		move.w	d1,d6
		and	#15,d6
		sub	#1,d6
		eor	#15,d6
		move.b	d6,(EOLShift)
		neg.w	d1
		swap	d1
		clr.w	d1
		move.l	d1,a5			; A5 is used to reset D1
		move.l	#$10000,d6
		move.l	d6,a4
		clr.l	d2
		clr.l	d3
		clr.l	d4
		clr.l	d5
		clr.l	d6
		clr.l	d7
		move.l	64(sp),d7
		movea.l	#$10000000,a6

.nextNibble	move.w	#7,d1
		cmpa.l	a0,a1
		beq	.end
		move.l	(a0)+,d0

.loop		move.b	d0,d6
		and.w	#$F,d6

		lsr.l	#1,d0
		roxl.w	#1,d2
		lsr.l	#1,d0
		roxl.w	#1,d3
		lsr.l	#1,d0
		roxl.w	#1,d4
		lsr.l	#1,d0
		roxl.w	#1,d5
		
		add.l	a6,d7
		bcs	.dump
		add.l	a4,d1			; Increase X counter
		bcs	.earlyDump		; Dump if end of line

.dumpOk		btst	d6,d7
		bne	.repeat
.afterRep	dbra	d1,.loop
		bra	.nextNibble

.dump		add.l	a4,d1	
		bcc	.dump2
.dump1		add.l	a5,d1	
.dump2		move.w	d2,(a2)+
		move.w	d3,(a2)+
		move.w	d4,(a2)+
		move.w	d5,(a2)+
		bra	.dumpOk

.earlyDump	swap	d7
		move.b	(EOLShift),d7
		lsl.w	d7,d2
		lsl.w	d7,d3
		lsl.w	d7,d4
		lsl.w	d7,d5
		clr.w	d7
		swap	d7
		bra	.dump1


.repeat:	; D6: Current color

		move.b	d6,(SavedColor)
		dbra	d1,.repfetched

		move.w	#7,d1
		cmpa.l	a0,a1
		beq	.end
		move.l	(a0)+,d0

.repfetched:	move.b	d0,d6
		lsr.l	#4,d0
		and.w	#$F,d6
		beq	.afterRep
		sub.b   #1,d6
		move.b	d0,-(sp)
.reploop:	move.b	(SavedColor),d0
		lsr.b	#1,d0
		roxl.w	#1,d2
		lsr.b	#1,d0
		roxl.w	#1,d3
		lsr.b	#1,d0
		roxl.w	#1,d4
		lsr.b	#1,d0
		roxl.w	#1,d5
		add.l	a6,d7
		bcs	.repdump
		add.l	a4,d1
		bcs	.repEarlyDump
		dbra	d6,.reploop
		move.b	(sp)+,d0

		bra	.afterRep

.repdump:	add.l	a4,d1		
		bcc .repdump2
.repdump1:	add.l	a5,d1
.repdump2:	move.w	d2,(a2)+
		move.w	d3,(a2)+
		move.w	d4,(a2)+
		move.w	d5,(a2)+
		dbra	d6,.reploop
		move.b	(sp)+,d0
		bra	.afterRep

.repEarlyDump	swap	d7
		move.b	(EOLShift),d7
		lsl.w	d7,d2
		lsl.w	d7,d3
		lsl.w	d7,d4
		lsl.w	d7,d5
		clr.w	d7
		swap	d7
		bra	.repdump1

.repend		move.b	(sp)+,d0
.end		swap	d7
		tst.w	d7
		beq	.ret
		swap	d7
.endroll	rol.w	d2
		rol.w	d3
		rol.w	d4
		rol.w	d5
		add.l	a6,d7
		bcs	.endroll
		move.w	d2,(a2)+
		move.w	d3,(a2)+
		move.w	d4,(a2)+
		move.w	d5,(a2)+
		
.ret		movem.l	(sp)+,d2-d7/a2-a6
		rts

		SECTION .data

SavedColor	dc.b	0
EOLShift	dc.b	0