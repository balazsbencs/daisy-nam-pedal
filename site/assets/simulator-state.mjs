export const MODELS = ['BE100', 'PT20 Clean', 'JM50 Crunch'];
export const IRS = ['Off', 'Friedman 212', 'V30 412'];
export const EDIT_FIELDS = ['model', 'ir', 'inputGain', 'outputVolume', 'bypass', 'bassFreq', 'midFreq', 'trebleFreq'];

const INITIAL_PRESETS = [
  { name: 'BE100 Lead', model: 'BE100', ir: 'Friedman 212', inputGain: 1, outputVolume: .8, bypass: false, bass: 2, mid: -1, treble: 1, bassFreq: 100, midFreq: 750, trebleFreq: 4000 },
  { name: 'PT20 Clean', model: 'PT20 Clean', ir: 'V30 412', inputGain: .8, outputVolume: .75, bypass: false, bass: 0, mid: 1, treble: 2, bassFreq: 120, midFreq: 800, trebleFreq: 4200 },
  { name: 'JM50 Crunch', model: 'JM50 Crunch', ir: 'Friedman 212', inputGain: 1.2, outputVolume: .7, bypass: false, bass: 1, mid: 2, treble: 0, bassFreq: 100, midFreq: 700, trebleFreq: 3800 },
];

const copy = value => JSON.parse(JSON.stringify(value));
const clamp = (value, minimum, maximum) => Math.min(maximum, Math.max(minimum, value));
const stepped = (value, steps, size, minimum, maximum) => Math.round(clamp(value + steps * size, minimum, maximum) * 1000) / 1000;

export function createInitialState() {
  const presets = copy(INITIAL_PRESETS);
  return {
    mode: 'performance',
    presets,
    activePreset: 0,
    live: copy(presets[0]),
    dirty: false,
    browseCursor: 0,
    edit: null,
  };
}

function loadPreset(state, index) {
  state.activePreset = index;
  state.live = copy(state.presets[index]);
  state.dirty = false;
  state.mode = 'performance';
  state.edit = null;
}

function turnPerformance(state, encoder, steps) {
  const controls = [
    ['inputGain', .1, 0, 2],
    ['bass', 1, -12, 12],
    ['mid', 1, -12, 12],
    ['treble', 1, -12, 12],
    ['outputVolume', .1, 0, 1],
  ];
  const control = controls[encoder];
  if (!control) return false;
  const [field, size, minimum, maximum] = control;
  state.live[field] = stepped(state.live[field], steps, size, minimum, maximum);
  state.dirty = JSON.stringify(state.live) !== JSON.stringify(state.presets[state.activePreset]);
  return true;
}

function turnEdit(state, steps) {
  if (!state.edit.editing) {
    state.edit.field = clamp(state.edit.field + steps, 0, EDIT_FIELDS.length - 1);
    return;
  }
  const field = EDIT_FIELDS[state.edit.field];
  const values = state.edit.values;
  if (field === 'model') {
    values.model = MODELS[clamp(MODELS.indexOf(values.model) + steps, 0, MODELS.length - 1)];
  } else if (field === 'ir') {
    values.ir = IRS[clamp(IRS.indexOf(values.ir) + steps, 0, IRS.length - 1)];
  } else if (field === 'inputGain') {
    values.inputGain = stepped(values.inputGain, steps, .1, 0, 2);
  } else if (field === 'outputVolume') {
    values.outputVolume = stepped(values.outputVolume, steps, .1, 0, 1);
  } else if (field === 'bypass' && steps) {
    values.bypass = !values.bypass;
  } else if (field === 'bassFreq') {
    values.bassFreq = stepped(values.bassFreq, steps, 20, 20, 500);
  } else if (field === 'midFreq') {
    values.midFreq = stepped(values.midFreq, steps, 50, 200, 2000);
  } else if (field === 'trebleFreq') {
    values.trebleFreq = stepped(values.trebleFreq, steps, 100, 1000, 8000);
  }
}

export function transition(current, action) {
  if (action.type === 'RESET') return createInitialState();
  if (action.type === 'BOTH_HOLD') {
    if (current.mode !== 'performance' && current.mode !== 'tuner') return current;
    const state = copy(current);
    state.mode = current.mode === 'tuner' ? 'performance' : 'tuner';
    return state;
  }

  const state = copy(current);
  if (action.type === 'TURN') {
    if (state.mode === 'performance' && turnPerformance(state, action.encoder, action.steps)) return state;
    if (state.mode === 'browse' && action.encoder === 0) {
      state.browseCursor = clamp(state.browseCursor + action.steps, 0, state.presets.length - 1);
      return state;
    }
    if (state.mode === 'edit' && action.encoder === 0) {
      turnEdit(state, action.steps);
      return state;
    }
    return current;
  }

  if (action.type === 'PRIMARY_CLICK') {
    if (state.mode === 'performance') {
      state.mode = 'browse';
      state.browseCursor = state.activePreset;
    } else if (state.mode === 'browse') {
      loadPreset(state, state.browseCursor);
    } else if (state.mode === 'edit') {
      state.edit.editing = !state.edit.editing;
    } else {
      return current;
    }
    return state;
  }

  if (action.type === 'PRIMARY_HOLD' && state.mode === 'browse') {
    state.mode = 'edit';
    state.edit = { presetIndex: state.browseCursor, field: 0, editing: false, values: copy(state.presets[state.browseCursor]) };
    return state;
  }

  if (action.type === 'FS1_TAP') {
    if (state.mode === 'performance') {
      loadPreset(state, (state.activePreset + 1) % state.presets.length);
    } else if (state.mode === 'browse') {
      state.mode = 'performance';
    } else if (state.mode === 'edit') {
      const index = state.edit.presetIndex;
      state.presets[index] = copy(state.edit.values);
      loadPreset(state, index);
    } else {
      return current;
    }
    return state;
  }

  if (action.type === 'FS2_TAP') {
    if (state.mode === 'performance') {
      loadPreset(state, (state.activePreset - 1 + state.presets.length) % state.presets.length);
    } else if (state.mode === 'browse' || state.mode === 'edit') {
      state.mode = 'performance';
      state.edit = null;
    } else {
      return current;
    }
    return state;
  }

  if (action.type === 'FS1_HOLD' && state.mode === 'performance') {
    state.presets[state.activePreset] = copy(state.live);
    state.dirty = false;
    return state;
  }
  if (action.type === 'FS2_HOLD' && state.mode === 'performance') {
    state.live = copy(state.presets[state.activePreset]);
    state.dirty = false;
    return state;
  }
  return current;
}
