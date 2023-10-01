	SECTION	.text

	xdef	_PlaySample

; Parameters from stack:

; APTR	Sample data
; ULONG	Sample length in bytes
; DWORD	Frequency 

_PlaySample:

		movem.l	d2-d7/a2-a6,-(sp)

		bsr	StopKeyboardBuffer
		bsr	SaveMFPState
		move.w	#$2700,sr			; Status register: supervisor mode + interrupt priority mask 7
		lea.l	EmptyInterruptHandler,a0
		bsr	SetMFPInterrupt13
		move.l	56(sp),d0			; Frequenc
		bsr	ProgramTimerA
		bsr	EnableTimerInterrupt
		bsr	ResetAY8910
		sf.b	(Playing)
		bsr	StartPlayingSample
		move.w	#$2500,sr			; Status register: supervisor mode + interrupt priority mask 5
		movea.l	48(sp),a6			; A6 := Sample data pointer
		move.l	a6,d6
		add.l	52(sp),d6			; D6 := Pointer to end of sample
		st.b	(Playing)
		lea.l	PlayerInterruptHandler,a0
		bsr	SetMFPInterrupt13
	.0:	tst.b	(Playing)
		bne	.0
		move.w	#$2700,sr			; Status register: supervisor mode + interrupt priority mask 7
		bsr	RestoreMFPState
		bsr	StartKeyboardBuffer
		move.w	#$2000,sr			; Status register: supervisor mode + interrupt priority mask 0
	.ret:	movem.l	(sp)+,d2-d7/a2-a6
		rts

SaveMFPState:

		movea.l #$fffa00,a0
		lea.l	MFPData,a1
		move.b	$07(a0),$00(a1)
		move.b	$09(a0),$01(a1)
		move.b	$13(a0),$02(a1)
		move.b	$15(a0),$03(a1)
		move.b	$1f(a0),$04(a1)
		move.b	$19(a0),$05(a1)
		move.b	$17(a0),$06(a1)
		rts

RestoreMFPState:

		movea.l #$fffa00,a0
		lea.l	MFPData,a1
		move.b	$06(a1),$17(a0)
		move.b	$05(a1),$19(a0)
		move.b	$04(a1),$1f(a0)
		move.b	$03(a1),$15(a0)
		move.b	$02(a1),$13(a0)
		move.b	$01(a1),$09(a0)
		move.b	$00(a1),$07(a0)
		rts

SetMFPInterrupt13:

		move.w	sr,d0
		move.w	#$2700,sr
		move.l	a0,$134
		move.w	d0,sr
		rts

StartPlayingSample:

		move.w	sr,d0
		move.w	#$2700,sr
		st.b	(Playing)
		movea.l	#$FF8800,a4
		lea.l	SampleTable,a3
		sf.b	d7
		move.w  d0,sr
		rts

StopKeyboardBuffer:

		pea.l	.data
		move.w	#$00,-(sp)
		move.w	#$19,-(sp)
		trap  	#$0e			; Ikbdws() <- write to keyboard controller
		add.l	#$08,sp
		rts

	.data:	dc.b 	$13, $00

StartKeyboardBuffer:

		pea.l	.data
		move.w	#$00,-(sp)
		move.w	#$19,-(sp)
		trap  	#$0e			; Ikbdws() <- write to keyboard controller
		add.l	#$08,sp
		rts

	.data:	dc.b 	$11, $00

ProgramTimerA:

		movea.l	#$fffa00,a1		; MFP 68901
		move.b	#$00,$19(a1)		; Timer A control
		and.w	#$07,d0
		lea.l	.data,a0		; Table of values: 0506 0505 0405 2901 1f01 0802 0602 0104

		; First byte is operation mode:
		;
		;	$01		Delay mode (/4)
		;	$02		Delay mode (/10)
		;	$03		Delay mode (/16)
		;	$04		Delay mode (/50)
		;	$05		Delay mode (/64)
		;	$06		Delay mode (/100)
		;	$07		Delay mode (/200)
		;
		; Second byte is the data counter

		lsl.w	#$01,d0
		move.w	$00(a0,d0.W),d0
		move.b	d0,$19(a1)		; Timer A control
		lsr.w	#$08,d0
		move.b	d0,$1f(a1)		; Timer A data
		rts  	

.data		dc.w	$0506, $0505, $0405, $2901, $1f01, $0802, $0602, $0104

EnableTimerInterrupt:

		movea.l	#$fffa00,a0		; MFP 68901
		move.b	#$20,$13(a0) 		; Int Mask A
		move.b	#$00,$15(a0) 		; Int Mask B
		move.b	#$20,$07(a0) 		; Int Enable A
		move.b	#$00,$09(a0) 		; Int Enable B
		bclr.b	#$0003,$17(a0)		; Vector Base
		rts  

ResetAY8910:
		
		movea.l	#$ff8800,a0		; AY-3-8910
		move.b	#$00,$0000(a0)
		move.b	#$00,$0002(a0)
		move.b	#$01,$0000(a0)
		move.b	#$00,$0002(a0)
		move.b	#$02,$0000(a0)
		move.b	#$00,$0002(a0)
		move.b	#$03,$0000(a0)
		move.b	#$00,$0002(a0)
		move.b	#$04,$0000(a0)
		move.b	#$00,$0002(a0)
		move.b	#$05,$0000(a0)
		move.b	#$00,$0002(a0)
		move.b	#$07,$0000(a0)
		move.b	#$ff,$0002(a0)
		move.b	#$08,$0000(a0)
		move.b	#$00,$0002(a0)
		move.b	#$09,$0000(a0)
		move.b	#$00,$0002(a0)
		move.b	#$0a,$0000(a0)
		move.b	#$00,$0002(a0)
		rts

; ------------------------------------------------------------------------------
;  Interrupt handlers
; ------------------------------------------------------------------------------

EmptyInterruptHandler:

	rte

PlayerInterruptHandler:

	move.b	(a6)+,d7
	cmpa.l	d6,a6
	bgt.b	.end

	and.w	#$ff,d7
	add.b	#$80,d7
	lsl.w	#$03,d7
	move.l	$00(a3,d7.w),d5
	move.w	$04(a3,d7.w),d4
	movep.l	d5,$00(a4)
	movep.w	d4,$00(a4)
	rte

.end	move.w	#$2700,sr
	lea.l	EmptyInterruptHandler,a0
	bsr	SetMFPInterrupt13
	sf.b	(Playing)
	move.w	#$2500,sr
	rte


; ------------------------------------------------------------------------------


Playing		dc.b	0
MFPData		ds.b	7


SampleTable

; 8 bytes per sample value

	dc.b	$08, $0c, $09, $0b, $0a, $09, $00, $00
	dc.b	$08, $0c, $09, $0b, $0a, $09, $00, $00
	dc.b	$08, $0d, $09, $08, $0a, $08, $00, $00
	dc.b	$08, $0b, $09, $0b, $0a, $0b, $00, $00
	dc.b	$08, $0d, $09, $09, $0a, $05, $00, $00
	dc.b	$08, $0c, $09, $0b, $0a, $08, $00, $00
	dc.b	$08, $0d, $09, $09, $0a, $02, $00, $00
	dc.b	$08, $0d, $09, $08, $0a, $06, $00, $00
	dc.b	$08, $0c, $09, $0b, $0a, $07, $00, $00
	dc.b	$08, $0d, $09, $07, $0a, $07, $00, $00
	dc.b	$08, $0c, $09, $0b, $0a, $06, $00, $00
	dc.b	$08, $0c, $09, $0a, $0a, $09, $00, $00
	dc.b	$08, $0b, $09, $0b, $0a, $0a, $00, $00
	dc.b	$08, $0c, $09, $0b, $0a, $02, $00, $00
	dc.b	$08, $0c, $09, $0b, $0a, $00, $00, $00
	dc.b	$08, $0c, $09, $0a, $0a, $08, $00, $00
	dc.b	$08, $0d, $09, $06, $0a, $04, $00, $00
	dc.b	$08, $0d, $09, $05, $0a, $05, $00, $00
	dc.b	$08, $0d, $09, $05, $0a, $04, $00, $00
	dc.b	$08, $0c, $09, $09, $0a, $09, $00, $00
	dc.b	$08, $0d, $09, $04, $0a, $03, $00, $00
	dc.b	$08, $0b, $09, $0b, $0a, $09, $00, $00
	dc.b	$08, $0c, $09, $0a, $0a, $05, $00, $00
	dc.b	$08, $0b, $09, $0a, $0a, $0a, $00, $00
	dc.b	$08, $0c, $09, $09, $0a, $08, $00, $00
	dc.b	$08, $0b, $09, $0b, $0a, $08, $00, $00
	dc.b	$08, $0c, $09, $0a, $0a, $00, $00, $00
	dc.b	$08, $0c, $09, $0a, $0a, $00, $00, $00
	dc.b	$08, $0c, $09, $09, $0a, $07, $00, $00
	dc.b	$08, $0b, $09, $0b, $0a, $07, $00, $00
	dc.b	$08, $0c, $09, $09, $0a, $06, $00, $00
	dc.b	$08, $0b, $09, $0b, $0a, $06, $00, $00
	dc.b	$08, $0b, $09, $0a, $0a, $09, $00, $00
	dc.b	$08, $0b, $09, $0b, $0a, $05, $00, $00
	dc.b	$08, $0a, $09, $0a, $0a, $0a, $00, $00
	dc.b	$08, $0b, $09, $0b, $0a, $02, $00, $00
	dc.b	$08, $0b, $09, $0a, $0a, $08, $00, $00
	dc.b	$08, $0c, $09, $07, $0a, $07, $00, $00
	dc.b	$08, $0c, $09, $08, $0a, $04, $00, $00
	dc.b	$08, $0c, $09, $07, $0a, $06, $00, $00
	dc.b	$08, $0b, $09, $09, $0a, $09, $00, $00
	dc.b	$08, $0c, $09, $06, $0a, $06, $00, $00
	dc.b	$08, $0a, $09, $0a, $0a, $09, $00, $00
	dc.b	$08, $0c, $09, $07, $0a, $03, $00, $00
	dc.b	$08, $0b, $09, $0a, $0a, $05, $00, $00
	dc.b	$08, $0b, $09, $09, $0a, $08, $00, $00
	dc.b	$08, $0b, $09, $0a, $0a, $03, $00, $00
	dc.b	$08, $0a, $09, $0a, $0a, $08, $00, $00
	dc.b	$08, $0b, $09, $0a, $0a, $00, $00, $00
	dc.b	$08, $0b, $09, $09, $0a, $07, $00, $00
	dc.b	$08, $0b, $09, $08, $0a, $08, $00, $00
	dc.b	$08, $0a, $09, $0a, $0a, $07, $00, $00
	dc.b	$08, $0a, $09, $09, $0a, $09, $00, $00
	dc.b	$08, $0c, $09, $01, $0a, $01, $00, $00
	dc.b	$08, $0a, $09, $0a, $0a, $06, $00, $00
	dc.b	$08, $0b, $09, $08, $0a, $07, $00, $00
	dc.b	$08, $0a, $09, $0a, $0a, $05, $00, $00
	dc.b	$08, $0a, $09, $09, $0a, $08, $00, $00
	dc.b	$08, $0a, $09, $0a, $0a, $02, $00, $00
	dc.b	$08, $0a, $09, $0a, $0a, $01, $00, $00
	dc.b	$08, $0a, $09, $0a, $0a, $00, $00, $00
	dc.b	$08, $09, $09, $09, $0a, $09, $00, $00
	dc.b	$08, $0a, $09, $08, $0a, $08, $00, $00
	dc.b	$08, $0b, $09, $08, $0a, $01, $00, $00
	dc.b	$08, $0a, $09, $09, $0a, $06, $00, $00
	dc.b	$08, $0b, $09, $07, $0a, $04, $00, $00
	dc.b	$08, $0a, $09, $09, $0a, $05, $00, $00
	dc.b	$08, $09, $09, $09, $0a, $08, $00, $00
	dc.b	$08, $0a, $09, $09, $0a, $03, $00, $00
	dc.b	$08, $0a, $09, $08, $0a, $06, $00, $00
	dc.b	$08, $0a, $09, $09, $0a, $00, $00, $00
	dc.b	$08, $09, $09, $09, $0a, $07, $00, $00
	dc.b	$08, $09, $09, $08, $0a, $08, $00, $00
	dc.b	$08, $0a, $09, $08, $0a, $04, $00, $00
	dc.b	$08, $09, $09, $09, $0a, $06, $00, $00
	dc.b	$08, $0a, $09, $08, $0a, $01, $00, $00
	dc.b	$08, $09, $09, $09, $0a, $05, $00, $00
	dc.b	$08, $09, $09, $08, $0a, $07, $00, $00
	dc.b	$08, $08, $09, $08, $0a, $08, $00, $00
	dc.b	$08, $09, $09, $09, $0a, $02, $00, $00
	dc.b	$08, $09, $09, $08, $0a, $06, $00, $00
	dc.b	$08, $09, $09, $09, $0a, $00, $00, $00
	dc.b	$08, $09, $09, $07, $0a, $07, $00, $00
	dc.b	$08, $08, $09, $08, $0a, $07, $00, $00
	dc.b	$08, $09, $09, $07, $0a, $06, $00, $00
	dc.b	$08, $09, $09, $08, $0a, $02, $00, $00
	dc.b	$08, $08, $09, $08, $0a, $06, $00, $00
	dc.b	$08, $09, $09, $06, $0a, $06, $00, $00
	dc.b	$08, $08, $09, $07, $0a, $07, $00, $00
	dc.b	$08, $08, $09, $08, $0a, $04, $00, $00
	dc.b	$08, $08, $09, $07, $0a, $06, $00, $00
	dc.b	$08, $08, $09, $08, $0a, $02, $00, $00
	dc.b	$08, $07, $09, $07, $0a, $07, $00, $00
	dc.b	$08, $08, $09, $06, $0a, $06, $00, $00
	dc.b	$08, $08, $09, $07, $0a, $04, $00, $00
	dc.b	$08, $07, $09, $07, $0a, $06, $00, $00
	dc.b	$08, $08, $09, $06, $0a, $05, $00, $00
	dc.b	$08, $08, $09, $06, $0a, $04, $00, $00
	dc.b	$08, $07, $09, $06, $0a, $06, $00, $00
	dc.b	$08, $07, $09, $07, $0a, $04, $00, $00
	dc.b	$08, $08, $09, $05, $0a, $04, $00, $00
	dc.b	$08, $06, $09, $06, $0a, $06, $00, $00
	dc.b	$08, $07, $09, $06, $0a, $04, $00, $00
	dc.b	$08, $07, $09, $05, $0a, $05, $00, $00
	dc.b	$08, $06, $09, $06, $0a, $05, $00, $00
	dc.b	$08, $06, $09, $06, $0a, $04, $00, $00
	dc.b	$08, $06, $09, $05, $0a, $05, $00, $00
	dc.b	$08, $06, $09, $06, $0a, $02, $00, $00
	dc.b	$08, $06, $09, $05, $0a, $04, $00, $00
	dc.b	$08, $05, $09, $05, $0a, $05, $00, $00
	dc.b	$08, $06, $09, $05, $0a, $02, $00, $00
	dc.b	$08, $05, $09, $05, $0a, $04, $00, $00
	dc.b	$08, $05, $09, $04, $0a, $04, $00, $00
	dc.b	$08, $05, $09, $05, $0a, $02, $00, $00
	dc.b	$08, $04, $09, $04, $0a, $04, $00, $00
	dc.b	$08, $04, $09, $04, $0a, $03, $00, $00
	dc.b	$08, $04, $09, $04, $0a, $02, $00, $00
	dc.b	$08, $04, $09, $03, $0a, $03, $00, $00
	dc.b	$08, $03, $09, $03, $0a, $03, $00, $00
	dc.b	$08, $03, $09, $03, $0a, $02, $00, $00
	dc.b	$08, $03, $09, $02, $0a, $02, $00, $00
	dc.b	$08, $02, $09, $02, $0a, $02, $00, $00
	dc.b	$08, $02, $09, $02, $0a, $01, $00, $00
	dc.b	$08, $01, $09, $01, $0a, $01, $00, $00
	dc.b	$08, $02, $09, $01, $0a, $00, $00, $00
	dc.b	$08, $01, $09, $01, $0a, $00, $00, $00
	dc.b	$08, $01, $09, $00, $0a, $00, $00, $00
	dc.b	$08, $00, $09, $00, $0a, $00, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0c, $00, $00
	dc.b	$08, $0f, $09, $03, $0a, $00, $00, $00
	dc.b	$08, $0f, $09, $03, $0a, $00, $00, $00
	dc.b	$08, $0f, $09, $03, $0a, $00, $00, $00
	dc.b	$08, $0f, $09, $03, $0a, $00, $00, $00
	dc.b	$08, $0f, $09, $03, $0a, $00, $00, $00
	dc.b	$08, $0f, $09, $03, $0a, $00, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0c, $00, $00
	dc.b	$08, $0e, $09, $0d, $0a, $00, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0d, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0d, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0d, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0d, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0d, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0d, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $0a, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0c, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0c, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $09, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $09, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $05, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $00, $00, $00
	dc.b	$08, $0e, $09, $0c, $0a, $00, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $0a, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0b, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0b, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $09, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $09, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $09, $00, $00
	dc.b	$08, $0d, $09, $0c, $0a, $0c, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $07, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $00, $00, $00
	dc.b	$08, $0e, $09, $0b, $0a, $00, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $09, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $09, $00, $00
	dc.b	$08, $0e, $09, $0a, $0a, $09, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $08, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $07, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $04, $00, $00
	dc.b	$08, $0d, $09, $0d, $0a, $00, $00, $00
	dc.b	$08, $0e, $09, $0a, $0a, $04, $00, $00
	dc.b	$08, $0e, $09, $09, $0a, $09, $00, $00
	dc.b	$08, $0e, $09, $09, $0a, $09, $00, $00
	dc.b	$08, $0d, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $09, $0a, $08, $00, $00
	dc.b	$08, $0e, $09, $09, $0a, $08, $00, $00
	dc.b	$08, $0e, $09, $09, $0a, $07, $00, $00
	dc.b	$08, $0e, $09, $08, $0a, $08, $00, $00
	dc.b	$08, $0e, $09, $09, $0a, $01, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $0c, $00, $00
	dc.b	$08, $0d, $09, $0c, $0a, $0a, $00, $00
	dc.b	$08, $0e, $09, $08, $0a, $06, $00, $00
	dc.b	$08, $0e, $09, $07, $0a, $07, $00, $00
	dc.b	$08, $0e, $09, $08, $0a, $00, $00, $00
	dc.b	$08, $0e, $09, $07, $0a, $05, $00, $00
	dc.b	$08, $0e, $09, $06, $0a, $06, $00, $00
	dc.b	$08, $0d, $09, $0c, $0a, $09, $00, $00
	dc.b	$08, $0e, $09, $05, $0a, $05, $00, $00
	dc.b	$08, $0e, $09, $04, $0a, $04, $00, $00
	dc.b	$08, $0d, $09, $0c, $0a, $08, $00, $00
	dc.b	$08, $0d, $09, $0b, $0a, $0b, $00, $00
	dc.b	$08, $0e, $09, $00, $0a, $00, $00, $00
	dc.b	$08, $0d, $09, $0c, $0a, $06, $00, $00
	dc.b	$08, $0d, $09, $0c, $0a, $05, $00, $00
	dc.b	$08, $0d, $09, $0c, $0a, $02, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $0b, $00, $00
	dc.b	$08, $0d, $09, $0b, $0a, $0a, $00, $00
	dc.b	$08, $0d, $09, $0b, $0a, $0a, $00, $00
	dc.b	$08, $0d, $09, $0b, $0a, $0a, $00, $00
	dc.b	$08, $0d, $09, $0b, $0a, $0a, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $0a, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $0a, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $0a, $00, $00
	dc.b	$08, $0d, $09, $0b, $0a, $09, $00, $00
	dc.b	$08, $0d, $09, $0b, $0a, $09, $00, $00
	dc.b	$08, $0d, $09, $0a, $0a, $0a, $00, $00
	dc.b	$08, $0d, $09, $0a, $0a, $0a, $00, $00
	dc.b	$08, $0d, $09, $0a, $0a, $0a, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $09, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $09, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $09, $00, $00
	dc.b	$08, $0d, $09, $0b, $0a, $06, $00, $00
	dc.b	$08, $0c, $09, $0b, $0a, $0b, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $08, $00, $00
	dc.b	$08, $0d, $09, $0b, $0a, $00, $00, $00
	dc.b	$08, $0d, $09, $0b, $0a, $00, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $07, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $06, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $05, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $03, $00, $00
	dc.b	$08, $0c, $09, $0c, $0a, $01, $00, $00
	dc.b	$08, $0c, $09, $0b, $0a, $0a, $00, $00
	dc.b	$08, $0d, $09, $0a, $0a, $05, $00, $00
	dc.b	$08, $0d, $09, $0a, $0a, $04, $00, $00
	dc.b	$08, $0d, $09, $0a, $0a, $02, $00, $00
	dc.b	$08, $0d, $09, $09, $0a, $08, $00, $00
	dc.b	$08, $0d, $09, $09, $0a, $08, $00, $00
	dc.b	$05, $06, $05, $05, $04, $05, $29, $01
	dc.b	$1f, $01, $08, $02, $06, $02, $01, $04
	dc.b	$13, $00, $11, $00, $00, $00, $00, $00
	dc.b	$00, $00, $00, $00, $00, $00, $00, $00
	dc.b	$00, $00, $00, $00, $00, $00, $00, $00
	dc.b	$00, $00, $00, $00, $00, $00, $00, $00
