/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Deckboy Contributors */
'use strict';
(() => {
  const el = id => document.getElementById(id);
  const KEY = 'deckboy-phone-key';
  let pin = '', key = '', state = null, busy = false, connected = false, generation = 0;
  let deckIndex = 0, chipShape = '', cueShape = '', dashShape = '', outShape = '';
  let faderHeld = false, faderTimer = 0, editing = 0;
  const store = {
    get() { try { return localStorage.getItem(KEY) || ''; } catch { return ''; } },
    set(v) { try { v ? localStorage.setItem(KEY, v) : localStorage.removeItem(KEY); } catch { /* private mode */ } }
  };

  // ── HAPTICS ────────────────────────────────────────────────────────────
  // Small and built up, never a constant buzz. Android vibrates patterns;
  // iPhone browsers have no vibrate, but Safari ticks when a switch toggles,
  // so a hidden switch gives a single tick there.
  const iosTick = el('tick');
  const feel = {
    tap: [12], go: [24], ok: [10, 40, 10], arm: [8, 70, 8, 50, 8, 30, 14], fired: [30],
    error: [40, 60, 40, 60, 40]
  };
  function buzz(kind) {
    if (!state || state.haptics === false) return;
    const pattern = feel[kind] || feel.tap;
    if (navigator.vibrate) { navigator.vibrate(pattern); return; }
    if (iosTick && kind !== 'error') iosTick.click();
  }

  const say = (id, text) => { const node = el(id); if (node && node.textContent !== text) node.textContent = text; };
  const clock = s => { s = Math.max(0, Math.floor(s || 0)); return `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`; };

  async function request(path, body = '', extra = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 6000);
    const headers = { 'Content-Type': 'text/plain', ...extra };
    if (key) headers['X-Deckboy-Key'] = key; else if (pin) headers['X-Deckboy-Pin'] = encodeURIComponent(pin);
    try {
      const response = await fetch(path, { method: 'POST', cache: 'no-store', headers, body, signal: controller.signal });
      const text = await response.text();
      if (!response.ok) { const e = new Error(text.trim() || `HTTP ${response.status}`); e.status = response.status; throw e; }
      return text;
    } finally { clearTimeout(timer); }
  }

  function showLogin(message) {
    el('controls').hidden = true; el('login').hidden = false;
    say('login-status', message || '');
  }

  // Tap does one thing, a held press another (half a second, with the arming
  // buzz), on the same button: cues are standby / take-now, DASH is fire / edit.
  function tapOrHold(button, onTap, onHold) {
    let timer = 0, held = false;
    button.addEventListener('pointerdown', () => {
      if (button.disabled) return;
      held = false;
      timer = setTimeout(() => { timer = 0; held = true; buzz('fired'); onHold(); }, 500);
    });
    const cancel = () => { if (timer) { clearTimeout(timer); timer = 0; } };
    for (const type of ['pointerup', 'pointerleave', 'pointercancel']) button.addEventListener(type, cancel);
    button.addEventListener('click', event => { event.preventDefault(); if (!held) onTap(); held = false; });
    button.addEventListener('contextmenu', event => event.preventDefault());
  }

  function cueRow(c) {
    const b = document.createElement('button');
    const n = document.createElement('span'); n.className = 'n'; n.textContent = String(c.index).padStart(2, '0');
    const t = document.createElement('span'); t.className = 't'; t.textContent = c.name || `Cue ${c.index}`;
    const len = document.createElement('span'); len.className = 'len'; len.textContent = c.len > 0 ? clock(c.len) : '';
    const g = document.createElement('span'); g.className = 'tag';
    b.append(n, t, len, g);
    b.dataset.cue = c.index;
    tapOrHold(b, () => { buzz('tap'); send(`DECK ${deckIndex} STANDBY ${c.index}`); },
                 () => send(`DECK ${deckIndex} TAKE ${c.index}`));
    return b;
  }

  // What a DASH button can be set to from the phone: the phone's own commands.
  function dashChoices() {
    const out = [];
    for (const d of state.decks || []) {
      const deck = d.name || `Deck ${d.index}`;
      out.push([`DECK ${d.index} GO`, `${deck}: TAKE`], [`DECK ${d.index} STOP`, `${deck}: STOP`],
               [`DECK ${d.index} PAUSE`, `${deck}: PAUSE`], [`DECK ${d.index} PLAY`, `${deck}: PLAY`]);
      for (const c of d.cues) out.push([`DECK ${d.index} TAKE ${c.index}`, `${deck}: take ${c.index} ${c.name || ''}`]);
    }
    out.push(['TRACKER GO', 'Tracker: GO'], ['TRACKER BACK', 'Tracker: BACK'], ['TRACKER PLAY', 'Tracker: PLAY'],
             ['TRACKER STOP', 'Tracker: STOP'], ['BLACKOUT ON', 'Blackout on'], ['BLACKOUT OFF', 'Blackout off'],
             ['OUTPUT TESTCARD ALL ON', 'Test card on'], ['OUTPUT TESTCARD ALL OFF', 'Test card off'],
             ['RECORD ON', 'Record on'], ['RECORD OFF', 'Record off']);
    return out;
  }

  function openDashEdit(slot) {
    editing = slot.index;
    say('dash-edit-title', slot.command ? `DASH ${slot.index}` : `NEW DASH ${slot.index}`);
    el('dash-label').value = slot.label || '';
    const select = el('dash-action');
    select.replaceChildren(...dashChoices().map(([value, label]) => {
      const o = document.createElement('option'); o.value = value; o.textContent = label; return o;
    }));
    if (slot.command && [...select.options].some(o => o.value === slot.command)) select.value = slot.command;
    el('dash-clear').hidden = !slot.command;
    el('dash-edit').hidden = false;
    el('dash-label').focus();
  }

  let extrasShape = '';
  function drawExtras() {
    const extras = state.extras || [];
    const shape = JSON.stringify(extras);
    if (shape === extrasShape) return;
    extrasShape = shape;
    el('extras').hidden = !extras.length;
    el('extras').replaceChildren(...extras.map(x => {
      const b = document.createElement('button');
      b.className = 'on';
      b.textContent = String(x.name).replace(/\s*\(.*\)$/, '').toUpperCase() + ' REMOTE';
      b.addEventListener('click', () => { buzz('tap'); location.href = '/remote/x/' + encodeURIComponent(x.page); });
      return b;
    }));
  }

  function draw() {
    drawExtras();
    // Decks.
    const decks = state.decks || [];
    if (!decks.some(d => d.index === deckIndex)) deckIndex = state.focus || (decks[0] && decks[0].index) || 0;
    const chips = JSON.stringify(decks.map(d => [d.index, d.name]));
    if (chips !== chipShape) {
      el('deck-chips').replaceChildren(...decks.map(d => {
        const b = document.createElement('button');
        b.dataset.deck = d.index;
        b.textContent = (d.name || `Deck ${d.index}`).toUpperCase();
        b.addEventListener('click', () => { deckIndex = d.index; buzz('tap'); cueShape = ''; draw(); });
        return b;
      }));
      chipShape = chips;
    }
    el('deck-chips').hidden = decks.length < 2;
    for (const b of el('deck-chips').children) b.setAttribute('aria-pressed', String(Number(b.dataset.deck) === deckIndex));
    const deck = decks.find(d => d.index === deckIndex);
    if (deck) {
      const live = deck.cues.find(c => c.index === deck.active);
      const nextIndex = deck.standby || deck.selected;
      const next = deck.cues.find(c => c.index === nextIndex && c.index !== deck.active) ||
                   deck.cues.find(c => c.index === deck.active + 1);
      const status = String(deck.status || '').toUpperCase();
      say('now-status', status);
      say('now-time', deck.length > 0 ? `${clock(deck.elapsed)} / ${clock(deck.length)}` : (live ? clock(deck.elapsed) : ''));
      say('now-name', live ? live.name || `Cue ${live.index}` : 'Nothing live');
      el('now-bar').style.width = deck.length > 0 ? `${Math.min(100, 100 * deck.elapsed / deck.length)}%` : '0';
      say('now-next', next ? next.name || `Cue ${next.index}` : '-');
      const playing = status.includes('PLAY') && !status.includes('PAUSE');
      say('playpause', playing ? 'II' : '>');
      el('playpause').setAttribute('aria-label', playing ? 'Pause' : 'Play');
      if (!faderHeld) { el('fader').value = String(deck.volume ?? 100); say('fader-value', `${deck.volume ?? 100}%`); }
      const shape = JSON.stringify([deck.index, deck.cues]);
      if (shape !== cueShape) { el('cues').replaceChildren(...deck.cues.map(cueRow)); cueShape = shape; }
      for (const b of el('cues').children) {
        const index = Number(b.dataset.cue);
        const isLive = index === deck.active, isNext = !isLive && index === nextIndex;
        b.className = isLive ? 'live' : isNext ? 'standby' : '';
        b.querySelector('.tag').textContent = isLive ? 'LIVE' : isNext ? 'SB' : '';
      }
    }

    // Tracker, if the show has one.
    const tracker = state.tracker || { steps: 0 };
    el('tracker-block').hidden = !tracker.steps;
    if (tracker.steps) {
      const names = tracker.names || [];
      say('trk-count', `STEP ${tracker.step || '-'} / ${tracker.steps}${tracker.playing ? ' - PLAYING' : ''}`);
      say('trk-name', tracker.step ? names[tracker.step - 1] || `Step ${tracker.step}` : 'Not started');
      say('trk-next', names[tracker.step] !== undefined ? names[tracker.step] || `Step ${tracker.step + 1}` : '-');
      say('trk-play', tracker.playing ? 'STOP' : 'PLAY');
    }

    // DASH: every button the desk has, and + for a new one. Hold to change.
    const slots = (state.dash || []).filter(s => s.command);
    const dash = JSON.stringify(slots);
    if (dash !== dashShape) {
      const buttons = slots.map(slot => {
        const b = document.createElement('button');
        b.textContent = (slot.label || `DASH ${slot.index}`).toUpperCase();
        tapOrHold(b, () => { buzz('go'); send(`DASH ${slot.index}`); }, () => openDashEdit(slot));
        return b;
      });
      const add = document.createElement('button');
      add.textContent = '+'; add.setAttribute('aria-label', 'Add a DASH button');
      add.addEventListener('click', () => {
        const all = state.dash || [];
        const free = all.find(s => !s.command);
        buzz('tap'); openDashEdit({ index: free ? free.index : all.length + 1, label: '', command: '' });
      });
      el('dash').replaceChildren(...buttons, add);
      dashShape = dash;
    }

    // Show.
    el('blackout').classList.toggle('on', !!state.blackout);
    say('blackout', state.blackout ? 'BLACK ON' : 'BLACKOUT');
    el('testcard').classList.toggle('on', !!state.testcard);
    el('record').classList.toggle('on', !!state.recording);
    say('record', state.recording ? 'REC ON' : 'REC');
    const outs = JSON.stringify((state.outputs || []).map(o => [o.index, o.name]));
    if (outs !== outShape) {
      el('outputs').replaceChildren(...(state.outputs || []).map(o => {
        const b = document.createElement('button'); b.dataset.output = o.index;
        b.textContent = String(o.name || `OUT ${o.index}`).toUpperCase();
        b.addEventListener('click', () => { buzz('tap'); send(`OUTPUT ${o.index} ${b.classList.contains('on') ? 'OFF' : 'ON'}`); });
        return b;
      }));
      outShape = outs;
    }
    for (const b of el('outputs').children) {
      const o = (state.outputs || []).find(x => x.index === Number(b.dataset.output));
      b.classList.toggle('on', !!(o && o.on));
    }
    el('anywhere').hidden = !state.anywhere;
    say('anywhere', state.anywhere ? `From anywhere (Tailscale): ${state.anywhere}` : '');
    enable();
  }

  function enable() {
    for (const b of el('controls').querySelectorAll('button, input, select')) {
      if (b.id === 'lock') continue;
      b.disabled = !connected;
    }
  }

  async function refresh() {
    const current = generation;
    try {
      const next = JSON.parse(await request('/remote/state'));
      if (current !== generation) return;
      const wasConnected = connected;
      state = next; connected = true;
      el('login').hidden = true; el('controls').hidden = false;
      say('link', 'LINK OK');
      if (!wasConnected) buzz('ok');
      draw();
    } catch (error) {
      if (current !== generation) return;
      if (error.status === 401 && key) { key = ''; store.set(''); showLogin(error.message.replace(/^ERR remote: /, '')); return; }
      if (error.status === 401 || error.status === 403) { showLogin(error.message.replace(/^ERR remote: /, '')); pin = ''; return; }
      if (connected) buzz('error');
      connected = false; say('link', 'NO LINK'); enable();
    }
  }

  async function send(command) {
    if (!connected || busy) return;
    const current = generation;
    busy = true;
    try {
      const reply = await request('/remote/command', command);
      if (current === generation) { say('result', reply.trim()); el('result').classList.remove('err'); buzz('ok'); }
      refresh();
    } catch (error) {
      if (current === generation) {
        say('result', error.message.replace(/^ERR remote: /, ''));
        el('result').classList.add('err'); buzz('error');
      }
    } finally { if (current === generation) busy = false; }
  }

  // Hold half a second: a pocket cannot black out the room or start a recording.
  function holdToFire(button, fire) {
    let timer = 0;
    const cancel = () => { clearTimeout(timer); timer = 0; button.classList.remove('arming'); };
    button.addEventListener('pointerdown', event => {
      if (button.disabled) return;
      event.preventDefault();
      button.classList.add('arming'); buzz('arm');
      timer = setTimeout(() => { cancel(); buzz('fired'); fire(); }, 500);
    });
    for (const type of ['pointerup', 'pointerleave', 'pointercancel']) button.addEventListener(type, cancel);
    button.addEventListener('click', event => event.preventDefault());
    button.addEventListener('keydown', event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); fire(); } });
  }

  // Wiring.
  el('pin-form').addEventListener('submit', async event => {
    event.preventDefault();
    if (!el('pin').value) return;
    pin = el('pin').value; key = ''; el('pin').value = ''; ++generation;
    say('login-status', 'Connecting...'); await refresh();
  });
  el('lock').addEventListener('click', () => {
    pin = ''; key = ''; store.set(''); ++generation; connected = false; state = null;
    say('result', ''); showLogin('Locked. Scan the desk\'s code or enter the passphrase.');
  });
  el('go').addEventListener('click', () => { buzz('go'); send(`DECK ${deckIndex} GO`); });
  for (const b of document.querySelectorAll('[data-verb]')) {
    b.addEventListener('click', () => { buzz('tap'); send(`DECK ${deckIndex} ${b.dataset.verb}`); });
  }
  el('playpause').addEventListener('click', () => {
    buzz('tap'); send(`DECK ${deckIndex} ${el('playpause').textContent === 'II' ? 'PAUSE' : 'PLAY'}`);
  });
  for (const b of document.querySelectorAll('[data-seek]')) {
    b.addEventListener('click', () => {
      const deck = state && (state.decks || []).find(d => d.index === deckIndex);
      if (!deck) return;
      const to = Math.max(0, (deck.elapsed || 0) + Number(b.dataset.seek));
      buzz('tap'); send(`DECK ${deckIndex} SEEK ${to.toFixed(1)}`);
    });
  }
  // The fader sends while it moves, a few times a second, and the poll does
  // not pull it back under the finger.
  el('fader').addEventListener('input', () => {
    faderHeld = true; say('fader-value', `${el('fader').value}%`);
    if (!faderTimer) faderTimer = setTimeout(() => { faderTimer = 0; send(`DECK ${deckIndex} VOLUME ${el('fader').value}`); }, 150);
  });
  el('fader').addEventListener('change', () => {
    send(`DECK ${deckIndex} VOLUME ${el('fader').value}`); setTimeout(() => { faderHeld = false; }, 800);
  });
  el('trk-go').addEventListener('click', () => { buzz('go'); send('TRACKER GO'); });
  el('trk-play').addEventListener('click', () => {
    buzz('tap'); send(`TRACKER ${state && state.tracker && state.tracker.playing ? 'STOP' : 'PLAY'}`);
  });
  for (const b of document.querySelectorAll('[data-tracker]')) {
    b.addEventListener('click', () => { buzz('tap'); send(`TRACKER ${b.dataset.tracker}`); });
  }
  el('dash-edit').addEventListener('submit', event => {
    event.preventDefault();
    const label = el('dash-label').value.replace(/[|\r\n]/g, ' ').trim() || 'DASH';
    send(`DASH SET ${editing} ${label} | ${el('dash-action').value}`);
    el('dash-edit').hidden = true; dashShape = '';
  });
  el('dash-clear').addEventListener('click', () => { send(`DASH CLEAR ${editing}`); el('dash-edit').hidden = true; dashShape = ''; });
  el('dash-cancel').addEventListener('click', () => { el('dash-edit').hidden = true; });
  holdToFire(el('blackout'), () => send(`BLACKOUT ${state && state.blackout ? 'OFF' : 'ON'}`));
  el('testcard').addEventListener('click', () => { buzz('tap'); send(`OUTPUT TESTCARD ALL ${state && state.testcard ? 'OFF' : 'ON'}`); });
  holdToFire(el('record'), () => send(`RECORD ${state && state.recording ? 'OFF' : 'ON'}`));

  async function poll() {
    if ((pin || key) && !busy && !document.hidden) await refresh();
    setTimeout(poll, 750);
  }
  document.addEventListener('visibilitychange', () => { if (!document.hidden && (pin || key)) refresh(); });

  // PAIRING: the desk's QR carries a one-time code in the address's #part,
  // which never leaves the phone except to this one request. Trade it for a
  // key of this phone's own, keep that, and wipe the code from the address.
  (async () => {
    // A code that is spent (scanned before, or older than two minutes) is no
    // reason to lock out a phone that is ALREADY paired: fall back to the key
    // it holds. Only a phone with neither is sent to the login.
    const match = /(?:^|[#&])pair=([0-9a-f]{32})/.exec(location.hash);
    if (match) {
      history.replaceState(null, '', location.pathname);
      try {
        const got = await request('/remote/pair', '', { 'X-Deckboy-Pair': match[1] });
        key = got.trim(); store.set(key);
      } catch (error) {
        key = store.get();
        if (!key) showLogin(error.message.replace(/^ERR remote: /, ''));
      }
    } else {
      key = store.get();
    }
    if (key) { say('login-status', 'Connecting...'); await refresh(); }
    setTimeout(poll, 750);
  })();
})();
