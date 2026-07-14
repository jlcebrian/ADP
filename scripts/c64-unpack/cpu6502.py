# Minimal NMOS 6502 core for sandboxed unpacker execution
class CPU:
    def __init__(self, ram):
        self.ram = ram
        self.a = self.x = self.y = 0
        self.sp = 0xFD
        self.pc = 0
        self.n = self.v = self.z = self.c = self.d = False
        self.i = True
        self.raster = 0
        self.halted = None
        # Synthesized ROM: RTS everywhere, real IRQ entry/exit stubs, vectors
        rom = bytearray(b'\x60' * 0x6000)   # covers A000-BFFF (0..1FFF) and E000-FFFF (4000..5FFF)
        E = 0x4000
        rom[E+0x1F48:E+0x1F50] = bytes([0x48,0x8A,0x48,0x98,0x48,0x6C,0x14,0x03])  # FF48: push regs, JMP ($0314)
        rom[E+0x0A31:E+0x0A37] = bytes([0x68,0xA8,0x68,0xAA,0x68,0x40])            # EA31: pop regs, RTI
        rom[E+0x0A81:E+0x0A87] = bytes([0x68,0xA8,0x68,0xAA,0x68,0x40])            # EA81: same
        rom[E+0x1FFA:E+0x2000] = bytes([0x31,0xEA, 0x00,0x08, 0x48,0xFF])          # NMI/RESET/IRQ vectors
        rom[E+0x1FE4:E+0x1FE7] = bytes([0xA9,0x20,0x60])                            # FFE4 GETIN: return space
        self.rom = rom

    def irq_allowed(self):
        return getattr(self, 'cia_en', 0x81) & 0x7F or getattr(self, 'vic_en', 0)

    def irq(self):
        if self.i or not self.irq_allowed(): return
        self.push(self.pc >> 8); self.push(self.pc & 0xFF)
        self.push(self.flags() & ~0x10)
        self.i = True
        self.pc = self.rd(0xFFFE) | self.rd(0xFFFF) << 8

    def rd(self, a):
        a &= 0xFFFF
        bank = self.ram[1]
        if a >= 0xE000 and (bank & 2):
            return self.rom[0x4000 + a - 0xE000]
        if 0xA000 <= a < 0xC000 and (bank & 3) == 3:
            return self.rom[a - 0xA000]
        if a == 0xD012:                      # fake raster sweep (own counter)
            self.raster = (self.raster + 1) & 0xFF
            return self.raster
        if a == 0xD011:                      # raster bit 8, independent counter
            self.raster11 = (getattr(self, 'raster11', 0) + 1) & 0xFF
            return 0x80 if self.raster11 & 0x40 else 0x00
        if a == 0xDC00 or a == 0xDC01:       # CIA1 keyboard: nothing pressed
            return 0xFF
        return self.ram[a]

    def wr(self, a, v):
        a &= 0xFFFF
        v &= 0xFF
        if a == 0xDC0D:                       # CIA1 interrupt mask
            en = getattr(self, 'cia_en', 0x81)
            if v & 0x80: en |= (v & 0x7F)
            else:        en &= ~(v & 0x7F)
            self.cia_en = en
        elif a == 0xD01A:                     # VIC interrupt enable
            self.vic_en = v & 0x0F
        self.ram[a] = v

    def push(self, v):
        self.wr(0x100 + self.sp, v); self.sp = (self.sp - 1) & 0xFF
    def pop(self):
        self.sp = (self.sp + 1) & 0xFF; return self.rd(0x100 + self.sp)

    def setnz(self, v):
        v &= 0xFF; self.n = v >= 0x80; self.z = v == 0; return v

    def flags(self):
        return ((self.n<<7)|(self.v<<6)|0x20|0x10|(self.d<<3)|(self.i<<2)|(self.z<<1)|(self.c and 1))
    def setflags(self, p):
        self.n=bool(p&0x80); self.v=bool(p&0x40); self.d=bool(p&8); self.i=bool(p&4); self.z=bool(p&2); self.c=bool(p&1)

    def adc(self, m):
        if self.d:
            lo = (self.a & 15) + (m & 15) + self.c
            hi = (self.a >> 4) + (m >> 4)
            if lo > 9: lo += 6; hi += 1
            self.v = False
            if hi > 9: hi += 6
            self.c = hi > 15
            self.a = self.setnz(((hi & 15) << 4) | (lo & 15))
        else:
            r = self.a + m + self.c
            self.v = (~(self.a ^ m) & (self.a ^ r) & 0x80) != 0
            self.c = r > 0xFF
            self.a = self.setnz(r)
    def sbc(self, m):
        if self.d:
            m = 0x99 - m
            self.adc(m)
        else:
            self.adc(m ^ 0xFF)
    def cmp_(self, r, m):
        t = r - m; self.c = t >= 0; self.setnz(t & 0xFF)

    def step(self):
        rd, wr = self.rd, self.wr
        pc = self.pc
        op = rd(pc); pc = (pc + 1) & 0xFFFF
        def imm():
            nonlocal pc
            v = rd(pc); pc = (pc+1)&0xFFFF; return v
        def zp():   return imm()
        def zpx():  return (imm() + self.x) & 0xFF
        def zpy():  return (imm() + self.y) & 0xFF
        def ab():
            nonlocal pc
            v = rd(pc) | rd(pc+1)<<8; pc = (pc+2)&0xFFFF; return v
        def abx():  return (ab() + self.x) & 0xFFFF
        def aby():  return (ab() + self.y) & 0xFFFF
        def inx():
            z = (imm() + self.x) & 0xFF
            return rd(z) | rd((z+1)&0xFF)<<8
        def iny():  
            z = imm()
            return ((rd(z) | rd((z+1)&0xFF)<<8) + self.y) & 0xFFFF
        def br(cond):
            nonlocal pc
            off = imm()
            if cond: pc = (pc + (off if off < 0x80 else off-256)) & 0xFFFF

        A = 'a'
        if   op == 0xA9: self.a = self.setnz(imm())
        elif op == 0xA5: self.a = self.setnz(rd(zp()))
        elif op == 0xB5: self.a = self.setnz(rd(zpx()))
        elif op == 0xAD: self.a = self.setnz(rd(ab()))
        elif op == 0xBD: self.a = self.setnz(rd(abx()))
        elif op == 0xB9: self.a = self.setnz(rd(aby()))
        elif op == 0xA1: self.a = self.setnz(rd(inx()))
        elif op == 0xB1: self.a = self.setnz(rd(iny()))
        elif op == 0xA2: self.x = self.setnz(imm())
        elif op == 0xA6: self.x = self.setnz(rd(zp()))
        elif op == 0xB6: self.x = self.setnz(rd(zpy()))
        elif op == 0xAE: self.x = self.setnz(rd(ab()))
        elif op == 0xBE: self.x = self.setnz(rd(aby()))
        elif op == 0xA0: self.y = self.setnz(imm())
        elif op == 0xA4: self.y = self.setnz(rd(zp()))
        elif op == 0xB4: self.y = self.setnz(rd(zpx()))
        elif op == 0xAC: self.y = self.setnz(rd(ab()))
        elif op == 0xBC: self.y = self.setnz(rd(abx()))
        elif op == 0x85: wr(zp(), self.a)
        elif op == 0x95: wr(zpx(), self.a)
        elif op == 0x8D: wr(ab(), self.a)
        elif op == 0x9D: wr(abx(), self.a)
        elif op == 0x99: wr(aby(), self.a)
        elif op == 0x81: wr(inx(), self.a)
        elif op == 0x91: wr(iny(), self.a)
        elif op == 0x86: wr(zp(), self.x)
        elif op == 0x96: wr(zpy(), self.x)
        elif op == 0x8E: wr(ab(), self.x)
        elif op == 0x84: wr(zp(), self.y)
        elif op == 0x94: wr(zpx(), self.y)
        elif op == 0x8C: wr(ab(), self.y)
        elif op == 0xAA: self.x = self.setnz(self.a)
        elif op == 0xA8: self.y = self.setnz(self.a)
        elif op == 0x8A: self.a = self.setnz(self.x)
        elif op == 0x98: self.a = self.setnz(self.y)
        elif op == 0xBA: self.x = self.setnz(self.sp)
        elif op == 0x9A: self.sp = self.x
        elif op == 0xE8: self.x = self.setnz(self.x + 1)
        elif op == 0xC8: self.y = self.setnz(self.y + 1)
        elif op == 0xCA: self.x = self.setnz(self.x - 1)
        elif op == 0x88: self.y = self.setnz(self.y - 1)
        elif op == 0xE6: a_ = zp(); wr(a_, self.setnz(rd(a_) + 1))
        elif op == 0xF6: a_ = zpx(); wr(a_, self.setnz(rd(a_) + 1))
        elif op == 0xEE: a_ = ab(); wr(a_, self.setnz(rd(a_) + 1))
        elif op == 0xFE: a_ = abx(); wr(a_, self.setnz(rd(a_) + 1))
        elif op == 0xC6: a_ = zp(); wr(a_, self.setnz(rd(a_) - 1))
        elif op == 0xD6: a_ = zpx(); wr(a_, self.setnz(rd(a_) - 1))
        elif op == 0xCE: a_ = ab(); wr(a_, self.setnz(rd(a_) - 1))
        elif op == 0xDE: a_ = abx(); wr(a_, self.setnz(rd(a_) - 1))
        elif op == 0x69: self.adc(imm())
        elif op == 0x65: self.adc(rd(zp()))
        elif op == 0x75: self.adc(rd(zpx()))
        elif op == 0x6D: self.adc(rd(ab()))
        elif op == 0x7D: self.adc(rd(abx()))
        elif op == 0x79: self.adc(rd(aby()))
        elif op == 0x61: self.adc(rd(inx()))
        elif op == 0x71: self.adc(rd(iny()))
        elif op == 0xE9: self.sbc(imm())
        elif op == 0xE5: self.sbc(rd(zp()))
        elif op == 0xF5: self.sbc(rd(zpx()))
        elif op == 0xED: self.sbc(rd(ab()))
        elif op == 0xFD: self.sbc(rd(abx()))
        elif op == 0xF9: self.sbc(rd(aby()))
        elif op == 0xE1: self.sbc(rd(inx()))
        elif op == 0xF1: self.sbc(rd(iny()))
        elif op == 0x29: self.a = self.setnz(self.a & imm())
        elif op == 0x25: self.a = self.setnz(self.a & rd(zp()))
        elif op == 0x35: self.a = self.setnz(self.a & rd(zpx()))
        elif op == 0x2D: self.a = self.setnz(self.a & rd(ab()))
        elif op == 0x3D: self.a = self.setnz(self.a & rd(abx()))
        elif op == 0x39: self.a = self.setnz(self.a & rd(aby()))
        elif op == 0x21: self.a = self.setnz(self.a & rd(inx()))
        elif op == 0x31: self.a = self.setnz(self.a & rd(iny()))
        elif op == 0x09: self.a = self.setnz(self.a | imm())
        elif op == 0x05: self.a = self.setnz(self.a | rd(zp()))
        elif op == 0x15: self.a = self.setnz(self.a | rd(zpx()))
        elif op == 0x0D: self.a = self.setnz(self.a | rd(ab()))
        elif op == 0x1D: self.a = self.setnz(self.a | rd(abx()))
        elif op == 0x19: self.a = self.setnz(self.a | rd(aby()))
        elif op == 0x01: self.a = self.setnz(self.a | rd(inx()))
        elif op == 0x11: self.a = self.setnz(self.a | rd(iny()))
        elif op == 0x49: self.a = self.setnz(self.a ^ imm())
        elif op == 0x45: self.a = self.setnz(self.a ^ rd(zp()))
        elif op == 0x55: self.a = self.setnz(self.a ^ rd(zpx()))
        elif op == 0x4D: self.a = self.setnz(self.a ^ rd(ab()))
        elif op == 0x5D: self.a = self.setnz(self.a ^ rd(abx()))
        elif op == 0x59: self.a = self.setnz(self.a ^ rd(aby()))
        elif op == 0x41: self.a = self.setnz(self.a ^ rd(inx()))
        elif op == 0x51: self.a = self.setnz(self.a ^ rd(iny()))
        elif op == 0xC9: self.cmp_(self.a, imm())
        elif op == 0xC5: self.cmp_(self.a, rd(zp()))
        elif op == 0xD5: self.cmp_(self.a, rd(zpx()))
        elif op == 0xCD: self.cmp_(self.a, rd(ab()))
        elif op == 0xDD: self.cmp_(self.a, rd(abx()))
        elif op == 0xD9: self.cmp_(self.a, rd(aby()))
        elif op == 0xC1: self.cmp_(self.a, rd(inx()))
        elif op == 0xD1: self.cmp_(self.a, rd(iny()))
        elif op == 0xE0: self.cmp_(self.x, imm())
        elif op == 0xE4: self.cmp_(self.x, rd(zp()))
        elif op == 0xEC: self.cmp_(self.x, rd(ab()))
        elif op == 0xC0: self.cmp_(self.y, imm())
        elif op == 0xC4: self.cmp_(self.y, rd(zp()))
        elif op == 0xCC: self.cmp_(self.y, rd(ab()))
        elif op == 0x0A: self.c = self.a >= 0x80; self.a = self.setnz(self.a << 1)
        elif op in (0x06,0x16,0x0E,0x1E):
            a_ = {0x06:zp,0x16:zpx,0x0E:ab,0x1E:abx}[op]()
            v = rd(a_); self.c = v >= 0x80; wr(a_, self.setnz(v << 1))
        elif op == 0x4A: self.c = self.a & 1; self.a = self.setnz(self.a >> 1)
        elif op in (0x46,0x56,0x4E,0x5E):
            a_ = {0x46:zp,0x56:zpx,0x4E:ab,0x5E:abx}[op]()
            v = rd(a_); self.c = v & 1; wr(a_, self.setnz(v >> 1))
        elif op == 0x2A:
            c = self.c; self.c = self.a >= 0x80; self.a = self.setnz(((self.a << 1) | c) & 0xFF)
        elif op in (0x26,0x36,0x2E,0x3E):
            a_ = {0x26:zp,0x36:zpx,0x2E:ab,0x3E:abx}[op]()
            v = rd(a_); c = self.c; self.c = v >= 0x80; wr(a_, self.setnz(((v << 1) | c) & 0xFF))
        elif op == 0x6A:
            c = self.c; self.c = self.a & 1; self.a = self.setnz((self.a >> 1) | (0x80 if c else 0))
        elif op in (0x66,0x76,0x6E,0x7E):
            a_ = {0x66:zp,0x76:zpx,0x6E:ab,0x7E:abx}[op]()
            v = rd(a_); c = self.c; self.c = v & 1; wr(a_, self.setnz((v >> 1) | (0x80 if c else 0)))
        elif op == 0x24: v = rd(zp()); self.n = v>=0x80; self.v = bool(v&0x40); self.z = (v & self.a)==0
        elif op == 0x2C: v = rd(ab()); self.n = v>=0x80; self.v = bool(v&0x40); self.z = (v & self.a)==0
        elif op == 0x10: br(not self.n)
        elif op == 0x30: br(self.n)
        elif op == 0x50: br(not self.v)
        elif op == 0x70: br(self.v)
        elif op == 0x90: br(not self.c)
        elif op == 0xB0: br(self.c)
        elif op == 0xD0: br(not self.z)
        elif op == 0xF0: br(self.z)
        elif op == 0x4C: pc = ab()
        elif op == 0x6C:
            a_ = ab(); pc = rd(a_) | rd((a_ & 0xFF00) | ((a_+1) & 0xFF))<<8
        elif op == 0x20:
            a_ = ab()
            ret = (pc - 1) & 0xFFFF
            self.push(ret >> 8); self.push(ret & 0xFF)
            pc = a_
        elif op == 0x60:
            lo = self.pop(); hi = self.pop(); pc = ((hi<<8)|lo) + 1 & 0xFFFF
        elif op == 0x40:
            self.setflags(self.pop()); lo = self.pop(); hi = self.pop(); pc = (hi<<8)|lo
        elif op == 0x48: self.push(self.a)
        elif op == 0x68: self.a = self.setnz(self.pop())
        elif op == 0x08: self.push(self.flags())
        elif op == 0x28: self.setflags(self.pop())
        elif op == 0x18: self.c = False
        elif op == 0x38: self.c = True
        elif op == 0x58: self.i = False
        elif op == 0x78: self.i = True
        elif op == 0xB8: self.v = False
        elif op == 0xD8: self.d = False
        elif op == 0xF8: self.d = True
        elif op == 0xEA: pass
        elif op == 0x00: self.halted = 'BRK'
        # --- undocumented opcodes ---
        elif op in (0x1A,0x3A,0x5A,0x7A,0xDA,0xFA): pass                     # NOP
        elif op in (0x80,0x82,0x89,0xC2,0xE2): imm()                          # NOP imm
        elif op in (0x04,0x44,0x64): zp()                                     # NOP zp
        elif op in (0x14,0x34,0x54,0x74,0xD4,0xF4): zpx()                     # NOP zp,x
        elif op == 0x0C: ab()                                                 # NOP abs
        elif op in (0x1C,0x3C,0x5C,0x7C,0xDC,0xFC): abx()                     # NOP abs,x
        elif op in (0x07,0x17,0x0F,0x1F,0x1B,0x03,0x13):                      # SLO
            a_ = {0x07:zp,0x17:zpx,0x0F:ab,0x1F:abx,0x1B:aby,0x03:inx,0x13:iny}[op]()
            v = rd(a_); self.c = v >= 0x80; v = (v << 1) & 0xFF; wr(a_, v)
            self.a = self.setnz(self.a | v)
        elif op in (0x27,0x37,0x2F,0x3F,0x3B,0x23,0x33):                      # RLA
            a_ = {0x27:zp,0x37:zpx,0x2F:ab,0x3F:abx,0x3B:aby,0x23:inx,0x33:iny}[op]()
            v = rd(a_); c = self.c; self.c = v >= 0x80; v = ((v << 1) | c) & 0xFF; wr(a_, v)
            self.a = self.setnz(self.a & v)
        elif op in (0x47,0x57,0x4F,0x5F,0x5B,0x43,0x53):                      # SRE
            a_ = {0x47:zp,0x57:zpx,0x4F:ab,0x5F:abx,0x5B:aby,0x43:inx,0x53:iny}[op]()
            v = rd(a_); self.c = v & 1; v >>= 1; wr(a_, v)
            self.a = self.setnz(self.a ^ v)
        elif op in (0x67,0x77,0x6F,0x7F,0x7B,0x63,0x73):                      # RRA
            a_ = {0x67:zp,0x77:zpx,0x6F:ab,0x7F:abx,0x7B:aby,0x63:inx,0x73:iny}[op]()
            v = rd(a_); c = self.c; self.c = v & 1; v = (v >> 1) | (0x80 if c else 0); wr(a_, v)
            self.adc(v)
        elif op in (0x87,0x97,0x8F,0x83):                                     # SAX
            a_ = {0x87:zp,0x97:zpy,0x8F:ab,0x83:inx}[op]()
            wr(a_, self.a & self.x)
        elif op in (0xA7,0xB7,0xAF,0xBF,0xA3,0xB3):                           # LAX
            a_ = {0xA7:zp,0xB7:zpy,0xAF:ab,0xBF:aby,0xA3:inx,0xB3:iny}[op]()
            self.a = self.x = self.setnz(rd(a_))
        elif op in (0xC7,0xD7,0xCF,0xDF,0xDB,0xC3,0xD3):                      # DCP
            a_ = {0xC7:zp,0xD7:zpx,0xCF:ab,0xDF:abx,0xDB:aby,0xC3:inx,0xD3:iny}[op]()
            v = (rd(a_) - 1) & 0xFF; wr(a_, v); self.cmp_(self.a, v)
        elif op in (0xE7,0xF7,0xEF,0xFF,0xFB,0xE3,0xF3):                      # ISC
            a_ = {0xE7:zp,0xF7:zpx,0xEF:ab,0xFF:abx,0xFB:aby,0xE3:inx,0xF3:iny}[op]()
            v = (rd(a_) + 1) & 0xFF; wr(a_, v); self.sbc(v)
        elif op in (0x0B,0x2B):                                               # ANC
            self.a = self.setnz(self.a & imm()); self.c = self.n
        elif op == 0x4B:                                                      # ALR
            self.a = self.setnz(self.a & imm()); self.c = self.a & 1; self.a = self.setnz(self.a >> 1)
        elif op == 0x6B:                                                      # ARR (approx)
            self.a = self.a & imm(); self.a = self.setnz((self.a >> 1) | (0x80 if self.c else 0))
            self.c = bool(self.a & 0x40); self.v = bool((self.a ^ (self.a << 1)) & 0x40)
        elif op == 0xCB:                                                      # AXS
            t = (self.a & self.x) - imm(); self.c = t >= 0; self.x = self.setnz(t & 0xFF)
        elif op == 0xEB: self.sbc(imm())                                      # USBC
        elif op in (0x02,0x12,0x22,0x32,0x42,0x52,0x62,0x72,0x92,0xB2,0xD2,0xF2):
            self.jams = getattr(self, 'jams', 0) + 1                          # JAM: skip
        else: self.halted = f'op {op:02X} @ {self.pc:04X}'
        self.pc = pc
