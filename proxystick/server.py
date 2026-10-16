"""ProxyStick's headless server: a folder, published as a FAT32 image over the wire protocol (docs/WIRE.md).

The stick only reads. This builds each disk image whole, publishes it under a name made from its
content (so a published image never changes), keeps the current and previous ones, and tells the
sticks by Server-Sent Events when the current one changes.

Two ways to host it: `start()` runs it on its own port in its own thread (the console and Casa Mia
do this), or `routes()` adds it to an aiohttp app of your own.

Ported from the prototype's server (2026-10-16), minus its photo library and jobs: what is served is
whatever is in the folder, in name order.
"""

from __future__ import annotations

import asyncio
import hashlib
import hmac
import json
import logging
import os
import re
import threading
from collections.abc import Awaitable, Callable
from pathlib import Path
from typing import Any, Self

from aiohttp import web

from . import fat

logger = logging.getLogger(__name__)

PROTOCOL = 0
PORT = 8766
PREFIX = "/proxystick"
IMAGE = re.compile(r"sha256-[0-9a-f]{64}\.img")
NO_STORE = {"Cache-Control": "no-store"}
HEARTBEAT_SECONDS = 15  # The wire protocol's promise; the stick's lease is a little longer.
STOP_SECONDS = 5


class ProxyStick:
    def __init__(self, folder: Path, state_dir: Path, *, token: str | None = None, label: str = fat.LABEL):
        self.folder, self.state_dir, self.token, self.label = folder, state_dir, token, label
        self.lock = threading.Lock()  # The state.
        self.building = threading.Lock()  # One build at a time.
        self.listeners: set[asyncio.Queue[str | None]] = set()
        self.loop: asyncio.AbstractEventLoop | None = None
        self.error: str | None = None
        self.port: int | None = None
        self.state = self._load()
        self._stop = threading.Event()
        self._serving: asyncio.Event | None = None
        self._thread: threading.Thread | None = None
        self._builder: threading.Thread | None = None

    # The state: {"revision", "generation", "bytes", "files", "previous"}, kept so the revision only ever
    # rises and a restart serves the same image.

    def _load(self) -> dict[str, Any]:
        try:
            return json.loads((self.state_dir / "current.json").read_text())
        except FileNotFoundError:
            return {"revision": 0}
        except (OSError, ValueError) as error:
            # A lost revision could go backwards, which sticks ignore; say so rather than start at 0 quietly.
            logger.error("Unreadable %s (%s); starting again from revision 0", self.state_dir, error)
            return {"revision": 0}

    def _save(self) -> None:
        self.state_dir.mkdir(parents=True, exist_ok=True)
        temporary = self.state_dir / ".current.json.partial"
        temporary.write_text(json.dumps(self.state))
        os.replace(temporary, self.state_dir / "current.json")

    def published(self) -> dict[str, Any] | None:
        state = self.state
        image = self.state_dir / f"{state.get('generation')}.img"
        return state if state.get("generation") and image.is_file() else None

    def health(self) -> dict[str, Any]:
        state = self.published() or {}
        status = "stopped"
        if self._thread is not None:
            status = "building" if self.building.locked() else "failed" if self.error else "serving"
        return {
            "status": status,
            "error": self.error,
            "port": self.port,
            "folder": str(self.folder),
            "revision": state.get("revision"),
            "generation": state.get("generation"),
            "bytes": state.get("bytes"),
            "files": state.get("files"),
            "sticks": len(self.listeners),
        }

    # Building ---------------------------------------------------------------------------------

    def files(self) -> list[tuple[str, Path, float]]:
        """The folder's files in name order, as (name, path, modified time); hidden files are not served."""
        found, skipped = [], 0
        for path in sorted(self.folder.iterdir(), key=lambda path: path.name):
            if path.name.startswith("."):
                continue
            if not path.is_file():
                skipped += 1
                continue
            found.append((path.name, path, path.stat().st_mtime))
        if skipped:
            logger.warning(
                "%d folder(s) in %s not served: only files in the folder itself are", skipped, self.folder
            )
        return found

    def refresh(self, force: bool = True) -> str:
        """Rebuild from the folder and publish it if it changed; if it did not, `force` makes the sticks
        reload it anyway. Waits for the build; returns what to tell the person."""
        with self.building:
            before = self.state["revision"]
            try:
                self._build()
            except fat.Cancelled:
                return "Stopped before the image was finished; nothing was published."
            except Exception as error:
                logger.exception("Could not build the disk image of %s", self.folder)
                self.error = f"The disk image could not be made: {error}"
                return self.error
            self.error = None
            if self.state["revision"] == before:
                if not force:
                    return f"Nothing changed; still serving revision {before}."
                with self.lock:
                    self.state = {**self.state, "revision": before + 1}
                    self._save()
                logger.info("Unchanged; revision %d makes the sticks reload it", self.state["revision"])
                self._announce()
                return f"Nothing changed; revision {self.state['revision']} makes the sticks reload it."
            return f"Published revision {self.state['revision']}: {self.state['files']} files, {self.state['bytes']:,} bytes."

    def _build(self) -> None:
        files = self.files()
        self.state_dir.mkdir(parents=True, exist_ok=True)
        temporary = self.state_dir / f".building-{os.getpid()}.img"
        try:
            fat.build(temporary, files, self.label, self._stop.is_set)
            size = temporary.stat().st_size
            if fat.listing(temporary) != [(name, source.stat().st_size) for name, source, _modified in files]:
                raise OSError(
                    "The disk image does not hold what it should (did a file change while it was read?)"
                )
            digest = hashlib.sha256()
            with open(temporary, "rb") as image:
                while block := image.read(1024 * 1024):
                    digest.update(block)
            generation = f"sha256-{digest.hexdigest()}"
            # aiohttp's file server makes its ETag from the file's modified time and size; taking the time from
            # the generation makes the ETag the generation's, and the same on every request (as If-Range needs).
            stamp = int(digest.hexdigest()[:12], 16)
            os.utime(temporary, ns=(stamp, stamp))
            with self.lock:
                if generation == self.state.get("generation") and self.published():
                    return
                os.replace(temporary, self.state_dir / f"{generation}.img")
                self.state = {
                    "revision": self.state["revision"] + 1,
                    "generation": generation,
                    "bytes": size,
                    "files": len(files),
                    "previous": self.state.get("generation"),
                }
                self._save()
                self._prune()
        finally:
            temporary.unlink(missing_ok=True)
        logger.info("Published %s as revision %d (%d files)", generation, self.state["revision"], len(files))
        self._announce()

    def _prune(self) -> None:
        keep = {f"{self.state['generation']}.img", f"{self.state.get('previous')}.img"}
        for path in self.state_dir.glob("sha256-*.img"):
            if path.name not in keep:
                # A stick still reading it keeps its open file; a new request for it gets 404 and the stick
                # discovers the current one.
                path.unlink(missing_ok=True)

    # Telling the sticks -----------------------------------------------------------------------

    def _event(self) -> str | None:
        state = self.published()
        if not state:
            return None
        data = json.dumps({"revision": state["revision"], "generation": state["generation"]})
        return f"event: refresh\nid: {state['revision']}\ndata: {data}\n\n"

    def _announce(self) -> None:
        """Tell every connected stick, from any thread."""
        loop, event = self.loop, self._event()
        if loop and event:
            loop.call_soon_threadsafe(lambda: [queue.put_nowait(event) for queue in tuple(self.listeners)])

    # Routes -----------------------------------------------------------------------------------

    @web.middleware
    async def _authorise(
        self, request: web.Request, handler: Callable[[web.Request], Awaitable[web.StreamResponse]]
    ) -> web.StreamResponse:
        if self.token and request.path.startswith(PREFIX):
            given = request.headers.get("Authorization", "")
            if not hmac.compare_digest(given.encode(), f"Bearer {self.token}".encode()):
                logger.warning(
                    "Refused %s %s from %s: wrong or missing token",
                    request.method,
                    request.path,
                    request.remote,
                )
                raise web.HTTPUnauthorized(headers={"WWW-Authenticate": "Bearer"})
        return await handler(request)

    async def get_current(self, _request: web.Request) -> web.Response:
        state = self.published()
        if not state:
            raise web.HTTPServiceUnavailable(
                text="No disk image has been published yet.", headers={**NO_STORE, "Retry-After": "5"}
            )
        current = {
            "protocol": PROTOCOL,
            "revision": state["revision"],
            "generation": state["generation"],
            "image_url": f"{PREFIX}/images/{state['generation']}.img",
            "bytes": state["bytes"],
            "sector_size": fat.SECTOR,
        }
        return web.json_response(current, headers=NO_STORE)

    async def get_image(self, request: web.Request) -> web.StreamResponse:
        name = request.match_info["name"]
        path = self.state_dir / name
        if not IMAGE.fullmatch(name) or not path.is_file():
            raise web.HTTPNotFound()
        headers = {
            "Content-Type": "application/octet-stream",
            "Content-Encoding": "identity",
            "Cache-Control": "public, max-age=31536000, immutable",
        }
        return web.FileResponse(path, headers=headers)

    async def get_events(self, request: web.Request) -> web.StreamResponse:
        if self._stop.is_set():
            raise web.HTTPServiceUnavailable(text="Shutting down")
        response = web.StreamResponse(headers={"Content-Type": "text/event-stream", **NO_STORE})
        await response.prepare(request)
        queue: asyncio.Queue[str | None] = asyncio.Queue()
        self.listeners.add(queue)
        logger.info("Stick connected from %s (%d now)", request.remote, len(self.listeners))
        try:
            if event := self._event():
                await response.write(event.encode())
            while True:
                try:
                    item = await asyncio.wait_for(queue.get(), HEARTBEAT_SECONDS)
                except TimeoutError:
                    item = ": keep-alive\n\n"  # Renews the stick's lease, and lets either end notice a dead link.
                if item is None:
                    break
                await response.write(item.encode())
        except (ConnectionResetError, asyncio.CancelledError):
            pass
        finally:
            self.listeners.discard(queue)
            logger.info("Stick at %s disconnected (%d left)", request.remote, len(self.listeners))
        return response

    async def post_refresh(self, _request: web.Request) -> web.Response:
        message = await asyncio.to_thread(self.refresh)
        state = self.published()
        if not state:
            raise web.HTTPServiceUnavailable(text=message, headers=NO_STORE)
        body = {"revision": state["revision"], "generation": state["generation"], "message": message}
        return web.json_response(body, status=202, headers=NO_STORE)

    def routes(self) -> list[web.RouteDef]:
        return [
            web.get(f"{PREFIX}/current", self.get_current),
            web.get(f"{PREFIX}/images/{{name}}", self.get_image),
            web.get(f"{PREFIX}/events", self.get_events),
            web.post(f"{PREFIX}/refresh", self.post_refresh),
        ]

    def application(self) -> web.Application:
        app = web.Application(middlewares=[self._authorise])
        app.add_routes(self.routes())
        return app

    # Running on its own -----------------------------------------------------------------------

    def start(self, host: str = "0.0.0.0", port: int = PORT) -> Self:
        """Serve on `host:port` (0 picks a free port; see `self.port`) and build the folder in the background."""
        ready = threading.Event()
        failure: list[Exception] = []
        self._stop.clear()
        self._thread = threading.Thread(
            target=lambda: asyncio.run(self._serve(host, port, ready, failure)),
            name="proxystick",
            daemon=True,
        )
        self._thread.start()
        if not ready.wait(STOP_SECONDS) or failure:
            self._thread = None
            raise RuntimeError(f"ProxyStick could not serve on {host}:{port}") from (
                failure[0] if failure else None
            )
        logger.info("Serving %s on port %d", self.folder, self.port)
        # Not forced: restarting the server with the same folder must not make every stick reload.
        self._builder = threading.Thread(
            target=self.refresh, args=(False,), name="proxystick-build", daemon=True
        )
        self._builder.start()
        return self

    async def _serve(self, host: str, port: int, ready: threading.Event, failure: list[Exception]) -> None:
        self.loop = asyncio.get_running_loop()
        self._serving = asyncio.Event()
        runner = web.AppRunner(self.application(), handle_signals=False, shutdown_timeout=1)
        await runner.setup()
        try:
            site = web.TCPSite(runner, host, port)
            await site.start()
            self.port = runner.addresses[0][1]
            ready.set()
            await self._serving.wait()
            # Release the event streams first, or cleanup waits on them.
            for queue in tuple(self.listeners):
                queue.put_nowait(None)
        except Exception as error:  # noqa: BLE001 - handed to start(), which raises it
            failure.append(error)
            ready.set()
        finally:
            await runner.cleanup()
            self.loop = None

    def stop(self) -> None:
        """Stop serving, and any build, promptly; never leaves a thread behind silently."""
        self._stop.set()
        if self.loop and self._serving:
            self.loop.call_soon_threadsafe(self._serving.set)
        for thread in (self._thread, self._builder):
            if thread is not None:
                thread.join(STOP_SECONDS)
                if thread.is_alive():
                    logger.error("%s did not stop within %d s", thread.name, STOP_SECONDS)
        self._thread = self._builder = None
        logger.info("Stopped serving %s", self.folder)

    def __enter__(self) -> Self:
        return self

    def __exit__(self, *_exc: object) -> None:
        self.stop()
