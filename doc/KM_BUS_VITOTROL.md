# Experimental KM-Bus Vitotrol emulation

The add-on has a separate `km_remote` mode for acting as a KM-Bus slave. It does not use the legacy `km` decoder mode, which actively polls using a different framing implementation.

## Protocol references and limits

- [OpenV KM-Bus protocol notes](https://github.com/openv/openv/wiki/KM-Bus) document 1200 baud, 8E1, master-initiated traffic, class/slot addressing, CRC-16/Kermit, and Vitotrol 200A/300 observations. The measurements are primarily from a Vitotronic 200 KW2 and the authors explicitly note that their findings may be incomplete.
- [OpenV KM-Bus interface notes](https://github.com/openv/openv/wiki/KM-Bus-Interface) warn that a PC/Linux response may miss the timing required by the bus; the suggested architecture uses a microcontroller for bus timing.
- [WiFiVitotrol](https://github.com/dumpfheimer/WiFiVitotrol) is an ESP-based virtual remote that provides additional examples of ID responses, read/write commands, room-temperature datasets, and mode requests. Its README identifies its license as NPOSL-3.0. This add-on implements the protocol independently; it does not copy that project's code.

No source located confirms Vitotrol 300 emulation against the specific Vitotronic 200 KM1. Treat this mode as experimental, not as a verified complete replacement for a physical remote.

## Configuration

Select:

- Protocol: `km_remote`
- Remote model: `vitotrol300` (default) or `vitotrol200`
- Heating circuit slot: 1, 2, or 3
- Serial adapter: FC722-based M-Bus slave USB interface (no echo)

In this mode, the add-on forces **1200 baud, 8E1**. A standard M-Bus electrical interface is not sufficient by itself: the host still has to implement the Viessmann KM-Bus telegram protocol. Use an isolated, bus-rated adapter and verify the USB device path before connecting to the heater.

The Vitotrol 300 ID uses class `0x11` and model byte `0x38`; its serial bytes
are `0x00 0x11` in the default `wifi` profile and `0x00 0x05` in `openv`.
The Vitotrol 200 uses model byte `0x34` and serial bytes `0x00 0x05`.
Public sources disagree on ID details, so confirm the selected profile against
a real bus capture for the target controller.

## Current implementation scope

The slave validates incoming CRC-16/Kermit frames and command-specific lengths.
An inter-byte timeout and resynchronisation recover from truncated or corrupt
traffic. It handles master ping, single/multiple register reads, both documented
multiple-write layouts, dataset writes and short dataset requests. Reads are
bounded by the one-byte telegram length (at most 123 address/value pairs) and
cannot wrap around the 256-byte register bank. ID registers `0xF8`–`0xFB` are
read-only; other virtual registers are not a map of the controller's memory.

Room temperature is initially 20 °C, transmitted in signed little-endian tenths
of a degree, not zero. Commands are queued atomically: an invalid value or full
queue changes neither local values nor the queue. Register and dataset responses
are sent only in response to addressed master telegrams. Broadcast updates are
accepted without transmitting an acknowledgement. Blocking serial drain calls
are avoided; the Linux loop wakes on received bytes rather than imposing a
fixed 10 ms delay. This reduces latency but is **not** an actual real-time guarantee.
Timeouts use a monotonic clock rather than wall time. An incomplete/nonblocking
write retains its offset and is retried without busy-waiting, but its remaining
bytes are abandoned if new RX traffic arrives or after 500 ms. The queued
command remains available for a later master grant. That 500 ms limit is a
transport recovery safeguard, **not** a sourced KM-Bus response deadline.
Partial RX frames also expire after 500 ms without a late response. Only a
fresh addressed master poll/request can initiate TX; plausible unrelated or
echoed frames are consumed whole, never scanned for embedded master grants.

### Source variants

The sources disagree and are not manufacturer specifications. The `/remote`
page and API allow selecting a runtime `profile`:

| Behaviour | `wifi` (default) | `openv` |
| --- | --- | --- |
| Unicast master write acknowledgement | PONG, matching WiFiVitotrol | Silent, matching OpenV |
| Virtual register `0x00` | `0x12` | `0x00` |
| Vitotrol 300 ID bytes | `11 38 00 11` | `11 38 00 05` |
| Room-temperature dataset | `0x20` | `0x1F + slot` |
| Normal/reduced temperature command dataset | `0x15` | `0x14 + slot` |
| Operating-mode command dataset | `0x14` | `0x14` |
| Party-on command | `0xCF` with party temperature | `0xCB` enable, remaining data bytes zero |
| Long `0x3F` telegram | Dataset reception, including WiFiVitotrol's `0x34` wrapper | Not treated as a dataset write |

The OpenV circuit-to-dataset mapping is a hypothesis in its documentation,
not a confirmed mapping for every device. Use the profile that matches a
capture of the target controller. Profile changes migrate waiting temperature
commands; they do not persist across restart or serial reconnection.
Changing profile during a partly transmitted queued telegram is rejected
atomically; retry after the transport has completed or abandoned that response.
Queued party-on commands are encoded using the profile active at transmission:
`wifi` sends `0xCF` with the requested temperature, while `openv` sends `0xCB`
without a temperature payload. Migration never sends a WiFi-specific temperature
payload with the OpenV command.
Both profiles handle a short `0x3F` dataset read, returning only an available
locally generated dataset. They never echo master status data back as a remote
response. Unknown datasets/commands are not answered with fabricated data.

Both raw address/value pairs (OpenV) and count-prefixed pairs (WiFiVitotrol) in
`0xB3` writes are recognised by their distinct lengths. Malformed writes do not
partially update registers. `0x1D` is a **dataset identifier**, not a command.
Only dataset data after the identifier is XOR-encoded with `0xAA`; register
traffic, telegram headers and CRC bytes are not globally XOR-encoded.

Implementation references:
[WiFiVitotrol message handling](https://github.com/dumpfheimer/WiFiVitotrol/blob/master/software/src/messageHandler.cpp),
[temperature encoding](https://github.com/dumpfheimer/WiFiVitotrol/blob/master/software/src/dataPerparators.cpp),
[outside temperature/heating flag](https://github.com/dumpfheimer/WiFiVitotrol/blob/master/software/src/dataset.cpp),
and [public mode telegram examples](https://github.com/boblegal31/Heater-remote/blob/master/NetRemote/example/inc/ViessMann.h).
These are protocol references, not code incorporated into this add-on.

The web page is `/remote`; the read API is `GET /api/remote`. `POST /api/remote` accepts JSON containing one or more of:

```json
{"room_temperature": 20.5}
{"desired_room_temperature": 21}
{"reduced_room_temperature": 16}
{"mode": "heat_water"}
{"mode": "party_on", "party_room_temperature": 22}
{"profile": "openv"}
```

Supported `mode` values are `off`, `water`, `heat_water`, `party_on`, `party_off`,
`economy_on`, and `economy_off`. Party/economy requests do not overwrite the base
operating mode. Normal (`0xCD`) and reduced (`0xCE`) setpoints are integral degrees,
5–35 °C; current room temperature allows tenths, −20–50 °C. The reduced command
is identified in WiFiVitotrol's temperature-encoding comments and still needs
verification on the target heater. Several fields can be sent in one JSON object;
the update is all-or-nothing. Unknown/duplicate fields and malformed JSON are
rejected. The queue is limited, and commands wait for a master ping.
Party-on is profile-dependent. In `wifi`, the published Heater-remote
`sendPartyModeOnTelegram` decodes to `0xCF` with 20 °C; the Party22 capture
uses `0xCF` with 22 °C. The implementation therefore sends `0xCF` with the
party temperature in the fourth command-data byte. Without an explicit value,
it uses the local party setpoint (initially 20 °C), never an unintended zero.
`party_room_temperature` is an integral 5–35 °C value and must be combined
with `mode: "party_on"`; it is not a standalone `0xCF` temperature-write command.

OpenV documents `0xCB` as party enable but does not establish a temperature
payload for it. The `openv` profile sends `0xCB` with the remaining data bytes
zero and rejects explicit `party_room_temperature`. Validation uses the
effective profile of the entire API update: a combined switch to `openv`
with a party-temperature request is rejected atomically, without changing
the profile, requested controls or queue. A switch to `wifi` with party-on
and a valid temperature is allowed. Neither source establishes a complete
Vitotrol replacement; verify the selected form on the target controller.

`GET /api/remote` preserves existing fields and adds `profile`,
`reduced_room_temperature`, `party_room_temperature`, `requested_party_mode`, `requested_economy_mode`,
`pending_commands`, `crc_errors`, `malformed_frames`, `unknown_commands`,
`outside_temperature`, `heating_enabled`, and `datasets`. Each received dataset
has `id`, XOR-decoded `data` bytes and `age_ms`. Missing interpreted measurements
are `null`, not a fabricated zero. Outside temperature and the heating-enable
flag use the WiFiVitotrol interpretation of the circuit status dataset; other
fields remain raw because their layout is insufficiently established. These
received measurements must be distinguished from local requested controls.
Received raw datasets `0x14`–`0x17` never confirm or replace requested operating,
party or economy modes: their provenance is not established.

WiFiVitotrol's raw dataset storage also includes identifiers outside OpenV's
`0x10`–`0x22` tables (examples include `0xAD` and `0xBE`). The `wifi` profile
retains identifiers `0x00`–`0xFD`, including wrapped dataset updates, with at
most 29 decoded bytes per dataset. This is bounded raw storage, not permission
to send arbitrary commands or a claim that their contents are understood.

The German `/remote` UI exposes all these controls and received datasets, updates
every two seconds and preserves unsent form edits. It is linked from the
dashboard in `km_remote` mode and uses relative paths for Home Assistant Ingress.

Container restart is available from the dashboard and settings page, subject
to an external restart manager: Docker restart policy or enabled Home Assistant
Watchdog. Without it the container stays stopped. Restart reloads saved startup
settings but resets the runtime profile to `wifi` and clears requested controls,
queued commands, received datasets and volatile bus logs. A successful restart
API response confirms only that shutdown was requested, not that the container
returned or the controller accepted any queued control.

### Deliberately unsupported operations

The [OpenV address tables](https://github.com/openv/openv/wiki/Adressen) describe
Optolink/KW controller memory, not a directly accessible KM-Bus remote register
space. Addresses such as `0x2301`, `0x3306`, `0x0896` or `0x7507` must not be
truncated to a byte or inserted into remote frames. The sources do not establish
a complete conversion between these addresses and KM-Bus datasets.

Schedules, clock/date updates, arbitrary controller-memory access, all other
temperature/error/status fields and a standalone party-temperature command are not
implemented from incomplete or opaque examples. A list of command codes alone
does not establish the required payload. This is still not a complete Vitotrol
300 replacement. The UART stays at **1200 8E1**, as both OpenV and WiFiVitotrol
specify; the supplied sources do not justify changing it to 8E2.

## Validation on a KM1

Begin with a passive capture if possible. Compare discovery/ID exchanges and master pings against the configured model and slot, then test one read, one room-temperature update, and one setpoint/mode change at a time. Confirm the returned values at the controller before enabling routine control. Linux scheduling can cause missed response windows; if captures show timing failures, move real-time KM-Bus handling to a microcontroller and use the add-on only for the higher-level interface.

KM-Bus wiring is electrically hazardous to the controller. Use appropriate galvanic isolation and have a qualified heating professional verify connections.
