"""Original PM bytes execute; unresolved PM transactions must never become replies."""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path

p = argparse.ArgumentParser()
for key in ('firmware', 'probe', 'report'):
    p.add_argument('--' + key, required=True)
a = p.parse_args()
probe = str(Path(a.probe).resolve())
firmware = Path(a.firmware)
stock = firmware.read_bytes()
stock_sha = hashlib.sha256(stock).hexdigest()
assert len(stock) == 0x800010 and stock_sha == 'd60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
pm = stock[0x10000:0x20000]
pm_sha = hashlib.sha256(pm).hexdigest()
assert pm_sha == '5f9d541120948f5067d162048c17f30fc704f871f9fdfbcc161181567f48fd3c'

with tempfile.TemporaryDirectory(prefix='manta-original-pm51-') as directory:
    image = Path(directory) / 'original-pm.bin'
    image.write_bytes(pm)

    def run(*flags):
        result = subprocess.run([probe, str(image), *flags], capture_output=True, text=True)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        assert 'PM_MAIN_MACHINE_ATTACHED false' in result.stdout
        assert 'no PM reply, marker, interrupt or ready signal supplied' in result.stdout
        return result.stdout

    bounded = run()
    assert 'PM_ORIGINAL_INSTRUCTIONS 3 PC 0x60dd' in bounded
    assert 'PM_XDATA write 0x1018 value 0x2' in bounded
    assert 'PM_PRIVATE_RAM_WRITES 0 READS 0 DEFERRED_WRITES 0' in bounded
    exploratory = run('--exploratory-private-ram')
    assert 'PM_ORIGINAL_INSTRUCTIONS 3406 PC 0x6dc7' in exploratory
    assert 'PM_XDATA read 0xec3' in exploratory
    assert 'PM_PRIVATE_RAM_WRITES 1032 READS 7 DEFERRED_WRITES 1' in exploratory
    assert 'PM_INSTRUCTION 0x934d' not in exploratory, 'unreached marker writer must not be presented as executed'
    assert run('--exploratory-private-ram') == exploratory, 'isolated PM instruction trace must be deterministic'
    candidate = run('--candidate-spi-private-ram')
    assert 'PM_ORIGINAL_INSTRUCTIONS 3406 PC 0x6dc7' in candidate
    assert 'PM_SPI_CONTROL_WRITES 1 ORIGINAL_PROGRAM_UNCHANGED true' in candidate
    assert 'PM_PRIVATE_RAM_WRITES 1032 READS 7 DEFERRED_WRITES 0' in candidate
    assert 'PM_INSTRUCTION 0x934d' not in candidate
    assert run('--candidate-spi-private-ram') == candidate
    traces = [next(line.split()[-1] for line in output.splitlines() if line.startswith('PM_ALL_INSTRUCTION_BUS_TRACE_SHA256')) for output in (bounded, exploratory, candidate)]
    assert all(len(value) == 64 for value in traces)
    image.write_bytes(pm[:-1])
    truncated = subprocess.run([probe, str(image)], capture_output=True, text=True)
    assert truncated.returncode == 1 and 'size/hash mismatch' in truncated.stderr
    damaged = bytearray(pm)
    damaged[0] ^= 1
    image.write_bytes(damaged)
    modified = subprocess.run([probe, str(image)], capture_output=True, text=True)
    assert modified.returncode == 1 and 'size/hash mismatch' in modified.stderr

assert hashlib.sha256(firmware.read_bytes()).hexdigest() == stock_sha
report = {
    'result': 'PASS', 'scope': 'isolated original PM51 standard-reset experiment; not PM board boot',
    'stock_sha256': stock_sha, 'flash_start': '0x10000', 'flash_end': '0x20000',
    'program_sha256': pm_sha, 'program_bytes': len(pm), 'architecture': '8051-family original instructions',
    'bounded_instructions': 3, 'bounded_PC': '0x60DD', 'bounded_XDATA': '0x1018 write 02',
    'exploratory_instructions': 3406, 'exploratory_PC': '0x6DC7', 'exploratory_XDATA': '0x0EC3 read',
    'original_RAM_writes': 1032, 'original_RAM_reads': 7,
    'deferred_unknown_writes': 1, 'private_RAM_range_hypothesis': '0x4000..0x43ff',
    'candidate_SPI_control_writes': 1, 'candidate_SPI_deferred_writes': 0,
    'SPI_control_interpretation': 'related Amber3/Kaiser ROM_CODE_CTRL=1018 / SPI_ENABLE=02; corroborated original reset write, exact chip mapping remains candidate',
    'standard_8051_reset_hypothesis': True, 'real_SPI_boot_mapping_validated': False,
    'PM_marker_writer_executed': False, 'main_machine_attached': False,
    'hardware_reply_produced': False, 'deterministic_isolated_replay': 'PASS',
    'modified_and_truncated_input_rejected': True,
    'all_instruction_bus_trace_sha256': dict(zip(('bounded','exploratory','candidate_SPI'), traces)),
    'probe_sha256': hashlib.sha256(Path(probe).read_bytes()).hexdigest(),
    'bounded_stdout': bounded, 'exploratory_stdout': exploratory, 'candidate_SPI_stdout': candidate,
}
Path(a.report).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('PASS: original PM51 execution, isolated replay and input rejection; PM ready/activity remain unknown')
