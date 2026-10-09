#ifndef VIESSMANN_ADAPTER_SETTINGS_H
#define VIESSMANN_ADAPTER_SETTINGS_H

inline const char* adapterSettingsPanel() {
    return R"ADAPTER_PANEL(
<section id="adapter-settings-panel" aria-labelledby="adapter-settings-heading">
  <style>
    #adapter-settings-panel { margin: 24px 0; padding: 20px; border: 1px solid var(--divider-color, #ccc); border-radius: 8px; background: var(--card-background, #fff); color: var(--primary-text, #212121); }
    #adapter-settings-panel .adapter-toolbar { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; }
    #adapter-settings-panel h2 { margin: 0; flex: 1; }
    #adapter-settings-panel button { cursor: pointer; padding: 8px 14px; color: var(--primary-text, #212121); background: var(--card-background, #fff); border: 1px solid var(--divider-color, #ccc); border-radius: 4px; }
    #adapter-settings-panel button:disabled { cursor: wait; opacity: .6; }
    #adapter-settings-panel .adapter-card { border: 1px solid var(--divider-color, #ccc); border-radius: 6px; padding: 12px; margin-top: 12px; overflow-wrap: anywhere; }
    #adapter-settings-panel .adapter-card h3 { margin: 0 0 8px; }
    #adapter-settings-panel .adapter-links { display: flex; gap: 16px; flex-wrap: wrap; margin-top: 8px; }
    #adapter-settings-panel .adapter-fields { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 12px; }
    #adapter-settings-panel label { display: block; margin: 8px 0 4px; }
    #adapter-settings-panel input:not([type="checkbox"]), #adapter-settings-panel select { box-sizing: border-box; width: 100%; padding: 8px; color: var(--primary-text, #212121); background: var(--card-background, #fff); border: 1px solid var(--divider-color, #ccc); }
    #adapter-settings-panel p { color: var(--secondary-text, #727272); }
    #adapter-settings-panel .adapter-actions { display: flex; gap: 12px; margin-top: 16px; }
    #adapter-settings-panel .adapter-notice { font-size: .95em; }
    #adapter-settings-panel [hidden] { display: none !important; }
    #adapter-settings-panel #adapter-settings-status { min-height: 1.4em; }
    #adapter-settings-panel #adapter-settings-status[data-error="true"] { color: #b00020; }
  </style>
  <div class="adapter-toolbar">
    <h2 id="adapter-settings-heading">Serielle Adapter</h2>
    <button type="button" id="adapter-settings-refresh">Aktualisieren</button>
    <button type="button" id="adapter-settings-add" aria-label="Seriellen Adapter hinzufügen" aria-expanded="false" aria-controls="adapter-settings-form">+</button>
  </div>
  <p>Mehrere Adapter können gleichzeitig mit eigenen Einstellungen betrieben werden. Die Einstellungen und die Fernbedienung sind für jeden Adapter separat erreichbar.</p>
  <p class="adapter-notice">Für Home Assistant jeden Adapter separat einrichten: gleicher Server und Port, aber die jeweilige API-Basis <code>/adapters/&lt;id&gt;</code> verwenden. Die API-Basis steht unten bei jedem Adapter; die Hauptadapter-Einstellungen gelten nicht für zusätzliche Adapter.</p>
  <p class="adapter-notice">Maximal 8 Adapter insgesamt (einschließlich Hauptadapter). Weitere USB-Geräte müssen bei Docker Compose unter <code>devices</code> zugeordnet werden. Beim Home-Assistant-Add-on müssen die erforderlichen Geräte-/UART-Berechtigungen vorhanden sein.</p>
  <p id="adapter-settings-status" role="status" aria-live="polite"></p>
  <div id="adapter-settings-list"></div>
  <form id="adapter-settings-form" hidden>
    <h3>Adapter hinzufügen</h3>
    <div class="adapter-fields">
      <div>
        <label for="adapter-settings-port">Serieller Gerätepfad</label>
        <input id="adapter-settings-port" name="serial_port" type="text" required pattern="/dev/.+" placeholder="/dev/serial/by-id/…" autocomplete="off">
      </div>
      <div>
        <label for="adapter-settings-protocol">Protokoll</label>
        <select id="adapter-settings-protocol" name="protocol">
          <option value="vbus">VBUS</option>
          <option value="kw">KW-Bus</option>
          <option value="p300">P300</option>
          <option value="km">KM-Bus</option>
          <option value="km_remote">KM-Bus Fernbedienung</option>
        </select>
      </div>
      <div>
        <label for="adapter-settings-baud">Baudrate</label>
        <select id="adapter-settings-baud" name="baud_rate">
          <option value="1200">1200</option>
          <option value="4800">4800</option><option value="9600" selected>9600</option>
          <option value="19200">19200</option><option value="38400">38400</option><option value="57600">57600</option>
          <option value="115200">115200</option>
        </select>
      </div>
      <div>
        <label for="adapter-settings-config">Serielle Konfiguration</label>
        <select id="adapter-settings-config" name="serial_config">
          <option value="8N1">8N1</option><option value="8E1">8E1</option><option value="8E2">8E2</option>
        </select>
      </div>
    </div>
    <label for="adapter-settings-invert"><input id="adapter-settings-invert" name="invert_serial" type="checkbox"> Serielles Signal invertieren</label>
    <div id="adapter-settings-remote-fields" hidden>
      <p>KM-Bus Fernbedienung verwendet immer 1200 Baud und 8E1.</p>
      <div class="adapter-fields">
        <div>
          <label for="adapter-settings-model">Fernbedienungsmodell</label>
          <select id="adapter-settings-model" name="remote_model">
            <option value="vitotrol200">Vitotrol 200</option><option value="vitotrol300">Vitotrol 300</option>
          </select>
        </div>
        <div>
          <label for="adapter-settings-slot">Fernbedienungs-Slot</label>
          <select id="adapter-settings-slot" name="remote_slot">
            <option value="1">1</option><option value="2">2</option><option value="3">3</option>
          </select>
        </div>
      </div>
    </div>
    <div class="adapter-actions">
      <button type="submit" id="adapter-settings-submit">Adapter hinzufügen</button>
      <button type="button" id="adapter-settings-cancel">Abbrechen</button>
    </div>
  </form>
  <script>
  (function () {
    'use strict';
    const panel = document.getElementById('adapter-settings-panel');
    const get = suffix => panel.querySelector('#adapter-settings-' + suffix);
    const form = get('form'), status = get('status'), list = get('list');
    const add = get('add'), submit = get('submit'), cancel = get('cancel');
    const protocol = get('protocol'), baud = get('baud'), config = get('config');
    // Keep the Supervisor ingress prefix on both root and adapter settings pages.
    const path = window.location.pathname;
    const suffix = /\/(?:adapters\/[A-Za-z0-9_-]+\/)?(?:settings|devices)\/?$/;
    const root = (path.replace(suffix, '') + '/').replace(/^\/+/, '/');
    const endpoint = root + 'api/adapters';
    const protocolNames = ['vbus', 'kw', 'p300', 'km', 'km_remote'];
    let pending = false;
    let loadVersion = 0;

    function message(text, error) {
      status.textContent = text;
      status.dataset.error = error ? 'true' : 'false';
    }
    function remoteMode() {
      const remote = protocol.value === 'km_remote';
      get('remote-fields').hidden = !remote;
      baud.disabled = remote;
      config.disabled = remote;
      if (remote) {
        baud.value = '1200';
        config.value = '8E1';
      }
    }
    function closeForm() {
      form.reset();
      remoteMode();
      form.hidden = true;
      add.setAttribute('aria-expanded', 'false');
      add.focus();
    }
    function textElement(tag, text) {
      const element = document.createElement(tag);
      element.textContent = text;
      return element;
    }
    function adapterBase(adapter) {
      const id = String(adapter.id === undefined ? '' : adapter.id);
      if (!/^[A-Za-z0-9_-]{1,64}$/.test(id)) return null;
      const expected = '/adapters/' + id;
      if (adapter.api_url !== undefined && adapter.api_url !== expected) return null;
      return root + expected.slice(1);
    }
    function render(adapters) {
      list.replaceChildren();
      if (!adapters.length) {
        list.appendChild(textElement('p', 'Keine seriellen Adapter verfügbar.'));
      }
      adapters.forEach(adapter => {
        if (!adapter || typeof adapter !== 'object') return;
        const card = textElement('article', '');
        card.className = 'adapter-card';
        const base = adapterBase(adapter);
        const rawProtocol = adapter.protocol;
        const name = typeof rawProtocol === 'number' ? protocolNames[rawProtocol] : rawProtocol;
        card.appendChild(textElement('h3', String(adapter.name || ('Adapter ' + adapter.id))));
        card.appendChild(textElement('p', String(adapter.serial_port || '') + ' · ' +
          String(name || 'unbekannt').toUpperCase() + ' · ' +
          String(adapter.baud_rate || '') + ' Baud · ' + String(adapter.serial_config || '')));
        card.appendChild(textElement('p', 'Seriell: ' +
          (adapter.serialConnected ? 'verbunden' : 'nicht verbunden') + ' · Status: ' +
          (adapter.ready ? 'bereit' : 'nicht bereit')));
        if (base) {
          const links = textElement('div', '');
          links.className = 'adapter-links';
          [['Einstellungen', '/settings'], ['Fernbedienung', '/remote'], ['API-Basis: /adapters/' + adapter.id, '/']].forEach(item => {
            const link = textElement('a', item[0]);
            link.href = base + item[1];
            links.appendChild(link);
          });
          card.appendChild(links);
        } else {
          card.appendChild(textElement('p', 'Ungültige Adapter-Adresse; Links nicht verfügbar.'));
        }
        list.appendChild(card);
      });
    }
    async function loadAdapters() {
      const version = ++loadVersion;
      get('refresh').disabled = true;
      try {
        const response = await fetch(endpoint, { headers: { 'Accept': 'application/json' } });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json();
        if (!data || !Array.isArray(data.adapters)) throw new Error('invalid response');
        if (version !== loadVersion) return false;
        render(data.adapters);
        return true;
      } catch (error) {
        if (version === loadVersion) message('Adapterliste konnte nicht geladen werden. Bitte erneut aktualisieren.', true);
        return false;
      } finally {
        if (version === loadVersion) get('refresh').disabled = false;
      }
    }
    add.addEventListener('click', function () {
      form.hidden = false;
      add.setAttribute('aria-expanded', 'true');
      get('port').focus();
    });
    cancel.addEventListener('click', closeForm);
    protocol.addEventListener('change', remoteMode);
    get('refresh').addEventListener('click', async function () {
      if (await loadAdapters()) message('Adapterliste aktualisiert.', false);
    });
    form.addEventListener('submit', async function (event) {
      event.preventDefault();
      if (pending || !form.reportValidity()) return;
      const port = get('port').value.trim();
      if (!/^\/dev\/.+/.test(port)) {
        message('Bitte einen absoluten Gerätepfad unter /dev/ eingeben.', true);
        get('port').focus();
        return;
      }
      remoteMode();
      const payload = {
        serial_port: port,
        protocol: protocol.value,
        baud_rate: Number(baud.value),
        serial_config: config.value,
        invert_serial: get('invert').checked,
        remote_model: get('model').value,
        remote_slot: Number(get('slot').value)
      };
      pending = true;
      submit.disabled = true;
      cancel.disabled = true;
      add.disabled = true;
      message('Adapter wird hinzugefügt …', false);
      try {
        const response = await fetch(endpoint, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
          body: JSON.stringify(payload)
        });
        if (response.status !== 201) {
          const errors = {
            400: 'Ungültige Einstellungen. Bitte Gerätepfad und Konfiguration prüfen.',
            409: 'Dieser serielle Gerätepfad wird bereits von einem Adapter verwendet.',
            503: 'Kein weiterer Adapter verfügbar: Das Limit (maximal 8) ist erreicht oder der Adapter konnte nicht gestartet werden.'
          };
          message(errors[response.status] || 'Adapter konnte nicht hinzugefügt werden. Bitte erneut versuchen.', true);
          return;
        }
        closeForm();
        if (await loadAdapters()) message('Adapter hinzugefügt. Seine Einstellungen können über „Einstellungen“ separat bearbeitet werden.', false);
      } catch (error) {
        message('Verbindungsfehler. Bitte die Adapterliste aktualisieren, bevor Sie erneut hinzufügen.', true);
      } finally {
        pending = false;
        submit.disabled = false;
        cancel.disabled = false;
        add.disabled = false;
      }
    });
    remoteMode();
    loadAdapters();
  }());
  </script>
</section>
)ADAPTER_PANEL";
}

#endif
