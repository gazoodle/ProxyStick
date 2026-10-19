"""Compare captures only across intact timing pairs; retain host protocol differences."""

from proxystick.serial_compare import analyse, compare
from tests.test_serial_watch import line


def test_capture_comparison(tmp_path):
    good = tmp_path / "reference.log"
    bad = tmp_path / "problem.log"
    good.write_text(
        "\n".join(
            [
                line(us=1000000),
                "T 00000001 00000002 00000000 00000000000f424a SCSI Data",
                line(3, "MSC_DATA", us=1004000, value=3000, callbacks=8192, flags=0x288100),
                line(4, "MSC_CSW", us=1005000, value=0, flags=0),
            ]
        )
        + "\n"
    )
    bad.write_text(line() + "\n" + line(3, "MSC_CSW", us=1005000, value=0, flags=0) + "\n")
    first, second = analyse(good), analyse(bad)
    assert first.records == 4 and not first.issues
    assert first.max_buffer_us == 3000 and first.max_queue_gap_us == 1000
    assert first.max_command_us == 5000
    assert second.issues and second.max_command_us == 0
    output = "\n".join(compare(first, second))
    assert "sequence gap" in output and "max buffer=3.000 ms" in output


def test_collector_error_and_missing_trace_are_visible(tmp_path):
    path = tmp_path / "empty.log"
    path.write_text("MONITOR CAPTURE INCOMPLETE: overrun\n")
    result = analyse(path)
    assert result.issues["no decoded records"] == 1
    assert "Per-buffer trace missing" in "\n".join(compare(result, result))
