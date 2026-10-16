"""The FAT disk image is a whole FAT32 volume, and the same photos always make the same bytes."""

import hashlib
import os

import pytest

from proxystick import fat as fat_image


def make(tmp_path, count=3, name="IMG{:05d}-O{:05d}.JPG", sizes=(400_000, 1234, 0)):
    files = []
    for index in range(count):
        source = tmp_path / f"source{index}.jpg"
        source.write_bytes(os.urandom(sizes[index % len(sizes)]))
        files.append((name.format(index + 1, 1), source, 1.7e9 + index))
    return files


def test_the_files_come_back_exactly_with_their_long_names(tmp_path):
    files = make(tmp_path) + [
        ("SHORT.TXT", tmp_path / "source1.jpg", 1e9),
        ("EXACTLY13CHAR.JPG", tmp_path / "source1.jpg", 1e9),
    ]
    image = tmp_path / "t.img"
    fat_image.build(image, files)
    assert image.stat().st_size % 512 == 0
    read = fat_image.read_files(image)
    assert list(read) == [name for name, _source, _time in files]
    assert all(read[name] == source.read_bytes() for name, source, _time in files)
    assert fat_image.listing(image) == [(name, source.stat().st_size) for name, source, _time in files]


def test_the_same_photos_make_the_same_image(tmp_path):
    files = make(tmp_path)
    fat_image.build(tmp_path / "a.img", files)
    fat_image.build(tmp_path / "b.img", files)
    assert (
        hashlib.sha256((tmp_path / "a.img").read_bytes()).digest()
        == hashlib.sha256((tmp_path / "b.img").read_bytes()).digest()
    )


def test_many_files_need_a_folder_of_several_clusters_and_unique_short_names(tmp_path):
    source = tmp_path / "one.jpg"
    source.write_bytes(b"x" * 700)
    files = [(f"IMG{number:05d}-O{number:05d}.JPG", source, 1.7e9) for number in range(1, 1501)]
    fat_image.build(tmp_path / "big.img", files)
    assert [name for name, _size in fat_image.listing(tmp_path / "big.img")] == [
        name for name, _s, _t in files
    ]
    assert fat_image.read_files(tmp_path / "big.img")["IMG01500-O01500.JPG"] == b"x" * 700


def test_a_truncated_or_foreign_image_is_refused(tmp_path):
    files = make(tmp_path)
    fat_image.build(tmp_path / "t.img", files)
    data = (tmp_path / "t.img").read_bytes()
    (tmp_path / "short.img").write_bytes(data[:-512])
    (tmp_path / "foreign.img").write_bytes(bytes(1024))
    for name in ("short.img", "foreign.img"):
        with pytest.raises(ValueError):
            fat_image.listing(tmp_path / name)


def test_a_cancelled_build_stops_between_files(tmp_path):
    with pytest.raises(fat_image.Cancelled):
        fat_image.build(tmp_path / "t.img", make(tmp_path), cancelled=lambda: True)
