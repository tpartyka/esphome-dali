// See esphome_dali.h for why <esphome.h> must be included before our own headers:
// it triggers ESPHome's alphabetical component-header traversal, which must reach
// esphome_dali.h's full DaliBusComponent definition before any other component
// header (e.g. esphome_dali_input_trigger.h, esphome_dali_light.h) references it.
#include <esphome.h>
#include "esphome_dali_web.h"

#ifdef DALI_WEB_DASHBOARD_ENABLED

#include "esphome_dali.h"
#include "esphome/components/json/json_util.h"
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cctype>

using namespace esphome;
using namespace esphome::dali;

namespace {

bool parse_int_strict(const std::string &text, int min_value, int max_value, int &value) {
    if (text.empty()) return false;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    errno = 0;
    char *end = nullptr;
    long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' || parsed < min_value || parsed > max_value) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

// Single-page "cockpit" dashboard: lamp + group cards with direct control
// (on/off, brightness, color temp), inline rename, group membership, scenes,
// discovery and a live bus diagnostics/event log. Vanilla JS, no build step.
// Polls /api/lamps every 3s, /api/names on load + after rename, /api/log
// every 7s.
static const char DASHBOARD_HTML[] = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>DALI Cockpit</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
         margin: 0; padding: 1rem; max-width: 1200px; margin-inline: auto;
         background: #0d1117; color: #e6edf3; }
  h1 { font-size: 1.4rem; margin: 0; }
  h2 { font-size: 1.05rem; margin: 0 0 0.6rem; }
  a { color: #58a6ff; }
  header { display: flex; align-items: center; justify-content: space-between;
           flex-wrap: wrap; gap: 0.5rem; margin-bottom: 0.75rem; }
  .pill { display: inline-flex; align-items: center; gap: 0.35rem; font-size: 0.8rem;
          padding: 0.2rem 0.6rem; border-radius: 999px; background: #161b22; border: 1px solid #30363d; }
  .dot { display: inline-block; width: 0.6rem; height: 0.6rem; border-radius: 50%; flex: none; }
  .dot.online { background: #2ecc71; }
  .dot.offline { background: #999; }
  .dot.problem { background: #e74c3c; }
  .dot.warn { background: #f1c40f; }
  section { margin-top: 1.25rem; }
  .panel { border: 1px solid #30363d; border-radius: 8px; padding: 0.75rem 1rem;
           background: #161b22; }
  .grid { display: grid; gap: 0.6rem; grid-template-columns: repeat(auto-fill, minmax(230px, 1fr)); }
  .card { border: 1px solid #30363d; border-radius: 8px; padding: 0.6rem 0.75rem; background: #0d1117; }
  .card-head { display: flex; align-items: center; gap: 0.4rem; margin-bottom: 0.4rem; }
  .card-head .name { flex: 1; min-width: 0; }
  .name-input { background: transparent; border: 1px solid transparent; color: inherit;
                 font-size: 0.95rem; font-weight: 600; width: 100%; padding: 0.1rem 0.2rem;
                 border-radius: 4px; }
  .name-input:hover, .name-input:focus { border-color: #30363d; background: #161b22; }
  .addr { font-size: 0.75rem; color: #8b949e; }
  .ctl-row { display: flex; align-items: center; gap: 0.5rem; margin: 0.3rem 0; }
  .ctl-row label { font-size: 0.75rem; color: #8b949e; width: 3.4rem; flex: none; }
  .ctl-row input[type=range] { flex: 1; }
  .ctl-row .val { font-size: 0.75rem; color: #8b949e; width: 3rem; text-align: right; flex: none; }
  .switch { position: relative; display: inline-block; width: 2.2rem; height: 1.25rem; flex: none; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider-switch { position: absolute; inset: 0; background: #30363d; border-radius: 999px;
                    cursor: pointer; transition: background 0.15s; }
  .slider-switch::before { content: ""; position: absolute; width: 0.95rem; height: 0.95rem; left: 0.15rem;
                             bottom: 0.15rem; background: #e6edf3; border-radius: 50%; transition: transform 0.15s; }
  .switch input:checked + .slider-switch { background: #2ecc71; }
  .switch input:checked + .slider-switch::before { transform: translateX(0.95rem); }
  .groups { display: flex; flex-wrap: wrap; gap: 2px; margin-top: 0.4rem; }
  .grp { width: 1.6rem; height: 1.6rem; line-height: 1.6rem; text-align: center; font-size: 0.75rem;
         border: 1px solid #30363d; border-radius: 4px; cursor: pointer; user-select: none;
         background: transparent; color: inherit; }
  .grp.member { background: #3498db; color: #fff; border-color: #3498db; }
  select, button, input[type=text] { font-size: 0.85rem; padding: 0.3rem 0.6rem; border-radius: 4px;
          border: 1px solid #30363d; background: #0d1117; color: inherit; }
  button { cursor: pointer; }
  button:hover { border-color: #58a6ff; }
  button.danger:hover { border-color: #e74c3c; }
  .row { display: flex; align-items: center; gap: 0.5rem; flex-wrap: wrap; }
  .scenes-table { width: 100%; border-collapse: collapse; font-size: 0.85rem; }
  .scenes-table th, .scenes-table td { text-align: left; padding: 0.3rem 0.4rem;
          border-bottom: 1px solid #21262d; vertical-align: middle; }
  .scenes-table input[type=text] { width: 100%; }
  .counters { display: flex; gap: 1rem; flex-wrap: wrap; margin-bottom: 0.6rem; font-size: 0.85rem; }
  .counters .c { display: flex; align-items: center; gap: 0.35rem; }
  .counters b { font-size: 1rem; }
  .log-table { width: 100%; border-collapse: collapse; font-size: 0.8rem; max-height: 260px; }
  .log-table th, .log-table td { text-align: left; padding: 0.2rem 0.4rem; border-bottom: 1px solid #21262d; }
  .log-wrap { max-height: 260px; overflow-y: auto; }
  #status { font-size: 0.8rem; color: #8b949e; margin-top: 0.75rem; }
  .empty { color: #8b949e; font-size: 0.85rem; }
</style>
</head>
<body>
<header>
  <h1>DALI Cockpit</h1>
  <div class="row">
    <span class="pill"><span class="dot online" id="bus-dot"></span><span id="bus-text">-</span></span>
    <span class="pill" id="last-refresh">-</span>
  </div>
</header>

<section>
  <h2>Lamps</h2>
  <div class="grid" id="lamps-grid"></div>
</section>

<section>
  <h2>Groups</h2>
  <div class="grid" id="groups-grid"></div>
</section>

<section>
  <h2>Scenes</h2>
  <div class="panel">
    <table class="scenes-table">
      <thead><tr><th>Scene</th><th>Name</th><th>Target</th><th></th></tr></thead>
      <tbody id="scenes-body"></tbody>
    </table>
  </div>
</section>

<section>
  <h2>Discovery</h2>
  <div class="panel row">
    <button onclick="discovery('discover')">Scan bus</button>
    <button onclick="discovery('unassigned')">Initialize new devices</button>
    <button class="danger" onclick="discoveryAll()">Initialize ALL (reassign addresses)</button>
  </div>
</section>

<section>
  <h2>Diagnostics &amp; Event Log</h2>
  <div class="panel">
    <div class="counters" id="log-counters"></div>
    <div class="log-wrap">
      <table class="log-table">
        <thead><tr><th>Age</th><th>Event</th><th>Addr</th></tr></thead>
        <tbody id="log-body"></tbody>
      </table>
    </div>
  </div>
</section>

<div id="status"></div>

<script>
const lampsGrid = document.getElementById('lamps-grid');
const groupsGrid = document.getElementById('groups-grid');
const scenesBody = document.getElementById('scenes-body');
const logCounters = document.getElementById('log-counters');
const logBody = document.getElementById('log-body');
const busDot = document.getElementById('bus-dot');
const busText = document.getElementById('bus-text');
const lastRefresh = document.getElementById('last-refresh');
const status = document.getElementById('status');

let names = { lamps: {}, groups: {}, scenes: {} };
let lastLamps = [];
const debounceTimers = {};

function esc(s) {
  return String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}

// --- names -----------------------------------------------------------

async function loadNames() {
  try {
    const res = await fetch('/api/names');
    names = await res.json();
    names.lamps = names.lamps || {};
    names.groups = names.groups || {};
    names.scenes = names.scenes || {};
  } catch (e) { /* keep previous names on failure */ }
}

async function rename(kind, index, value) {
  await fetch(`/api/names?kind=${kind}&index=${index}&name=${encodeURIComponent(value)}`, { method: 'POST' });
  await loadNames();
}

function nameInput(kind, index, current, placeholder) {
  const input = document.createElement('input');
  input.type = 'text';
  input.className = 'name-input';
  input.value = current || '';
  input.placeholder = placeholder;
  input.maxLength = 15;
  input.addEventListener('change', () => rename(kind, index, input.value));
  input.addEventListener('keydown', e => { if (e.key === 'Enter') input.blur(); });
  return input;
}

// --- targets (shared by scenes) --------------------------------------

function targetOptions(selected, lamps) {
  const opts = [['all', 'All lamps (broadcast)']];
  for (let g = 0; g < 16; g++) opts.push(['group:' + g, 'Group ' + g + (names.groups[g] ? ' - ' + names.groups[g] : '')]);
  for (const l of lamps) opts.push(['lamp:' + l.addr, 'Lamp ' + l.addr + (l.name ? ' - ' + l.name : '')]);
  return opts.map(([v, label]) =>
    `<option value="${v}"${v === selected ? ' selected' : ''}>${esc(label)}</option>`).join('');
}

// --- lamp / group control cards ---------------------------------------

function controlCard(opts) {
  const card = document.createElement('div');
  card.className = 'card';

  const head = document.createElement('div');
  head.className = 'card-head';
  const dot = document.createElement('span');
  dot.className = 'dot ' + opts.dotClass;
  dot.title = opts.statusText;
  head.appendChild(dot);
  head.appendChild(nameInput(opts.nameKind, opts.nameIndex, opts.currentName, opts.placeholder));
  const addr = document.createElement('span');
  addr.className = 'addr';
  addr.textContent = opts.addrLabel;
  head.appendChild(addr);
  card.appendChild(head);

  // On/off
  const onRow = document.createElement('div');
  onRow.className = 'ctl-row';
  const onLabel = document.createElement('label');
  onLabel.textContent = 'Power';
  onRow.appendChild(onLabel);
  const sw = document.createElement('label');
  sw.className = 'switch';
  const cb = document.createElement('input');
  cb.type = 'checkbox';
  cb.checked = !!opts.on;
  cb.onchange = () => sendControl(opts.target, { on: cb.checked ? 1 : 0 });
  sw.appendChild(cb);
  const slider = document.createElement('span');
  slider.className = 'slider-switch';
  sw.appendChild(slider);
  onRow.appendChild(sw);
  card.appendChild(onRow);

  // Brightness
  const brRow = document.createElement('div');
  brRow.className = 'ctl-row';
  const brLabel = document.createElement('label');
  brLabel.textContent = 'Bright';
  brRow.appendChild(brLabel);
  const brInput = document.createElement('input');
  brInput.type = 'range';
  brInput.min = 0; brInput.max = 100;
  brInput.value = opts.brightness_pct || 0;
  const brVal = document.createElement('span');
  brVal.className = 'val';
  brVal.textContent = brInput.value + '%';
  brInput.oninput = () => {
    brVal.textContent = brInput.value + '%';
    debounce('br-' + opts.target, () => sendControl(opts.target, { brightness_pct: brInput.value }));
  };
  brInput.onchange = () => sendControl(opts.target, { brightness_pct: brInput.value });
  brRow.appendChild(brInput);
  brRow.appendChild(brVal);
  card.appendChild(brRow);

  // Color temperature
  if (opts.color_temp_mireds !== null && opts.color_temp_mireds !== undefined) {
    const ctRow = document.createElement('div');
    ctRow.className = 'ctl-row';
    const ctLabel = document.createElement('label');
    ctLabel.textContent = 'CT';
    ctRow.appendChild(ctLabel);
    const ctInput = document.createElement('input');
    ctInput.type = 'range';
    ctInput.min = opts.color_temp_min_mireds; ctInput.max = opts.color_temp_max_mireds;
    ctInput.value = opts.color_temp_mireds;
    const ctVal = document.createElement('span');
    ctVal.className = 'val';
    ctVal.textContent = ctInput.value + 'mk';
    ctInput.oninput = () => {
      ctVal.textContent = ctInput.value + 'mk';
      debounce('ct-' + opts.target, () => sendControl(opts.target, { color_temp_mireds: ctInput.value }));
    };
    ctInput.onchange = () => sendControl(opts.target, { color_temp_mireds: ctInput.value });
    ctRow.appendChild(ctInput);
    ctRow.appendChild(ctVal);
    card.appendChild(ctRow);
  }

  // Circadian (read-only: toggled via the "DALI Group N Circadian" HA switch,
  // or auto-disabled here on a manual CT change above).
  if (opts.circadian_supported) {
    const circRow = document.createElement('div');
    circRow.className = 'ctl-row';
    const circLabel = document.createElement('label');
    circLabel.textContent = 'Circadian';
    circRow.appendChild(circLabel);
    const circVal = document.createElement('span');
    circVal.className = 'val';
    circVal.textContent = opts.circadian_enabled ? 'On' : 'Off';
    circRow.appendChild(circVal);
    card.appendChild(circRow);
  }

  if (opts.extra) card.appendChild(opts.extra);

  return card;
}

function debounce(key, fn, ms = 200) {
  clearTimeout(debounceTimers[key]);
  debounceTimers[key] = setTimeout(fn, ms);
}

async function sendControl(target, params) {
  const qs = new URLSearchParams(Object.assign({ target }, params)).toString();
  await fetch(`/api/lamp?${qs}`, { method: 'POST' });
  setTimeout(refresh, 300);
}

// --- lamps -------------------------------------------------------------

function renderLamps(lamps) {
  lampsGrid.innerHTML = '';
  if (lamps.length === 0) {
    lampsGrid.innerHTML = '<div class="empty">No lamps discovered yet. Use Discovery below.</div>';
    return;
  }
  for (const l of lamps) {
    const dotClass = l.problem ? 'problem' : (l.online ? 'online' : 'offline');
    const statusText = l.problem ? 'Problem' : (l.online ? 'Online' : 'Offline');
    const groupsDiv = document.createElement('div');
    groupsDiv.className = 'groups';
    for (let g = 0; g < 16; g++) {
      const btn = document.createElement('button');
      const isMember = (l.groups || []).includes(g);
      btn.className = 'grp' + (isMember ? ' member' : '');
      btn.textContent = g;
      btn.title = (isMember ? 'Remove from' : 'Add to') + ' group ' + g;
      btn.onclick = () => groupAction(l.addr, g, isMember ? 'remove' : 'add');
      groupsDiv.appendChild(btn);
    }
    const identifyBtn = document.createElement('button');
    identifyBtn.textContent = 'Identify';
    identifyBtn.title = 'Blink this lamp to identify it';
    identifyBtn.onclick = () => identify(l.addr);

    const extra = document.createElement('div');
    extra.appendChild(groupsDiv);
    const idRow = document.createElement('div');
    idRow.className = 'ctl-row';
    idRow.appendChild(identifyBtn);
    extra.appendChild(idRow);

    const card = controlCard({
      nameKind: 'lamp', nameIndex: l.addr, currentName: l.name, placeholder: 'Lamp ' + l.addr,
      addrLabel: '#' + l.addr, dotClass, statusText,
      target: 'lamp:' + l.addr, on: l.on, brightness_pct: l.brightness_pct,
      color_temp_mireds: l.color_temp_mireds,
      color_temp_min_mireds: l.color_temp_min_mireds, color_temp_max_mireds: l.color_temp_max_mireds,
      extra,
    });
    lampsGrid.appendChild(card);
  }
}

// --- groups --------------------------------------------------------------

function renderGroups(groups, lamps) {
  groupsGrid.innerHTML = '';
  if (!groups || groups.length === 0) {
    groupsGrid.innerHTML = '<div class="empty">No groups created yet. Assign a lamp to a group below.</div>';
    return;
  }
  for (const g of groups) {
    const members = lamps.filter(l => (l.groups || []).includes(g.group)).map(l => l.addr);
    const membersDiv = document.createElement('div');
    membersDiv.className = 'addr';
    membersDiv.style.marginTop = '0.4rem';
    membersDiv.textContent = members.length ? ('Members: ' + members.join(', ')) : 'No members';

    const card = controlCard({
      nameKind: 'group', nameIndex: g.group, currentName: g.name, placeholder: 'Group ' + g.group,
      addrLabel: 'Grp ' + g.group, dotClass: g.on ? 'online' : 'offline',
      statusText: g.on ? 'On' : 'Off',
      target: 'group:' + g.group, on: g.on, brightness_pct: g.brightness_pct,
      color_temp_mireds: g.color_temp_mireds,
      color_temp_min_mireds: g.color_temp_min_mireds, color_temp_max_mireds: g.color_temp_max_mireds,
      circadian_supported: g.circadian_supported, circadian_enabled: g.circadian_enabled,
      extra: membersDiv,
    });
    groupsGrid.appendChild(card);
  }
}

async function groupAction(addr, group, action) {
  await fetch(`/api/group?addr=${addr}&group=${group}&action=${action}`, { method: 'POST' });
  setTimeout(refresh, 300);
}

async function identify(addr) {
  await fetch(`/api/identify?addr=${addr}`, { method: 'POST' });
}

// --- scenes ----------------------------------------------------------

function renderScenes(lamps) {
  scenesBody.innerHTML = '';
  for (let s = 0; s < 16; s++) {
    const tr = document.createElement('tr');

    const tdNum = document.createElement('td');
    tdNum.textContent = s;
    tr.appendChild(tdNum);

    const tdName = document.createElement('td');
    tdName.appendChild(nameInput('scene', s, names.scenes[s], 'Scene ' + s));
    tr.appendChild(tdName);

    const tdTarget = document.createElement('td');
    const sel = document.createElement('select');
    sel.innerHTML = targetOptions('all', lamps);
    sel.dataset.scene = s;
    tdTarget.appendChild(sel);
    tr.appendChild(tdTarget);

    const tdActions = document.createElement('td');
    const recallBtn = document.createElement('button');
    recallBtn.textContent = 'Recall';
    recallBtn.onclick = () => sceneAction(s, sel.value, 'recall');
    const storeBtn = document.createElement('button');
    storeBtn.textContent = 'Store';
    storeBtn.title = 'Store current state of target as this scene';
    storeBtn.onclick = () => sceneAction(s, sel.value, 'store');
    const removeBtn = document.createElement('button');
    removeBtn.textContent = 'Remove';
    removeBtn.className = 'danger';
    removeBtn.onclick = () => sceneAction(s, sel.value, 'remove');
    tdActions.className = 'row';
    tdActions.appendChild(recallBtn);
    tdActions.appendChild(storeBtn);
    tdActions.appendChild(removeBtn);
    tr.appendChild(tdActions);

    scenesBody.appendChild(tr);
  }
}

async function sceneAction(scene, target, action) {
  status.textContent = 'Sending...';
  await fetch(`/api/scene?scene=${scene}&target=${target}&action=${action}`, { method: 'POST' });
  status.textContent = 'Done.';
  setTimeout(refresh, 300);
}

// --- discovery ---------------------------------------------------------

async function discovery(mode) {
  status.textContent = 'Running discovery...';
  await fetch(`/api/discovery?mode=${mode}`, { method: 'POST' });
  status.textContent = 'Discovery queued.';
  setTimeout(refresh, 1000);
}

async function discoveryAll() {
  if (!confirm('This re-initializes ALL short addresses on the bus and may ' +
                'temporarily disrupt every lamp. Continue?')) return;
  status.textContent = 'Running full re-initialization...';
  await fetch('/api/discovery?mode=all&confirm=yes', { method: 'POST' });
  status.textContent = 'Full discovery queued.';
  setTimeout(refresh, 1000);
}

// --- diagnostics / log --------------------------------------------------

const EVENT_LABELS = {
  bus_up: 'Bus online', bus_down: 'Bus down', collision: 'Collision',
  disconnected: 'Bus disconnected', reconnected: 'Bus reconnected',
  lamp_available: 'Lamp available', lamp_unavailable: 'Lamp unavailable',
  lamp_problem: 'Lamp problem', lamp_ok: 'Lamp OK',
};

function renderLog(data) {
  busDot.className = 'dot ' + (data.counters.disconnected ? 'problem' : (data.counters.online ? 'online' : 'warn'));
  busText.textContent = data.counters.disconnected ? 'Disconnected'
      : (data.counters.online ? 'Bus online' : 'Bus down');

  logCounters.innerHTML = '';
  const addCounter = (label, value) => {
    const c = document.createElement('div');
    c.className = 'c';
    c.innerHTML = `<b>${esc(value)}</b><span>${esc(label)}</span>`;
    logCounters.appendChild(c);
  };
  addCounter('Errors', data.counters.errors);
  addCounter('Bus down events', data.counters.bus_down);
  addCounter('Collisions', data.counters.collisions);

  logBody.innerHTML = '';
  const events = (data.events || []).slice().reverse();
  if (events.length === 0) {
    logBody.innerHTML = '<tr><td colspan="3" class="empty">No events recorded since boot.</td></tr>';
    return;
  }
  for (const e of events) {
    const tr = document.createElement('tr');
    const age = Math.max(0, Math.round((data.now_ms - e.ts) / 1000));
    const tdAge = document.createElement('td');
    tdAge.textContent = age < 60 ? (age + 's ago') : (Math.round(age / 60) + 'm ago');
    const tdType = document.createElement('td');
    tdType.textContent = EVENT_LABELS[e.type] || e.type;
    const tdAddr = document.createElement('td');
    tdAddr.textContent = (e.addr === null || e.addr === undefined) ? '-' : e.addr;
    tr.appendChild(tdAge);
    tr.appendChild(tdType);
    tr.appendChild(tdAddr);
    logBody.appendChild(tr);
  }
}

async function refreshLog() {
  try {
    const res = await fetch('/api/log');
    renderLog(await res.json());
  } catch (e) { /* keep previous log on failure */ }
}

// --- main refresh --------------------------------------------------------

async function refresh() {
  try {
    const res = await fetch('/api/lamps');
    const data = await res.json();
    lastLamps = data.lamps || [];
    renderLamps(lastLamps);
    renderGroups(data.groups, lastLamps);
    renderScenes(lastLamps);
    lastRefresh.textContent = 'Updated ' + new Date().toLocaleTimeString();
    status.textContent = '';
  } catch (e) {
    status.textContent = 'Failed to refresh: ' + e;
  }
}

async function fullRefresh() {
  await loadNames();
  await refresh();
}

fullRefresh();
refreshLog();
setInterval(refresh, 3000);
setInterval(refreshLog, 7000);
</script>
</body>
</html>
)html";

}  // namespace

namespace esphome {
namespace dali {

void DaliWebDashboard::begin(web_server_base::WebServerBase* server_base, DaliBusComponent* bus) {
    bus_ = bus;
    server_base->add_handler(this);
}

// NOTE: ESPHome's web_server_idf AsyncWebServerRequest::init_response_() only maps
// status codes 200/404/409 to their real HTTP status lines; any other code (202,
// 400, 503, ...) is sent to the client as a 500 even though the body is correct.
// POST handlers below therefore only use 200 (success/queued) and 409
// (validation error / busy) so clients see an accurate status.
bool DaliWebDashboard::canHandle(AsyncWebServerRequest* request) const {
    return true;
}

void DaliWebDashboard::handleRequest(AsyncWebServerRequest* request) {
    char buf[AsyncWebServerRequest::URL_BUF_SIZE];
    std::string path = request->url_to(buf).str();
    http_method method = request->method();

    if (path == "/" && method == HTTP_GET) {
        handle_index_(request);
    } else if (path == "/api/lamps" && method == HTTP_GET) {
        handle_lamps_(request);
    } else if (path == "/api/group" && method == HTTP_POST) {
        handle_group_action_(request);
    } else if (path == "/api/scene" && method == HTTP_POST) {
        handle_scene_action_(request);
    } else if (path == "/api/identify" && method == HTTP_POST) {
        handle_identify_action_(request);
    } else if (path == "/api/lamp" && method == HTTP_POST) {
        handle_lamp_control_(request);
    } else if (path == "/api/names" && method == HTTP_GET) {
        handle_names_get_(request);
    } else if (path == "/api/names" && method == HTTP_POST) {
        handle_names_set_(request);
    } else if (path == "/api/discovery" && method == HTTP_POST) {
        handle_discovery_(request);
    } else if (path == "/api/log" && method == HTTP_GET) {
        handle_log_(request);
    } else {
        request->send(404, "text/plain", "Not Found");
    }
}

void DaliWebDashboard::handle_index_(AsyncWebServerRequest* request) {
    request->send(200, "text/html", DASHBOARD_HTML);
}

void DaliWebDashboard::handle_lamps_(AsyncWebServerRequest* request) {
    std::string body = json::build_json([this](JsonObject root) {
        JsonArray lamps = root["lamps"].to<JsonArray>();
        for (size_t i = 0; i < bus_->lamp_count(); i++) {
            auto info = bus_->lamp_info(i);
            JsonObject lamp = lamps.add<JsonObject>();
            lamp["addr"] = info.addr;
            lamp["name"] = bus_->lamp_name(info.addr);
            lamp["online"] = info.online;
            lamp["problem"] = info.problem;
            lamp["on"] = info.on;
            lamp["brightness_pct"] = info.brightness_pct;
            if (info.has_color_temp) {
                lamp["color_temp_mireds"] = info.color_temp_mireds;
                lamp["color_temp_min_mireds"] = info.color_temp_min_mireds;
                lamp["color_temp_max_mireds"] = info.color_temp_max_mireds;
            } else {
                lamp["color_temp_mireds"] = nullptr;
            }
            JsonArray groups = lamp["groups"].to<JsonArray>();
            for (uint8_t g = 0; g < 16; g++) {
                if (info.groups & (uint16_t) (1u << g)) groups.add(g);
            }
        }

        JsonArray groups = root["groups"].to<JsonArray>();
        for (uint8_t g = 0; g < 16; g++) {
            DaliLight* gl = bus_->group_light(g);
            if (gl == nullptr) continue;
            JsonObject jg = groups.add<JsonObject>();
            jg["group"] = g;
            jg["name"] = bus_->group_name(g);
            const auto& rv = gl->remote_values();
            jg["on"] = rv.is_on();
            jg["brightness_pct"] = (uint8_t) lroundf(rv.get_brightness() * 100.0f);
            if (gl->supports_color_temp()) {
                jg["color_temp_mireds"] = gl->current_color_temp_mired();
                jg["color_temp_min_mireds"] = gl->min_mireds();
                jg["color_temp_max_mireds"] = gl->max_mireds();
            } else {
                jg["color_temp_mireds"] = nullptr;
            }
            jg["circadian_supported"] = bus_->circadian_configured(g);
            if (bus_->circadian_configured(g)) jg["circadian_enabled"] = bus_->circadian_enabled(g);
        }
    });
    request->send(200, "application/json", body.c_str());
}

void DaliWebDashboard::handle_group_action_(AsyncWebServerRequest* request) {
    if (!request->hasArg("addr") || !request->hasArg("group") || !request->hasArg("action")) {
        request->send(409, "text/plain", "missing addr/group/action");
        return;
    }
    int addr;
    int group;
    std::string action = request->arg("action");
    if (!parse_int_strict(request->arg("addr"), 0, ADDR_SHORT_MAX, addr) ||
        !parse_int_strict(request->arg("group"), 0, 15, group)) {
        request->send(409, "text/plain", "addr/group out of range");
        return;
    }
    if (action != "add" && action != "remove") {
        request->send(409, "text/plain", "action must be add or remove");
        return;
    }

    auto* pending = new DaliPendingAction{};
    pending->kind = (action == "add") ? DaliPendingAction::Kind::GroupAdd : DaliPendingAction::Kind::GroupRemove;
    pending->target_addr = (uint8_t) addr;
    pending->value = (uint8_t) group;
    if (!queue_.push(pending)) {
        delete pending;
        request->send(409, "text/plain", "busy, try again");
        return;
    }
    request->send(200, "text/plain", "queued");
}

void DaliWebDashboard::handle_scene_action_(AsyncWebServerRequest* request) {
    if (!request->hasArg("scene") || !request->hasArg("target") || !request->hasArg("action")) {
        request->send(409, "text/plain", "missing scene/target/action");
        return;
    }
    int scene;
    if (!parse_int_strict(request->arg("scene"), 0, 15, scene)) {
        request->send(409, "text/plain", "scene out of range");
        return;
    }

    std::string target = request->arg("target");
    uint8_t target_addr;
    if (target == "all") {
        target_addr = ADDR_BROADCAST;
    } else if (target.rfind("group:", 0) == 0) {
        int g;
        if (!parse_int_strict(target.substr(6), 0, 15, g)) {
            request->send(409, "text/plain", "bad group target");
            return;
        }
        target_addr = static_cast<uint8_t>(ADDR_GROUP | g);
    } else if (target.rfind("lamp:", 0) == 0) {
        int a;
        if (!parse_int_strict(target.substr(5), 0, ADDR_SHORT_MAX, a)) {
            request->send(409, "text/plain", "bad lamp target");
            return;
        }
        target_addr = static_cast<uint8_t>(a);
    } else {
        request->send(409, "text/plain", "bad target");
        return;
    }

    DaliPendingAction::Kind kind;
    std::string action = request->arg("action");
    if (action == "recall") kind = DaliPendingAction::Kind::SceneRecall;
    else if (action == "store") kind = DaliPendingAction::Kind::SceneStore;
    else if (action == "remove") kind = DaliPendingAction::Kind::SceneRemove;
    else {
        request->send(409, "text/plain", "bad action");
        return;
    }

    auto* pending = new DaliPendingAction{kind, target_addr, (uint8_t) scene};
    if (!queue_.push(pending)) {
        delete pending;
        request->send(409, "text/plain", "busy, try again");
        return;
    }
    request->send(200, "text/plain", "queued");
}

void DaliWebDashboard::handle_identify_action_(AsyncWebServerRequest* request) {
    if (!request->hasArg("addr")) {
        request->send(409, "text/plain", "missing addr");
        return;
    }
    int addr;
    if (!parse_int_strict(request->arg("addr"), 0, ADDR_SHORT_MAX, addr)) {
        request->send(409, "text/plain", "addr out of range");
        return;
    }

    auto* pending = new DaliPendingAction{DaliPendingAction::Kind::Identify, (uint8_t) addr, 0};
    if (!queue_.push(pending)) {
        delete pending;
        request->send(409, "text/plain", "busy, try again");
        return;
    }
    request->send(200, "text/plain", "queued");
}

void DaliWebDashboard::handle_lamp_control_(AsyncWebServerRequest* request) {
    if (!request->hasArg("target")) {
        request->send(409, "text/plain", "missing target");
        return;
    }
    std::string target = request->arg("target");
    uint8_t target_addr;
    int parsed_target;
    if (target.rfind("group:", 0) == 0) {
        if (!parse_int_strict(target.substr(6), 0, 15, parsed_target) ||
            bus_->group_light(static_cast<uint8_t>(parsed_target)) == nullptr) {
            request->send(409, "text/plain", "bad group target");
            return;
        }
        target_addr = static_cast<uint8_t>(ADDR_GROUP | parsed_target);
    } else if (target.rfind("lamp:", 0) == 0) {
        if (!parse_int_strict(target.substr(5), 0, ADDR_SHORT_MAX, parsed_target)) {
            request->send(409, "text/plain", "bad lamp target");
            return;
        }
        target_addr = static_cast<uint8_t>(parsed_target);
    } else {
        request->send(409, "text/plain", "bad target");
        return;
    }

    auto* pending = new DaliPendingAction{};
    pending->kind = DaliPendingAction::Kind::LampControl;
    pending->target_addr = target_addr;

    if (request->hasArg("on")) {
        int on_value;
        if (!parse_int_strict(request->arg("on"), 0, 1, on_value)) {
            delete pending;
            request->send(409, "text/plain", "on must be 0 or 1");
            return;
        }
        pending->has_on = true;
        pending->on = on_value != 0;
    }
    if (request->hasArg("brightness_pct")) {
        int b;
        if (!parse_int_strict(request->arg("brightness_pct"), 0, 100, b)) {
            delete pending;
            request->send(409, "text/plain", "brightness_pct out of range");
            return;
        }
        pending->has_brightness = true;
        pending->brightness_pct = static_cast<uint8_t>(b);
    }
    if (request->hasArg("color_temp_mireds")) {
        int ct;
        if (!parse_int_strict(request->arg("color_temp_mireds"), 0, UINT16_MAX, ct)) {
            delete pending;
            request->send(409, "text/plain", "color_temp_mireds out of range");
            return;
        }
        pending->has_color_temp = true;
        pending->color_temp_mireds = static_cast<uint16_t>(ct);
    }

    if (!pending->has_on && !pending->has_brightness && !pending->has_color_temp) {
        delete pending;
        request->send(409, "text/plain", "no control fields given");
        return;
    }

    if (!queue_.push(pending)) {
        delete pending;
        request->send(409, "text/plain", "busy, try again");
        return;
    }
    request->send(200, "text/plain", "queued");
}

void DaliWebDashboard::handle_names_get_(AsyncWebServerRequest* request) {
    std::string body = json::build_json([this](JsonObject root) {
        char key[4];
        JsonObject lamps = root["lamps"].to<JsonObject>();
        for (uint8_t a = 0; a <= ADDR_SHORT_MAX; a++) {
            const char* name = bus_->lamp_name(a);
            if (dali_names::has_name(name)) {
                snprintf(key, sizeof(key), "%u", a);
                lamps[key] = name;
            }
        }
        JsonObject groups = root["groups"].to<JsonObject>();
        for (uint8_t g = 0; g < 16; g++) {
            const char* name = bus_->group_name(g);
            if (dali_names::has_name(name)) {
                snprintf(key, sizeof(key), "%u", g);
                groups[key] = name;
            }
        }
        JsonObject scenes = root["scenes"].to<JsonObject>();
        for (uint8_t s = 0; s < 16; s++) {
            const char* name = bus_->scene_name(s);
            if (dali_names::has_name(name)) {
                snprintf(key, sizeof(key), "%u", s);
                scenes[key] = name;
            }
        }
    });
    request->send(200, "application/json", body.c_str());
}

void DaliWebDashboard::handle_names_set_(AsyncWebServerRequest* request) {
    if (!request->hasArg("kind") || !request->hasArg("index")) {
        request->send(409, "text/plain", "missing kind/index");
        return;
    }
    std::string kind_str = request->arg("kind");
    DaliBusComponent::NameKind kind;
    uint8_t max_index;
    if (kind_str == "lamp") {
        kind = DaliBusComponent::NameKind::Lamp;
        max_index = ADDR_SHORT_MAX;
    } else if (kind_str == "group") {
        kind = DaliBusComponent::NameKind::Group;
        max_index = 15;
    } else if (kind_str == "scene") {
        kind = DaliBusComponent::NameKind::Scene;
        max_index = 15;
    } else {
        request->send(409, "text/plain", "bad kind");
        return;
    }

    int index;
    if (!parse_int_strict(request->arg("index"), 0, max_index, index)) {
        request->send(409, "text/plain", "index out of range");
        return;
    }

    auto* pending = new DaliPendingAction{};
    pending->kind = DaliPendingAction::Kind::Rename;
    pending->target_addr = (uint8_t) index;
    pending->value = (uint8_t) kind;
    dali_names::copy_name(pending->name, request->hasArg("name") ? request->arg("name") : std::string());

    if (!queue_.push(pending)) {
        delete pending;
        request->send(409, "text/plain", "busy, try again");
        return;
    }
    request->send(200, "text/plain", "queued");
}

void DaliWebDashboard::handle_discovery_(AsyncWebServerRequest* request) {
    if (!request->hasArg("mode")) {
        request->send(409, "text/plain", "missing mode");
        return;
    }
    std::string mode = request->arg("mode");
    DaliInitMode init_mode;
    if (mode == "discover") {
        init_mode = DaliInitMode::DiscoverOnly;
    } else if (mode == "unassigned") {
        init_mode = DaliInitMode::InitializeUnassigned;
    } else if (mode == "all") {
        if (!request->hasArg("confirm") || request->arg("confirm") != "yes") {
            request->send(409, "text/plain", "mode=all requires confirm=yes");
            return;
        }
        init_mode = DaliInitMode::InitializeAll;
    } else {
        request->send(409, "text/plain", "bad mode");
        return;
    }

    auto* pending = new DaliPendingAction{};
    pending->kind = DaliPendingAction::Kind::Discovery;
    pending->value = (uint8_t) init_mode;

    if (!queue_.push(pending)) {
        delete pending;
        request->send(409, "text/plain", "busy, try again");
        return;
    }
    request->send(200, "text/plain", "queued");
}

namespace {
const char* event_type_name(dali_event_log::EventType type) {
    switch (type) {
        case dali_event_log::EventType::BusUp: return "bus_up";
        case dali_event_log::EventType::BusDown: return "bus_down";
        case dali_event_log::EventType::Collision: return "collision";
        case dali_event_log::EventType::Disconnected: return "disconnected";
        case dali_event_log::EventType::Reconnected: return "reconnected";
        case dali_event_log::EventType::LampAvailable: return "lamp_available";
        case dali_event_log::EventType::LampUnavailable: return "lamp_unavailable";
        case dali_event_log::EventType::LampProblem: return "lamp_problem";
        case dali_event_log::EventType::LampOk: return "lamp_ok";
    }
    return "unknown";
}
}  // namespace

void DaliWebDashboard::handle_log_(AsyncWebServerRequest* request) {
    std::string body = json::build_json([this](JsonObject root) {
        root["now_ms"] = (uint32_t) millis();
        JsonObject counters = root["counters"].to<JsonObject>();
        counters["errors"] = bus_->error_count();
        counters["bus_down"] = bus_->bus_down_count();
        counters["collisions"] = bus_->collision_count();
        counters["disconnected"] = bus_->is_disconnected();
        counters["online"] = bus_->is_bus_online();

        JsonArray events = root["events"].to<JsonArray>();
        bus_->event_log().for_each([&events](const dali_event_log::Event& e) {
            JsonObject je = events.add<JsonObject>();
            je["ts"] = e.timestamp_ms;
            je["type"] = event_type_name(e.type);
            if (e.addr == dali_event_log::NO_ADDR) {
                je["addr"] = nullptr;
            } else {
                je["addr"] = e.addr;
            }
            je["value"] = e.value;
        });
    });
    request->send(200, "application/json", body.c_str());
}

void DaliWebDashboard::process_pending_actions() {
    DaliPendingAction* action;
    while ((action = queue_.pop()) != nullptr) {
        switch (action->kind) {
            case DaliPendingAction::Kind::GroupAdd:
                bus_->add_to_group(action->target_addr, action->value);
                break;
            case DaliPendingAction::Kind::GroupRemove:
                bus_->remove_from_group(action->target_addr, action->value);
                break;
            case DaliPendingAction::Kind::SceneRecall:
                bus_->do_scene_action(action->target_addr, action->value, DaliBusComponent::SceneAction::Recall);
                break;
            case DaliPendingAction::Kind::SceneStore:
                bus_->do_scene_action(action->target_addr, action->value, DaliBusComponent::SceneAction::Store);
                break;
            case DaliPendingAction::Kind::SceneRemove:
                bus_->do_scene_action(action->target_addr, action->value, DaliBusComponent::SceneAction::Remove);
                break;
            case DaliPendingAction::Kind::Identify:
                bus_->identify_lamp(action->target_addr);
                break;
            case DaliPendingAction::Kind::LampControl: {
                DaliLight* light = bus_->find_light_for_address(action->target_addr);
                if (light == nullptr) break;
                ControlRequest req{};
                req.has_state = action->has_on;
                req.state = action->on;
                req.has_brightness = action->has_brightness;
                req.brightness = action->brightness_pct / 100.0f;
                req.has_color_temp = action->has_color_temp;
                req.color_temp_mireds = action->color_temp_mireds;
                light->perform_call(req);
                break;
            }
            case DaliPendingAction::Kind::Rename:
                bus_->set_name((DaliBusComponent::NameKind) action->value, action->target_addr,
                               std::string(action->name));
                break;
            case DaliPendingAction::Kind::Discovery:
                bus_->run_discovery((DaliInitMode) action->value);
                break;
        }
        delete action;
    }
}

}  // namespace dali
}  // namespace esphome

#endif  // DALI_WEB_DASHBOARD_ENABLED
