"""Dependency-free contract tests with narrow Home Assistant/HTTP test doubles.

These exercise integration logic, not Home Assistant's own runtime behavior.
"""

import asyncio
import importlib.util
import json
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import AsyncMock, patch

from test_data import payload

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "_decoder_contract_tests"


class HttpError(Exception):
    pass


class UpdateFailed(Exception):
    pass


class DuplicateEntry(Exception):
    pass


class BaseEntity:
    pass


class CoordinatorEntity(BaseEntity):
    def __init__(self, coordinator):
        self.coordinator = coordinator

    @property
    def available(self):
        return self.coordinator.last_update_success


class Coordinator:
    def __init__(self, *args, **kwargs):
        self.listeners = []
        self.last_update_success = True
        self.data = None

    def async_add_listener(self, listener):
        self.listeners.append(listener)
        return lambda: self.listeners.remove(listener)

    async def async_config_entry_first_refresh(self):
        self.data = await self._async_update_data()

    def publish(self, data):
        self.data = data
        for listener in list(self.listeners):
            listener()


class Entry:
    def __init__(self, identity="entry_one"):
        self.entry_id = identity
        self.title = "Viessmann Decoder"
        self.data = {"url": "http://decoder:8099"}
        self.callbacks = []

    def async_on_unload(self, callback):
        self.callbacks.append(callback)

    def unload(self):
        for callback in self.callbacks:
            callback()
        self.callbacks.clear()


class ConfigFlow:
    configured = set()

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__()

    async def async_set_unique_id(self, value):
        self.unique_id = value

    def _abort_if_unique_id_configured(self):
        if self.unique_id in self.configured:
            raise DuplicateEntry()

    def async_create_entry(self, **kwargs):
        return {"type": "create_entry", **kwargs}

    def async_show_form(self, **kwargs):
        return {"type": "form", **kwargs}


def module(name, **attrs):
    result = types.ModuleType(name)
    result.__dict__.update(attrs)
    return result


class ContractTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        package = module(PACKAGE)
        package.__path__ = [str(ROOT)]
        stubs = {
            PACKAGE: package,
            "aiohttp": module("aiohttp", ClientError=HttpError, ClientTimeout=lambda **kw: kw),
            "voluptuous": module("voluptuous", Schema=lambda value: value, Required=lambda value: value),
            "homeassistant": module("homeassistant"),
            "homeassistant.config_entries": module("homeassistant.config_entries", ConfigFlow=ConfigFlow),
            "homeassistant.core": module("homeassistant.core", callback=lambda fn: fn),
            "homeassistant.const": module(
                "homeassistant.const", PERCENTAGE="%",
                UnitOfTemperature=types.SimpleNamespace(CELSIUS="°C"),
                Platform=types.SimpleNamespace(SENSOR="sensor", BINARY_SENSOR="binary_sensor"),
            ),
            "homeassistant.helpers": module("homeassistant.helpers"),
            "homeassistant.helpers.aiohttp_client": module(
                "homeassistant.helpers.aiohttp_client", async_get_clientsession=lambda hass: hass.session
            ),
            "homeassistant.helpers.entity": module(
                "homeassistant.helpers.entity", DeviceInfo=dict,
                EntityCategory=types.SimpleNamespace(DIAGNOSTIC="diagnostic"),
            ),
            "homeassistant.helpers.update_coordinator": module(
                "homeassistant.helpers.update_coordinator", DataUpdateCoordinator=Coordinator,
                CoordinatorEntity=CoordinatorEntity, UpdateFailed=UpdateFailed,
            ),
            "homeassistant.components": module("homeassistant.components"),
            "homeassistant.components.sensor": module(
                "homeassistant.components.sensor", SensorEntity=BaseEntity,
                SensorDeviceClass=types.SimpleNamespace(TEMPERATURE="temperature"),
                SensorStateClass=types.SimpleNamespace(MEASUREMENT="measurement"),
            ),
            "homeassistant.components.binary_sensor": module(
                "homeassistant.components.binary_sensor", BinarySensorEntity=BaseEntity,
                BinarySensorDeviceClass=types.SimpleNamespace(CONNECTIVITY="connectivity"),
            ),
        }
        self.patcher = patch.dict(sys.modules, stubs)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)
        self.api_module = self.load("api")
        self.coordinator_module = self.load("coordinator")
        self.sensor = self.load("sensor")
        self.binary_sensor = self.load("binary_sensor")
        self.flow_module = self.load("config_flow")
        self.setup_module = self.load("setup", "__init__.py")
        self.hass = types.SimpleNamespace(
            data={}, session=object(),
            config_entries=types.SimpleNamespace(
                async_forward_entry_setups=AsyncMock(),
                async_unload_platforms=AsyncMock(return_value=True),
            ),
        )
        self.entry = Entry()
        self.api = self.api_module.DecoderApi(None, self.entry.data["url"])
        self.coordinator = self.coordinator_module.DecoderCoordinator(self.hass, self.api, self.entry)
        self.coordinator.data = payload()
        self.hass.data["viessmann_decoder"] = {self.entry.entry_id: self.coordinator}

    def load(self, name, filename=None):
        qualified_name = f"{PACKAGE}.{name}"
        spec = importlib.util.spec_from_file_location(
            qualified_name, ROOT / (filename or f"{name}.py"), submodule_search_locations=None
        )
        result = importlib.util.module_from_spec(spec)
        sys.modules[qualified_name] = result
        spec.loader.exec_module(result)
        return result

    async def add_platforms(self):
        entities = []
        await self.sensor.async_setup_entry(self.hass, self.entry, entities.extend)
        await self.binary_sensor.async_setup_entry(self.hass, self.entry, entities.extend)
        return entities

    async def test_late_discovery_deduplication_and_listener_cleanup(self):
        self.coordinator.data = {**payload(), "temperatures": [], "pumps": [], "relays": []}
        entities = await self.add_platforms()
        self.assertEqual(len(entities), 6)
        self.coordinator.publish(payload())
        self.assertEqual(len(entities), 13)
        self.coordinator.publish(payload())
        self.assertEqual(len(entities), 13)
        self.coordinator.publish(payload(4))
        self.assertEqual(len(entities), 20)
        self.coordinator.publish(payload(4))
        self.assertEqual(len(entities), 20)
        self.assertEqual(len({entity._attr_unique_id for entity in entities}), 20)
        self.assertEqual(len(self.coordinator.listeners), 2)
        self.entry.unload()
        self.assertEqual(self.coordinator.listeners, [])
        self.coordinator.publish({**payload(), "temperatures": [1, 2, 3]})
        self.assertEqual(len(entities), 20)

    async def test_failed_updates_do_not_discover_channels(self):
        self.coordinator.data = {**payload(), "temperatures": [], "pumps": [], "relays": []}
        entities = await self.add_platforms()
        self.coordinator.last_update_success = False
        self.coordinator.publish(payload())
        self.assertEqual(len(entities), 6)
        self.coordinator.last_update_success = True
        self.coordinator.publish(payload())
        self.assertEqual(len(entities), 13)

    async def test_availability_and_shrinking_arrays(self):
        entities = await self.add_platforms()
        temperature = next(item for item in entities if item._attr_unique_id.endswith("temperatures_0"))
        relay = next(item for item in entities if item._attr_unique_id.endswith("relay_0"))
        status = next(item for item in entities if item._attr_unique_id.endswith("_status"))
        self.assertEqual(temperature.native_value, -10.5)
        self.assertTrue(relay.is_on)
        self.assertEqual(temperature._attr_native_unit_of_measurement, "°C")
        pump = next(item for item in entities if item._attr_unique_id.endswith("pumps_1"))
        self.assertEqual(pump.native_value, 50)
        self.assertEqual(pump._attr_native_unit_of_measurement, "%")
        for key in ("ready", "serialConnected", "compatible"):
            self.coordinator.data = {**payload(), key: False}
            self.assertFalse(temperature.available)
            self.assertFalse(relay.available)
            self.assertTrue(status.available)
        self.coordinator.data = {**payload(), "temperatures": [], "relays": []}
        self.assertFalse(temperature.available)
        self.assertIsNone(temperature.native_value)
        self.assertFalse(relay.available)
        self.assertIsNone(relay.is_on)
        self.coordinator.last_update_success = False
        self.assertTrue(all(not entity.available for entity in entities))
        self.coordinator.last_update_success = True
        self.coordinator.publish(payload())
        self.assertTrue(all(entity.available for entity in entities))

    async def test_shared_device_and_per_entry_identity(self):
        entities = await self.add_platforms()
        self.assertTrue(all(item._attr_device_info == entities[0]._attr_device_info for item in entities))
        second = self.coordinator_module.DecoderCoordinator(self.hass, self.api, Entry("entry_two"))
        other = self.sensor.DecoderChannelSensor(second, "temperatures", 0)
        original = self.sensor.DecoderChannelSensor(self.coordinator, "temperatures", 0)
        self.assertNotEqual(other._attr_unique_id, original._attr_unique_id)
        self.assertNotEqual(other._attr_device_info["identifiers"], original._attr_device_info["identifiers"])

    async def test_remote_readings_and_protocol_changes(self):
        self.coordinator.data = payload(4)
        entities = await self.add_platforms()
        room = next(item for item in entities if item._attr_unique_id.endswith("remote_room_temperature"))
        online = next(item for item in entities if item._attr_unique_id.endswith("remote_online"))
        self.assertEqual(room.native_value, 20.5)
        self.assertTrue(room.available)
        self.coordinator.data["remote"]["online"] = False
        self.assertFalse(room.available)
        self.assertTrue(online.available)
        self.assertFalse(online.is_on)
        self.coordinator.publish(payload())
        self.assertFalse(room.available)
        self.assertFalse(online.available)
        self.assertIsNone(room.native_value)
        self.assertIsNone(online.is_on)

    async def test_update_errors_are_wrapped(self):
        for error in (self.api_module.CannotConnect("offline"), self.api_module.InvalidDecoderData("invalid")):
            with patch.object(self.api, "async_get_data", AsyncMock(side_effect=error)):
                with self.assertRaises(UpdateFailed):
                    await self.coordinator._async_update_data()

    async def test_flow_success_and_normalized_duplicate(self):
        flow = self.flow_module.DecoderConfigFlow()
        flow.hass = self.hass
        with patch.object(self.api_module.DecoderApi, "async_get_data", AsyncMock(return_value=payload())) as get_data:
            result = await flow.async_step_user({"url": "HTTP://Decoder:80/"})
            self.assertEqual(result["data"], {"url": "http://decoder"})
            self.assertEqual(flow.unique_id, "http://decoder")
            get_data.assert_awaited_once()
            with patch.object(ConfigFlow, "configured", {"http://decoder"}):
                with self.assertRaises(DuplicateEntry):
                    await flow.async_step_user({"url": "http://DECODER/"})
            self.assertEqual(get_data.await_count, 1)

    async def test_flow_validation_and_connection_errors(self):
        flow = self.flow_module.DecoderConfigFlow()
        flow.hass = self.hass
        result = await flow.async_step_user({"url": "ftp://decoder"})
        self.assertEqual(result["errors"], {"url": "invalid_url"})
        for exception, error in (
            (self.api_module.CannotConnect(), "cannot_connect"),
            (self.api_module.InvalidDecoderData(), "invalid_response"),
        ):
            with patch.object(self.api_module.DecoderApi, "async_get_data", AsyncMock(side_effect=exception)):
                result = await flow.async_step_user({"url": "http://decoder"})
                self.assertEqual(result["errors"], {"base": error})

    async def test_setup_and_unload(self):
        self.hass.data = {}
        with patch.object(self.api_module.DecoderApi, "async_get_data", AsyncMock(return_value=payload())):
            self.assertTrue(await self.setup_module.async_setup_entry(self.hass, self.entry))
        coordinator = self.hass.data["viessmann_decoder"][self.entry.entry_id]
        self.assertEqual(coordinator.data, payload())
        self.hass.config_entries.async_forward_entry_setups.assert_awaited_once()
        self.hass.config_entries.async_unload_platforms.return_value = False
        self.assertFalse(await self.setup_module.async_unload_entry(self.hass, self.entry))
        self.assertIn(self.entry.entry_id, self.hass.data["viessmann_decoder"])
        self.hass.config_entries.async_unload_platforms.return_value = True
        self.assertTrue(await self.setup_module.async_unload_entry(self.hass, self.entry))
        self.assertNotIn(self.entry.entry_id, self.hass.data["viessmann_decoder"])

    async def test_startup_failure_does_not_forward_entities(self):
        self.hass.data = {}
        with patch.object(
            self.api_module.DecoderApi, "async_get_data",
            AsyncMock(side_effect=self.api_module.CannotConnect()),
        ):
            with self.assertRaises(UpdateFailed):
                await self.setup_module.async_setup_entry(self.hass, self.entry)
        self.hass.config_entries.async_forward_entry_setups.assert_not_awaited()
        self.assertEqual(self.hass.data, {})

    async def request(self, body, status=200):
        class Content:
            async def iter_chunked(self, size):
                for index in range(0, len(body), size):
                    yield body[index:index + size]

        class Response:
            content = Content()

            async def __aenter__(self):
                return self

            async def __aexit__(self, *args):
                return False

        response = Response()
        response.status = status
        calls = []

        class Session:
            def get(self, url, **kwargs):
                calls.append((url, kwargs))
                return response

        api = self.api_module.DecoderApi(Session(), "http://decoder:8099")
        result = await api.async_get_data()
        self.assertEqual(calls[0][0], "http://decoder:8099/data")
        self.assertFalse(calls[0][1]["allow_redirects"])
        self.assertEqual(calls[0][1]["timeout"], {"total": 10})
        return result

    async def test_http_get_and_valid_json(self):
        self.assertEqual(await self.request(json.dumps(payload()).encode()), payload())

    async def test_http_status_redirects_and_response_size(self):
        for status in (301, 401, 404, 500):
            with self.subTest(status=status), self.assertRaises(self.api_module.CannotConnect):
                await self.request(b"{}", status)
        with self.assertRaises(self.api_module.InvalidDecoderData):
            await self.request(b" " * 65537)

    async def test_malformed_json_and_nonfinite_json(self):
        for body in (b"not JSON", b"\xff", b"[]", json.dumps({**payload(), "temperatures": [float("nan")]}).encode()):
            with self.subTest(body=body), self.assertRaises(self.api_module.InvalidDecoderData):
                await self.request(body)

    async def test_transport_errors_and_cancellation(self):
        for error in (HttpError(), TimeoutError()):
            api = self.api_module.DecoderApi(types.SimpleNamespace(get=lambda *a, **kw: None), "http://decoder")
            with patch.object(api._session, "get", side_effect=error):
                with self.assertRaises(self.api_module.CannotConnect):
                    await api.async_get_data()
        with patch.object(api._session, "get", side_effect=asyncio.CancelledError()):
            with self.assertRaises(asyncio.CancelledError):
                await api.async_get_data()

    async def test_total_timeout_bounds_a_hung_request(self):
        class HungResponse:
            async def __aenter__(self):
                await asyncio.sleep(60)

            async def __aexit__(self, *args):
                return False

        session = types.SimpleNamespace(get=lambda *a, **kw: HungResponse())
        api = self.api_module.DecoderApi(session, "http://decoder")
        with patch.object(self.api_module, "REQUEST_TIMEOUT", 0.01):
            with self.assertRaises(self.api_module.CannotConnect):
                await api.async_get_data()
