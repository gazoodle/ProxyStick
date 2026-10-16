"""`proxystick serve <folder>`: the server with a console, for running it by hand and for testing."""

from __future__ import annotations

import argparse
import cmd
import logging
import os
import signal
import socket
import sys
from collections.abc import Callable
from pathlib import Path

from .server import PORT, ProxyStick

HELP = """REFRESH   Rebuild from the folder and publish; if nothing changed, the sticks reload anyway
STATUS    What is being served, and to how many sticks
HELP      This list
QUIT/BYE  Stop (Ctrl+C does too)"""


def lan_address() -> str:
    """This machine's address on the LAN: the interface the kernel would route off-LAN traffic through.
    A UDP connect sends nothing. Not the `.local` name, which sticks have resolved unreliably."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect(("192.0.2.1", 80))
        return str(probe.getsockname()[0])


class Console(cmd.Cmd):
    prompt = "> "

    def __init__(self, stick: ProxyStick):
        super().__init__()
        self.stick = stick

    def precmd(self, line: str) -> str:
        return line.strip().lower()

    def say(self, text: str) -> None:
        print(text, file=self.stdout, flush=True)

    def do_refresh(self, _arg: str) -> None:
        self.say("Building the disk image…")
        self.say(self.stick.refresh())

    def do_status(self, _arg: str) -> None:
        health = self.stick.health()
        self.say("\n".join(f"  {key:<10} {value}" for key, value in health.items() if value is not None))

    def do_help(self, arg: str) -> None:
        self.say(HELP)

    def do_quit(self, _arg: str) -> bool:
        return True

    do_bye = do_quit
    do_EOF = do_quit

    def emptyline(self) -> bool:
        return False

    def default(self, line: str) -> None:
        self.say(f"Not a command: {line.split()[0].upper()}. Type HELP.")


def parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="proxystick", description="Serve a folder to ProxySticks.")
    commands = parser.add_subparsers(dest="command", required=True)
    serve = commands.add_parser("serve", help="serve a folder, with a console")
    serve.add_argument("folder", type=Path)
    serve.add_argument("--port", type=int, default=PORT)
    serve.add_argument(
        "--state",
        type=Path,
        default=Path.home() / ".cache" / "proxystick",
        help="where the published images and revision are kept (default ~/.cache/proxystick)",
    )
    return parser


def serve(args: argparse.Namespace, banner: Callable[[ProxyStick], str] = lambda _stick: "") -> int:
    """Run the server and its console until QUIT. The token, if any, comes from PROXYSTICK_TOKEN,
    never the command line, where any user of this machine could read it."""
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s: %(message)s", datefmt="%H:%M:%S")
    # SIGTERM ends the process without unwinding by default; this makes it stop the server first.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    stick = ProxyStick(
        args.folder.expanduser().resolve(), args.state.expanduser(), token=os.environ.get("PROXYSTICK_TOKEN")
    )
    with stick.start(port=args.port):
        print(f"Serving {stick.folder}")
        print(f"    Stick endpoint  http://{lan_address()}:{stick.port}")
        print("    Token           " + ("from PROXYSTICK_TOKEN" if stick.token else "none (open to the LAN)"))
        if extra := banner(stick):
            print(extra)
        print("Building in the background; type HELP for commands.")
        try:
            Console(stick).cmdloop()
        except KeyboardInterrupt:
            print()
    return 0


def main(argv: list[str] | None = None) -> int:
    return serve(parser().parse_args(argv))
