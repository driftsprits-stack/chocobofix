"""Exercise fresh solves on a renamed network, never the committed answers.

Run from the repository root: python3 tests/hidden_input_smoke.py build/trackaccess
"""
import pathlib
import subprocess
import sys
import tempfile

solver = str(pathlib.Path(sys.argv[1]).resolve())
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="chocobofix-hidden-") as tmp:
    work = pathlib.Path(tmp)
    data = work / "input"
    data.mkdir()
    for source in (root / "data/upstream/PS1/01_data").glob("*.csv"):
        text = source.read_text()
        for old, new in (("ALP", "RED"), ("BET", "BLUE"), ("H01", "J01"), ("H02", "J02")):
            text = text.replace(old, new)
        (data / source.name).write_text(text)
    subprocess.run([solver, "solve", "--data", str(data), "--out", str(work / "output"),
                    "--scenario", "all", "--seconds", "30", "--workers", "1"], check=True)
    for scenario in "ABC":
        result = work / "output" / scenario
        subprocess.run([sys.executable, str(root / "tools/derive/crosscheck.py"),
                        str(data), str(result)], check=True)
        for name in ("SCHEDULE_ACCESS.csv", "SCHEDULE_OCCUPANCY.csv", "RESULTS.csv"):
            assert (result / name).is_file(), f"Missing {scenario}/{name}"
    print("Renamed hidden-input smoke test: A/B/C solved and independently checked.")
