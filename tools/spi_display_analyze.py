"""Disassemble the recovered stock SPI/display paths; never export a guest binary."""
import argparse, hashlib, json, struct
from pathlib import Path
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
from firmware_extract import validate, inflate_at, KNOWN
BASE = 0x803b0180
RANGES = {
 'CF_probe': (0x8053c574,0x8053c784),
 'JEDEC_probe': (0x8053c784,0x8053c920),
 'flash_descriptor_selection': (0x8053db88,0x8053dd80),
 'GOP_initializer': (0x80539ce4,0x80539fc0),
 'GOP_banked_helpers': (0x8053a140,0x8053a4b8),
 'SC_timing': (0x805e5ab8,0x805e6220),
 'gamma_RAM_transfer': (0x805e55d4,0x805e56ec),
 'clock_snapshot': (0x805fd3d4,0x805fd5a0),
 'vertical_edge_wait': (0x805f24a4,0x805f253c),
 'banked_status_reader': (0x805fbc54,0x805fbcb0),
 'input_sync_reader': (0x805fbbf8,0x805fbc54),
 'scaler_initializer': (0x805f8300,0x805f87e0),
 'panel_timing_getter': (0x805eee4c,0x805eef04),
 'extra_request_subwindow_dependency': (0x805fe024,0x805fe150),
 'main_prescaling_coefficients': (0x805fe150,0x805fe238),
 'DLC_curve_initializer': (0x805e3874,0x805e3e70),
 'application_display_init': (0x803e0b94,0x803e0e7c),
 'GOP_initializer_caller': (0x805333ac,0x80533920),
 'application_GOP_call': (0x80404460,0x80404520),
 'DLC_chroma_gain_getter': (0x805e0cec,0x805e0d28),
 'COMB_PQ_table_loader': (0x803c68a8,0x803c6ba0),
}
def analyze(path):
 flash = validate(Path(path).read_bytes()); end, runtime = inflate_at(flash,KNOWN[0][0])
 assert end==KNOWN[0][1] and hashlib.sha256(runtime).hexdigest()==KNOWN[0][3]
 image=flash[0x20080:0x23880]+runtime
 cs=Cs(CS_ARCH_MIPS,CS_MODE_MIPS32|CS_MODE_LITTLE_ENDIAN)
 report={'original_sha256':hashlib.sha256(Path(path).read_bytes()).hexdigest(),
  'runtime_sha256':KNOWN[0][3],'image_base':hex(BASE),'functions':{},'direct_callers':[]}
 for name,(start,end) in RANGES.items():
  report['functions'][name]=[{'pc':hex(i.address),'word':hex(struct.unpack('<I',i.bytes)[0]),
    'instruction':i.mnemonic+' '+i.op_str} for i in cs.disasm(image[start-BASE:end-BASE],start)]
 targets={s for s,e in RANGES.values()}
 for offset in range(0,0x80613000-BASE,4):
  w=struct.unpack_from('<I',image,offset)[0]; pc=BASE+offset
  if w>>26==3:
   target=(pc&0xf0000000)|((w&0x3ffffff)<<2)
   if target in targets:report['direct_callers'].append({'pc':hex(pc),'target':hex(target)})
 return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--firmware',required=True);p.add_argument('--report',required=True)
 a=p.parse_args();Path(a.report).write_text(json.dumps(analyze(a.firmware),indent=2)+'\n')
