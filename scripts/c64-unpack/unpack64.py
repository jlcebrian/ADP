import sys, re
sys.path.insert(0, '.')
from cpu6502 import CPU

SECTORS = [21]*17 + [19]*7 + [18]*6 + [17]*5
def files_of(data):
    def sec(t, s):
        off = sum(SECTORS[:t-1])*256 + s*256
        return data[off:off+256]
    t, s = 18, 1
    out = []
    while t:
        d = sec(t, s)
        for i in range(0, 256, 32):
            e = d[i:i+32]
            if e[2] == 0: continue
            name = bytes(b & 0x7f for b in e[5:21]).decode('ascii','replace').rstrip('\xa0 ')
            ft, fs = e[3], e[4]
            f = b''
            while ft:
                dd = sec(ft, fs)
                if dd[0] == 0:
                    f += dd[2:2+dd[1]-1]; break
                f += dd[2:]; ft, fs = dd[0], dd[1]
            out.append((name, f))
        t, s = d[0], d[1]
    return out

def ddb_valid(ram):
    if ram[0x3880] not in (1,2) or (ram[0x3881]>>4) != 2 or ram[0x3882] != 0x5F:
        return False
    voc = ram[0x3880+0x16] | ram[0x3880+0x17]<<8
    if voc < 0x3880 or voc > 0xD000: return False
    off = voc; words = 0; spaces = False
    while off + 7 < 0xD000:
        if ram[off] == 0: break
        if ram[off+6] > 6: return False
        if ram[off+4] == 0xDF: spaces = True
        off += 7; words += 1
    # graphics footer: start pointer at 0xCBEF
    start = ram[0xCBEF] | ram[0xCBF0]<<8
    ending = ram[0xCBFB] | ram[0xCBFC]<<8
    return words >= 16 and spaces and ending == 0xFFFF and 0x3880 < start < 0xCBEF

def run_prg(f, name):
    ram = bytearray(65536)
    ram[1] = 0x37
    load = f[0] | f[1]<<8
    payload = f[2:]
    end = min(len(payload), 65536-load)
    ram[load:load+end] = payload[:end]
    # find SYS in BASIC stub
    stub = bytes(ram[0x801:0x900])
    m = re.search(rb'\x9e[ (]*(\d+)', stub)
    if not m:
        return f'{name}: no SYS in BASIC stub'
    entry = int(m.group(1))
    cpu = CPU(ram)
    cpu.pc = entry
    budget = 200_000_000
    check = 0
    while budget > 0:
        cpu.step()
        budget -= 1
        if cpu.halted:
            return f'{name}: halted ({cpu.halted}) after {200_000_000-budget} steps, ddb_valid={ddb_valid(ram)}'
        check += 1
        if check >= 65536:
            check = 0
            if ddb_valid(ram):
                return f'{name}: DDB VALID at $3880 after ~{200_000_000-budget} steps (pc={cpu.pc:04X})'
    return f'{name}: budget exhausted, ddb_valid={ddb_valid(ram)} pc={cpu.pc:04X}'

if __name__ == '__main__':
    import glob
    for path in sorted(glob.glob('/home/jlcebrian/Src/ADP/tests/games/*/c64/*.[dD]64')):
        for name, f in files_of(open(path,'rb').read()):
            if len(f) < 20000: continue
            print(run_prg(f, name), flush=True)

