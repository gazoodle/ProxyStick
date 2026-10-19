"""The one-off RAM snapshot's layout bounds and zero-tail reads, without an ESPHome build."""

import subprocess
from pathlib import Path


def test_snapshot_layout_and_reads_on_host(tmp_path):
    source = Path(__file__).with_name("psram_snapshot_check.cpp")
    executable = tmp_path / "psram-check"
    subprocess.run(["c++", "-std=c++17", str(source), "-o", str(executable)], check=True, capture_output=True)
    subprocess.run([str(executable)], check=True, capture_output=True)
