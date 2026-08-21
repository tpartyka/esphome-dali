#!/usr/bin/env python3
"""Regression check for target-specific DALI API client address validation."""

from pathlib import Path
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
ESPHOME = REPO / ".venv" / "bin" / "esphome"
INVALID_CONFIGS = (
    REPO / "tests" / "configs" / "dali_api_client_invalid_group_address.yaml",
    REPO / "tests" / "configs" / "dali_api_client_invalid_scene_group_address.yaml",
)


def assert_invalid_group_config(config: Path) -> None:
    result = subprocess.run(
        [str(ESPHOME), "config", str(config)],
        cwd=REPO,
        text=True,
        capture_output=True,
    )
    output = result.stdout + result.stderr
    if result.returncode == 0:
        raise AssertionError(f"DALI group address 16 was accepted in {config.name}")
    if "group target address" not in output:
        raise AssertionError(f"Unexpected validation error for {config.name}:\n{output}")


def main() -> int:
    try:
        for config in INVALID_CONFIGS:
            assert_invalid_group_config(config)
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("PASS: out-of-range DALI group addresses rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
