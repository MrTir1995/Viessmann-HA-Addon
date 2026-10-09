"""Dependency-free HTTP/PTY regression tests; set VIESSMANN_WEBSERVER to the binary."""

import json
import os
from pathlib import Path
import pty
import select
import socket
import subprocess
import time
import unittest
import urllib.error
import urllib.request


def telegram(command, data=b"", destination=0x11, slot=1):
    packet = bytearray([destination, 0, command, 8 + len(data), slot, 1])
    packet.extend(data)
    crc = 0
    for byte in packet:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
    packet.extend([crc & 255, crc >> 8])
    return bytes(packet)


@unittest.skipUnless(os.environ.get("VIESSMANN_WEBSERVER"), "Set VIESSMANN_WEBSERVER")
class RemoteApiTests(unittest.TestCase):
    def setUp(self):
        self.master, self.slave = pty.openpty()
        os.set_blocking(self.master, False)
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            self.port = reservation.getsockname()[1]
        self.process = subprocess.Popen(
            [
                str(Path(os.environ["VIESSMANN_WEBSERVER"]).resolve()),
                "-p", os.ttyname(self.slave), "-t", "km_remote",
                "-w", str(self.port),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.addCleanup(self.stop)
        deadline = time.monotonic() + 5
        while True:
            try:
                self.request("/health")
                break
            except OSError:
                if time.monotonic() >= deadline:
                    self.fail("Webserver did not start")
                time.sleep(0.01)

    def stop(self):
        self.process.terminate()
        self.process.wait(timeout=5)
        os.close(self.master)
        os.close(self.slave)

    def request(self, path, body=None, method=None):
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}{path}",
            data=body,
            method=method,
            headers={"Content-Type": "application/json"},
        )
        try:
            response = urllib.request.urlopen(request, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return response.status, response.read()

    def state(self):
        status, body = self.request("/api/remote")
        self.assertEqual(status, 200)
        return json.loads(body)

    def post(self, value):
        return self.request("/api/remote", json.dumps(value).encode())

    def exchange(self, packet, expect_reply=True):
        os.write(self.master, packet)
        if not expect_reply:
            self.assertFalse(select.select([self.master], [], [], 0.1)[0])
            return b""
        response = bytearray()
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if select.select([self.master], [], [], 0.02)[0]:
                response.extend(os.read(self.master, 1024))
                if len(response) >= 4 and len(response) == response[3]:
                    return bytes(response)
        self.fail(f"Incomplete reply: {response.hex()}")

    def test_initial_room_temperature_and_live_logs(self):
        self.assertEqual(self.state()["room_temperature"], 20)
        reply = self.exchange(telegram(0))
        self.assertEqual(reply[6:10], bytes([0x20, 200 ^ 0xAA, 0xAA, 0xAA]))
        status, body = self.request("/api/bus-logs")
        self.assertEqual(status, 200)
        self.assertIn(b"RX", body)
        self.assertIn(b"TX", body)
        self.assertIn(b"Anzeige pausieren", self.request("/logs")[1])

    def test_atomic_commands_and_requested_state(self):
        self.exchange(telegram(0))
        self.assertEqual(self.post({
            "room_temperature": 21.5,
            "desired_room_temperature": 22,
            "reduced_room_temperature": 17,
            "mode": "water",
        })[0], 200)
        before = self.state()
        self.assertEqual(before["pending_commands"], 4)
        self.assertEqual(self.post({"desired_room_temperature": 23, "mode": "off"})[0], 503)
        after = self.state()
        for key in ["room_temperature", "desired_room_temperature", "reduced_room_temperature", "mode"]:
            self.assertEqual(before[key], after[key])
        for _ in range(4):
            self.exchange(telegram(0))
        self.assertEqual(self.post({"mode": "party_on", "party_room_temperature": 22})[0], 200)
        state = self.state()
        self.assertEqual(state["mode"], 0xC9)
        self.assertTrue(state["requested_party_mode"])
        self.assertEqual(state["party_room_temperature"], 22)
        self.assertEqual(self.post({"mode": "economy_on"})[0], 200)
        self.assertTrue(self.state()["requested_economy_mode"])

    def test_strict_json_rejects_partial_or_ambiguous_updates(self):
        before = self.state()
        for body in [
            b'{}', b'[]', b'{"mode":"off",}', b'{"mode":"off"} garbage',
            b'{"mode":"off","mode":"water"}', b'{"unknown":1}',
            b'{"room_temperature":NaN}', b'{"room_temperature":+20}',
            b'{"room_temperature":020}', b'{"room_temperature":1e999}',
            b'{"desired_room_temperature":20.5}', b'{"mode":"unknown"}',
            b'{"room_temperature":22,"desired_room_temperature":99}',
            b'{"nested":{"mode":"off"}}', b'{"profile":"unknown"}',
            b'{"party_room_temperature":22}',
            b'{"mode":"off","party_room_temperature":22}',
        ]:
            with self.subTest(body=body):
                self.assertEqual(self.request("/api/remote", body)[0], 400)
        after = self.state()
        for key in ["room_temperature", "desired_room_temperature", "mode", "pending_commands"]:
            self.assertEqual(before[key], after[key])
        self.assertEqual(self.request("/api/remote", b"x" * 1025)[0], 413)
        self.assertEqual(self.request("/api/settings", b"x" * 1025)[0], 413)
        self.assertEqual(self.request("/api/remote", method="DELETE")[0], 405)

    def test_profile_and_source_status_datasets(self):
        self.assertEqual(self.post({"profile": "openv"})[0], 200)
        self.assertEqual(self.state()["profile"], "openv")
        self.assertIsNone(self.state()["outside_temperature"])
        values = bytearray(12)
        values[6] = 0xFB  # -5 degrees, signed byte in WiFiVitotrol's interpretation
        values[10] = 0x40
        packet = telegram(0xBF, bytes([0x1D]) + bytes(value ^ 0xAA for value in values))
        self.exchange(packet, expect_reply=False)
        state = self.state()
        self.assertEqual(state["outside_temperature"], -5)
        self.assertTrue(state["heating_enabled"])
        self.assertEqual(state["datasets"][0]["data"], list(values))
        self.assertGreaterEqual(state["datasets"][0]["age_ms"], 0)
        # Broadcast data must not produce simultaneous slave acknowledgements.
        self.exchange(telegram(0xBF, bytes([0x19, 0xAA, 0xAA]), destination=0xFF, slot=0),
                      expect_reply=False)

    def test_ui_and_existing_routes(self):
        dashboard = self.request("/")[1]
        self.assertIn(b"remoteControls", dashboard)
        remote = self.request("/remote")[1]
        for fragment in [b"reduced_room_temperature", b"api/remote", b"dirty", b"datasets"]:
            self.assertIn(fragment, remote)
        for path in ["/data", "/health", "/settings", "/devices"]:
            self.assertEqual(self.request(path)[0], 200)


if __name__ == "__main__":
    unittest.main()
