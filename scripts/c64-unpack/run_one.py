import re, sys
from cpu6502 import CPU
from unpack64 import files_of

path, want, mode = sys.argv[1], sys.argv[2], sys.argv[3]
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
    ending = ram[0xCBFB] | ram[0xCBFC]<<8
    start = ram[0xCBEF] | ram[0xCBF0]<<8
    return words >= 16 and spaces and ending == 0xFFFF and 0x3880 < start < 0xCBEF

for name, f in files_of(open(path,'rb').read()):
    if name != want: continue
    ram = bytearray(65536)
    ram[1] = 0x37
    if 'novec' not in mode:
        for v in range(0x0300, 0x0334, 2):
            ram[v] = 0x31; ram[v+1] = 0xEA
    load = f[0] | f[1]<<8
    ram[load:load+len(f)-2] = f[2:2+65536-load]
    m = re.search(rb'\x9e[ (]*(\d+)', bytes(ram[0x801:0x900]))
    cpu = CPU(ram); cpu.pc = int(m.group(1))
    for step in range(150_000_000):
        cpu.step()
        if step % 5000 == 4999:
            if ddb_valid(ram):
                print(f'{want}: DDB OK step {step}'); open(f'{sys.argv[4]}.ram','wb').write(ram); break
            vec = ram[0x314] | ram[0x315]<<8
            if vec > 0x200 and (mode == 'always' or True):
                cpu.irq()
        if cpu.halted:
            print(f'{want}: halted {cpu.halted} step {step} ending={ram[0xCBFB]|ram[0xCBFC]<<8:04X}')
            open(f'{sys.argv[4]}-halt.ram','wb').write(ram)
            break
    else:
        print(f'{want}: budget out pc={cpu.pc:04X}')
