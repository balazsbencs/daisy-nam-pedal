import { EDIT_FIELDS, createInitialState, transition } from './simulator-state.mjs';

let state = createInitialState();
const pedal = document.querySelector('#pedal');
const screen = document.querySelector('#pedal-screen');
const encoders = [...document.querySelectorAll('[data-encoder]')];
const footswitches = [...document.querySelectorAll('[data-action]')];
const modeName = document.querySelector('#mode-name');
const modeHelp = document.querySelector('#mode-help');

const dispatch = action => {
  state = transition(state, action);
  render();
};

const escapeHtml = value => String(value).replace(/[&<>"]/g, character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[character]);
const db = value => `${value > 0 ? '+' : ''}${value} dB`;
const percent = value => Math.round(value * 100);
const clampPercent = value => Math.max(5, Math.min(100, value));

function performanceScreen() {
  const preset = state.live;
  const meters = [percent(preset.inputGain / 2), clampPercent((preset.bass + 12) / 24 * 100), clampPercent((preset.mid + 12) / 24 * 100), clampPercent((preset.treble + 12) / 24 * 100), percent(preset.outputVolume)];
  return `
    <div class="display-header"><span>${String(state.activePreset + 1).padStart(2, '0')}/${String(state.presets.length).padStart(2, '0')}</span><span class="display-badge ${preset.bypass ? 'bypass' : ''}">${preset.bypass ? 'BYPASS' : 'ACTIVE'}</span></div>
    <div class="display-title">${escapeHtml(preset.name)}</div>
    ${state.dirty ? '<div class="display-edited">◆ EDITED</div>' : ''}
    <div class="display-card"><small>AMP</small>${escapeHtml(preset.model)}</div>
    <div class="display-card cab"><small>CAB</small>${escapeHtml(preset.ir)}</div>
    <div class="display-meters">${meters.map((value, index) => `<div class="display-meter" style="--meter:${value}%"><span>${['GAIN', 'BASS', 'MID', 'TRE', 'VOL'][index]}</span></div>`).join('')}</div>`;
}

function browseScreen() {
  return `
    <div class="display-header"><span>PRESETS</span><span>${state.browseCursor + 1}/${state.presets.length}</span></div>
    <div class="display-title">SELECT RIG</div>
    <ul class="display-list">${state.presets.map((preset, index) => `<li class="${index === state.browseCursor ? 'active' : ''}">${escapeHtml(preset.name)}<small>${escapeHtml(preset.model)} · ${escapeHtml(preset.ir)}</small></li>`).join('')}</ul>`;
}

function editValue(field, values) {
  const labels = {
    model: values.model, ir: values.ir, inputGain: values.inputGain.toFixed(1), outputVolume: values.outputVolume.toFixed(1), bypass: values.bypass ? 'On' : 'Off', bassFreq: `${values.bassFreq} Hz`, midFreq: `${values.midFreq} Hz`, trebleFreq: `${values.trebleFreq} Hz`,
  };
  return labels[field];
}

function editScreen() {
  const labels = ['MODEL', 'CAB', 'IN GAIN', 'OUT VOL', 'BYPASS', 'BASS FREQ', 'MID FREQ', 'TRE FREQ'];
  return `
    <div class="display-header"><span>EDIT</span><span>${state.edit.presetIndex + 1}/${state.presets.length}</span></div>
    <div class="display-title">${escapeHtml(state.edit.values.name)}</div>
    <ul class="display-list">${EDIT_FIELDS.map((field, index) => `<li class="${index === state.edit.field ? `active ${state.edit.editing ? 'editing' : ''}` : ''}"><small>${labels[index]}</small>${escapeHtml(editValue(field, state.edit.values))}</li>`).join('')}</ul>`;
}

function tunerScreen() {
  return `
    <div class="display-header"><span>TUNER</span><span>DEMO SIGNAL</span></div>
    <div class="display-tuner"><div><div class="display-note">A4</div><div>440.0 Hz</div><div class="tuner-track"></div><div>0 cents · IN TUNE</div></div></div>`;
}

const helpByMode = {
  performance: ['Turn any encoder to change its live value.', 'Tap right/left for next/previous preset.', 'Hold right to save or left to revert.', 'Click Gain to browse presets.', 'Hold both footswitches for the tuner.'],
  browse: ['Turn Gain to move the preset cursor.', 'Click Gain to load the highlighted preset.', 'Hold Gain for 800 ms to edit it.', 'Tap either footswitch to cancel.'],
  edit: ['Turn Gain to move between fields.', 'Click Gain to toggle value editing.', 'Tap right to apply and save.', 'Tap left to discard the working copy.'],
  tuner: ['The device output is muted in tuner mode.', 'Hold both footswitches again—or press T—to return.'],
};

function encoderState(index) {
  const preset = state.live;
  const controls = [
    [preset.inputGain, '0', '2', preset.inputGain.toFixed(1), -135 + preset.inputGain / 2 * 270],
    [preset.bass, '-12', '12', db(preset.bass), -135 + (preset.bass + 12) / 24 * 270],
    [preset.mid, '-12', '12', db(preset.mid), -135 + (preset.mid + 12) / 24 * 270],
    [preset.treble, '-12', '12', db(preset.treble), -135 + (preset.treble + 12) / 24 * 270],
    [preset.outputVolume, '0', '1', preset.outputVolume.toFixed(1), -135 + preset.outputVolume * 270],
  ];
  if (state.mode === 'performance') return controls[index];
  if (index !== 0 || state.mode === 'tuner') return [0, '0', '0', '—', 0];
  const position = state.mode === 'browse' ? state.browseCursor : state.edit.field;
  const maximum = state.mode === 'browse' ? state.presets.length - 1 : EDIT_FIELDS.length - 1;
  return [position, '0', String(maximum), String(position + 1), -135 + position / Math.max(1, maximum) * 270];
}

function render() {
  screen.innerHTML = state.mode === 'performance' ? performanceScreen() : state.mode === 'browse' ? browseScreen() : state.mode === 'edit' ? editScreen() : tunerScreen();
  modeName.textContent = state.mode[0].toUpperCase() + state.mode.slice(1);
  modeHelp.innerHTML = helpByMode[state.mode].map(item => `<li>${item}</li>`).join('');
  encoders.forEach((button, index) => {
    const [value, minimum, maximum, text, turn] = encoderState(index);
    button.disabled = state.mode === 'tuner' || (state.mode !== 'performance' && index !== 0);
    button.setAttribute('aria-valuemin', minimum);
    button.setAttribute('aria-valuemax', maximum);
    button.setAttribute('aria-valuenow', value);
    button.setAttribute('aria-valuetext', text);
    button.querySelector('.control-value').textContent = text;
    button.querySelector('.knob').style.setProperty('--turn', `${turn}deg`);
  });
}

for (const button of encoders) {
  const encoder = Number(button.dataset.encoder);
  let pointerId = null;
  let lastY = 0;
  let dragged = false;
  let ignoreClick = false;
  let holdTimer;

  button.addEventListener('wheel', event => {
    event.preventDefault();
    dispatch({ type: 'TURN', encoder, steps: event.deltaY < 0 ? 1 : -1 });
  }, { passive: false });
  button.addEventListener('keydown', event => {
    const positive = event.key === 'ArrowUp' || event.key === 'ArrowRight';
    const negative = event.key === 'ArrowDown' || event.key === 'ArrowLeft';
    if (!positive && !negative && event.key !== 'Home' && event.key !== 'End') return;
    event.preventDefault();
    dispatch({ type: 'TURN', encoder, steps: event.key === 'Home' ? -999 : event.key === 'End' ? 999 : positive ? 1 : -1 });
  });
  button.addEventListener('pointerdown', event => {
    if (event.button !== 0) return;
    pointerId = event.pointerId;
    lastY = event.clientY;
    dragged = false;
    ignoreClick = false;
    button.setPointerCapture(pointerId);
    if (encoder === 0) {
      holdTimer = setTimeout(() => {
        if (dragged) return;
        if (state.mode === 'performance') dispatch({ type: 'PRIMARY_CLICK' });
        dispatch({ type: 'PRIMARY_HOLD' });
        ignoreClick = true;
      }, 800);
    }
  });
  button.addEventListener('pointermove', event => {
    if (event.pointerId !== pointerId) return;
    const distance = lastY - event.clientY;
    if (Math.abs(distance) < 12) return;
    const steps = Math.trunc(distance / 12);
    lastY -= steps * 12;
    dragged = true;
    ignoreClick = true;
    clearTimeout(holdTimer);
    dispatch({ type: 'TURN', encoder, steps });
  });
  const release = () => {
    clearTimeout(holdTimer);
    pointerId = null;
    setTimeout(() => { ignoreClick = false; }, 0);
  };
  button.addEventListener('pointerup', release);
  button.addEventListener('pointercancel', release);
  button.addEventListener('lostpointercapture', release);
  button.addEventListener('click', event => {
    if (encoder !== 0 || ignoreClick || dragged) return;
    dispatch({ type: 'PRIMARY_CLICK' });
    event.preventDefault();
  });
}

const pressed = new Map(footswitches.map(button => [button.dataset.action, { button, down: false, held: false, timer: null }]));
let chordTimer = null;
let chordFired = false;

function footAction(name, suffix) {
  return `${name.toUpperCase()}_${suffix}`;
}

function startFootswitch(name, pointerId) {
  const entry = pressed.get(name);
  if (entry.down) return;
  entry.down = true;
  entry.held = false;
  entry.button.dataset.pressed = '';
  entry.button.setPointerCapture?.(pointerId);
  const other = pressed.get(name === 'fs1' ? 'fs2' : 'fs1');
  if (other.down) {
    clearTimeout(entry.timer);
    clearTimeout(other.timer);
    chordFired = false;
    chordTimer = setTimeout(() => {
      if (!entry.down || !other.down) return;
      chordFired = true;
      entry.held = other.held = true;
      dispatch({ type: 'BOTH_HOLD' });
    }, 800);
  } else {
    entry.timer = setTimeout(() => {
      if (!entry.down) return;
      entry.held = true;
      dispatch({ type: footAction(name, 'HOLD') });
    }, 800);
  }
}

function endFootswitch(name, cancelled = false) {
  const entry = pressed.get(name);
  if (!entry.down) return;
  clearTimeout(entry.timer);
  clearTimeout(chordTimer);
  entry.down = false;
  delete entry.button.dataset.pressed;
  if (!cancelled && !entry.held && !chordFired) dispatch({ type: footAction(name, 'TAP') });
  if (![...pressed.values()].some(value => value.down)) chordFired = false;
}

for (const button of footswitches) {
  const name = button.dataset.action;
  button.addEventListener('pointerdown', event => {
    if (event.button === 0) startFootswitch(name, event.pointerId);
  });
  button.addEventListener('pointerup', () => endFootswitch(name));
  button.addEventListener('pointercancel', () => endFootswitch(name, true));
  button.addEventListener('lostpointercapture', () => endFootswitch(name, true));
  button.addEventListener('click', event => {
    if (event.detail === 0) dispatch({ type: footAction(name, 'TAP') });
  });
}

pedal.addEventListener('keydown', event => {
  if (event.key.toLowerCase() === 't') {
    event.preventDefault();
    dispatch({ type: 'BOTH_HOLD' });
  }
});

document.querySelector('#reset-demo').addEventListener('click', () => dispatch({ type: 'RESET' }));
render();
