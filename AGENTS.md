# AGENTS.md: ProxyStick

Read `AGENTS.core.md` first: it holds the shared core rules (generated; see its header).
This file holds only what is specific to this project, and it wins where the two conflict.

**What it is:** an ESPHome external component that makes an ESP32-S3 look like a read-only
USB memory stick, whose sectors are fetched over WiFi from a server that builds and
publishes a FAT32 image (`components/proxy_stick/`), plus that server as a Python package
(`proxystick/`): a headless core that Casa Mia hosts as a module, and a console (CUI) that
runs it locally for testing. The two halves meet only at `docs/WIRE.md`. First use: a
curated photo library for a photo frame with no WiFi. HID support is planned, not built.

**Commands:**
- Setup: `tools/setup`
- Tests: `.venv/bin/pytest -q`
- Lint/format: `.venv/bin/ruff check .` / `.venv/bin/ruff format .`
- Types: `.venv/bin/pyright`
- Component builds are run by the owner on the live HA/ESPHome box; agents do not run `tools/compile`.
- Dev loop: `tools/dev <folder>` (see below)

**Versioning:** semantic, `MAJOR.MINOR.BUILD`, held once in `pyproject.toml`; the component
reads it from there. Bump BUILD for every change, MINOR for a feature or a wire-protocol
change; only the owner bumps MAJOR. The wire protocol has its own integer version in
`docs/WIRE.md`, which changes only when the wire does.

**Branches / deploy:** work on `dev`; `main` moves only at a release, tagged `vX.Y.Z`.

**Constraints:**
- Public repo, MIT licence (owner, 2026-10-16). Core "No real private data" applies in
  full: no hostnames, addresses, house or device names, even in examples.
- The owner's HA box config share is mounted at `/Volumes/config`. `tools/dev` writes only
  its own `esphome/proxystick-dev.yaml` there. The device half of that file comes from
  `tools/dev-device.yaml` on the Mac, the owner's own and gitignored. The prototype's own
  config and component on the box stay untouched until ProxyStick replaces them.
- Never proxy host writes. The drive is read-only until there is an explicit design for
  ownership and cache coherency.

## Development loop (owner, 2026-10-16)

`tools/dev` is one console that starts both the fake git host and a ProxyStick server on a
folder, and writes one self-contained `proxystick-dev.yaml` to the box's ESPHome folder
(owner, 2026-10-16: one file pushed, nothing else to maintain on the box). That YAML takes the
component from the fake git host (`refresh: 0s`) and points the device at the Mac's server.
Edit, then Install in ESPHome on the box: it builds the working tree and updates the device
over the air. Nothing is pushed to GitHub.

- The fake git host is the owner's own repo (`fake-git-host`), a dev dependency. Never copy
  it into this repo. Why: the owner has written it several times already (tablet-provision,
  casa-mia), and DRY across projects means one copy.
- The snapshot branch is built from the working tree, uncommitted edits included, by
  `git daemon --access-hook` at each fetch. Never poll for changes. Why: core "Push, never
  poll"; the fetch itself is the event.
- Containerised development is a fine option for others, but not the owner's setup. Don't
  propose it as a replacement.
