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

The Vitotrol 300 ID profile uses class `0x11`, model byte `0x38`, and the reported serial bytes `0x00 0x11`; the Vitotrol 200 profile uses model byte `0x34` and serial bytes `0x00 0x05`. Public sources disagree on ID details, so these values should be confirmed from a real bus capture for the target controller.

## Current implementation scope

The slave validates incoming CRC-16/Kermit frames and handles master ping, single/multiple register reads, register writes, and dataset writes. It reports its ID through the documented `0xF8` register range, returns PONG for supported master writes, and sends queued remote datasets only when granted the bus by a master ping. It never initiates a telegram without a master request.

The web page is `/remote`; the read API is `GET /api/remote`. `POST /api/remote` accepts JSON containing one or more of:

```json
{"room_temperature": 20.5}
{"desired_room_temperature": 21}
{"mode": "heat_water"}
```

Supported `mode` values are `off`, `water`, `heat_water`, `party_on`, `party_off`, `economy_on`, and `economy_off`. The current-room temperature is sent as a room-temperature dataset; requested setpoints and modes are queued for master-granted transmission. These are the limited controls for which the references contain examples, not a complete Vitotrol 300 feature set. In particular, schedules, every heating-circuit setting, and all controller status fields are not implemented or verified.

## Validation on a KM1

Begin with a passive capture if possible. Compare discovery/ID exchanges and master pings against the configured model and slot, then test one read, one room-temperature update, and one setpoint/mode change at a time. Confirm the returned values at the controller before enabling routine control. Linux scheduling can cause missed response windows; if captures show timing failures, move real-time KM-Bus handling to a microcontroller and use the add-on only for the higher-level interface.

KM-Bus wiring is electrically hazardous to the controller. Use appropriate galvanic isolation and have a qualified heating professional verify connections.
