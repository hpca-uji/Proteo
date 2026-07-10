"use strict";

let schema = null;
let config = null;
const phaseOpenState = new Map();
const groupOpenState = new Map();

const messagesEl = document.getElementById("messages");
const generalFieldsEl = document.getElementById("general-fields");
const phasesContainerEl = document.getElementById("phases-container");
const groupsContainerEl = document.getElementById("groups-container");
const phasesCountEl = document.getElementById("phases-count");
const groupsCountEl = document.getElementById("groups-count");
const configFileNameEl = document.getElementById("config-file-name");
const fileInputEl = document.getElementById("file-input");

function makeDangerButton(label, onClick) {
  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = "btn-danger";
  btn.textContent = label;
  btn.addEventListener("click", onClick);
  return btn;
}

function getDownloadFileName() {
  const name = configFileNameEl.value.trim();
  return name || "config.json";
}

function showMessage(text, type = "ok", items = []) {
  messagesEl.className = `messages ${type}`;
  if (items.length) {
    messagesEl.innerHTML = `<strong>${escapeHtml(text)}</strong><ul>${items.map((e) => `<li>${escapeHtml(e)}</li>`).join("")}</ul>`;
  } else {
    messagesEl.textContent = text;
  }
}

function showValidationResult(title, errors = [], warnings = []) {
  let type = "ok";
  if (errors.length) {
    type = "error";
  } else if (warnings.length) {
    type = "warn";
  }
  messagesEl.className = `messages ${type}`;
  let html = `<strong>${escapeHtml(title)}</strong>`;
  if (errors.length) {
    html += `<ul>${errors.map((e) => `<li>${escapeHtml(e)}</li>`).join("")}</ul>`;
  }
  if (warnings.length) {
    html += `<p class="warn-heading">Warnings:</p><ul class="warn-list">${warnings.map((w) => `<li>${escapeHtml(w)}</li>`).join("")}</ul>`;
  }
  messagesEl.innerHTML = html;
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

function parseStrategy(text) {
  const trimmed = String(text).trim();
  if (!trimmed) {
    return [0];
  }
  return trimmed.split(",").map((part) => parseInt(part.trim(), 10));
}

function formatStrategy(arr) {
  if (!Array.isArray(arr) || arr.length === 0) {
    return "";
  }
  if (arr.length === 1 && arr[0] === 0) {
    return "";
  }
  return arr.join(",");
}

const STAGE_IO = new Set([9, 10]);
const STAGE_COMPUTE = new Set([0, 1]);

function stageUsesBytes(stageType) {
  return !STAGE_COMPUTE.has(stageType);
}

function stageAlwaysShowsGranularity(stageType) {
  return STAGE_COMPUTE.has(stageType);
}

function stageOptionalAllowed(stageType, fieldKey) {
  if (fieldKey === "Granularity" && stageAlwaysShowsGranularity(stageType)) {
    return false;
  }
  if (fieldKey === "Stage_Involved_Procs") {
    return STAGE_IO.has(stageType);
  }
  if (fieldKey === "Stage_Identifier") {
    return stageType === 3 || stageType === 4;
  }
  return true;
}

function prepareStageForType(stage, stageType) {
  if (STAGE_COMPUTE.has(stageType)) {
    stage.Stage_Bytes = 0;
    if (stage.Granularity === undefined) {
      stage.Granularity = 600;
    }
  }
}

function makeStrategyLegend(strategiesMap) {
  const legend = document.createElement("ul");
  legend.className = "strategy-legend";
  Object.entries(strategiesMap || {}).forEach(([key, text]) => {
    const item = document.createElement("li");
    item.textContent = `${key} — ${text}`;
    legend.appendChild(item);
  });
  return legend;
}

function makeStrategyLegendWrap(title, strategiesMap) {
  const wrap = document.createElement("div");
  wrap.className = "strategy-legend-wrap";
  wrap.innerHTML = `<strong>${title}</strong>`;
  wrap.appendChild(makeStrategyLegend(strategiesMap));
  const note = document.createElement("p");
  note.className = "strategy-legend-note";
  note.textContent = "Leave field empty for default (clear).";
  wrap.appendChild(note);
  return wrap;
}

function syncTotals() {
  config.general.Total_Phases = config.phases.length;
  config.general.Total_Resizes = Math.max(0, config.groups.length - 1);
  config.phases.forEach((phase) => {
    phase.Total_Stages = phase.stages.length;
  });
}

function defaultStage() {
  return { Stage_Type: 0, Stage_Bytes: 0, Stage_Time: 0 };
}

function defaultPhase() {
  return { Total_Iters: 1, Total_Stages: 1, stages: [defaultStage()] };
}

function defaultGroup() {
  return {
    Iters: 1,
    Procs: 2,
    FactorS: 1,
    Dist: "compact",
    Redistribution_Method: 0,
    Redistribution_Strategy: [0],
    Spawn_Method: 0,
    Spawn_Strategy: [0],
  };
}

function makeNumberField(label, value, hint, onChange, step = "1") {
  const wrap = document.createElement("div");
  wrap.className = "field";
  const id = `f-${label}-${Math.random().toString(36).slice(2, 8)}`;
  wrap.innerHTML = `<label for="${id}">${label}<span class="hint">${hint || ""}</span></label>`;
  const input = document.createElement("input");
  input.type = "number";
  input.id = id;
  input.step = step;
  input.value = value;
  input.addEventListener("change", () => onChange(parseFloat(input.value)));
  wrap.appendChild(input);
  return wrap;
}

function makeTextField(label, value, hint, onChange) {
  const wrap = document.createElement("div");
  wrap.className = "field";
  const id = `f-${label}-${Math.random().toString(36).slice(2, 8)}`;
  wrap.innerHTML = `<label for="${id}">${label}<span class="hint">${hint || ""}</span></label>`;
  const input = document.createElement("input");
  input.type = "text";
  input.id = id;
  input.value = value;
  input.addEventListener("change", () => onChange(input.value));
  wrap.appendChild(input);
  return wrap;
}

function makeSelectField(label, optionsMap, value, hint, onChange) {
  const wrap = document.createElement("div");
  wrap.className = "field";
  wrap.innerHTML = `<label>${label}<span class="hint">${hint || ""}</span></label>`;
  const select = document.createElement("select");
  Object.entries(optionsMap).forEach(([key, text]) => {
    const opt = document.createElement("option");
    opt.value = key;
    opt.textContent = `${key} — ${text}`;
    if (String(key) === String(value)) {
      opt.selected = true;
    }
    select.appendChild(opt);
  });
  select.addEventListener("change", () => onChange(parseInt(select.value, 10)));
  wrap.appendChild(select);
  return wrap;
}

function renderGeneral() {
  generalFieldsEl.innerHTML = "";
  const hints = Object.fromEntries((schema.general_fields || []).map(([k, h]) => [k, h]));

  const fields = [
    ["Total_Resizes", config.general.Total_Resizes, () => {}, true],
    ["Total_Phases", config.general.Total_Phases, () => {}, true],
    ["SDR", config.general.SDR, (v) => { config.general.SDR = v; }, false, "any"],
    ["ADR", config.general.ADR, (v) => { config.general.ADR = v; }, false, "any"],
    ["Datasize", config.general.Datasize, (v) => { config.general.Datasize = v | 0; }],
    ["Rigid", config.general.Rigid, (v) => { config.general.Rigid = v | 0; }, false, "1", schema.rigid_options],
    ["Capture_Method", config.general.Capture_Method, (v) => { config.general.Capture_Method = v | 0; }, false, "1", schema.capture_method_options],
  ];

  fields.forEach(([name, val, onChange, readonly, step, options]) => {
    if (options) {
      generalFieldsEl.appendChild(makeSelectField(name, options, val, hints[name], onChange));
    } else {
      const el = makeNumberField(name, val, hints[name], onChange, step || "1");
      if (readonly) {
        el.querySelector("input").readOnly = true;
      }
      generalFieldsEl.appendChild(el);
    }
  });
}

function renderStage(stage, phaseIndex, stageIndex) {
  const card = document.createElement("div");
  card.className = "stage-card";
  card.innerHTML = `<strong>Stage ${stageIndex}</strong>`;
  const grid = document.createElement("div");
  grid.className = "field-grid";

  grid.appendChild(makeSelectField(
    "Stage_Type",
    schema.stage_types,
    stage.Stage_Type,
    "",
    (v) => {
      stage.Stage_Type = v;
      prepareStageForType(stage, v);
      renderAll();
    }
  ));

  const hintText = (schema.stage_type_hints || {})[stage.Stage_Type];
  if (hintText) {
    const hintEl = document.createElement("p");
    hintEl.className = "stage-hint";
    hintEl.textContent = hintText;
    grid.appendChild(hintEl);
  }

  if (stageUsesBytes(stage.Stage_Type)) {
    grid.appendChild(makeNumberField("Stage_Bytes", stage.Stage_Bytes, "", (v) => { stage.Stage_Bytes = v | 0; }));
  }
  grid.appendChild(makeNumberField("Stage_Time", stage.Stage_Time, "seconds", (v) => { stage.Stage_Time = v; }, "any"));

  if (stageAlwaysShowsGranularity(stage.Stage_Type)) {
    prepareStageForType(stage, stage.Stage_Type);
    const granHint = (schema.stage_optional_fields || []).find(([k]) => k === "Granularity");
    grid.appendChild(makeNumberField(
      "Granularity",
      stage.Granularity,
      granHint ? granHint[1] : "",
      (v) => { stage.Granularity = v | 0; }
    ));
  }

  schema.stage_optional_fields.forEach(([key, hint]) => {
    if (key === "Granularity" && stageAlwaysShowsGranularity(stage.Stage_Type)) {
      return;
    }
    if (stage[key] !== undefined && stage[key] !== null && stage[key] !== "") {
      if (key === "Stage_Time_Capped") {
        grid.appendChild(makeSelectField(
          key,
          schema.stage_time_capped_options || { 0: "Operation count (0)", 1: "Time cap (1)" },
          stage[key],
          hint,
          (v) => { stage[key] = v; }
        ));
      } else {
        grid.appendChild(makeNumberField(key, stage[key], hint, (v) => { stage[key] = v | 0; }));
      }
    }
  });

  const optRow = document.createElement("div");
  optRow.className = "card-actions";
  schema.stage_optional_fields.forEach(([key]) => {
    if (stage[key] === undefined && stageOptionalAllowed(stage.Stage_Type, key)) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.textContent = `+ ${key}`;
      btn.addEventListener("click", () => {
        if (key === "Granularity") {
          stage[key] = 600;
        } else if (key === "Stage_Involved_Procs") {
          stage[key] = 0;
        } else if (key === "Stage_Time_Capped") {
          stage[key] = 0;
        } else {
          stage[key] = 1;
        }
        renderAll();
      });
      optRow.appendChild(btn);
    }
  });
  const removeBtn = makeDangerButton("Remove stage", () => {
    if (config.phases[phaseIndex].stages.length <= 1) {
      showMessage("Each phase must have at least one stage.", "error");
      return;
    }
    config.phases[phaseIndex].stages.splice(stageIndex, 1);
    renderAll();
  });
  optRow.appendChild(removeBtn);

  card.appendChild(grid);
  card.appendChild(optRow);
  return card;
}

function isPhaseOpen(phaseIndex) {
  if (phaseOpenState.has(phaseIndex)) {
    return phaseOpenState.get(phaseIndex);
  }
  return phaseIndex === 0 || config.phases.length === 1;
}

function renderPhase(phase, phaseIndex) {
  const details = document.createElement("details");
  details.className = "card phase-panel";
  details.open = isPhaseOpen(phaseIndex);

  details.addEventListener("toggle", () => {
    phaseOpenState.set(phaseIndex, details.open);
  });

  const summary = document.createElement("summary");
  summary.textContent = `Phase ${phaseIndex} — ${phase.Total_Iters} iters, ${phase.stages.length} stages`;
  details.appendChild(summary);

  const body = document.createElement("div");
  body.className = "phase-body";

  const grid = document.createElement("div");
  grid.className = "field-grid";
  grid.appendChild(makeNumberField("Total_Iters", phase.Total_Iters, "", (v) => {
    phase.Total_Iters = v | 0;
    renderPhases();
  }));
  const ts = makeNumberField("Total_Stages", phase.stages.length, "auto-synced", () => {}, "1");
  ts.querySelector("input").readOnly = true;
  grid.appendChild(ts);
  body.appendChild(grid);

  const actions = document.createElement("div");
  actions.className = "card-actions";
  const addStage = document.createElement("button");
  addStage.type = "button";
  addStage.textContent = "Add stage";
  addStage.addEventListener("click", () => {
    phase.stages.push(defaultStage());
    phaseOpenState.set(phaseIndex, true);
    renderAll();
  });
  const removePhase = makeDangerButton("Remove phase", () => {
    if (config.phases.length <= 1) {
      showMessage("At least one phase is required.", "error");
      return;
    }
    config.phases.splice(phaseIndex, 1);
    phaseOpenState.clear();
    renderAll();
  });
  actions.appendChild(addStage);
  actions.appendChild(removePhase);
  body.appendChild(actions);

  phase.stages.forEach((stage, si) => {
    body.appendChild(renderStage(stage, phaseIndex, si));
  });

  details.appendChild(body);
  return details;
}

function isGroupOpen(groupIndex) {
  if (groupOpenState.has(groupIndex)) {
    return groupOpenState.get(groupIndex);
  }
  return groupIndex === 0 || config.groups.length === 1;
}

function renderGroup(group, groupIndex) {
  const details = document.createElement("details");
  details.className = "card group-panel";
  details.open = isGroupOpen(groupIndex);

  details.addEventListener("toggle", () => {
    groupOpenState.set(groupIndex, details.open);
  });

  const summary = document.createElement("summary");
  summary.textContent = `Group ${groupIndex} — ${group.Iters} iters, ${group.Procs} procs`;
  details.appendChild(summary);

  const body = document.createElement("div");
  body.className = "group-body";

  const grid = document.createElement("div");
  grid.className = "field-grid";

  const hints = Object.fromEntries((schema.group_fields || []).map(([k, h]) => [k, h]));

  grid.appendChild(makeNumberField("Iters", group.Iters, hints.Iters, (v) => {
    group.Iters = v | 0;
    renderGroups();
  }));
  grid.appendChild(makeNumberField("Procs", group.Procs, hints.Procs, (v) => {
    group.Procs = v | 0;
    renderGroups();
  }));
  grid.appendChild(makeNumberField("FactorS", group.FactorS, hints.FactorS, (v) => { group.FactorS = v; }, "any"));

  const distWrap = document.createElement("div");
  distWrap.className = "field";
  distWrap.innerHTML = `<label>Dist<span class="hint">${hints.Dist}</span></label>`;
  const distSelect = document.createElement("select");
  schema.dist_options.forEach((opt) => {
    const o = document.createElement("option");
    o.value = opt;
    o.textContent = opt;
    if (group.Dist === opt) o.selected = true;
    distSelect.appendChild(o);
  });
  distSelect.addEventListener("change", () => { group.Dist = distSelect.value; });
  distWrap.appendChild(distSelect);
  grid.appendChild(distWrap);

  grid.appendChild(makeSelectField("Redistribution_Method", schema.redistribution_methods, group.Redistribution_Method, hints.Redistribution_Method, (v) => { group.Redistribution_Method = v; }));
  grid.appendChild(makeTextField("Redistribution_Strategy", formatStrategy(group.Redistribution_Strategy), hints.Redistribution_Strategy, (v) => { group.Redistribution_Strategy = parseStrategy(v); }));
  grid.appendChild(makeSelectField("Spawn_Method", schema.spawn_methods, group.Spawn_Method, hints.Spawn_Method, (v) => { group.Spawn_Method = v; }));
  grid.appendChild(makeTextField("Spawn_Strategy", formatStrategy(group.Spawn_Strategy), hints.Spawn_Strategy, (v) => { group.Spawn_Strategy = parseStrategy(v); }));

  body.appendChild(grid);
  body.appendChild(makeStrategyLegendWrap(
    "Redistribution strategies",
    schema.redistribution_strategy_options || schema.redistribution_strategies
  ));
  body.appendChild(makeStrategyLegendWrap(
    "Spawn strategies",
    schema.spawn_strategy_options || schema.spawn_strategies
  ));

  details.appendChild(body);
  return details;
}

function renderPhases() {
  phasesContainerEl.innerHTML = "";
  config.phases.forEach((phase, i) => {
    phasesContainerEl.appendChild(renderPhase(phase, i));
  });
  phasesCountEl.textContent = `(${config.phases.length})`;
}

function renderGroups() {
  groupsContainerEl.innerHTML = "";
  config.groups.forEach((group, i) => {
    groupsContainerEl.appendChild(renderGroup(group, i));
  });
  groupsCountEl.textContent = `(${config.groups.length})`;
}

function renderAll() {
  syncTotals();
  renderGeneral();
  renderPhases();
  renderGroups();
}

async function loadSchema() {
  const res = await fetch("/api/schema");
  schema = await res.json();
}

async function loadBlank() {
  const res = await fetch("/api/blank");
  config = await res.json();
  configFileNameEl.value = "config.json";
  phaseOpenState.clear();
  groupOpenState.clear();
  renderAll();
  messagesEl.className = "messages";
  messagesEl.textContent = "";
}

async function loadTemplate() {
  const res = await fetch("/api/template");
  config = await res.json();
  configFileNameEl.value = "config.json";
  renderAll();
  showMessage("Loaded template configuration.");
}

async function runValidation() {
  syncTotals();
  const res = await fetch("/api/validate", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(config),
  });
  const data = await res.json();
  if (data.sanitized) {
    config = data.sanitized;
    renderAll();
  }
  return data;
}

async function validateConfig() {
  const data = await runValidation();
  const errors = data.errors || [];
  const warnings = data.warnings || [];
  if (data.valid && warnings.length === 0) {
    showMessage("Configuration is valid.", "ok");
  } else if (data.valid) {
    showValidationResult("Configuration is valid with warnings:", [], warnings);
  } else {
    showValidationResult("Validation failed:", errors, warnings);
  }
  return data.valid;
}

async function downloadJson() {
  const data = await runValidation();
  const errors = data.errors || [];
  const warnings = data.warnings || [];
  if (errors.length) {
    showValidationResult("Downloaded with validation errors:", errors, warnings);
  } else if (warnings.length) {
    showValidationResult("Download started with warnings:", [], warnings);
  } else {
    showMessage("Download started.", "ok");
  }

  syncTotals();
  const blob = new Blob([JSON.stringify(config, null, 2) + "\n"], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = getDownloadFileName();
  a.click();
  URL.revokeObjectURL(url);
}

function openLocalFile(file) {
  const reader = new FileReader();
  reader.onload = () => {
    try {
      config = JSON.parse(reader.result);
      configFileNameEl.value = file.name;
      renderAll();
      showMessage(`Opened ${file.name}`, "ok");
    } catch (err) {
      showMessage(`Invalid JSON: ${err.message}`, "error");
    }
  };
  reader.readAsText(file);
}

document.getElementById("btn-new").addEventListener("click", loadTemplate);
document.getElementById("btn-validate").addEventListener("click", validateConfig);
document.getElementById("btn-download").addEventListener("click", downloadJson);
document.getElementById("btn-open-file").addEventListener("click", () => fileInputEl.click());
fileInputEl.addEventListener("change", () => {
  if (fileInputEl.files[0]) {
    openLocalFile(fileInputEl.files[0]);
    fileInputEl.value = "";
  }
});

document.getElementById("btn-add-phase").addEventListener("click", () => {
  const newIndex = config.phases.length;
  config.phases.push(defaultPhase());
  phaseOpenState.set(newIndex, true);
  renderAll();
});

document.getElementById("btn-add-group").addEventListener("click", () => {
  const newIndex = config.groups.length;
  config.groups.push(defaultGroup());
  groupOpenState.set(newIndex, true);
  renderAll();
});

document.getElementById("btn-remove-group").addEventListener("click", () => {
  if (config.groups.length <= 1) {
    showMessage("At least one group is required.", "error");
    return;
  }
  config.groups.pop();
  groupOpenState.clear();
  renderAll();
});

(async function init() {
  await loadSchema();
  await loadBlank();
})();
