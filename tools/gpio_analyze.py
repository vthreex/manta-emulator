"""Reproduce stock GPIO/I2C descriptor and instruction evidence without exporting firmware."""
import argparse, hashlib, json, struct
from pathlib import Path
from firmware_extract import validate, inflate_at, KNOWN

def analyze(firmware):
    flash = validate(Path(firmware).read_bytes())
    _, runtime = inflate_at(flash, KNOWN[0][0])
    if hashlib.sha256(runtime).hexdigest() != KNOWN[0][3]:
        raise ValueError('runtime checksum mismatch')
    image = flash[0x20080:0x23880] + runtime
    base = 0x803b0180
    table = 0x80631f88
    def physical(index): return 0x1f000000 + 2*index - (index & 1)
    rows = []
    for gpio in (65,66,69,70):
        values = struct.unpack_from('<6I', image, table-base+24*gpio)
        rows.append(dict(gpio=gpio, descriptor_address=hex(table+24*gpio),
                         oen=dict(logical=hex(values[0]), physical=hex(physical(values[0])), mask=hex(values[1])),
                         output=dict(logical=hex(values[2]), physical=hex(physical(values[2])), mask=hex(values[3])),
                         input=dict(logical=hex(values[4]), physical=hex(physical(values[4])), mask=hex(values[5]))))
    expected = [(0x1f203d05,2),(0x1f203d05,4),(0x1f203d11,4),(0x1f203d11,8)]
    assert [(int(r['input']['physical'],16),int(r['input']['mask'],16)) for r in rows] == expected
    assert struct.unpack_from('<I',image,0x8053bde4-base)[0] == 0x90650000 # LBU a1,0(v1)
    tuples=struct.unpack_from('<15H',image,0x806563f8-base)
    assert tuples == (65,66,40,65,66,40,69,70,40,65,66,40,65,66,40)
    from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
    cs=Cs(CS_ARCH_MIPS,CS_MODE_MIPS32|CS_MODE_LITTLE_ENDIAN)
    regions=[]
    for start,end in [(0x8053bd98,0x8053bdfc),(0x803bcf10,0x803bd160)]:
        regions.append(dict(start=hex(start),instructions=[dict(pc=hex(i.address),mnemonic=i.mnemonic,operands=i.op_str)
                       for i in cs.disasm(image[start-base:end-base],start)]))
    return dict(original_sha256=hashlib.sha256(Path(firmware).read_bytes()).hexdigest(),
                runtime_sha256=hashlib.sha256(runtime).hexdigest(),
                descriptor_table=hex(table),descriptor_stride=24,rows=rows,
                pair_table=hex(0x806563f8),pairs=[dict(bus_index=i,scl=tuples[3*i],sda=tuples[3*i+1],delay=tuples[3*i+2]) for i in range(5)],
                stalled_read=hex(0x8053bde4),caller_branch=hex(0x803bcfb4),
                interpretation='released-line I2C initialization; nine-clock bus recovery when SDA or SCL stays low',
                physical_pad_mapping='unverified: supplied reference schematic contains no GPIO enum-to-register mapping',
                disassembly=regions)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware',required=True);p.add_argument('--report',required=True)
    p.add_argument('--schematic');a=p.parse_args()
    report=analyze(a.firmware)
    if a.schematic:
        report['reference_schematic']=dict(file=Path(a.schematic).name,sha256=hashlib.sha256(Path(a.schematic).read_bytes()).hexdigest(),
            pages=[14,16],board='MSD309 V7.0-A; not verified as Manta MSD309-T2B / 471-0101-D3202G')
    Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: stock LBU, 24-byte GPIO descriptors and five SCL/SDA/delay tuples verified (two physical pairs)')
