# ProxyStick wire protocol v0

The contract between a ProxyStick and its server. Anything that speaks it can serve a stick.
Version 0 is a draft: it may change until the hardware tests pass, and then becomes 1.

The stick is an outbound-only HTTP client. It never listens for network connections. The
server owns image construction and publication; the stick only exposes the selected image
as a read-only USB mass-storage device.

All paths below are relative to the stick's configured `endpoint`.

## Rules

- Sector size is 512 bytes.
- A published image is immutable and its byte length is a positive multiple of 512.
- Every publication has a unique opaque `generation` and a monotonically increasing
  integer `revision`.
- A generation-specific image URL must keep serving identical bytes while any stick may
  still be mounted from it. Never replace an image in place.
- JSON responses use `Content-Type: application/json` and `Cache-Control: no-store`.
- Image responses use `Content-Encoding: identity`; byte positions always refer to the raw
  FAT image.

## Discover the current image

`GET /proxystick/current`

Response `200 OK`:

```json
{
  "protocol": 0,
  "revision": 42,
  "generation": "sha256-7d9c...",
  "image_url": "/proxystick/images/sha256-7d9c....img",
  "bytes": 67108864,
  "sector_size": 512
}
```

`generation` and `image_url` are opaque to the stick. `image_url` may be absolute or
relative to the endpoint's origin. The stick rejects an unsupported protocol, a sector size
other than 512, or a byte length that is zero or not divisible by 512.

If no complete image has ever been published, return `503 Service Unavailable`; never name
a partial image.

## Read the image

`HEAD {image_url}` returns `200 OK`, the full `Content-Length`, an `ETag` equal to or derived
from the generation, and `Accept-Ranges: bytes`.

`GET {image_url}` with one closed range:

```http
Range: bytes=1024-1535
Accept-Encoding: identity
```

returns exactly:

```http
HTTP/1.1 206 Partial Content
Content-Type: application/octet-stream
Content-Encoding: identity
Accept-Ranges: bytes
Content-Length: 512
Content-Range: bytes 1024-1535/67108864
ETag: "sha256-7d9c..."
```

The body must contain exactly the requested bytes. Return `416 Range Not Satisfiable` with
`Content-Range: bytes */{total}` for a range outside the image. Redirects are not part of v0.

## Events: the only way the stick learns of a change

`GET /proxystick/events` is a Server-Sent Events stream. The stick connects as a client,
sending `Last-Event-ID` when it has one, and never polls `current` on a timer: it discovers
only when told to.

Immediately on connection, and whenever the selected image changes or a forced refresh is
requested, send:

```text
event: refresh
id: 42
data: {"revision":42,"generation":"sha256-7d9c..."}

```

The `id` is the decimal revision. Duplicate events are allowed; revisions lower than the
stick's current one are ignored.

**The stream is the stick's lease on the server.** Send an SSE comment (`: keep-alive`) at
least every 15 seconds. A stick that hears nothing for 20 seconds takes the server as gone:
it reports no medium to the USB host, drops the connection and reconnects after 5 seconds.
On reconnection the immediate `refresh` event brings the medium back.

On a `refresh` event the stick:

1. Fetches and validates `current`, and reads the image's boot sector.
2. If the revision or generation differs from what it has mounted, disconnects USB and
   reports no medium.
3. Waits 1.5 seconds.
4. Switches to the new immutable image URL and capacity.
5. Reconnects USB.

If discovery fails, the stick keeps what it has mounted and tries again 5 seconds later. A
failed range read removes the medium at once and triggers discovery; the stick never
mounts mixed or unvalidated sectors.

## Force an unload/reload

`POST /proxystick/refresh`

Rebuilds from the source and publishes if anything changed; otherwise increments
`revision` and emits a `refresh` event with the same `generation`, so every stick reloads.

Response `202 Accepted`:

```json
{"revision":43,"generation":"sha256-7d9c...","message":"..."}
```

Publishing a newly completed image performs the same revision increment and event emission
automatically; callers never need a second refresh request.

## Publication order

1. Build and fsync a new image under a temporary name.
2. Validate its length and filesystem image.
3. Move it atomically to its generation-specific immutable URL.
4. Atomically update `current` and increment `revision`.
5. Emit the `refresh` event.

Keep at least the current and immediately previous generation. Removing older generations
is a server policy, but must not race active range requests.

## Authentication

Optional. With a token configured, every request above carries
`Authorization: Bearer <token>`, and the server answers `401` without it. The stick takes
its token from its ESPHome config (`secrets.yaml`). Never put a token in a URL or a log.
