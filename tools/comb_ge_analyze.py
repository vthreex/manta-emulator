"""Disassemble the unchanged dump's COMB -> GOP table -> GE startup dependency.
Requires Capstone. Reference vendor sources are not bundled or substituted.
"""
import argparse, hashlib, json, struct
from pathlib import Path
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
from firmware_extract import validate, inflate_at, KNOWN

BASE = 0x803B3980 # original loader's 0x3800 prefix ends here; not the vector base
REGIONS = {
 'pq_table_loader': (0x803C6418, 0x803C6B80),
 'pq_gop_table': (0x803C1DF4, 0x803C1FD8),
 'display_delays_and_return': (0x803E0DF0, 0x803E0E84),
 'application_display_startup': (0x8040440C, 0x80404504),
 'hardware_semaphore_hal': (0x8053B924, 0x8053BA24),
 'ge_context_save_restore': (0x8052681C, 0x805269D4),
 'ge_command_queue_wait': (0x80526240, 0x80526540),
 'gop_initializer': (0x80539CE4, 0x80539FC0),
 'ge_wait_idle': (0x805265D0, 0x8052681C),
 'ge_init_context_and_registers': (0x805269D4, 0x805271BC),
 'gop_api_initialization': (0x805333AC, 0x80533890),
 'gop_strided_bank_accessors': (0x8053A198, 0x8053A548),
 'tsp_startup': (0x805D5B28, 0x805D5F98),
 'tsp_filter_route': (0x805D6B68, 0x805D6D1C),
 'tsp_reset_indirect_enable_and_bank': (0x805D76BC, 0x805D7758),
 'tsp_interrupt_disable': (0x805D7A04, 0x805D7A48),
 'tsp_command_queue_count': (0x805D7B30, 0x805D7B48),
}
SIGNATURES = {
 'comb_duplicate_restore': (0x803C69C4, [0x00408821,0x00141827,0x02B42024,0x00431024,
   0x00441025,0x7C021620,0x3043FFFF,0x00031A00,0x00431025,0x3045FFFF,
   0x02002021,0x0C17E204,0x3406FFFF]),
 'gop_selector_mask': (0x803C1F60, [0x3042000F,0x3C040010,0x02202821,0x34841FFE,0x1051FFD8,0x2406000F]),
 'ge_context_reader': (0x805268DC, [0x94640000,0x8FBF001C,0x8FB10018,0x00C42024,
   0x00A42025,0x3082FFFF,0x8FB00014,0x03E00008,0x27BD0020]),
 'tsp_cmd_count': (0x805D7B30, [0x3C02806E,0x8C436E84,0x946200FC,
   0x946300F8,0x03E00008,0x3042001F]),
 'tsp_count_poll_caller': (0x805D5E48, [0x0C175ECC,0,0x1440FFFD,0]),
}

def analyze(firmware):
 b = Path(firmware).read_bytes(); validate(b)
 end, image = inflate_at(b[:0x800000], KNOWN[0][0])
 assert end == KNOWN[0][1] and hashlib.sha256(image).hexdigest() == KNOWN[0][3]
 cs = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
 verified = {}
 for name, (pc, words) in SIGNATURES.items():
  actual = struct.unpack_from('<' + 'I'*len(words), image, pc-BASE)
  assert actual == tuple(words), (name, 'original instruction signature changed')
  verified[name] = {'pc':hex(pc),'words':[hex(w) for w in actual]}
 functions = {}
 for name, (start, stop) in REGIONS.items():
  calls = [hex(BASE+i) for i in range(0,0x80613000-BASE,4)
    if (w:=struct.unpack_from('<I',image,i)[0])>>26 == 3 and
      (((BASE+i)&0xf0000000)|((w&0x3ffffff)<<2)) == start]
  functions[name] = {'start':hex(start),'end_exclusive':hex(stop),
    'aligned_jal_candidates_not_reachability_proof':calls,
    'instructions':[{'pc':hex(i.address),'word':hex(int.from_bytes(i.bytes,'little')),
      'asm':i.mnemonic+' '+i.op_str} for i in cs.disasm(image[start-BASE:stop-BASE],start)]}
 return {'original_sha256':hashlib.sha256(b).hexdigest(), 'runtime_sha256':KNOWN[0][3],
   'isa':'MIPS32 little endian', 'signatures':verified,'regions':functions,
   'register_semantics_limit':'COMB reg8F analog meaning/reset unknown. GE reset/clock/latency remain explicit experiments. TSP BF202AFC bits0..4 are command count by matching HAL body; producer/reset/cmd execution unimplemented. No authentic framebuffer/pixels proved.'}

if __name__ == '__main__':
 p=argparse.ArgumentParser();p.add_argument('--firmware',required=True);p.add_argument('--output',required=True)
 a=p.parse_args();result=analyze(a.firmware)
 Path(a.output).write_text(json.dumps(result,indent=2)+'\n')
 print(f'PASS: immutable firmware hashes, {len(SIGNATURES)} exact instruction signatures, {len(REGIONS)} disassembled dependency regions')
