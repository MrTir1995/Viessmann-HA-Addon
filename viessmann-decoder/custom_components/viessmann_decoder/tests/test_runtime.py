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

from test_data import extended_payload, payload

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "_decoder_contract_tests"


class HttpError(Exception):
    pass


class HomeAssistantError(Exception):
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

    async def async_request_refresh(self):
        pass

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
            "homeassistant.exceptions": module(
                "homeassistant.exceptions", HomeAssistantError=HomeAssistantError
            ),
            "homeassistant.const": module(
                "homeassistant.const", PERCENTAGE="%",
                UnitOfTemperature=types.SimpleNamespace(CELSIUS="°C"),
                Platform=types.SimpleNamespace(
                    SENSOR="sensor", BINARY_SENSOR="binary_sensor",
                    NUMBER="number", SELECT="select", SWITCH="switch",
                ),
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
                BinarySensorDeviceClass=types.SimpleNamespace(
                    CONNECTIVITY="connectivity", PROBLEM="problem"
                ),
            ),
            "homeassistant.components.number": module(
                "homeassistant.components.number", NumberEntity=BaseEntity,
                NumberMode=types.SimpleNamespace(BOX="box"),
            ),
            "homeassistant.components.select": module(
                "homeassistant.components.select", SelectEntity=BaseEntity,
            ),
            "homeassistant.components.switch": module(
                "homeassistant.components.switch", SwitchEntity=BaseEntity,
            ),
        }
        self.patcher = patch.dict(sys.modules, stubs)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)
        self.api_module = self.load("api")
        self.coordinator_module = self.load("coordinator")
        self.sensor = self.load("sensor")
        self.binary_sensor = self.load("binary_sensor")
        self.number = self.load("number")
        self.select = self.load("select")
        self.switch = self.load("switch")
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

    async def test_config_flow_distinct_adapter_paths_and_normalized_duplicates(self):
        primary = "http://decoder:8099/adapters/primary"
        secondary = "http://decoder:8099/adapters/adapter_1"
        configured = set()
        with patch.object(ConfigFlow, "configured", configured), patch.object(
            self.api_module.DecoderApi, "async_get_data", AsyncMock(return_value=extended_payload())
        ) as get_data:
            for url, expected in (
                ("HTTP://DECODER:8099/adapters/primary/", primary),
                (secondary, secondary),
            ):
                flow = self.flow_module.DecoderConfigFlow()
                flow.hass = self.hass
                result = await flow.async_step_user({"url": url})
                self.assertEqual(result["type"], "create_entry")
                self.assertEqual(result["data"], {"url": expected})
                self.assertEqual(flow.unique_id, expected)
                configured.add(flow.unique_id)
            self.assertEqual(configured, {primary, secondary})
            duplicate = self.flow_module.DecoderConfigFlow()
            duplicate.hass = self.hass
            with self.assertRaises(DuplicateEntry):
                await duplicate.async_step_user({"url": "HTTP://DECODER:8099/adapters/adapter_1/"})
            self.assertEqual(get_data.await_count, 2)

    async def test_two_adapter_get_post_identity_state_and_unload_isolation(self):
        bases = (
            "http://decoder:8099/adapters/primary",
            "http://decoder:8099/adapters/adapter_1",
        )
        snapshots = {base: extended_payload() for base in bases}
        snapshots[bases[0]]["remote"].update(room_temperature=20, pending_commands=1)
        snapshots[bases[1]]["remote"].update(room_temperature=22, pending_commands=3)
        calls = []

        class Response:
            status = 200

            def __init__(self, payload):
                self.body = json.dumps(payload).encode()
                self.content = self

            async def iter_chunked(self, size):
                for index in range(0, len(self.body), size):
                    yield self.body[index:index + size]

            async def __aenter__(self):
                return self

            async def __aexit__(self, *args):
                return False

        class Session:
            def get(self, url, **kwargs):
                calls.append(("get", url, kwargs))
                self.assert_endpoint(url, "/data")
                return Response(snapshots[url.removesuffix("/data")])

            def post(self, url, **kwargs):
                calls.append(("post", url, kwargs))
                self.assert_endpoint(url, "/api/remote")
                return Response({"status": "queued"})

            def assert_endpoint(self, url, suffix):
                if url not in {base + suffix for base in bases}:
                    raise AssertionError(f"Request escaped its adapter namespace: {url}")

        self.hass.session = Session()
        entries = []
        coordinators = []
        groups = []
        for index, base in enumerate(bases):
            entry = Entry(f"adapter_entry_{index}")
            entry.data = {"url": base}
            entries.append(entry)
            self.assertTrue(await self.setup_module.async_setup_entry(self.hass, entry))
            coordinator = self.hass.data["viessmann_decoder"][entry.entry_id]
            coordinators.append(coordinator)
            entities = []
            for platform in (self.sensor, self.binary_sensor, self.number, self.select, self.switch):
                await platform.async_setup_entry(self.hass, entry, entities.extend)
            groups.append(entities)
            self.assertIs(coordinator.api._session, self.hass.session)
            self.assertEqual(coordinator.api.url, base)
            self.assertTrue(all(e._attr_device_info["configuration_url"] == base for e in entities))
        self.assertEqual([call[1] for call in calls], [base + "/data" for base in bases])
        self.assertTrue(all(not call[2]["allow_redirects"] for call in calls))
        self.assertTrue(
            {e._attr_unique_id for e in groups[0]}.isdisjoint(
                {e._attr_unique_id for e in groups[1]}
            )
        )
        self.assertNotEqual(
            groups[0][0]._attr_device_info["identifiers"],
            groups[1][0]._attr_device_info["identifiers"],
        )
        rooms = [
            next(e for e in group if isinstance(e, self.number.DecoderRemoteNumber)
                 and e._field == "room_temperature")
            for group in groups
        ]
        self.assertEqual([room.native_value for room in rooms], [20, 22])
        original = [json.loads(json.dumps(c.data)) for c in coordinators]
        with patch.object(coordinators[0], "async_request_refresh", AsyncMock()) as refresh0, patch.object(
            coordinators[1], "async_request_refresh", AsyncMock()
        ) as refresh1:
            await rooms[0].async_set_native_value(21.5)
            self.assertEqual(calls[-1][:2], ("post", bases[0] + "/api/remote"))
            self.assertEqual(calls[-1][2]["json"], {"room_temperature": 21.5})
            refresh0.assert_awaited_once()
            refresh1.assert_not_awaited()
            await rooms[1].async_set_native_value(23.5)
            self.assertEqual(calls[-1][:2], ("post", bases[1] + "/api/remote"))
            self.assertEqual(calls[-1][2]["json"], {"room_temperature": 23.5})
            refresh0.assert_awaited_once()
            refresh1.assert_awaited_once()
        self.assertEqual([c.data for c in coordinators], original)
        self.assertEqual([room.native_value for room in rooms], [20, 22])
        self.assertEqual(
            self.number.DecoderRemoteNumber(coordinators[0], "room_temperature")._attr_unique_id,
            rooms[0]._attr_unique_id,
        )
        offline = json.loads(json.dumps(original[0]))
        offline["remote"]["online"] = False
        coordinators[0].publish(offline)
        self.assertFalse(rooms[0].available)
        self.assertTrue(rooms[1].available)
        self.assertEqual(coordinators[1].data, original[1])
        coordinators[0].last_update_success = False
        self.assertTrue(all(not e.available for e in groups[0]))
        self.assertTrue(rooms[1].available)
        entries[0].unload()
        self.assertEqual(coordinators[0].listeners, [])
        self.assertEqual(len(coordinators[1].listeners), 5)
        self.assertTrue(await self.setup_module.async_unload_entry(self.hass, entries[0]))
        self.assertNotIn(entries[0].entry_id, self.hass.data["viessmann_decoder"])
        self.assertIs(
            self.hass.data["viessmann_decoder"][entries[1].entry_id], coordinators[1]
        )
        entries[1].unload()

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
        self.assertEqual(
            self.setup_module.PLATFORMS, ["sensor", "binary_sensor", "number", "select", "switch"]
        )
        self.assertIs(coordinator.api._session, self.hass.session)
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

    async def request(self, body, status=200, command=None):
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

            post = get

        api = self.api_module.DecoderApi(Session(), "http://decoder:8099")
        if command is None:
            result = await api.async_get_data()
        else:
            result = await api.async_set_remote(command)
            self.assertEqual(calls[0][1]["json"], command)
        self.assertEqual(
            calls[0][0], "http://decoder:8099/data" if command is None else "http://decoder:8099/api/remote"
        )
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

    async def controls(self):
        entities = []
        for platform in (self.number, self.select, self.switch):
            await platform.async_setup_entry(self.hass, self.entry, entities.extend)
        return entities

    async def test_control_late_discovery_profile_changes_and_legacy_read_only(self):
        self.coordinator.data = payload(4)
        entities = await self.controls()
        self.assertEqual(len(entities), 3)
        self.assertTrue(all(not entity.available for entity in entities))
        self.coordinator.publish(extended_payload("openv"))
        self.assertEqual(len(entities), 6)
        self.assertTrue(all(entity.available for entity in entities))
        self.coordinator.publish(extended_payload())
        self.assertEqual(len(entities), 7)
        self.coordinator.publish(extended_payload())
        self.assertEqual(len(entities), 7)
        party = next(e for e in entities if e._field == "party_room_temperature")
        self.coordinator.publish(extended_payload("openv"))
        self.assertFalse(party.available)
        self.coordinator.publish(payload(4))
        self.assertTrue(all(not entity.available for entity in entities))
        self.entry.unload()
        self.assertEqual(self.coordinator.listeners, [])

    async def test_controls_gate_connection_protocol_and_extension_support(self):
        self.coordinator.data = extended_payload()
        entities = await self.controls()
        self.assertTrue(all(e.available for e in entities))
        for key in ("serialConnected", "ready", "compatible"):
            self.coordinator.data = {**extended_payload(), key: False}
            self.assertTrue(all(not e.available for e in entities))
        self.coordinator.data = extended_payload()
        self.coordinator.data["remote"]["online"] = False
        self.assertTrue(all(not e.available for e in entities))
        self.coordinator.data = payload()
        self.assertTrue(all(not e.available for e in entities))
        for key in self.load("data").CONTROL_FIELDS:
            self.coordinator.data = extended_payload()
            del self.coordinator.data["remote"][key]
            self.assertTrue(all(not e.available for e in entities))
        self.coordinator.data = extended_payload()
        self.coordinator.last_update_success = False
        self.assertTrue(all(not e.available for e in entities))

    async def test_offline_zero_snapshot_polls_successfully_but_controls_unavailable(self):
        data = extended_payload()
        data.update(serialConnected=False, compatible=False, ready=False)
        data["remote"].update(
            online=False, room_temperature=0, desired_room_temperature=0,
            reduced_room_temperature=0, party_room_temperature=0, mode=0,
            last_master_dataset=0,
        )
        self.coordinator.data = await self.request(json.dumps(data).encode())
        self.assertEqual(self.coordinator.data, data)
        entities = await self.controls()
        self.assertTrue(all(not e.available for e in entities))
        with patch.object(self.api, "async_set_remote", AsyncMock()) as send:
            with self.assertRaises(HomeAssistantError):
                await entities[0].async_set_native_value(20)
            send.assert_not_awaited()

    async def test_number_select_switch_commands_refresh_without_optimistic_mutation(self):
        self.coordinator.data = extended_payload()
        entities = await self.controls()
        send = AsyncMock()
        refresh = AsyncMock()
        with patch.object(self.api, "async_set_remote", send), patch.object(
            self.coordinator, "async_request_refresh", refresh
        ):
            for entity in entities:
                before = json.loads(json.dumps(self.coordinator.data))
                if isinstance(entity, self.number.DecoderRemoteNumber):
                    await entity.async_set_native_value(22)
                    expected = {entity._field: 22}
                    if entity._field == "party_room_temperature":
                        expected["mode"] = "party_on"
                    send.assert_awaited_with(expected)
                elif isinstance(entity, self.select.DecoderRemoteMode):
                    self.assertEqual(entity.current_option, "heat_water")
                    for option in entity._attr_options:
                        await entity.async_select_option(option)
                        send.assert_awaited_with({"mode": option})
                else:
                    self.assertFalse(entity.is_on)
                    await entity.async_turn_on()
                    send.assert_awaited_with({"mode": f"{entity._mode}_on"})
                    await entity.async_turn_off()
                    send.assert_awaited_with({"mode": f"{entity._mode}_off"})
                self.assertEqual(self.coordinator.data, before)
            self.assertEqual(refresh.await_count, send.await_count)
        room = next(e for e in entities if e._field == "room_temperature")
        self.assertEqual(
            (room._attr_native_min_value, room._attr_native_max_value, room._attr_native_step),
            (-20, 50, 0.1),
        )

    async def test_controls_fail_without_refresh_or_mutation(self):
        self.coordinator.data = extended_payload()
        entities = await self.controls()
        room = entities[0]
        before = json.loads(json.dumps(self.coordinator.data))
        for error in (
            self.api_module.CannotConnect("offline"), self.api_module.InvalidDecoderData("invalid"),
        ):
            with patch.object(self.api, "async_set_remote", AsyncMock(side_effect=error)), patch.object(
                self.coordinator, "async_request_refresh", AsyncMock()
            ) as refresh:
                with self.assertRaises(HomeAssistantError):
                    await room.async_set_native_value(21)
                refresh.assert_not_awaited()
                self.assertEqual(self.coordinator.data, before)
        mode = next(e for e in entities if isinstance(e, self.select.DecoderRemoteMode))
        with self.assertRaises(HomeAssistantError):
            await mode.async_select_option("party_on")
        self.coordinator.data["ready"] = False
        with patch.object(self.api, "async_set_remote", AsyncMock()) as send:
            with self.assertRaises(HomeAssistantError):
                await room.async_set_native_value(21)
            send.assert_not_awaited()

    async def test_number_invalid_values_raise_ha_errors_before_transport(self):
        self.coordinator.data = extended_payload()
        entities = await self.controls()
        session = types.SimpleNamespace(post=AsyncMock())
        self.api._session = session
        for entity in entities:
            if not isinstance(entity, self.number.DecoderRemoteNumber):
                continue
            for value in (True, float("nan"), float("inf"), -21, 51, 20.05):
                with self.subTest(field=entity._field, value=value):
                    with self.assertRaises(HomeAssistantError):
                        await entity.async_set_native_value(value)
        session.post.assert_not_called()

    async def test_received_measurements_null_unverified_and_stale(self):
        self.coordinator.data = extended_payload()
        entities = await self.add_platforms()
        received = [
            next(e for e in entities if e._attr_unique_id.endswith(f"remote_{field}"))
            for field in ("outside_temperature", "heating_enabled", "controller_fault")
        ]
        self.assertTrue(all(not e.available for e in received))
        remote = self.coordinator.data["remote"]
        remote.update(
            outside_temperature=25, heating_enabled=True, controller_fault=False,
            status_dataset_age_ms=0,
        )
        self.assertTrue(all(not e.available for e in received))
        remote.update(measurements_verified=True, outside_temperature=9)
        self.assertTrue(all(e.available for e in received))
        self.assertEqual(received[0].native_value, 9)
        self.assertTrue(received[1].is_on)
        self.assertFalse(received[2].is_on)
        for field in ("serialConnected", "ready", "compatible"):
            self.coordinator.data[field] = False
            self.assertTrue(all(not e.available for e in received))
            self.coordinator.data[field] = True
        remote["online"] = False
        self.assertTrue(all(not e.available for e in received))
        remote["online"] = True
        for age, available in ((180000, True), (180001, False), (None, False)):
            remote["status_dataset_age_ms"] = age
            self.assertTrue(all(e.available == available for e in received))
        del remote["status_dataset_age_ms"]
        self.assertTrue(all(not e.available for e in received))
        remote["status_dataset_age_ms"] = 1
        remote["controller_fault"] = None
        self.assertFalse(received[2].available)
        remote["outside_temperature"] = None
        remote["heating_enabled"] = None
        self.assertTrue(all(not e.available for e in received))
        self.coordinator.publish(payload())
        self.assertTrue(all(not e.available for e in received))

    async def test_diagnostic_candidates_never_discover_real_sensor_mappings(self):
        self.coordinator.data = extended_payload()
        self.coordinator.data["remote"].update(
            outside_temperature_candidate=25, heating_enabled_candidate=True,
        )
        entities = await self.add_platforms()
        self.assertTrue(all("candidate" not in e._attr_unique_id for e in entities))
        for field in ("outside_temperature", "heating_enabled", "controller_fault"):
            entity = next(e for e in entities if e._attr_unique_id.endswith(f"remote_{field}"))
            self.assertFalse(entity.available)

    async def test_diagnostics_not_heating_fault_and_preserved_sensor_ids(self):
        self.coordinator.data = extended_payload()
        entities = await self.add_platforms()
        problem = next(e for e in entities if e._attr_unique_id.endswith("remote_communication_problem"))
        self.assertTrue(problem.available)
        self.assertFalse(problem.is_on)
        remote = self.coordinator.data["remote"]
        for field in ("crc_errors", "malformed_frames", "unknown_commands"):
            diagnostic = next(e for e in entities if e._attr_unique_id.endswith(f"remote_{field}"))
            remote[field] = 2
            self.assertEqual(diagnostic.native_value, 2)
            self.assertTrue(problem.is_on)
            remote[field] = 0
            self.assertFalse(problem.is_on)
        remote["pending_commands"] = 2
        self.assertFalse(problem.is_on)
        remote["online"] = False
        self.assertTrue(problem.is_on)
        self.assertTrue(problem.available)
        remote["online"] = True
        for field in ("serialConnected", "ready", "compatible"):
            self.coordinator.data[field] = False
            self.assertTrue(problem.is_on)
            self.coordinator.data[field] = True
        self.coordinator.last_update_success = False
        self.assertFalse(problem.available)
        for key in ("room_temperature", "desired_room_temperature", "mode"):
            self.assertIn(
                f"entry_one_remote_{key}", {e._attr_unique_id for e in entities}
            )

    async def test_http_post_validation_acceptance_and_bounded_response(self):
        self.assertIsNone(await self.request(b'{"status":"queued"}', command={"mode": "off"}))
        for status in (301, 400, 401, 404, 409, 503):
            with self.subTest(status=status), self.assertRaises(self.api_module.CannotConnect):
                await self.request(b'{"status":"queued"}', status, {"mode": "off"})
        for body in (b"{}", b"[]", b'{"status":"ok"}', b"bad", b"\xff", b" " * 65537):
            with self.subTest(body=body[:30]), self.assertRaises(self.api_module.InvalidDecoderData):
                await self.request(body, command={"mode": "off"})
        session = types.SimpleNamespace(post=AsyncMock())
        api = self.api_module.DecoderApi(session, "http://decoder")
        with self.assertRaises(self.api_module.InvalidDecoderData):
            await api.async_set_remote({"room_temperature": float("nan")})
        session.post.assert_not_called()

    async def test_post_transport_timeout_and_cancellation(self):
        for error in (HttpError(), TimeoutError()):
            session = types.SimpleNamespace(post=lambda *a, **kw: None)
            api = self.api_module.DecoderApi(session, "http://decoder")
            with patch.object(session, "post", side_effect=error):
                with self.assertRaises(self.api_module.CannotConnect):
                    await api.async_set_remote({"mode": "off"})
        with patch.object(session, "post", side_effect=asyncio.CancelledError()):
            with self.assertRaises(asyncio.CancelledError):
                await api.async_set_remote({"mode": "off"})

        class HungResponse:
            async def __aenter__(self):
                await asyncio.sleep(60)

            async def __aexit__(self, *args):
                return False

        session.post = lambda *a, **kw: HungResponse()
        with patch.object(self.api_module, "REQUEST_TIMEOUT", 0.01):
            with self.assertRaises(self.api_module.CannotConnect):
                await api.async_set_remote({"mode": "off"})
