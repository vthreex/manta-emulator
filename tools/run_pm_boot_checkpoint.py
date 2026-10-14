"""Reproduce the explicitly exploratory original five-CPU cold-boot checkpoint."""
import argparse, json, subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware', required=True)
p.add_argument('--emulator', default=str(Path(__file__).resolve().parents[1] / 'bin/manta.exe'))
p.add_argument('--summary', default='pm51-checkpoint.json')
p.add_argument('--trace', help='Optional full trace; the current checkpoint produces about 10 GB')
p.add_argument('--coverage')
p.add_argument('--pm51-history', help='Bounded original PM instruction and bus observations in JSON Lines')
p.add_argument('--exploratory-pm-power-good', action='store_true', help='Explicit related-source aggregate power-good hypothesis; not verified MSD309 detector wiring')
p.add_argument('--candidate-pm51-uart', action='store_true', help='Candidate R8051 UART configuration; no unsupported completion')
p.add_argument('--explore-pm-nonresume-branch', action='store_true', help='Diagnostic single symbolic non-resume predicate; no valid PM byte or cold-boot claim')
p.add_argument('--window', action='store_true', help='Use the existing native window; no authentic frame is demonstrated')
p.add_argument('--max-instructions', type=int, default=2000000000)
p.add_argument('--max-or1k-instructions', type=int, default=400000000)
p.add_argument('--exploratory-cpi', type=int, choices=(1,2,4,8,16), default=1,
               help='UNVERIFIED average instruction cost; shared Count/peripheral cycles; default unchanged')
p.add_argument('--copro-probes',action='store_true',help='Inspect actual original multimedia loader output; no ready response')
p.add_argument('--print-command', action='store_true')
a = p.parse_args()
if a.max_instructions < 1 or a.max_or1k_instructions < 1:
    p.error('instruction limit must be positive')
flags = ['--firmware', a.firmware, '--symbolic-init', '--candidate-miu', '--candidate-bdma',
 '--candidate-uart', '--gpio-i2c', '--defer-diagnostic-reads', '--virtual-rtc', '--cp0-timer',
 '--candidate-utmi', '--candidate-spi', '--exploratory-spi-input', 'pull-up', '--candidate-chip-id',
 '--exploratory-sc-bank', '0', '--candidate-gamma', '--exploratory-gamma-enable', '0',
 '--exploratory-display-clock', '148500000', '--exploratory-subwindow-enable', '0',
 '--exploratory-dlc-pending', '0', '--exploratory-dlc-timeout', '--exploratory-comb-bank', '0',
 '--exploratory-comb-paired-restore', '--exploratory-gop-bank', '0', '--candidate-semaphore',
 '--exploratory-semaphore-idle', '--candidate-ge', '--exploratory-ge-reset', '--candidate-gop-palette',
 '--candidate-tsp', '--exploratory-tsp-reset-low', '--exploratory-tsp-cpu', '--defer-platform-identity',
 '--exploratory-pm-ac-on', '--exploratory-sar-fullscale', '--exploratory-ir-dma-reset',
 '--exploratory-gop-reset-windows', '--exploratory-scart-grounded', '--exploratory-scmi-format-byte', '0',
 '--exploratory-analog-disconnected', '--exploratory-pq-sram-idle', '--exploratory-pq-disabled-ports',
 '--graphics-probes', '--or1k-trace-first-visits', '--or1k-dispatch-divider', '4',
 '--max-or1k-instructions', str(a.max_or1k_instructions), '--exploratory-fetch-revision-byte', '2',
 '--candidate-vd-mcu-program', '--candidate-avd-mcu-program', '--exploratory-vd-mcu-startup',
 '--osd-probes', '--defer-vd-sfr8e-write', '--defer-vd-reset-key-writes',
 '--exploratory-vd-suspended-boundary', '--exploratory-ci-detect-high', '--exploratory-vd-shared-run',
 '--candidate-audio-idma', '--exploratory-avd-mcu-startup', '--exploratory-avd-suspended-boundary',
 '--exploratory-audio-no-ack', '--exploratory-pm51-spi-startup', '--pm51-dispatch-divider', '32',
 '--max-instructions', str(a.max_instructions), '--summary', a.summary]
if not a.window:
    flags.append('--headless')
if a.trace:
    flags += ['--record-trace', a.trace]
if a.coverage:
    flags += ['--coverage', a.coverage]
if a.pm51_history:
    flags += ['--pm51-history', a.pm51_history]
if a.exploratory_pm_power_good:
    flags += ['--exploratory-pm-power-good']
if a.candidate_pm51_uart:
    flags += ['--candidate-pm51-uart']
if a.explore_pm_nonresume_branch:
    flags += ['--explore-pm-nonresume-branch']
if a.exploratory_cpi!=1:
    flags += ['--exploratory-cpi',str(a.exploratory_cpi)]
if a.copro_probes:
    flags += ['--copro-probes']
command = [a.emulator] + flags
if a.print_command:
    print(json.dumps(command, indent=2))
else:
    print('EXPLORATORY hardware/reset/clock/input hypotheses; original firmware executes; essential unknown inputs stay invalid.', flush=True)
    raise SystemExit(subprocess.call(command))
