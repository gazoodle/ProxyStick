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

> **Status:** early. Sticks built from this repository serve photos to two different frames.
> Expect changes, including to the wire protocol, until 1.0.

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
without a USB-OTG controller (the original ESP32, the C3, C6 and H2) can't do this. Boards
that plug straight into the frame (a USB-A plug) need no cable.

| Board | Status |
|---|---|
| [M5Stack AtomS3](https://docs.m5stack.com/en/core/AtomS3) | Tested |
| [M5Stack AtomS3U](https://docs.m5stack.com/en/core/AtomS3U) | Tested (USB-A plug, so it goes straight into the frame) |
| [M5Stack AtomS3R](https://docs.m5stack.com/en/core/AtomS3R) | Expected to work; not yet tested (8 MB PSRAM, room for a much larger read-ahead) |
| [LilyGO T-Dongle-S3](https://wiki.lilygo.cc/products/t-dongle-series/t-dongle-s3/) | Candidate, not yet tested (USB-A plug, 8 MB PSRAM, TF slot) |
| [Waveshare ESP32-S3-GEEK](https://docs.waveshare.com/ESP32-S3-GEEK) | Candidate, not yet tested (USB-A plug, 2 MB PSRAM, TF slot) |

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

## Frames that keep restarting the stick

Some frames are quick, almost twitchy, about USB. When the stick goes in they show "storage
connected", then "disconnected", over and over, and never show a picture. These frames treat
a USB device that changes or disappears as faulty and cut its power to reset it.

The cause is in the ESP32-S3 itself. For its first fraction of a second after power-on, before
any firmware runs, the chip presents its own built-in USB device: a serial port and debugger
(USB-Serial/JTAG). ProxyStick then takes the USB pins over to present the drive. A patient
host, such as a computer or most frames, simply sees the drive arrive. A rapid frame has
already noticed the first device, sees it disappear, power-cycles the stick, and the whole
thing repeats.

The fix is to tell the chip, permanently, to give its USB pins to the drive from power-on, so
a host only ever meets the drive. That's an eFuse, a one-time switch inside the chip, and the
stick can set it itself from Home Assistant:

1. In the stick's device page, enable the three hidden entities: **USB PHY fuse armed**,
   **Burn USB PHY fuse on next boot** and **USB drive-only at power-on**. They come from
   [example/proxystick.yaml](example/proxystick.yaml).
2. Turn on **USB PHY fuse armed**.
3. Press **Burn USB PHY fuse on next boot**.
4. Restart the stick. **USB drive-only at power-on** turns on, and from the next time it's
   plugged in the frame sees only the drive.

Only do this for a frame that needs it; most don't. What it costs you, on that stick, for good:

- **It can't be undone.** The switch is burnt into the chip.
- **No built-in serial port or debugger at power-on.** You can't watch the chip's start-up
  messages over USB or attach a debugger from reset. ESPHome's own logs come over WiFi as
  usual, so day to day you won't notice.
- **Reflashing over USB still works,** through a different route: hold the reset button for
  about 2 s to put it into download mode. Some early ESP32-S3 chips have that route disabled
  at the factory; the stick checks for this and refuses to burn the fuse on them, because
  they could then never be reflashed over USB. A USB-serial adapter on the chip's UART pins
  remains as a last resort.
- **Nothing else changes.** Over-the-air updates work as before, and the planned HID support
  uses the same USB route as the drive, so it isn't affected.

The arm switch is always off after a restart, and turning it off cancels a burn that hasn't
happened yet. To see what a frame is doing before deciding, set `usb_trace: true` under
`proxy_stick:` and press **Dump USB trace**: the stick records every USB event and command,
across power cycles, for its last 16 boots.

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
