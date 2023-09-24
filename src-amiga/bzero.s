|	new version of bcopy and memset
|	uses movem to set 256 bytes blocks faster.
|	Alexander Lehmann	alexlehm@iti.informatik.th-darmstadt.de
|	sortof inspired by jrbs bcopy
|	has to be preprocessed (int parameter in memset)

	.text
	.even

	.globl fillMemWithZero

|	void bzero( void *dest, size_t length );
|	void _bzero( void *dest, unsigned long length );

fillMemWithZero:

	move.l	4(sp),a0	| dest
	move.l	8(sp),d1	| length
scommon:
	jeq	exit		| length==0? (size_t)
	clr.b	d0		| value

do_set: 			| a0 dest, d0.b byte, d1.l length
	move.l	d2,-(sp)

	add.l	d1,a0		| a0 points to end of area, needed for predec

	move.w	a0,d2		| test for alignment
	btst	#0,d2		| odd ?
	jeq		areeven
	move.b	d0,-(a0) 	| set one byte, now we are even
	subq.l	#1,d1
areeven:
	move.b	d0,d2
	lsl.w	#8,d0
	move.b	d2,d0
	move.w	d0,d2
	swap	d2
	move.w	d0,d2		| d2 has byte now four times

	moveq	#0,d0		| save length less 256
	move.b	d1,d0
	lsr.l	#8,d1		| number of 256 bytes blocks
	jeq		less256
	movem.l	d0/d3-d7/a2/a3/a5/a6,-(sp)	| d2 is already saved
				| exclude a4 because of -mbaserel
	move.l	d2,d0
	move.l	d2,d3
	move.l	d2,d4
	move.l	d2,d5
	move.l	d2,d6
	move.l	d2,d7
	move.l	d2,a2
	move.l	d2,a3
	move.l	d2,a5
	move.l	d2,a6
set256:
	movem.l	d0/d2-d7/a2/a3/a5/a6,-(a0)	| set 5*44+36=256 bytes
	movem.l	d0/d2-d7/a2/a3/a5/a6,-(a0)
	movem.l	d0/d2-d7/a2/a3/a5/a6,-(a0)
	movem.l	d0/d2-d7/a2/a3/a5/a6,-(a0)
	movem.l	d0/d2-d7/a2/a3/a5/a6,-(a0)
	movem.l	d0/d2-d7/a2-a3,-(a0)
	subq.l	#1,d1
	jne		set256			| next, please
	movem.l	(sp)+,d0/d3-d7/a2/a3/a5/a6
less256:			| set 16 bytes blocks
	move.w	d0,-(sp) 	| save length below 256 for last 3 bytes
	lsr.w	#2,d0		| number of 4 bytes blocks
	jeq		less4		| less that 4 bytes left
	move.w	d0,d1
	neg.w	d1
	andi.w	#3,d1		| d1 = number of bytes below 16 (-n)&3
	subq.w	#1,d0
	lsr.w	#2,d0		| number of 16 bytes blocks minus 1, if d1==0
	add.w	d1,d1		| offset in code (movl two bytes)
	jmp		2(pc,d1.w)	| jmp into loop
set16:
	move.l	d2,a0@-
	move.l	d2,a0@-
	move.l	d2,a0@-
	move.l	d2,a0@-
	dbra	d0,set16
less4:
	move.w	(sp)+,d0
	btst	#1,d0
	jeq		less2
	move.w	d2,a0@-
less2:
	btst	#0,d0
	jeq		none
	move.b	d2,a0@-
none:
exit_d2:
	move.l	(sp)+,d2
exit:
	move.l a0,d0		| return dest (for memset only), in a0 by predec now
	rts
