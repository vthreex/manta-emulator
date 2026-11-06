"""Pin DTV findings to unmodified firmware; static evidence, not channel reception."""
import argparse
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path
from firmware_resources import recover

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware', required=True, type=Path)
p.add_argument('--report', required=True, type=Path)
a = p.parse_args()
flash = a.firmware.read_bytes()
manifest, resources = recover(flash)
runtime = zlib.decompress(flash[0x23880:0x16124e], -15)
assert hashlib.sha256(runtime).hexdigest() == '149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
copro = resources[0x61]
assert hashlib.sha256(copro).hexdigest() == 'bc00e532ab47b35e2f3a8be0b59a40b7ae80111dd366b6ceb741cdb74ca95ae1'
dtv = resources[0x51]
assert len(dtv) == 946752
assert hashlib.sha256(dtv).hexdigest() == '6f64d6edc9306c7aae88483efdd25edd4c7d2b326ccba39cc9961da71f4298ee'
assert struct.unpack_from('<9I', dtv, 0x659c8) == (
    0x18e0a020, 0xa8e75700, 0x94e70000, 0xb8c70010,
    0x18a00001, 0xa8a50100, 0xe0e62800, 0x84e70000, 0x18a0fa54)
header = (Path(__file__).resolve().parents[1] / 'src/peripherals/copro.hpp').read_text()
signature = header.split('static constexpr uint32_t words[]={', 1)[1].split('};', 1)[0]
words = [int(x, 16) for x in re.findall(r'0x[0-9a-f]+', signature)]
assert len(words) == 16
assert copro[0xc8454:0xc8494] == struct.pack('<16I', *words)
mcu = runtime[0x806a4a04-0x803b3980:0x806aa00e-0x803b3980]
assert hashlib.sha256(mcu).hexdigest() == '2bdb82e863899ea863f63bb4596235615c8f5aa5f77bdc465bec2d465bde22a5'
assert mcu[0x3ce7:0x3cec] == bytes.fromhex('90 23 e0 e4 f0')
assert mcu[0x445f:0x4465] == bytes.fromhex('90 23 e0 74 01 f0')
def word(address):
    return struct.unpack_from('<I', runtime, address-0x803b3980)[0]
assert word(0x804f506c) == 0x240423e0  # original FEC status request
assert word(0x804f5088) == 0x2402000b  # original lock-state comparison
assert word(0x804f50d8) == 0x24042070  # original TPS status request
assert word(0x804f2ff8) == 0x2412002e  # initialize 46 DSP parameter bytes
assert word(0x804f3008) == 0x0c13cbc5  # original WriteDspReg at804F2F14
assert word(0x804f2f60) == 0x24030004  # parameter-write command4
assert word(0x804f2fa4) == 0x10860008  # bounded polling; no sleep in this loop
assert word(0x804f4b5c) == 0x0c13d225  # configuration calls reset helper804F4894 again
assert word(0x804f57f4) == 0x0c13d225  # initial code loader calls the same reset helper
assert word(0x804f48c0) == 0xa0620000  # helper asserts CPU reset
assert word(0x804f48d8) == 0xa0400000  # helper releases CPU reset
assert mcu[0x4af5:0x4b07] == bytes.fromhex('12 54 44 12 54 5c 50 0a 90 2f 03 e0 fd 12 54 69 ed f0')
assert mcu[0x5469:0x5474] == bytes.fromhex('74 00 2f f5 82 e4 34 80 f5 83 22')
tsp = flash[0x1e7838:0x1ebc40]
assert hashlib.sha256(tsp).hexdigest() == '69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a'
for pc, expected in {0xe10:0x18e00021, 0xe14:0xa8c70200, 0xe20:0x84e60014,
                     0xe24:0xa4e70100, 0xe38:0x8626001c, 0xe78:0xb8f10007,
                     0xe80:0xa8c61000, 0xd10:0x9ce000bc, 0x418:0xd4063824}.items():
    assert struct.unpack_from('<I', tsp, pc)[0] == expected
# The original MIPS register save/restore loops prove the same logical bank
# layout as the related T3 header, independently of any proposed packet model.
assert word(0x805d878c) == 0x24120030  # 48 PID words
assert word(0x805d8794) == 0x001020c0  # PID index * 8
assert word(0x805d87b4) == 0x34522000  # section bank 0x422000
assert word(0x805d87f4) == 0x26520100  # section stride 0x100 (CP byte stride 0x80)
assert hashlib.sha256(runtime[0x804f58c8-0x803b3980:0x804f5a3c-0x803b3980]).hexdigest() == '30a8f64bbd9857bfd4c9820d1e062cec8999d141db6dbf37a4537f8e72e22edd'
assert struct.unpack_from('<dd', runtime, 0x80625748-0x803b3980) == (36.167, 6.0)
assert int((682+36.167)*6) == 4309  # observed native CH47 GPIO frame10 D5 90 14 C0
a.report.write_text(json.dumps(dict(result='PASS', scope='static unmodified-firmware evidence only',
    original_flash_sha256=hashlib.sha256(flash).hexdigest(),
    copro_identity_cache_signature_verified=True,
    dtv_resource_sha256=hashlib.sha256(dtv).hexdigest(),
    boot_info_base_read_is_original=True,
    boot_info_base_hardware_reset_known=False,
    fec_state_is_written_by_original_mcu=True,
    fec_lock_host_override_permitted=False,
    parameter_initializer='original MIPS804F2FD0 writes46 bytes through command4',
    parameter_mapping='original MCU5469 maps DSP index into XDATA8000+index',
    parameter_polling_is_bounded=True,
    original_configuration_resets_mcu_after_parameter_initialization=True,
    tsp_original_packet_processing_instructions_verified=True,
    tsp_packet_ready_observation='original PC0xe20 reads 0x210214 bit8',
    tsp_section_assembly_should_run_in_original_firmware=True,
    digital_tuner_function='original MIPS804F58C8..804F5A38',
    digital_tuner_if_mhz=36.167,
    digital_tuner_steps_per_mhz=6,
    observed_ch47_divider=4309,
    dtv_channel_reception_demonstrated=False), indent=2) + '\n')
print('PASS: exact original identity-cache instructions and native MCU ownership of FEC state')
