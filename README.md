# ProxyStick

A USB memory stick whose contents live on your server.

ProxyStick turns an ESP32-S3 into a read-only USB drive. Plug it into anything that reads
USB sticks. Each time the host reads a sector, the stick fetches that sector over WiFi from
a server, which holds the whole drive as a FAT32 image. Change the files on the server and
you run `REFRESH` (or POST to the server), and the stick ejects itself, waits a moment, and
comes back with the new contents.

The first use is photo frames. Many good frames have a USB port but no WiFi, or WiFi only
through a vendor's cloud. With ProxyStick in the port, the frame shows a curated photo
library from a folder on your own server, and you never take the stick out to update it.

> **Status:** early. A prototype of the device code has shown served photos on a real frame.
> This version is that prototype reworked: the server is tested, and the device code builds
> but has not yet run on hardware. Expect changes, including to the wire protocol, until 1.0.

## How it works

```
 photo folder ──► ProxyStick server ──── WiFi ────► ESP32-S3 ──── USB ────► photo frame
                  builds a FAT32 image   HTTP range  pretends to be
                  serves it by range     reads, SSE  a USB stick
```

- **The server** builds a FAT32 image of a folder. A build is deterministic: the same files
  always give the same bytes. It publishes each image under a name made from its content,
  and it tells connected sticks when the current image changes.
- **The stick** builds nothing. It is a byte proxy: a USB read becomes an HTTP range
  request, with read-ahead so the host isn't kept waiting.
- **Updates are clean.** When a new image is published, the stick disconnects from USB,
  pauses, switches to the new image and reconnects. The host never sees a mix of old and new
  sectors. If the server goes away, the stick reports "no medium" rather than serve stale
  or partial data.
- **Read-only.** Writes from the host are refused. A host writing to a drive whose contents
  change under it would corrupt the image.

The protocol between the two halves is in [docs/WIRE.md](docs/WIRE.md). Any server that
speaks it will work.

## Hardware

Any ESP32-S3 board with the S3's native USB brought out to its connector. ESP32 variants
without native USB (the original ESP32, the C3) can't do this.

| Board | Status |
|---|---|
| M5Stack AtomS3 | Tested |
| M5Stack AtomS3U | Expected to work (USB-A plug, so it goes straight into the frame) |

## Quick start

### 1. Run the server

```sh
pip install git+https://github.com/gazoodle/ProxyStick
proxystick serve ~/FramePhotos
```

This prints the address the stick should use, builds the image in the background and opens a
console: `REFRESH` (rebuild from the folder and publish; if nothing changed, the sticks reload
anyway), `STATUS`, `HELP` and `QUIT`. Published images and the revision live in
`~/.cache/proxystick` (`--state` to change it); `--port` changes the port (default 8766).

The drive holds the folder's files, not its subfolders or hidden files, in name order. Frames
usually play files in name order too, so name them in the order you want them shown.

### 2. Flash the stick

Add this to an ESPHome device config for your board:

```yaml
esp32:
  board: m5stack-atoms3
  framework:
    type: esp-idf

external_components:
  - source: github://gazoodle/ProxyStick
    components: [proxy_stick]

proxy_stick:
  endpoint: http://192.0.2.10:8766  # the address `proxystick serve` printed
```

Flash it once over USB, then plug the stick into the frame. Later updates go over the air.

[example/proxystick.yaml](example/proxystick.yaml) is a whole device config with every option:
a token, the read-ahead block size, the USB manufacturer and product strings, a switch to turn
the drive off, buttons to replug it and clear its statistics, and the diagnostic sensors
(bytes served, throughput, HTTP timings, read-ahead hits).

### Hosting the server

`proxystick` is a Python package with a headless core and a console that wraps it. Run the
console on any always-on machine, or embed the core in your own service:

```python
from proxystick import ProxyStick

stick = ProxyStick(folder, state_dir, token=token).start(port=8766)  # builds in the background
stick.refresh()  # after changing the folder; returns what happened
stick.health()  # status, revision, files, connected sticks, last error
stick.stop()
```

or add `stick.routes()` to an aiohttp app of your own.
[Casa Mia](https://github.com/gazoodle/casa-mia) will host it in Home Assistant.

## Limits

- Read-only. 512-byte sectors, FAT32.
- Throughput is WiFi-bound, in the hundreds of KB/s: fine for photos, slow for video.
- The stick needs the server while it is in use. Without the server there is no drive.

## Security

The stick only makes outgoing connections; it never listens. Without a token, anyone on your
network can read the image and trigger a refresh. On an untrusted network, set
`PROXYSTICK_TOKEN` in the server's environment (never on its command line, where other users
of the machine can read it) and put the same token in the stick's config as
`token: !secret proxystick_token`. Never put a token in a URL.

The drive is read-only on purpose. A host writing to a drive whose contents the server
replaces underneath it would corrupt both.

## Roadmap

- HID: the same stick also acting as a keyboard or remote control for the device it is
  plugged into.
- Writeable: Turn the proxy stick into a device that keeps your air-gapped laptop air-gapped
  while still allowing access to the LAN for file transfer.

## Contributing

`tools/setup` makes the venv (it expects the owner's `fake-git-host` repository beside this
checkout; set `FAKE_GIT_HOST` to its path otherwise). `.venv/bin/pytest` tests the server, and `tools/compile` builds the example config
against the working tree's component. `tools/dev <folder>` is the development loop: it serves
the folder and serves this working tree as a git repository, so an ESPHome builder elsewhere
on the network builds the stick from the code in your editor. See [AGENTS.md](AGENTS.md) for
the rest, and [docs/WIRE.md](docs/WIRE.md) for the protocol.

## Licence

MIT. See [LICENSE](LICENSE).
