"""The dev loop (AGENTS.md, "Development loop"): build a stick on the HA box from this working tree.

*** NEVER TEST THIS ***

    tools/dev <folder> [--port N] [--state DIR]     (the same options as `proxystick serve`)

Starts the fake git host on this checkout and a ProxyStick server on the folder, with the server's
console, and writes one self-contained `proxystick-dev.yaml` into the box's ESPHome folder (its config
share, mounted here). That config takes the component from the fake git host, so Install in ESPHome on
the box builds whatever is in the editor now, and points the stick at this Mac's server.

The config is two parts. The device (board, WiFi, API, OTA, its name) comes from `tools/dev-device.yaml`,
which is yours and gitignored, since it names your device and secrets and this repository is public. Each
further stick has its own `tools/dev-device-<name>.yaml`, written as `proxystick-dev-<name>.yaml`.
The stick comes from `example/proxystick.yaml`, from its `proxy_stick:` line on, so the example is the
one copy. The device goes in as an inline package, so both parts may have `sensor:` and the like.
"""

from __future__ import annotations

import logging
import os
import re
import sys
import threading
from pathlib import Path

from fake_git_host import FakeGitHost, lan_address

from proxystick import console

REPO = Path(__file__).resolve().parents[1]
BOX = Path(os.environ.get("PROXYSTICK_BOX_ESPHOME", "/Volumes/config/esphome"))
DEVICE_PREFIX = "dev-device"
logger = logging.getLogger(__name__)


def stick_config(endpoint: str) -> str:
    """The example's stick, pointed at this Mac; no token, since the dev server has none."""
    example = (REPO / "example" / "proxystick.yaml").read_text()
    stick = example[example.index("\nproxy_stick:\n") + 1 :]
    stick = re.sub(r"(?m)^  token: .*\n", "", stick)
    return re.sub(r"(?m)^  endpoint: .*$", f"  endpoint: {endpoint}", stick)


def devices() -> list[Path]:
    return sorted((REPO / "tools").glob(f"{DEVICE_PREFIX}*.yaml"))


def config_name(device: Path) -> str:
    """dev-device.yaml -> proxystick-dev.yaml; dev-device-s3u.yaml -> proxystick-dev-s3u.yaml."""
    return f"proxystick-dev{device.stem.removeprefix(DEVICE_PREFIX)}.yaml"


def write_config(git: FakeGitHost, endpoint: str, device_file: Path) -> Path:
    lines = device_file.read_text().splitlines(True)
    device = "".join(f"    {line}" if line.strip() else line for line in lines)
    config = f"""# Written by ProxyStick's tools/dev, rewritten each time it starts: edit the repo, not this.
packages:
  device:
{device}
external_components:
  - source:
      type: git
      url: {git.url}
      ref: {git.branch}
    refresh: 0s  # Fetch at every build: the fake git host snapshots the working tree as it is then.
    components: [proxy_stick]

{stick_config(endpoint)}"""
    target = BOX / config_name(device_file)
    partial = target.with_name(f".{target.name}.partial")
    partial.write_text(config)
    partial.replace(target)
    return target


def watch_configs(git: FakeGitHost, endpoint: str, device_files: list[Path], stop: threading.Event) -> None:
    """Rewrite the box configs when either half changes, without restarting either server."""
    sources = [REPO / "example" / "proxystick.yaml", *device_files]
    modified = {source: source.stat().st_mtime_ns for source in sources}
    while not stop.wait(2):
        try:
            current = {source: source.stat().st_mtime_ns for source in sources}
            if current == modified:
                continue
            for device in device_files:
                write_config(git, endpoint, device)
            modified = current
            logger.info("Updated the ESPHome development configs")
        except Exception:
            logger.exception("Could not update the ESPHome development configs; will retry")


def main() -> int:
    args = console.parser().parse_args(["serve", *sys.argv[1:]])
    args.debug_diagnostics = True  # Temporary debug build: matches example/proxystick.yaml.
    if not BOX.is_dir():
        print(f"No {BOX}: mount the box's config share first.", file=sys.stderr)
        return 1
    if not devices():
        print(
            f"No tools/{DEVICE_PREFIX}*.yaml: write a device part (see the top of example/proxystick.yaml).",
            file=sys.stderr,
        )
        return 1

    with FakeGitHost(REPO) as git:
        endpoint = f"http://{lan_address()}:{args.port}"
        device_files = devices()
        targets = [write_config(git, endpoint, device) for device in device_files]

        def banner(_stick: object) -> str:
            configs = "".join(f"    ESPHome config  {target}\n" for target in targets)
            return (
                f"    Component       {git.url} (branch {git.branch}, the working tree at each fetch)\n"
                f"{configs}    Install them from ESPHome on the box"
            )

        stop = threading.Event()
        watcher = threading.Thread(
            target=watch_configs,
            args=(git, endpoint, device_files, stop),
            name="proxystick-config",
            daemon=True,
        )
        watcher.start()
        try:
            return console.serve(args, banner)
        finally:
            stop.set()
            watcher.join()


if __name__ == "__main__":
    raise SystemExit(main())
