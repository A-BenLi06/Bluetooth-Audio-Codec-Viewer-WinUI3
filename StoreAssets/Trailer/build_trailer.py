"""Build the current approved Fluent motion trailer and compatibility exports."""
from pathlib import Path
import subprocess

if __name__ == "__main__":
    source = Path(__file__).resolve().parents[1] / "FluentCurrent"
    subprocess.run(["node", str(source / "build-trailer.cjs")], check=True)
    subprocess.run(["node", str(source / "sync-legacy.cjs")], check=True)