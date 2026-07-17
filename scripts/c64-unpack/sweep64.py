import re, glob
from cpu6502 import CPU
from unpack64 import files_of

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
    start = ram[0xCBEF] | ram[0xCBF0]<<8
    ending = ram[0xCBFB] | ram[0xCBFC]<<8
    return words >= 16 and spaces and ending == 0xFFFF and 0x3880 < start < 0xCBEF

def run(name, f, tag):
    ram = bytearray(65536)
    ram[1] = 0x37
    # Standard vector table: everything points at a ROM RTS/RTI stub so
    # jumps through uninitialized-looking vectors survive
    for v in range(0x0300, 0x0334, 2):
        ram[v] = 0x31; ram[v+1] = 0xEA
    load = f[0] | f[1]<<8
    ram[load:load+len(f)-2] = f[2:2+65536-load]
    m = re.search(rb'\x9e[ (]*(\d+)', bytes(ram[0x801:0x900]))
    if not m:
        print(f'{name}: no SYS', flush=True); return
    cpu = CPU(ram); cpu.pc = int(m.group(1))
    for step in range(120_000_000):
        cpu.step()
        if step % 5000 == 4999:
            if ddb_valid(ram):
                print(f'{name}: DDB OK step {step}', flush=True)
                open(f'{tag}.ram','wb').write(ram)
                return
            sig = ram[0x3880] in (1,2) and (ram[0x3881]>>4) == 2 and ram[0x3882] == 0x5F
            vec = ram[0x314] | ram[0x315]<<8
            if not sig and vec > 0x200:
                cpu.irq()
        if cpu.halted:
            sig = ram[0x3880] in (1,2) and (ram[0x3881]>>4) == 2 and ram[0x3882] == 0x5F
            end = ram[0xCBFB] | ram[0xCBFC]<<8
            print(f'{name}: halted {cpu.halted} step {step} sig@3880={sig} ending={end:04X}', flush=True)
            open(f'{tag}-halt.ram','wb').write(ram)
            return
        if step % 65536 == 0 and ddb_valid(ram):
            print(f'{name}: DDB OK step {step}', flush=True)
            open(f'{tag}.ram','wb').write(ram)
            return
    sig = ram[0x3880] in (1,2) and (ram[0x3881]>>4) == 2 and ram[0x3882] == 0x5F
    print(f'{name}: budget out pc={cpu.pc:04X} sig@3880={sig}', flush=True)
    open(f'{tag}-out.ram','wb').write(ram)

jobs = []
for path in sorted(glob.glob('/home/jlcebrian/Src/ADP/tests/games/*/c64/*.[dD]64')):
    for name, f in files_of(open(path,'rb').read()):
        if len(f) < 20000: continue
        tag = re.sub(r'\W+', '', name).lower()
        jobs.append((name, f, tag))

if __name__ == '__main__':
    import multiprocessing as mp
    with mp.Pool(min(9, mp.cpu_count())) as pool:
        pool.starmap(run, jobs)
