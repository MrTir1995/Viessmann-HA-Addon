"""Concurrent adapters: run with VIESSMANN_WEBSERVER pointing to a built server."""

from concurrent.futures import ThreadPoolExecutor
from contextlib import closing
import http.client
import json
import os
from pathlib import Path
import pty
import select
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from urllib.parse import urljoin

from test_remote_api import telegram


@unittest.skipUnless(os.environ.get("VIESSMANN_WEBSERVER"), "Set VIESSMANN_WEBSERVER")
class AdapterApiTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix=".adapter-api-", dir=Path.cwd())
        self.directory = Path(directory.name)
        self.addCleanup(directory.cleanup)
        self.descriptors = []
        self.primary_master, self.primary_port = self.new_pty()
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            self.port = reservation.getsockname()[1]
        self.process = None
        self.addCleanup(self.stop)
        self.start()

    def new_pty(self):
        master, slave = pty.openpty()
        self.descriptors.extend([master, slave])
        os.set_blocking(master, False)
        return master, os.ttyname(slave)

    def stop_process(self):
        if self.process is not None:
            self.process.terminate()
            self.process.wait(timeout=6)
            self.process = None

    def stop(self):
        self.stop_process()
        for descriptor in self.descriptors:
            os.close(descriptor)

    def start(self):
        self.process = subprocess.Popen(
            [str(Path(os.environ["VIESSMANN_WEBSERVER"]).resolve()),
             "-p", self.primary_port, "-t", "km_remote", "-w", str(self.port)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            env={**os.environ, "VIESSMANN_DATA_DIR": str(self.directory)},
        )
        self.wait(lambda: self.request("/health")[0] == 200)
        self.wait(lambda: self.state("/data")["serialConnected"])

    def wait(self, predicate, timeout=7):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                if predicate():
                    return
            except OSError:
                pass
            time.sleep(0.01)
        self.fail("Timed out waiting for adapter state")

    def request(self, path, body=None, method=None, headers=None):
        if isinstance(body, dict):
            body = json.dumps(body).encode()
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}{path}", data=body, method=method,
            headers={"Content-Type": "application/json", **(headers or {})},
        )
        try:
            response = urllib.request.urlopen(request, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return response.status, response.read()

    def state(self, path="/api/adapters"):
        status, body = self.request(path)
        self.assertEqual(status, 200)
        return json.loads(body)

    def options(self, port, **overrides):
        return {
            "serial_port": port, "protocol": "km_remote", "baud_rate": 1200,
            "serial_config": "8E1", "invert_serial": False,
            "remote_model": "vitotrol300", "remote_slot": 1, **overrides,
        }

    def create(self, serial_port, **overrides):
        status, body = self.request("/api/adapters", self.options(serial_port, **overrides))
        self.assertEqual(status, 201, body)
        created = json.loads(body)
        self.assertRegex(created["id"], r"^adapter_[1-7]$")
        self.assertEqual(created["api_url"], "/adapters/" + created["id"])
        return created["api_url"]

    def exchange(self, master, slot=1):
        os.write(master, telegram(0, slot=slot))
        response = bytearray()
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if select.select([master], [], [], 0.02)[0]:
                response.extend(os.read(master, 1024))
                if len(response) >= 4 and len(response) == response[3]:
                    self.assertEqual(response[4], slot)
                    return bytes(response)
        self.fail(f"Incomplete response: {response.hex()}")

    def test_two_ptys_concurrent_controls_data_and_bus_logs_are_isolated(self):
        master, port = self.new_pty()
        base = self.create(port, name='Zweiter Adapter "Büro"', remote_slot=2)
        self.wait(lambda: self.state(base + "/data")["serialConnected"])
        adapters = self.state()["adapters"]
        self.assertEqual([item["id"] for item in adapters], ["primary", "adapter_1"])
        self.assertEqual(adapters[0]["api_url"], "/adapters/primary")
        self.assertEqual(adapters[1]["name"], 'Zweiter Adapter "Büro"')
        self.assertFalse(adapters[1]["ready"])
        self.exchange(self.primary_master)
        self.exchange(master, slot=2)

        def poll(master, slot, expected, prefix):
            for _ in range(40):
                self.assertEqual(self.request(
                    prefix + "/api/remote", {"room_temperature": expected})[0], 200)
                reply = self.exchange(master, slot)
                encoded = int(expected * 10)
                self.assertEqual(reply[6:10], bytes([
                    0x20, (encoded & 255) ^ 0xAA, (encoded >> 8) ^ 0xAA, 0xAA,
                ]))

        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [
                pool.submit(poll, self.primary_master, 1, 21.5, ""),
                pool.submit(poll, master, 2, 26, base),
            ]
            for future in futures:
                future.result(timeout=10)
        for prefix, expected in [("", 21.5), ("/adapters/primary", 21.5), (base, 26)]:
            data = self.state(prefix + "/data")
            remote = self.state(prefix + "/api/remote")
            self.assertEqual(data["remote"]["room_temperature"], expected)
            self.assertEqual(remote["room_temperature"], expected)
            self.assertFalse(remote["measurements_verified"])
            self.assertIsNone(remote["outside_temperature"])
        self.assertTrue(all(item["ready"] for item in self.state()["adapters"]))
        self.assertEqual(self.request(base + "/api/remote", {"desired_room_temperature": 24})[0], 200)
        self.assertEqual(self.state(base + "/api/remote")["pending_commands"], 1)
        self.assertEqual(self.state("/api/remote")["pending_commands"], 0)
        primary_logs = self.request("/api/bus-logs")[1]
        extra_logs = self.request(base + "/api/bus-logs")[1]
        self.assertIn(b"11 00 00 08 01 01", primary_logs)
        self.assertNotIn(b"11 00 00 08 02 01", primary_logs)
        self.assertIn(b"11 00 00 08 02 01", extra_logs)
        self.assertNotIn(b"11 00 00 08 01 01", extra_logs)

    def test_routes_settings_and_restart_persistence(self):
        master, port = self.new_pty()
        base = self.create(port, baud_rate=9600, serial_config="8N1")
        self.wait(lambda: self.state(base + "/data")["serialConnected"])
        self.assertEqual(self.state(base + "/api/settings")["baud_rate"], 1200)
        self.assertEqual(self.state(base + "/api/settings")["serial_config"], "8E1")
        for prefix in ["", "/adapters/primary", base]:
            for route in ["/", "/settings", "/remote", "/logs", "/devices", "/health",
                          "/data", "/api/settings", "/api/remote", "/api/bus-logs"]:
                with self.subTest(prefix=prefix, route=route):
                    self.assertEqual(self.request(prefix + route)[0], 200)
            self.assertIn(b"adapter", self.request(prefix + "/settings")[1])
        for route in ["/data", "/api/remote", "/api/settings", "/settings", "/health", "/"]:
            self.assertEqual(self.request("/adapters/unknown" + route)[0], 404)
        self.assertEqual(self.request("/adapters/unknown/api/remote", {"room_temperature": 31})[0], 404)
        primary_settings = self.options(self.primary_port)
        primary_settings.pop("serial_port")
        self.assertEqual(self.request("/api/settings", primary_settings)[0], 200)
        settings_file = self.directory / "ui_settings.json"
        primary_saved = settings_file.read_bytes()
        extra_settings = self.options(port, remote_model="vitotrol200", remote_slot=3,
                                      baud_rate=9600, serial_config="8N1")
        self.assertEqual(self.request(base + "/api/settings", extra_settings)[0], 200)
        self.assertEqual(settings_file.read_bytes(), primary_saved)
        self.assertEqual(self.state("/api/settings")["remote_slot"], 1)
        # The running instance remains on its original settings until restart.
        self.assertEqual(self.state(base + "/api/settings")["remote_slot"], 1)
        self.exchange(master)
        self.stop_process()
        self.start()
        self.wait(lambda: self.state(base + "/data")["serialConnected"])
        settings = self.state(base + "/api/settings")
        self.assertEqual(settings["remote_model"], "vitotrol200")
        self.assertEqual(settings["remote_slot"], 3)
        self.assertEqual(settings["baud_rate"], 1200)
        self.assertEqual(settings["serial_config"], "8E1")
        self.exchange(master, slot=3)
        self.exchange(self.primary_master)
        self.assertEqual(len(self.state()["adapters"]), 2)

    def test_bad_values_and_duplicate_device_aliases(self):
        master, port = self.new_pty()
        invalid = [
            {"protocol": "wrong"}, {"baud_rate": "1200"}, {"baud_rate": 1234},
            {"baud_rate": 1200.5}, {"remote_slot": 0}, {"remote_slot": 4},
            {"remote_slot": 1.5}, {"remote_model": "200"}, {"serial_config": "8O1"},
            {"invert_serial": "false"}, {"invert_serial": 1}, {"serial_port": ""},
            {"serial_port": "relative"}, {"serial_port": "/dev/a\nb"},
            {"serial_port": "/dev/" + "a" * 257}, {"serial_port": str(self.directory)},
            {"name": ""}, {"name": "a" * 81}, {"name": 42}, {"name": "bad\nname"},
            {"serial_port": "/dev/bad\bname"}, {"unknown": 1},
        ]
        for override in invalid:
            with self.subTest(override=override):
                self.assertEqual(self.request("/api/adapters", self.options(port, **override))[0], 400)
        for body in [
            b"{}", b"[]", b'{"serial_port":"/dev/x",}', b'{"nested":{"serial_port":"/dev/x"}}',
            json.dumps(self.options(port)).encode() + b"\x00",
            json.dumps(self.options(port)).encode() + b"junk",
            json.dumps(self.options(port)).encode().replace(b'"remote_slot": 1', b'"remote_slot": 01'),
            json.dumps(self.options(port)).encode().replace(
                b'"remote_slot": 1', b'"remote_slot": 1, "remote_slot": 2'),
            json.dumps(self.options(port)).encode().replace(b"km_remote", b"km_remot\xff"),
            json.dumps(self.options(port, name="test")).encode().replace(b"test", b"\\ud800"),
        ]:
            self.assertEqual(self.request("/api/adapters", body)[0], 400, body)
        for missing in self.options(port):
            body = self.options(port)
            del body[missing]
            self.assertEqual(self.request("/api/adapters", body)[0], 400)
        self.assertEqual(self.request("/api/adapters", b"x" * 1025)[0], 413)
        self.assertEqual(self.request("/api/adapters", method="DELETE")[0], 405)
        self.assertEqual(self.request("/api/adapters", self.options(self.primary_port))[0], 409)
        alias = self.directory / "primary-alias"
        alias.symlink_to(self.primary_port)
        self.assertEqual(self.request("/api/adapters", self.options(str(alias)))[0], 409)
        nested = self.directory / "nested"
        nested.mkdir()
        canonical_alias = str(nested) + "/../primary-alias"
        self.assertEqual(self.request("/api/adapters", self.options(canonical_alias))[0], 409)
        base = self.create(port)
        alias.unlink()
        alias.symlink_to(port)
        self.assertEqual(self.request("/api/adapters", self.options(str(alias)))[0], 409)
        self.assertEqual(self.request(base + "/api/settings", self.options(self.primary_port))[0], 409)
        self.assertEqual(len(self.state()["adapters"]), 2)
        self.assertFalse(list(self.directory.glob("adapters.jsonl.*")))

    def test_editing_extra_serial_port_persists_without_mutating_primary(self):
        master, port = self.new_pty()
        replacement_master, replacement_port = self.new_pty()
        base = self.create(port)
        self.wait(lambda: self.state(base + "/data")["serialConnected"])
        primary_settings = self.state("/api/settings")
        primary_html = self.request("/settings")[1]
        extra_html = self.request(base + "/settings")[1]
        self.assertIn(b"name='serial_port'", extra_html)
        self.assertIn(b"<option value='57600'>57600</option>", extra_html)
        self.assertNotIn(b"<option value='2400'", extra_html)
        self.assertIn(b"<option value='2400'>2400</option>", primary_html)
        self.assertIn(f"value='{self.primary_port}' readonly".encode(), primary_html)
        self.assertNotIn(f"value='{port}' readonly".encode(), extra_html)
        changed = self.options(replacement_port)
        self.assertEqual(self.request(base + "/api/settings", changed)[0], 200)
        self.assertEqual(self.state("/api/settings"), primary_settings)
        self.assertEqual(self.state(base + "/api/settings")["serial_port"], port)
        # Pending and currently open ports remain reserved until the restart.
        self.assertEqual(self.request("/api/adapters", self.options(replacement_port))[0], 409)
        self.assertEqual(self.request("/api/adapters", self.options(port))[0], 409)
        self.exchange(master)
        self.stop_process()
        self.start()
        self.wait(lambda: self.state(base + "/data")["serialConnected"])
        self.assertEqual(self.state(base + "/api/settings")["serial_port"], replacement_port)
        self.exchange(replacement_master)
        self.exchange(self.primary_master)

    def test_config_mutations_refuse_cross_origin_requests(self):
        master, port = self.new_pty()
        for headers in [
            {"Origin": "https://attacker.example"},
            {"Origin": "null"},
            {"Sec-Fetch-Site": "cross-site"},
            {"Content-Type": "text/plain", "Origin": "https://attacker.example"},
        ]:
            self.assertEqual(self.request(
                "/api/adapters", self.options(port), headers=headers)[0], 403)
        self.assertEqual(len(self.state()["adapters"]), 1)
        status, _ = self.request(
            "/api/adapters", self.options(port),
            headers={"Origin": f"http://127.0.0.1:{self.port}"},
        )
        self.assertEqual(status, 201)
        base = self.state()["adapters"][1]["api_url"]
        for endpoint in ["/api/settings", base + "/api/settings"]:
            for headers in [
                {"Origin": "https://attacker.example"},
                {"Origin": "null"},
                {"Sec-Fetch-Site": "cross-site"},
                {"Content-Type": "text/plain", "Origin": "https://attacker.example"},
            ]:
                settings = self.options(self.primary_port if endpoint == "/api/settings" else port)
                settings.pop("serial_port")
                self.assertEqual(self.request(endpoint, settings, headers=headers)[0], 403)
        self.assertFalse((self.directory / "ui_settings.json").exists())
        self.assertEqual(self.request(
            base + "/api/settings", self.options(port),
            headers={"Origin": "https://homeassistant.local", "Sec-Fetch-Site": "same-origin"},
        )[0], 200)

    def test_namespaced_navigation_and_redirect_preserve_ingress_prefix(self):
        master, port = self.new_pty()
        base = self.create(port)
        with closing(http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)) as connection:
            connection.request("GET", base)
            response = connection.getresponse()
            self.assertEqual(response.status, 301)
            location = response.getheader("Location")
            response.read()
        for prefix in ["", "/api/hassio_ingress/example"]:
            original = f"http://homeassistant.local{prefix}{base}"
            self.assertEqual(urljoin(original, location), original + "/")
        dashboard = self.request(base + "/")[1]
        for relative in [b"href='settings'", b"href='devices'", b"href='logs'", b"href='remote'",
                         b"fetch('data')"]:
            self.assertIn(relative, dashboard)
        for route in ["/settings", "/devices", "/logs", "/remote"]:
            page = self.request(base + route)[1]
            self.assertNotIn(b'window.location.href="/"', page)
            self.assertNotIn(b'window.location.href=\\"/\\"', page)
            self.assertNotIn(b"href='/'", page)
            self.assertNotIn(b'href="/"', page)
        self.assertIn(b"fetch('api/settings'", self.request(base + "/settings")[1])
        self.assertIn(b"fetch('api/remote'", self.request(base + "/remote")[1])

    def test_failed_persistence_rolls_back_creation_and_settings(self):
        master, port = self.new_pty()
        configuration = self.directory / "adapters.jsonl"
        configuration.mkdir()
        self.assertEqual(self.request("/api/adapters", self.options(port))[0], 500)
        self.assertEqual(len(self.state()["adapters"]), 1)
        configuration.rmdir()
        base = self.create(port, name="Original")
        self.wait(lambda: self.state(base + "/data")["serialConnected"])
        previous = configuration.read_bytes()
        configuration.unlink()
        configuration.mkdir()
        updated = self.options(port, name="Not saved", remote_slot=3)
        self.assertEqual(self.request(base + "/api/settings", updated)[0], 500)
        self.assertEqual(self.state()["adapters"][1]["name"], "Original")
        self.assertEqual(self.state(base + "/api/settings")["remote_slot"], 1)
        self.exchange(master)
        configuration.rmdir()
        configuration.write_bytes(previous)
        self.stop_process()
        self.start()
        self.assertEqual(self.state()["adapters"][1]["name"], "Original")
        self.assertFalse(list(self.directory.glob("adapters.jsonl.*")))

    def test_capacity_disconnected_ports_and_no_fallback_to_primary(self):
        for i in range(7):
            self.create(str(self.directory / f"unplugged-{i}"))
        self.assertEqual(len(self.state()["adapters"]), 8)
        self.assertEqual(self.request("/api/adapters", self.options(
            str(self.directory / "too-many")))[0], 503)
        for item in self.state()["adapters"][1:]:
            self.assertFalse(item["serialConnected"])
            self.assertFalse(item["ready"])
            self.assertFalse(self.state(item["api_url"] + "/data")["serialConnected"])
        self.exchange(self.primary_master)
        self.stop_process()
        self.start()
        self.assertEqual(len(self.state()["adapters"]), 8)
        self.exchange(self.primary_master)

    def test_unplug_reconnect_does_not_interrupt_other_adapter(self):
        master, port = self.new_pty()
        alias = self.directory / "reconnect-adapter"
        alias.symlink_to(port)
        base = self.create(str(alias), remote_slot=2)
        self.wait(lambda: self.state(base + "/data")["serialConnected"])
        self.exchange(master, slot=2)
        os.close(master)
        self.descriptors.remove(master)
        self.wait(lambda: not self.state(base + "/data")["serialConnected"])
        for _ in range(10):
            self.exchange(self.primary_master)
        new_master, new_port = self.new_pty()
        alias.unlink()
        alias.symlink_to(new_port)
        self.wait(lambda: self.state(base + "/data")["serialConnected"])
        self.exchange(new_master, slot=2)
        self.exchange(self.primary_master)

    def test_retargeting_to_reserved_device_releases_disconnected_open_device_claim(self):
        old_master, old_port = self.new_pty()
        other_master, other_port = self.new_pty()
        alias = self.directory / "retargeted-adapter"
        alias.symlink_to(old_port)
        first = self.create(str(alias))
        second = self.create(other_port, remote_slot=2)
        self.wait(lambda: self.state(first + "/data")["serialConnected"])
        self.wait(lambda: self.state(second + "/data")["serialConnected"])
        self.exchange(old_master)
        self.exchange(other_master, slot=2)
        alias.unlink()
        alias.symlink_to(other_port)
        self.wait(lambda: not self.state(first + "/data")["serialConnected"])
        # The old physical device is closed, not configured or pending anywhere.
        # A reconnect blocked by the second adapter must not reserve it forever.
        third = self.create(old_port, remote_slot=3)
        self.wait(lambda: self.state(third + "/data")["serialConnected"])
        self.exchange(old_master, slot=3)
        self.exchange(other_master, slot=2)
        self.exchange(self.primary_master)
        self.assertFalse(self.state(first + "/data")["serialConnected"])

    def test_57600_settings_selection_and_unrelated_save_preserve_baud_after_restart(self):
        port = str(self.directory / "unplugged-57600")
        base = self.create(port, protocol="vbus", baud_rate=57600, serial_config="8N1")
        page = self.request(base + "/settings")[1]
        self.assertIn(b"<option value='57600' selected>57600</option>", page)
        self.assertNotIn(b"<option value='1200' selected>", page)
        changed = self.options(port, protocol="vbus", baud_rate=57600, serial_config="8N1",
                               invert_serial=True)
        changed.pop("serial_port")
        self.assertEqual(self.request(base + "/api/settings", changed)[0], 200)
        self.assertEqual(self.state(base + "/api/settings")["baud_rate"], 57600)
        self.stop_process()
        self.start()
        settings = self.state(base + "/api/settings")
        self.assertEqual(settings["baud_rate"], 57600)
        self.assertTrue(settings["invert_serial"])
        self.assertIn(b"<option value='57600' selected>57600</option>",
                      self.request(base + "/settings")[1])
        self.assertEqual(self.state()["adapters"][1]["baud_rate"], 57600)

    def test_rejects_unbounded_or_invalid_persisted_configuration(self):
        configuration = self.directory / "adapters.jsonl"
        line = {"id": "adapter_1", "name": "test", **self.options(str(self.directory / "missing"))}
        invalid_files = [
            "x" * 16385,
            json.dumps({**line, "id": "../unsafe"}) + "\n",
            json.dumps({**line, "protocol": "unknown"}) + "\n",
            (json.dumps(line) + "\n") * 8,
            (json.dumps(line) + "\n") * 2,
        ]
        for body in invalid_files:
            with self.subTest(body=body[:80]):
                self.stop_process()
                configuration.write_text(body)
                self.start()
                self.assertEqual(len(self.state()["adapters"]), 1)
                self.exchange(self.primary_master)

    def test_changed_primary_options_reserve_port_without_losing_persisted_adapter(self):
        master, port = self.new_pty()
        base = self.create(port, remote_slot=2)
        self.wait(lambda: self.state(base + "/data")["serialConnected"])
        self.exchange(master, slot=2)
        original_port = self.primary_port
        self.stop_process()
        self.primary_port = port
        self.start()
        self.assertEqual(len(self.state()["adapters"]), 2)
        self.assertFalse(self.state(base + "/data")["serialConnected"])
        self.exchange(master)
        self.stop_process()
        self.primary_port = original_port
        self.start()
        self.wait(lambda: self.state(base + "/data")["serialConnected"])
        self.exchange(master, slot=2)
        self.exchange(self.primary_master)


if __name__ == "__main__":
    unittest.main()
