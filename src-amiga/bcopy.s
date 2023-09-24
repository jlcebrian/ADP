|	new version of bcopy, memcpy and memmove
|	handles overlap, odd/even alignment
|	uses movem to copy 256 bytes blocks faster.
|	Alexander Lehmann	alexlehm@iti.informatik.th-darmstadt.de
|	sortof inspired by jrbs bcopy

	.text
	.even
	.globl ___bcopy
	.globl __bcopy
	.globl _bcopy
	.globl _memcpy
	.globl _memmove
	.local common

|	void *memcpy( void *dest, const void *src, size_t len );
|	void *memmove( void *dest, const void *src, size_t len );
|	returns dest
|	functions are aliased

_memcpy:
_memmove:
	move.l	4(sp),a0	| dest
	move.l	8(sp),a1	| src
	jra	common		| the rest is samea as bcopy

|	void bcopy( const void *src, void *dest, size_t length );
|	void _bcopy( const void *src, void *dest, unsigned long length );
|	return value not used (returns src)
|	functions are aliased (except for HSC -- sb)

_bcopy:
__bcopy:
___bcopy:
	move.l	4(sp),a1	| src
	move.l	8(sp),a0	| dest
common:	move.l	12(sp),d0	| length

common2:

	jeq	exit		| length==0? (size_t)

				| a1 src, a0 dest, d0.l length
	move.l	a0,-(sp)
	move.l	d2,-(sp)

	| overlay ?
	cmp.l	a1,a0
	jgt	top_down

	move.w	a1,d1		| test for alignment
	move.w	a0,d2
	eor.w	d2,d1
	btst	#0,d1		| one odd one even ?
	jne	slow_copy
	btst	#0,d2		| both even ?
	jeq	both_even
	move.b	(a1)+,(a0)+	| copy one byte, now we are both even
	subq.l	#1,d0
both_even:
	moveq	#0,d1		| save length less 256
	move.b	d0,d1
	lsr.l	#8,d0		| number of 256 bytes blocks
	jeq	less256
	movem.l	d1/d3-d7/a2/a3/a5/a6,-(sp)	| d2 is already saved
					| exclude a4 because of -mbaserel
copy256:
	movem.l	(a1)+,d1-d7/a2/a3/a5/a6	| copy 5*44+36=256 bytes
	movem.l	d1-d7/a2/a3/a5/a6,(a0)
	movem.l	(a1)+,d1-d7/a2/a3/a5/a6
	movem.l	d1-d7/a2/a3/a5/a6,44(a0)
	movem.l	(a1)+,d1-d7/a2/a3/a5/a6
	movem.l	d1-d7/a2/a3/a5/a6,88(a0)
	movem.l	(a1)+,d1-d7/a2/a3/a5/a6
	movem.l	d1-d7/a2/a3/a5/a6,132(a0)
	movem.l	(a1)+,d1-d7/a2/a3/a5/a6
	movem.l	d1-d7/a2/a3/a5/a6,176(a0)
	movem.l	(a1)+,d1-d7/a2-a3
	movem.l	d1-d7/a2-a3,220(a0)
	lea	256(a0),a0		| increment dest, src is already
	subq.l	#1,d0
	jne	copy256 		| next, please
	movem.l	(sp)+,d1/d3-d7/a2/a3/a5/a6
less256:			| copy 16 bytes blocks
	move.w	d1,d0
	lsr.w	#2,d0		| number of 4 bytes blocks
	jeq	less4		| less that 4 bytes left
	move.w	d0,d2
	neg.w	d2
	andi.w	#3,d2		| d2 = number of bytes below 16 (-n)&3
	subq.w	#1,d0
	lsr.w	#2,d0		| number of 16 bytes blocks minus 1, if d2==0
	add.w	d2,d2		| offset in code (movl two bytes)
	jmp	2(pc, d2.w)
	|jmp	pc@(2,d2:w)	| jmp into loop
copy16:
	move.l	(a1)+,(a0)+
	move.l	(a1)+,(a0)+
	move.l	(a1)+,(a0)+
	move.l	(a1)+,(a0)+
	dbra	d0,copy16
less4:
	btst	#1,d1
	jeq	less2
	move.w	(a1)+,(a0)+
less2:
	btst	#0,d1
	jeq	none
	move.b	(a1),(a0)
none:
exit_d2:
	move.l	(sp)+,d2
	move.l	(sp)+,a0
exit:
	move.l 	a0,d0		| return dest (for memcpy only)
	rts

slow_copy:			| byte by bytes copy
	move.w	d0,d1
	neg.w	d1
	andi.w	#7,d1		| d1 = number of bytes blow 8 (-n)&7
	addq.l	#7,d0
	lsr.l	#3,d0		| number of 8 bytes block plus 1, if d1!=0
	add.w	d1,d1		| offset in code (movb two bytes)
	jmp	2(pc, d1.w)
	|jmp	pc@(2,d1:w)	| jump into loop
scopy:
	move.b	(a1)+,(a0)+
	move.b	(a1)+,(a0)+
	move.b	(a1)+,(a0)+
	move.b	(a1)+,(a0)+
	move.b	(a1)+,(a0)+
	move.b	(a1)+,(a0)+
	move.b	(a1)+,(a0)+
	move.b	(a1)+,(a0)+
	subq.l	#1,d0
	jne	scopy
	jra	exit_d2

top_down:
	add.l	d0,a1		| a1 byte after end of src
	add.l	d0,a0		| a0 byte after end of dest

	move.w	a1,d1		| exact the same as above, only with predec
	move.w	a0,d2
	eor.w	d2,d1
	btst	#0,d1
	jne	slow_copy_d

	btst	#0,d2
	jeq	both_even_d
	move.b	-(a1),-(a0)
	subq.l	#1,d0
both_even_d:
	move.q	#0,d1
	move.b	d0,d1
	lsr.l	#8,d0
	jeq	less256_d
	movem.l	d1/d3-d7/a2/a3/a5/a6,-(sp)
copy256_d:
	movem.l	-44(a1),d1-d7/a2/a3/a5/a6
	movem.l	d1-d7/a2/a3/a5/a6,-(a0)
	movem.l	-88(a1),d1-d7/a2/a3/a5/a6
	movem.l	d1-d7/a2/a3/a5/a6,-(a0)
	movem.l	-132(a1),d1-d7/a2/a3/a5/a6
	movem.l	d1-d7/a2/a3/a5/a6,-(a0)
	movem.l	-176(a1),d1-d7/a2/a3/a5/a6
	movem.l	d1-d7/a2/a3/a5/a6,-(a0)
	movem.l	-220(a1),d1-d7/a2/a3/a5/a6
	movem.l	d1-d7/a2/a3/a5/a6,-(a0)
	movem.l	-256(a1),d1-d7/a2-a3
	movem.l	d1-d7/a2-a3,-(a0)
	lea	-256(a1),a1
	subq.l	#1,d0
	jne	copy256_d
	movem.l	(sp)+,d1/d3-d7/a2/a3/a5/a6
less256_d:
	move.w	d1,d0
	lsr.w	#2,d0
	jeq	less4_d
	move.w	d0,d2
	neg.w	d2
	andi.w	#3,d2
	subq.w	#1,d0
	lsr.w	#2,d0
	add.w	d2,d2
	jmp	2(pc, d2.w)
	|jmp	pc@(2,d2:w)
copy16_d:
	move.l	-(a1),-(a0)
	move.l	-(a1),-(a0)
	move.l	-(a1),-(a0)
	move.l	-(a1),-(a0)
	dbra	d0,copy16_d
less4_d:
	btst	#1,d1
	jeq	less2_d
	move.w	-(a1),-(a0)
less2_d:
	btst	#0,d1
	jeq	exit_d2
	move.b	-(a1),-(a0)
	jra	exit_d2
slow_copy_d:
	move.w	d0,d1
	neg.w	d1
	andi.w	#7,d1
	addq.l	#7,d0
	lsr.l	#3,d0
	add.w	d1,d1
	jmp	2(pc, d1.w)
	|jmp	pc@(2,d1:w)
scopy_d:
	move.b	-(a1),-(a0)
	move.b	-(a1),-(a0)
	move.b	-(a1),-(a0)
	move.b	-(a1),-(a0)
	move.b	-(a1),-(a0)
	move.b	-(a1),-(a0)
	move.b	-(a1),-(a0)
	move.b	-(a1),-(a0)
	subq.l	#1,d0
	jne	scopy_d
	jra	exit_d2
