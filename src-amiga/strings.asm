	xdef	strlen
	xdef	putchar_

strlen:

		move.l	4(sp),a0
		move.l	a0,d0
.loop:		tst.b	(a0)+
		bne	.loop
		sub.l	a0,d0
		neg.l	d0
		rts

putchar_:	rts
