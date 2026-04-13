.text

.globl CallWithStack
CallWithStack:
	move.l 4(%sp), %a0
	move.l 8(%sp), %a1
	move.l %sp, %d1
	move.l %a0, %sp
	move.l %d1, -(%sp)
	jsr (%a1)
	move.l (%sp)+, %a0
	move.l %a0, %sp
	rts
