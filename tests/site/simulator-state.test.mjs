import test from 'node:test';
import assert from 'node:assert/strict';
import { createInitialState, transition } from '../../site/assets/simulator-state.mjs';

test('performance edits clamp, become dirty, save, and revert', () => {
  let state = createInitialState();
  state = transition(state, { type: 'TURN', encoder: 0, steps: 30 });
  assert.equal(state.live.inputGain, 2);
  assert.equal(state.dirty, true);
  state = transition(state, { type: 'FS1_HOLD' });
  assert.equal(state.dirty, false);
  state = transition(state, { type: 'TURN', encoder: 0, steps: -3 });
  state = transition(state, { type: 'FS2_HOLD' });
  assert.equal(state.live.inputGain, 2);
  assert.equal(state.dirty, false);
});

test('preset taps wrap and browse selection is bounded', () => {
  let state = createInitialState();
  state = transition(state, { type: 'FS2_TAP' });
  assert.equal(state.activePreset, state.presets.length - 1);
  state = transition(state, { type: 'PRIMARY_CLICK' });
  state = transition(state, { type: 'TURN', encoder: 0, steps: -99 });
  assert.equal(state.browseCursor, 0);
  state = transition(state, { type: 'PRIMARY_CLICK' });
  assert.equal(state.mode, 'performance');
  assert.equal(state.activePreset, 0);
});

test('browse hold opens edit; cancel discards and apply saves', () => {
  let state = transition(createInitialState(), { type: 'PRIMARY_CLICK' });
  state = transition(state, { type: 'PRIMARY_HOLD' });
  assert.equal(state.mode, 'edit');
  state = transition(state, { type: 'PRIMARY_CLICK' });
  state = transition(state, { type: 'TURN', encoder: 0, steps: 1 });
  const changedModel = state.edit.values.model;
  state = transition(state, { type: 'FS2_TAP' });
  assert.equal(state.mode, 'performance');
  assert.notEqual(state.live.model, changedModel);

  state = transition(state, { type: 'PRIMARY_CLICK' });
  state = transition(state, { type: 'PRIMARY_HOLD' });
  state = transition(state, { type: 'PRIMARY_CLICK' });
  state = transition(state, { type: 'TURN', encoder: 0, steps: 1 });
  state = transition(state, { type: 'FS1_TAP' });
  assert.equal(state.mode, 'performance');
  assert.equal(state.live.model, changedModel);
  assert.equal(state.dirty, false);
});

test('both-switch hold toggles tuner and reset restores defaults', () => {
  let state = transition(createInitialState(), { type: 'BOTH_HOLD' });
  assert.equal(state.mode, 'tuner');
  state = transition(state, { type: 'BOTH_HOLD' });
  assert.equal(state.mode, 'performance');
  state = transition(state, { type: 'FS1_TAP' });
  state = transition(state, { type: 'RESET' });
  assert.deepEqual(state, createInitialState());
});
