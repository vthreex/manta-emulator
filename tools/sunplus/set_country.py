"""Make a copy of a Manta LED4202 flash dump with a different saved COUNTRY setting (user data, not firmware code).
The system-settings block (0x1d bytes, saved at flash 0x3e6da2 in the Manta dump; byte 2 = country index) selects the OSD language:
firmware 0x8126cdd4 maps country index -> language code -> language id (0x81244030). Index 11 = Russia -> Russian; index 1 (and any index >= 29) -> the English fallback.
usage: python set_country.py IN.bin OUT.bin COUNTRY_INDEX"""
import sys
data = bytearray(open(sys.argv[1], 'rb').read())
sig = bytes.fromhex('00000b0d0d0c05')
i = data.find(sig)
if i < 0: sys.exit('settings block signature not found (unmodified Manta dump expected)')
data[i + 2] = int(sys.argv[3], 0)
open(sys.argv[2], 'wb').write(data); print('country byte at flash offset', hex(i + 2), '->', sys.argv[3])
