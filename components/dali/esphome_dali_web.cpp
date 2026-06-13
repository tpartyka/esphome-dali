// See esphome_dali.h for why <esphome.h> must be included before our own headers:
// it triggers ESPHome's alphabetical component-header traversal, which must reach
// esphome_dali.h's full DaliBusComponent definition before any other component
// header (e.g. esphome_dali_input_trigger.h, esphome_dali_light.h) references it.
#include <esphome.h>
#include "esphome_dali_web.h"

#ifdef DALI_WEB_DASHBOARD_ENABLED

#include "esphome_dali.h"
#include "esphome/components/json/json_util.h"
#include <cstdlib>

using namespace esphome;
using namespace esphome::dali;

namespace {

// Single-page dashboard: lamp table (status, brightness, color temp, group
// membership) + a scenes panel. Vanilla JS, polls /api/lamps every 3s.
static const char DASHBOARD_HTML[] = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>DALI Dashboard</title>
<style>
  :root { color-scheme: light dark; }
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
         margin: 0; padding: 1rem; max-width: 960px; margin-inline: auto; }
  h1 { font-size: 1.4rem; margin: 0 0 0.75rem; }
  h2 { font-size: 1.1rem; margin: 1.5rem 0 0.5rem; }
  table { width: 100%; border-collapse: collapse; font-size: 0.9rem; }
  th, td { text-align: left; padding: 0.4rem 0.5rem; border-bottom: 1px solid #8884; vertical-align: middle; }
  th { font-weight: 600; }
  .dot { display: inline-block; width: 0.6rem; height: 0.6rem; border-radius: 50%; margin-right: 0.35rem; }
  .dot.online { background: #2ecc71; }
  .dot.offline { background: #999; }
  .dot.problem { background: #e74c3c; }
  .bar { display: inline-block; width: 80px; height: 8px; background: #8883; border-radius: 4px; overflow: hidden; vertical-align: middle; margin-right: 0.4rem; }
  .bar > span { display: block; height: 100%; background: #f1c40f; }
  .groups { display: flex; flex-wrap: wrap; gap: 2px; }
  .grp { width: 1.6rem; height: 1.6rem; line-height: 1.6rem; text-align: center; font-size: 0.75rem;
         border: 1px solid #8888; border-radius: 4px; cursor: pointer; user-select: none; background: transparent; }
  .grp.member { background: #3498db; color: #fff; border-color: #3498db; }
  .panel { border: 1px solid #8884; border-radius: 8px; padding: 0.75rem 1rem; margin-top: 0.5rem; }
  .panel label { margin-right: 1rem; }
  select, button { font-size: 0.95rem; padding: 0.3rem 0.6rem; border-radius: 4px; border: 1px solid #8888; }
  button { cursor: pointer; }
  .row { display: flex; align-items: center; gap: 0.5rem; flex-wrap: wrap; }
  #status { font-size: 0.8rem; color: #888; margin-top: 0.5rem; }
</style>
</head>
<body>
<h1>DALI Dashboard</h1>
<table id="lamps">
  <thead><tr><th>Addr</th><th>Status</th><th>On</th><th>Brightness</th><th>CT (mireds)</th><th>Groups</th><th></th></tr></thead>
  <tbody></tbody>
</table>

<h2>Scenes</h2>
<div class="panel row">
  <label>Scene
    <select id="scene-num"></select>
  </label>
  <label>Target
    <select id="scene-target"></select>
  </label>
  <button onclick="sceneAction('recall')">Recall</button>
  <button onclick="sceneAction('store')">Store current as scene</button>
  <button onclick="sceneAction('remove')">Remove scene</button>
</div>
<div id="status"></div>

<script>
const lampsBody = document.querySelector('#lamps tbody');
const sceneNum = document.getElementById('scene-num');
const sceneTarget = document.getElementById('scene-target');
const status = document.getElementById('status');

for (let s = 0; s < 16; s++) {
  const o = document.createElement('option');
  o.value = s; o.textContent = 'Scene ' + s;
  sceneNum.appendChild(o);
}

function rebuildTargets(lamps) {
  const prev = sceneTarget.value;
  sceneTarget.innerHTML = '';
  const addOpt = (value, label) => {
    const o = document.createElement('option');
    o.value = value; o.textContent = label;
    sceneTarget.appendChild(o);
  };
  addOpt('all', 'All lamps (broadcast)');
  for (let g = 0; g < 16; g++) addOpt('group:' + g, 'Group ' + g);
  for (const l of lamps) addOpt('lamp:' + l.addr, 'Lamp ' + l.addr);
  if ([...sceneTarget.options].some(o => o.value === prev)) sceneTarget.value = prev;
}

function renderLamps(lamps) {
  lampsBody.innerHTML = '';
  for (const l of lamps) {
    const tr = document.createElement('tr');

    const tdAddr = document.createElement('td');
    tdAddr.textContent = l.addr;
    tr.appendChild(tdAddr);

    const tdStatus = document.createElement('td');
    const dot = document.createElement('span');
    dot.className = 'dot ' + (l.problem ? 'problem' : (l.online ? 'online' : 'offline'));
    tdStatus.appendChild(dot);
    tdStatus.appendChild(document.createTextNode(
        l.problem ? 'Problem' : (l.online ? 'Online' : 'Offline')));
    tr.appendChild(tdStatus);

    const tdOn = document.createElement('td');
    tdOn.textContent = l.on ? 'On' : 'Off';
    tr.appendChild(tdOn);

    const tdBright = document.createElement('td');
    const bar = document.createElement('span');
    bar.className = 'bar';
    const fill = document.createElement('span');
    fill.style.width = l.brightness_pct + '%';
    bar.appendChild(fill);
    tdBright.appendChild(bar);
    tdBright.appendChild(document.createTextNode(l.brightness_pct + '%'));
    tr.appendChild(tdBright);

    const tdCt = document.createElement('td');
    tdCt.textContent = (l.color_temp_mireds === null || l.color_temp_mireds === undefined)
        ? '-' : l.color_temp_mireds;
    tr.appendChild(tdCt);

    const tdGroups = document.createElement('td');
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
    tdGroups.appendChild(groupsDiv);
    tr.appendChild(tdGroups);

    const tdIdentify = document.createElement('td');
    const identifyBtn = document.createElement('button');
    identifyBtn.textContent = 'Identify';
    identifyBtn.title = 'Blink this lamp to identify it';
    identifyBtn.onclick = () => identify(l.addr);
    tdIdentify.appendChild(identifyBtn);
    tr.appendChild(tdIdentify);

    lampsBody.appendChild(tr);
  }
}

async function refresh() {
  try {
    const res = await fetch('/api/lamps');
    const data = await res.json();
    renderLamps(data.lamps);
    rebuildTargets(data.lamps);
    status.textContent = '';
  } catch (e) {
    status.textContent = 'Failed to refresh: ' + e;
  }
}

async function groupAction(addr, group, action) {
  await fetch(`/api/group?addr=${addr}&group=${group}&action=${action}`, { method: 'POST' });
  setTimeout(refresh, 300);
}

async function identify(addr) {
  await fetch(`/api/identify?addr=${addr}`, { method: 'POST' });
}

async function sceneAction(action) {
  const scene = sceneNum.value;
  const target = sceneTarget.value;
  status.textContent = 'Sending...';
  await fetch(`/api/scene?scene=${scene}&target=${target}&action=${action}`, { method: 'POST' });
  status.textContent = 'Done.';
  setTimeout(refresh, 300);
}

refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>
)html";

}  // namespace

namespace esphome {
namespace dali {

void DaliWebDashboard::begin(uint16_t port, DaliBusComponent* bus) {
    bus_ = bus;
    server_ = new AsyncWebServer(port);
    server_->addHandler(this);
    server_->begin();
}

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
            lamp["online"] = info.online;
            lamp["problem"] = info.problem;
            lamp["on"] = info.on;
            lamp["brightness_pct"] = info.brightness_pct;
            if (info.has_color_temp) {
                lamp["color_temp_mireds"] = info.color_temp_mireds;
            } else {
                lamp["color_temp_mireds"] = nullptr;
            }
            JsonArray groups = lamp["groups"].to<JsonArray>();
            for (uint8_t g = 0; g < 16; g++) {
                if (info.groups & (uint16_t) (1u << g)) groups.add(g);
            }
        }
    });
    request->send(200, "application/json", body.c_str());
}

void DaliWebDashboard::handle_group_action_(AsyncWebServerRequest* request) {
    if (!request->hasArg("addr") || !request->hasArg("group") || !request->hasArg("action")) {
        request->send(400, "text/plain", "missing addr/group/action");
        return;
    }
    int addr = atoi(request->arg("addr").c_str());
    int group = atoi(request->arg("group").c_str());
    std::string action = request->arg("action");
    if (addr < 0 || addr > ADDR_SHORT_MAX || group < 0 || group > 15) {
        request->send(400, "text/plain", "addr/group out of range");
        return;
    }
    if (action != "add" && action != "remove") {
        request->send(400, "text/plain", "action must be add or remove");
        return;
    }

    auto* pending = new DaliPendingAction{};
    pending->kind = (action == "add") ? DaliPendingAction::Kind::GroupAdd : DaliPendingAction::Kind::GroupRemove;
    pending->target_addr = (uint8_t) addr;
    pending->value = (uint8_t) group;
    if (!queue_.push(pending)) {
        delete pending;
        request->send(503, "text/plain", "busy, try again");
        return;
    }
    request->send(202, "text/plain", "queued");
}

void DaliWebDashboard::handle_scene_action_(AsyncWebServerRequest* request) {
    if (!request->hasArg("scene") || !request->hasArg("target") || !request->hasArg("action")) {
        request->send(400, "text/plain", "missing scene/target/action");
        return;
    }
    int scene = atoi(request->arg("scene").c_str());
    if (scene < 0 || scene > 15) {
        request->send(400, "text/plain", "scene out of range");
        return;
    }

    std::string target = request->arg("target");
    uint8_t target_addr;
    if (target == "all") {
        target_addr = ADDR_BROADCAST;
    } else if (target.rfind("group:", 0) == 0) {
        int g = atoi(target.c_str() + 6);
        if (g < 0 || g > 15) {
            request->send(400, "text/plain", "bad group target");
            return;
        }
        target_addr = (uint8_t) (ADDR_GROUP | g);
    } else if (target.rfind("lamp:", 0) == 0) {
        int a = atoi(target.c_str() + 5);
        if (a < 0 || a > ADDR_SHORT_MAX) {
            request->send(400, "text/plain", "bad lamp target");
            return;
        }
        target_addr = (uint8_t) a;
    } else {
        request->send(400, "text/plain", "bad target");
        return;
    }

    DaliPendingAction::Kind kind;
    std::string action = request->arg("action");
    if (action == "recall") kind = DaliPendingAction::Kind::SceneRecall;
    else if (action == "store") kind = DaliPendingAction::Kind::SceneStore;
    else if (action == "remove") kind = DaliPendingAction::Kind::SceneRemove;
    else {
        request->send(400, "text/plain", "bad action");
        return;
    }

    auto* pending = new DaliPendingAction{kind, target_addr, (uint8_t) scene};
    if (!queue_.push(pending)) {
        delete pending;
        request->send(503, "text/plain", "busy, try again");
        return;
    }
    request->send(202, "text/plain", "queued");
}

void DaliWebDashboard::handle_identify_action_(AsyncWebServerRequest* request) {
    if (!request->hasArg("addr")) {
        request->send(400, "text/plain", "missing addr");
        return;
    }
    int addr = atoi(request->arg("addr").c_str());
    if (addr < 0 || addr > ADDR_SHORT_MAX) {
        request->send(400, "text/plain", "addr out of range");
        return;
    }

    auto* pending = new DaliPendingAction{DaliPendingAction::Kind::Identify, (uint8_t) addr, 0};
    if (!queue_.push(pending)) {
        delete pending;
        request->send(503, "text/plain", "busy, try again");
        return;
    }
    request->send(202, "text/plain", "queued");
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
        }
        delete action;
    }
}

}  // namespace dali
}  // namespace esphome

#endif  // DALI_WEB_DASHBOARD_ENABLED
