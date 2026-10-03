#!/usr/bin/env python3
"""Drive the single normal make runuefi session through its optional QMP socket.

Does not start QEMU or change its display/input devices. Screenshots are evidence,
not an automatic assertion that the visual behavior is correct.
"""
import json
import socket
import sys
import time

SOCKET = "/tmp/bursztyn-shell-current-qmp.sock"

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
    connection.connect(SOCKET)
    stream = connection.makefile("rwb")
    stream.readline()

    def command(name, arguments=None):
        request = {"execute": name}
        if arguments is not None:
            request["arguments"] = arguments
        stream.write((json.dumps(request) + "\n").encode())
        stream.flush()
        while True:
            response = json.loads(stream.readline())
            if "error" in response:
                raise RuntimeError(response)
            if "return" in response:
                return response["return"]

    command("qmp_capabilities")
    for action in json.loads(sys.argv[1]):
        kind = action[0]
        if kind == "key":
            command("human-monitor-command", {"command-line": "sendkey " + action[1] + " 20"})
            time.sleep(0.07)
        elif kind == "text":
            for char in action[1]:
                key = {" ": "spc", '"': "shift-apostrophe", "-": "minus", ".": "dot", "/": "slash", "@": "shift-2"}.get(char, char)
                command("human-monitor-command", {"command-line": "sendkey " + key + " 20"})
                time.sleep(0.06)
        elif kind == "move":
            command("input-send-event", {"events": [
                {"type": "abs", "data": {"axis": "x", "value": round(action[1] * 32767 / (action[3] if len(action) > 3 else 1280))}},
                {"type": "abs", "data": {"axis": "y", "value": round(action[2] * 32767 / (action[4] if len(action) > 4 else 720))}},
            ]})
            time.sleep(0.03)
        elif kind == "button":
            command("input-send-event", {"events": [{"type": "btn", "data": {"button": "left", "down": bool(action[1])}}]})
            time.sleep(0.03)
        elif kind == "wheel":
            for _ in range(action[2]):
                command("input-send-event", {"events": [
                    {"type": "btn", "data": {"button": action[1], "down": True}},
                    {"type": "btn", "data": {"button": action[1], "down": False}},
                ]})
                if len(action) > 3:
                    time.sleep(action[3])
        elif kind == "shot":
            time.sleep(0.4)
            filename = "/tmp/bursztyn-shell-current-" + action[1] + ".png"
            command("screendump", {"filename": filename, "format": "png"})
            print(filename, flush=True)
        elif kind == "wait":
            time.sleep(min(action[1], 30))
        elif kind == "quit":
            command("quit")
        elif kind == "blocks":
            print(json.dumps(command("query-block")), flush=True)
        elif kind == "status":
            print(json.dumps(command("query-status")), flush=True)
            print(command("human-monitor-command", {"command-line": "info registers"}), flush=True)
        elif kind == "media":
            command("blockdev-change-medium", {"device": action[1], "filename": action[2], "read-only-mode": "read-only", "force": True})
        elif kind == "reset":
            command("system_reset")
        else:
            raise ValueError(action)
