"""Builds a stand-alone MantaControl.exe (PyInstaller, one folder) into outputs/manta-emulator/dist/MantaControl.
The emulator executables, firmware, snapshots and policy files are found relative to the repository (see emulator_session.find_repo_root):
keep the folder inside the repository, or set the emulator path under Advanced."""
import subprocess, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
sep = ';'
cmd = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--windowed', '--name', 'MantaControl', '--distpath', str(root / 'dist'),
       '--workpath', str(root.parents[1] / 'work' / 'pyi-build'), '--specpath', str(root.parents[1] / 'work' / 'pyi-build'),
       '--add-data', str(root / 'configs') + sep + 'configs', '--paths', str(root / 'frontend'),
       '--exclude-module', 'PySide6.QtQml', '--exclude-module', 'PySide6.QtQuick', '--exclude-module', 'PySide6.QtNetwork', '--exclude-module', 'PySide6.QtPdf',
       str(root / 'frontend' / 'manta_control.py')]
subprocess.run(cmd, check=True)
print('built', root / 'dist' / 'MantaControl' / 'MantaControl.exe')
