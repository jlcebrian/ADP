; Exact source reconstruction of src-amiga/bootblock.bin
; Assembles to the same 1024-byte boot block.
; The checksum/root field are the same values currently stored in bootblock.bin.

        ; Header
        dc.b    'D','O','S',0
        dc.l    $E33D0E73
        dc.l    880

        ; Code starts at offset $000C
start:
        lea     dos_name(pc),a1
        moveq   #37,d0
        jsr     -$228(a6)              ; OpenLibrary("dos.library", 37)
        tst.l   d0
        beq.s   fail

        movea.l d0,a1
        bset    #6,$22(a1)
        jsr     -$19E(a6)              ; CloseLibrary(d0)

        lea     expansion_name(pc),a1
        jsr     -$60(a6)               ; OldOpenLibrary("expansion.library")
        tst.l   d0
        beq.s   fail

        movea.l d0,a0
        movea.l $16(a0),a0
        moveq   #0,d0
        rts

fail:
        moveq   #-1,d0
        rts

dos_name:
        dc.b    "dos.library",0

expansion_name:
        dc.b    "expansion.library",0

        ; Remaining bytes are zero-filled in the shipped boot block.
        ; Current payload up to here is 0x5E bytes, so pad to 1024 bytes.
        dcb.b   1024-(*),0
