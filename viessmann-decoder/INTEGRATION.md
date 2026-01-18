# Home Assistant Integration Guide

This guide shows how to integrate the Viessmann Decoder addon with Home Assistant to create sensors, automations, and dashboards.

## Table of Contents

1. [REST Sensor Configuration](#rest-sensor-configuration)
2. [Template Sensors](#template-sensors)
3. [Dashboard Cards](#dashboard-cards)
4. [Automations](#automations)
5. [Notifications](#notifications)
6. [MQTT Bridge (Optional)](#mqtt-bridge-optional)

## REST Sensor Configuration

The addon exposes a JSON API at `http://localhost:8099/data`. Add this to your `configuration.yaml`:

```yaml
sensor:
  - platform: rest
    resource: http://localhost:8099/data
    name: Viessmann Heating System
    json_attributes:
      - temperatures
      - pumps
      - relays
      - ready
      - protocol
    value_template: '{{ value_json.status }}'
    scan_interval: 10
```

Reload your configuration or restart Home Assistant to activate the sensor.

## Template Sensors

Extract individual values from the REST sensor:

```yaml
template:
  - sensor:
      # Temperature Sensors
      - name: "Boiler Temperature"
        unique_id: viessmann_boiler_temp
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement
        state: >
          {% set temps = state_attr('sensor.viessmann_heating_system', 'temperatures') %}
          {{ temps[0] if temps and temps|length > 0 else 'unavailable' }}
      
      - name: "Hot Water Temperature"
        unique_id: viessmann_hot_water_temp
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement
        state: >
          {% set temps = state_attr('sensor.viessmann_heating_system', 'temperatures') %}
          {{ temps[1] if temps and temps|length > 1 else 'unavailable' }}
      
      - name: "Outdoor Temperature"
        unique_id: viessmann_outdoor_temp
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement
        state: >
          {% set temps = state_attr('sensor.viessmann_heating_system', 'temperatures') %}
          {{ temps[2] if temps and temps|length > 2 else 'unavailable' }}
      
      # Pump Sensors
      - name: "Circulation Pump Power"
        unique_id: viessmann_circ_pump
        unit_of_measurement: "%"
        icon: mdi:pump
        state: >
          {% set pumps = state_attr('sensor.viessmann_heating_system', 'pumps') %}
          {{ pumps[0] if pumps and pumps|length > 0 else 'unavailable' }}
      
      - name: "Solar Pump Power"
        unique_id: viessmann_solar_pump
        unit_of_measurement: "%"
        icon: mdi:pump
        state: >
          {% set pumps = state_attr('sensor.viessmann_heating_system', 'pumps') %}
          {{ pumps[1] if pumps and pumps|length > 1 else 'unavailable' }}
      
      # System Status
      - name: "Heating System Status"
        unique_id: viessmann_system_status
        icon: mdi:information
        state: "{{ states('sensor.viessmann_heating_system') }}"
      
      - name: "Heating System Ready"
        unique_id: viessmann_system_ready
        icon: >
          {% if state_attr('sensor.viessmann_heating_system', 'ready') %}
            mdi:check-circle
          {% else %}
            mdi:alert-circle
          {% endif %}
        state: >
          {% if state_attr('sensor.viessmann_heating_system', 'ready') %}
            Online
          {% else %}
            Offline
          {% endif %}

  - binary_sensor:
      # Relay Binary Sensors
      - name: "Burner Active"
        unique_id: viessmann_burner
        device_class: heat
        state: >
          {% set relays = state_attr('sensor.viessmann_heating_system', 'relays') %}
          {{ relays[0] if relays and relays|length > 0 else false }}
      
      - name: "Hot Water Circulation"
        unique_id: viessmann_hw_circ
        device_class: running
        state: >
          {% set relays = state_attr('sensor.viessmann_heating_system', 'relays') %}
          {{ relays[1] if relays and relays|length > 1 else false }}
```

## Dashboard Cards

### Entities Card

Simple list of all heating sensors:

```yaml
type: entities
title: Viessmann Heating System
entities:
  - entity: sensor.boiler_temperature
    name: Boiler
  - entity: sensor.hot_water_temperature
    name: Hot Water
  - entity: sensor.outdoor_temperature
    name: Outdoor
  - entity: sensor.circulation_pump_power
    name: Circulation Pump
  - entity: binary_sensor.burner_active
    name: Burner
  - entity: sensor.heating_system_ready
    name: System Status
```

### Gauge Cards for Temperatures

```yaml
type: horizontal-stack
cards:
  - type: gauge
    entity: sensor.boiler_temperature
    name: Boiler
    min: 0
    max: 100
    severity:
      green: 0
      yellow: 70
      red: 85
  
  - type: gauge
    entity: sensor.hot_water_temperature
    name: Hot Water
    min: 0
    max: 80
    severity:
      green: 40
      yellow: 60
      red: 70
```

### History Graph

```yaml
type: history-graph
title: Temperature History
entities:
  - entity: sensor.boiler_temperature
  - entity: sensor.hot_water_temperature
  - entity: sensor.outdoor_temperature
hours_to_show: 24
refresh_interval: 60
```

### Pump Status Card

```yaml
type: vertical-stack
cards:
  - type: sensor
    entity: sensor.circulation_pump_power
    graph: line
    name: Circulation Pump
    detail: 2
  
  - type: sensor
    entity: sensor.solar_pump_power
    graph: line
    name: Solar Pump
    detail: 2
```

### Custom Button Card

If using custom button card (install from HACS):

```yaml
type: custom:button-card
entity: sensor.heating_system_ready
name: Heating System
show_state: true
state:
  - value: 'Online'
    color: green
    icon: mdi:radiator
  - value: 'Offline'
    color: red
    icon: mdi:radiator-off
tap_action:
  action: url
  url_path: http://localhost:8099
```

## Automations

### Low Temperature Alert

```yaml
automation:
  - alias: "Alert: Low Boiler Temperature"
    description: "Notify when boiler temperature drops below threshold"
    trigger:
      - platform: numeric_state
        entity_id: sensor.boiler_temperature
        below: 30
        for:
          minutes: 5
    action:
      - service: notify.notify
        data:
          title: "⚠️ Heating Alert"
          message: "Boiler temperature is {{ states('sensor.boiler_temperature') }}°C"
      - service: persistent_notification.create
        data:
          title: "Low Boiler Temperature"
          message: "Check heating system - temperature below 30°C"
```

### High Temperature Warning

```yaml
automation:
  - alias: "Alert: High Boiler Temperature"
    description: "Notify when boiler overheats"
    trigger:
      - platform: numeric_state
        entity_id: sensor.boiler_temperature
        above: 90
    action:
      - service: notify.notify
        data:
          title: "🔥 HEATING WARNING"
          message: "Boiler temperature HIGH: {{ states('sensor.boiler_temperature') }}°C"
          data:
            priority: high
```

### System Offline Detection

```yaml
automation:
  - alias: "Alert: Heating System Offline"
    description: "Notify when heating system stops communicating"
    trigger:
      - platform: state
        entity_id: sensor.heating_system_ready
        to: 'Offline'
        for:
          minutes: 5
    action:
      - service: notify.notify
        data:
          title: "Heating System Offline"
          message: "No data received from Viessmann controller"
```

### Pump Running Too Long

```yaml
automation:
  - alias: "Alert: Pump Running Extended Time"
    description: "Notify if circulation pump runs continuously"
    trigger:
      - platform: numeric_state
        entity_id: sensor.circulation_pump_power
        above: 50
        for:
          hours: 4
    action:
      - service: notify.notify
        data:
          message: "Circulation pump has been running high for 4+ hours"
```

### Daily Status Report

```yaml
automation:
  - alias: "Daily Heating System Report"
    description: "Send daily summary of heating system"
    trigger:
      - platform: time
        at: "08:00:00"
    condition:
      - condition: state
        entity_id: sensor.heating_system_ready
        state: 'Online'
    action:
      - service: notify.notify
        data:
          title: "Heating System Daily Report"
          message: >
            Boiler: {{ states('sensor.boiler_temperature') }}°C
            Hot Water: {{ states('sensor.hot_water_temperature') }}°C
            Outdoor: {{ states('sensor.outdoor_temperature') }}°C
            System: {{ states('sensor.heating_system_status') }}
```

## Notifications

### Mobile App Notifications

```yaml
automation:
  - alias: "Mobile: Heating Critical Alert"
    trigger:
      - platform: numeric_state
        entity_id: sensor.boiler_temperature
        above: 95
    action:
      - service: notify.mobile_app_<your_device>
        data:
          title: "CRITICAL: Boiler Overheating"
          message: "Temperature: {{ states('sensor.boiler_temperature') }}°C"
          data:
            priority: high
            ttl: 0
            importance: high
            notification_icon: "mdi:fire-alert"
```

### Persistent Notification with Action

```yaml
automation:
  - alias: "Persistent Alert with Action"
    trigger:
      - platform: state
        entity_id: sensor.heating_system_ready
        to: 'Offline'
    action:
      - service: persistent_notification.create
        data:
          title: "Heating System Offline"
          message: "Click to view system status"
          notification_id: "heating_offline"
      - service: notify.notify
        data:
          message: "Heating system offline. Check addon logs."
```

## MQTT Bridge (Optional)

If you want to publish data to MQTT for other integrations:

### Install MQTT Add-on

1. Install "Mosquitto broker" addon from Home Assistant store
2. Configure username/password
3. Start the addon

### MQTT Automation

```yaml
automation:
  - alias: "Publish Heating Data to MQTT"
    trigger:
      - platform: state
        entity_id: sensor.viessmann_heating_system
    condition:
      - condition: template
        value_template: "{{ trigger.to_state.state != 'unavailable' }}"
    action:
      # Publish temperatures
      - service: mqtt.publish
        data:
          topic: "viessmann/boiler/temperature"
          payload: "{{ states('sensor.boiler_temperature') }}"
          retain: true
      
      - service: mqtt.publish
        data:
          topic: "viessmann/hotwater/temperature"
          payload: "{{ states('sensor.hot_water_temperature') }}"
          retain: true
      
      # Publish pump status
      - service: mqtt.publish
        data:
          topic: "viessmann/pump/circulation"
          payload: "{{ states('sensor.circulation_pump_power') }}"
          retain: true
      
      # Publish system status
      - service: mqtt.publish
        data:
          topic: "viessmann/system/status"
          payload: "{{ states('sensor.heating_system_status') }}"
          retain: true
```

## Energy Dashboard Integration

To track heating energy usage in the Energy Dashboard:

```yaml
sensor:
  - platform: integration
    source: sensor.circulation_pump_power
    name: Heating Pump Energy
    unit_prefix: k
    unit_time: h
    method: left
```

Then add this sensor to Settings → Dashboards → Energy → Add consumption.

## Tips and Best Practices

1. **Scan Interval**: Adjust `scan_interval` based on your needs (default: 10 seconds is good)
2. **Availability**: Add availability templates to handle offline states gracefully
3. **Logging**: Enable debug logging for the REST sensor if issues occur
4. **Backup**: Always backup your `configuration.yaml` before making changes
5. **Testing**: Test automations individually before enabling all at once
6. **Naming**: Use consistent naming conventions for easy identification

## Troubleshooting

### Sensor Shows "Unavailable"

1. Check addon is running: Settings → Add-ons → Viessmann Decoder
2. Test API manually: `curl http://localhost:8099/data`
3. Check addon logs for errors
4. Verify serial port configuration

### Template Errors

1. Use Developer Tools → Template to test templates
2. Check attribute names match exactly
3. Verify array indices are within bounds
4. Add error handling with default values

### Slow Updates

1. Reduce `scan_interval` if data updates too slowly
2. Increase `scan_interval` if too much load
3. Check network connectivity
4. Verify addon isn't restarting frequently

## Additional Resources

- [Home Assistant Templating](https://www.home-assistant.io/docs/configuration/templating/)
- [RESTful Sensor Documentation](https://www.home-assistant.io/integrations/rest/)
- [Template Sensor Documentation](https://www.home-assistant.io/integrations/template/)
- [Automation Documentation](https://www.home-assistant.io/docs/automation/)
