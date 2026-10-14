/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Deckboy Contributors */
'use strict';
(() => {
  const el = id => document.getElementById(id);
  let pin = '', state = null, busy = false, connected = false, generation = 0;
  let deckShape = '', cueShape = '', dashShape = '';
  const say = (id, text) => { if (el(id).textContent !== text) el(id).textContent = text; };
  const selectedDeck = () => state?.decks.find(d => d.index === Number(el('deck').value));
  function enable() {
    for (const button of document.querySelectorAll('[data-verb], #cues button, #dash button')) {
      button.disabled = !connected || busy;
    }
    el('deck').disabled = !connected || busy;
  }
  async function request(path, body = '') {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 6000);
    try {
      const response = await fetch(path, { method: 'POST', cache: 'no-store',
        headers: { 'X-Deckboy-Pin': encodeURIComponent(pin), 'Content-Type': 'text/plain' },
        body, signal: controller.signal });
      const text = await response.text();
      if (!response.ok) throw new Error(text.trim() || `HTTP ${response.status}`);
      return text;
    } finally { clearTimeout(timer); }
  }
  function draw() {
    const decks = JSON.stringify(state.decks.map(d => [d.index, d.name]));
    if (decks !== deckShape) {
      const old = Number(el('deck').value) || state.focus;
      el('deck').replaceChildren(...state.decks.map(d => new Option(d.name || `Deck ${d.index}`, d.index)));
      el('deck').value = state.decks.some(d => d.index === old) ? old : state.focus;
      deckShape = decks;
    }
    const deck = selectedDeck();
    if (!deck) return;
    say('transport', `${deck.status} | selected ${deck.selected || '-'} | active ${deck.active || '-'} | standby ${deck.standby || '-'}`);
    const cues = JSON.stringify([deck.index, deck.cues]);
    if (cues !== cueShape) {
      el('cues').replaceChildren(...deck.cues.map(c => {
        const button = document.createElement('button');
        button.dataset.cue = c.index;
        button.onclick = () => send(`DECK ${deck.index} SELECT ${c.index}`);
        return button;
      }));
      cueShape = cues;
    }
    for (const [i, button] of [...el('cues').children].entries()) {
      const cue = deck.cues[i];
      const label = `${cue.index}. ${cue.name}${cue.index === deck.active ? ' [ACTIVE]' : ''}${cue.index === deck.standby ? ' [STANDBY]' : ''}`;
      if (button.textContent !== label) button.textContent = label;
      button.setAttribute('aria-pressed', String(cue.index === deck.selected));
    }
    const dash = JSON.stringify(state.dash);
    if (dash !== dashShape) {
      el('dash').replaceChildren(...state.dash.map(slot => {
        const button = document.createElement('button');
        button.textContent = slot.label || `DASH ${slot.index}`;
        button.onclick = () => send(`DASH ${slot.index}`);
        return button;
      }));
      if (!state.dash.length) el('dash').textContent = 'No DASH buttons configured on the desk.';
      dashShape = dash;
    }
    enable();
  }
  async function refresh() {
    const current = generation;
    try {
      const next = JSON.parse(await request('/remote/state'));
      if (current !== generation) return;
      state = next; connected = true;
      el('login').hidden = true; el('controls').hidden = false;
      say('connection', 'Connected'); draw();
    } catch (error) {
      if (current !== generation) return;
      connected = false;
      say('connection', `Disconnected: ${error.message}`); enable();
    }
  }
  async function send(command) {
    if (!connected || busy) return;
    const current = generation;
    busy = true; enable(); say('result', 'Sending...');
    try {
      const reply = await request('/remote/command', command);
      if (current === generation) say('result', reply.trim());
    } catch (error) {
      if (current === generation) {
        connected = false;
        say('result', `${error.message}. Check the desk before retrying.`);
      }
    } finally {
      if (current === generation) { busy = false; enable(); }
    }
  }
  el('login').onsubmit = async event => {
    event.preventDefault();
    if (!el('pin').value) return;
    pin = el('pin').value; el('pin').value = ''; ++generation;
    say('connection', 'Connecting...'); await refresh();
  };
  el('lock').onclick = () => {
    pin = ''; ++generation; connected = false; busy = false; state = null;
    el('controls').hidden = true; el('login').hidden = false;
    say('connection', 'Locked'); say('result', ''); el('pin').focus();
  };
  el('deck').onchange = draw;
  for (const button of document.querySelectorAll('[data-verb]')) {
    button.onclick = () => {
      const deck = selectedDeck();
      if (deck) send(`DECK ${deck.index} ${button.dataset.verb}`);
    };
  }
  async function poll() {
    if (pin && !busy && !document.hidden) await refresh();
    setTimeout(poll, 750);
  }
  document.addEventListener('visibilitychange', () => {
    connected = false; enable();
    if (pin && !document.hidden) say('connection', 'Reconnecting...');
  });
  setTimeout(poll, 750);
})();
