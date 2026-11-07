"""Compatibility entry point for the approved transparent icon exporter."""
from pathlib import Path
import subprocess

if __name__ == "__main__":
    subprocess.run(["node", str(Path(__file__).resolve().parents[1] / "FluentCurrent" / "build-icons.cjs")], check=True)