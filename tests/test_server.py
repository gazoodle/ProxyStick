"""The wire protocol (docs/WIRE.md) served from a folder, and the server's life: start, refresh, stop."""

import asyncio
import os
import time

import aiohttp
import pytest
from aiohttp.test_utils import TestClient, TestServer

from proxystick import ProxyStick, fat

pytestmark = pytest.mark.asyncio
EMPTY = "/proxystick/images/sha256-" + "0" * 64 + ".img"


@pytest.fixture
def folder(tmp_path):
    photos = tmp_path / "photos"
    photos.mkdir()
    (photos / "b.jpg").write_bytes(os.urandom(300_000))
    (photos / "a.jpg").write_bytes(os.urandom(2000))
    (photos / ".DS_Store").write_bytes(b"hidden")
    (photos / "nested").mkdir()
    return photos


def stick_of(tmp_path, folder, **options):
    stick = ProxyStick(folder, tmp_path / "state", **options)
    stick.loop = asyncio.get_running_loop()  # As start() would; lets builds announce to the test's streams.
    return stick


def image_of(stick):
    return stick.state_dir / f"{stick.state['generation']}.img"


async def test_nothing_is_named_until_a_complete_image_is_published(tmp_path, folder):
    stick = stick_of(tmp_path, folder)
    async with TestClient(TestServer(stick.application())) as client:
        response = await client.get("/proxystick/current")
        assert response.status == 503 and response.headers["Cache-Control"] == "no-store"
        assert (await client.get(EMPTY)).status == 404


async def test_a_published_image_is_discovered_and_read_by_range(tmp_path, folder):
    stick = stick_of(tmp_path, folder)
    assert "Published revision 1: 2 files" in stick.refresh()
    async with TestClient(TestServer(stick.application())) as client:
        current = await (await client.get("/proxystick/current")).json()
        assert current["protocol"] == 0 and current["revision"] == 1 and current["sector_size"] == 512
        assert current["bytes"] % 512 == 0 and current["generation"].startswith("sha256-")
        head = await client.head(current["image_url"])
        assert head.status == 200 and int(head.headers["Content-Length"]) == current["bytes"]
        assert head.headers["Accept-Ranges"] == "bytes" and head.headers["ETag"]
        ranged = await client.get(
            current["image_url"], headers={"Range": "bytes=1024-1535", "Accept-Encoding": "identity"}
        )
        assert ranged.status == 206 and await ranged.read() == image_of(stick).read_bytes()[1024:1536]
        assert ranged.headers["Content-Range"] == f"bytes 1024-1535/{current['bytes']}"
        assert ranged.headers["Content-Length"] == "512"
        assert ranged.headers["Content-Type"] == "application/octet-stream"
        assert ranged.headers.get("Content-Encoding") == "identity"
        assert (
            ranged.headers["ETag"] == head.headers["ETag"]
        )  # Derived from the generation, so the same each time.
        again = await client.get(
            current["image_url"], headers={"Range": "bytes=0-511", "If-Range": head.headers["ETag"]}
        )
        assert again.status == 206
        beyond = await client.get(
            current["image_url"], headers={"Range": f"bytes={current['bytes']}-{current['bytes'] + 511}"}
        )
        assert beyond.status == 416 and beyond.headers["Content-Range"] == f"bytes */{current['bytes']}"
    files = fat.read_files(image_of(stick))
    assert list(files) == ["a.jpg", "b.jpg"]  # Name order; hidden files and folders left out.
    assert files["b.jpg"] == (folder / "b.jpg").read_bytes()


async def test_only_a_change_publishes_unless_forced_and_the_revision_survives_a_restart(tmp_path, folder):
    stick = stick_of(tmp_path, folder)
    stick.refresh()
    first = dict(stick.state)
    assert "still serving revision 1" in stick.refresh(force=False)
    assert stick.state == first
    assert "Nothing changed; revision 2" in stick.refresh()  # Forced: the same image, reloaded.
    assert stick.state["generation"] == first["generation"]
    (folder / "c.jpg").write_bytes(b"new")
    assert "Published revision 3: 3 files" in stick.refresh(force=False)
    (folder / "c.jpg").unlink()
    stick.refresh(force=False)
    assert len(list(stick.state_dir.glob("sha256-*.img"))) == 2  # The current and the previous.
    again = ProxyStick(folder, stick.state_dir)
    assert again.state["revision"] == 4 and again.published()


async def test_a_build_that_fails_is_reported_and_publishes_nothing(tmp_path):
    stick = stick_of(tmp_path, tmp_path / "missing")
    assert "could not be made" in stick.refresh()
    assert stick.published() is None and stick.error


async def test_sticks_are_told_of_each_refresh(tmp_path, folder):
    stick = stick_of(tmp_path, folder)
    stick.refresh()
    async with TestClient(TestServer(stick.application())) as client:
        events = await client.get("/proxystick/events")
        assert events.headers["Content-Type"].startswith("text/event-stream")
        assert b"event: refresh\nid: 1\n" in await asyncio.wait_for(events.content.readuntil(b"\n\n"), 5)
        refreshed = await client.post("/proxystick/refresh")
        assert refreshed.status == 202 and (await refreshed.json())["revision"] == 2
        assert b"id: 2\n" in await asyncio.wait_for(events.content.readuntil(b"\n\n"), 5)
        assert stick.health()["sticks"] == 1
        for queue in tuple(stick.listeners):
            queue.put_nowait(None)


async def test_with_a_token_every_request_needs_it(tmp_path, folder):
    stick = stick_of(tmp_path, folder, token="drama-07700-900123")
    stick.refresh()
    async with TestClient(TestServer(stick.application())) as client:
        assert (await client.get("/proxystick/current")).status == 401
        assert (
            await client.get("/proxystick/current", headers={"Authorization": "Bearer wrong"})
        ).status == 401
        assert (await client.post("/proxystick/refresh")).status == 401
        good = {"Authorization": "Bearer drama-07700-900123"}
        current = await (await client.get("/proxystick/current", headers=good)).json()
        assert (
            await client.get(current["image_url"], headers={**good, "Range": "bytes=0-511"})
        ).status == 206


async def test_it_serves_on_its_own_and_stops_promptly_with_a_stick_connected(tmp_path, folder):
    stick = await asyncio.to_thread(
        lambda: ProxyStick(folder, tmp_path / "state").start(host="127.0.0.1", port=0)
    )
    try:
        base = f"http://127.0.0.1:{stick.port}/proxystick"
        async with aiohttp.ClientSession() as session:
            events = await session.get(f"{base}/events")
            first = await asyncio.wait_for(
                events.content.readuntil(b"\n\n"), 5
            )  # Published by the start-up build.
            assert b"id: 1\n" in first and stick.health()["status"] == "serving"
            started = time.monotonic()
            await asyncio.to_thread(stick.stop)
            assert time.monotonic() - started < 2
            assert (
                await asyncio.wait_for(events.content.read(), 2) == b""
            )  # The stream was closed, not left open.
        assert stick.health()["status"] == "stopped"
    finally:
        stick.stop()


async def test_a_port_in_use_fails_start_loudly(tmp_path, folder):
    first = await asyncio.to_thread(
        lambda: ProxyStick(folder, tmp_path / "one").start(host="127.0.0.1", port=0)
    )
    try:
        assert first.port
        with pytest.raises(RuntimeError, match="could not serve") as raised:
            ProxyStick(folder, tmp_path / "two").start(host="127.0.0.1", port=first.port)
        assert isinstance(raised.value.__cause__, OSError)
    finally:
        first.stop()
