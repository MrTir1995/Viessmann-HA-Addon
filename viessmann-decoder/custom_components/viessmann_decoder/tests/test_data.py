"""Run with python -m unittest discover -s <integration>/tests."""

import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("decoder_data_validation", ROOT / "data.py")
DATA = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DATA)


def payload(protocol=0):
    data = {
        "serialConnected": True,
        "compatible": True,
        "ready": True,
        "status": "OK",
        "protocol": protocol,
        "serialPort": "/dev/ttyUSB0",
        "temperatures": [-10.5, 20.0],
        "pumps": [0, 50, 100],
        "relays": [True, False],
    }
    if protocol == 4:
        data["remote"] = {
            "model": "Vitotrol 200",
            "slot": 1,
            "online": True,
            "room_temperature": 20.5,
            "desired_room_temperature": 21.0,
            "mode": 2,
            "last_master_dataset": 12345,
        }
    return data


class UrlTests(unittest.TestCase):
    def test_normalization_and_idempotence(self):
        cases = {
            " HTTP://DECODER.local.:80/ ": "http://decoder.local",
            "https://Decoder:443/proxy///": "https://decoder/proxy",
            "http://Decoder:8099/proxy/../decoder/": "http://decoder:8099/decoder",
            "http://[2001:0db8::1]:8099/": "http://[2001:db8::1]:8099",
            "https://wärme.local/": "https://xn--wrme-loa.local",
        }
        for url, expected in cases.items():
            with self.subTest(url=url):
                self.assertEqual(DATA.normalize_url(url), expected)
                self.assertEqual(DATA.normalize_url(expected), expected)

    def test_rejects_invalid_or_authenticated_urls(self):
        for url in (
            "", None, 123, "decoder:8099", "ftp://decoder", "http:///data",
            "******decoder", "http://@decoder", "http://decoder?x=1",
            "http://decoder?", "http://decoder#fragment", "http://decoder#",
            "http://decoder:0", "http://decoder:65536", "http://decoder:bad",
            "http://decoder/a\nb", "http://decoder\\evil", "http://[not-ipv6]",
            "http://decoder/%00", "http://bad host",
        ):
            with self.subTest(url=url), self.assertRaises(ValueError):
                DATA.normalize_url(url)


class DataTests(unittest.TestCase):
    def test_all_protocols(self):
        for protocol in range(5):
            with self.subTest(protocol=protocol):
                self.assertEqual(DATA.validate_data(payload(protocol)), payload(protocol))

    def test_disconnected_empty_response_is_valid(self):
        data = payload()
        data.update(serialConnected=False, compatible=False, ready=False)
        for key in ("temperatures", "pumps", "relays"):
            data[key] = []
        self.assertEqual(DATA.validate_data(data), data)

    def test_missing_required_fields(self):
        for key in payload():
            data = payload()
            del data[key]
            with self.subTest(key=key), self.assertRaises(DATA.InvalidDecoderData):
                DATA.validate_data(data)

    def test_invalid_fields(self):
        cases = {
            "serialConnected": [1, "true", None],
            "compatible": [False, 0],
            "ready": [1, "false"],
            "status": [None, {}],
            "serialPort": [123],
            "protocol": [-1, 5, 0.0, True, "0"],
            "temperatures": [None, {}, ["20"], [True], [float("nan")], [float("inf")], [10**1000], [1] * 33],
            "pumps": [[-1], [101], [50.5], [False], ["50"]],
            "relays": [[1], ["false"], [None]],
        }
        for key, values in cases.items():
            for value in values:
                if key == "compatible" and value is False:
                    continue
                data = payload()
                data[key] = value
                with self.subTest(key=key, value=value), self.assertRaises(DATA.InvalidDecoderData):
                    DATA.validate_data(data)

    def test_non_objects(self):
        for value in (None, [], 1, True, "OK"):
            with self.subTest(value=value), self.assertRaises(DATA.InvalidDecoderData):
                DATA.validate_data(value)

    def test_remote_validation(self):
        for key in payload(4)["remote"]:
            data = payload(4)
            del data["remote"][key]
            with self.subTest(key=key), self.assertRaises(DATA.InvalidDecoderData):
                DATA.validate_data(data)
        for key, value in (
            ("room_temperature", float("nan")),
            ("desired_room_temperature", "21"),
            ("online", 1), ("slot", -1), ("mode", 2.5), ("last_master_dataset", True),
        ):
            data = payload(4)
            data["remote"][key] = value
            with self.subTest(key=key), self.assertRaises(DATA.InvalidDecoderData):
                DATA.validate_data(data)
        for remote in (None, [], "remote"):
            data = payload(4)
            data["remote"] = remote
            with self.assertRaises(DATA.InvalidDecoderData):
                DATA.validate_data(data)

    def test_validation_copies_lists_and_discards_unknown_fields(self):
        source = payload(4)
        original = copy.deepcopy(source)
        source["unknown"] = "ignored"
        result = DATA.validate_data(source)
        self.assertEqual(result, original)
        result["temperatures"].append(1)
        result["remote"]["mode"] = 3
        self.assertEqual(source["temperatures"], original["temperatures"])
        self.assertEqual(source["remote"], original["remote"])


class MetadataTests(unittest.TestCase):
    def test_translation_parity(self):
        strings = json.loads((ROOT / "strings.json").read_text())
        english = json.loads((ROOT / "translations/en.json").read_text())
        german = json.loads((ROOT / "translations/de.json").read_text())
        self.assertEqual(strings, english)

        def keys(value):
            return {key: keys(item) if isinstance(item, dict) else None for key, item in value.items()}

        self.assertEqual(keys(strings), keys(german))
        for platform in strings["entity"]:
            for key, entity in strings["entity"][platform].items():
                self.assertEqual(
                    "{channel}" in entity["name"],
                    "{channel}" in german["entity"][platform][key]["name"],
                )

    def test_manifest(self):
        manifest = json.loads((ROOT / "manifest.json").read_text())
        self.assertEqual(manifest["domain"], ROOT.name)
        self.assertTrue(manifest["config_flow"])
        self.assertEqual(manifest["requirements"], [])
        self.assertEqual(manifest["iot_class"], "local_polling")
