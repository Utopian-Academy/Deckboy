/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Deckboy Contributors */
'use strict';
(() => {
  const el = id => document.getElementById(id);
  const KEY = 'deckboy-phone-key';
  let pin = '', key = '', state = null, busy = false, connected = false, generation = 0, tab = 'decks';
  let deckIndex = 0, chipShape = '', cueShape = '', dashShape = '', outShape = '';
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

  function setTab(name) {
    tab = name;
    for (const b of el('tabs').querySelectorAll('button')) b.setAttribute('aria-pressed', String(b.dataset.tab === name));
    for (const page of document.querySelectorAll('[data-page]')) page.hidden = page.dataset.page !== name;
  }

  function cueButton(cls, n, title, tag, onClick) {
    const b = document.createElement('button');
    b.className = cls;
    const a = document.createElement('span'); a.className = 'n'; a.textContent = n;
    const t = document.createElement('span'); t.className = 't'; t.textContent = title;
    const g = document.createElement('span'); g.className = 'tag'; g.textContent = tag;
    b.append(a, t, g);
    b.addEventListener('click', onClick);
    return b;
  }

  function draw() {
    // Tabs the show and the level call for.
    const tracker = state.tracker || { steps: 0 };
    for (const b of el('tabs').querySelectorAll('button')) {
      b.hidden = (b.dataset.tab === 'tracker' && !tracker.steps) || (b.dataset.tab === 'show' && state.level < 1);
    }
    if (el('tabs').querySelector(`[data-tab="${tab}"]`).hidden) setTab('decks');

    // Decks.
    const decks = state.decks || [];
    if (!decks.some(d => d.index === deckIndex)) deckIndex = state.focus || (decks[0] && decks[0].index) || 0;
    const chips = JSON.stringify(decks.map(d => [d.index, d.name]));
    if (chips !== chipShape) {
      el('deck-chips').replaceChildren(...decks.map(d => {
        const b = document.createElement('button');
        b.dataset.deck = d.index;
        b.textContent = (d.name || `Deck ${d.index}`).toUpperCase();
        b.addEventListener('click', () => { deckIndex = d.index; buzz('tap'); draw(); });
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
      say('now-status', String(deck.status || '').toUpperCase());
      say('now-time', deck.length > 0 ? `${clock(deck.elapsed)} / ${clock(deck.length)}` : (live ? clock(deck.elapsed) : ''));
      say('now-name', live ? live.name || `Cue ${live.index}` : 'Nothing live');
      el('now-bar').style.width = deck.length > 0 ? `${Math.min(100, 100 * deck.elapsed / deck.length)}%` : '0';
      say('now-next', next ? next.name || `Cue ${next.index}` : '-');
      const shape = JSON.stringify([deck.index, deck.cues]);
      if (shape !== cueShape) {
        el('cues').replaceChildren(...deck.cues.map(c => cueButton('', String(c.index).padStart(2, '0'),
          c.name || `Cue ${c.index}`, '', () => { buzz('tap'); send(`DECK ${deck.index} SELECT ${c.index}`); })));
        cueShape = shape;
      }
      for (const [i, b] of [...el('cues').children].entries()) {
        const c = deck.cues[i];
        const isLive = c.index === deck.active, isNext = !isLive && c.index === nextIndex;
        b.className = isLive ? 'live' : isNext ? 'next' : '';
        b.setAttribute('aria-pressed', String(c.index === deck.selected));
        b.querySelector('.tag').textContent = isLive ? 'LIVE' : isNext ? 'NEXT' : '';
      }
    }

    // Tracker.
    if (tracker.steps) {
      const names = tracker.names || [];
      say('trk-state', tracker.playing ? 'PLAYING' : 'HOLDING');
      say('trk-count', `STEP ${tracker.step || '-'} / ${tracker.steps}`);
      say('trk-name', tracker.step ? names[tracker.step - 1] || `Step ${tracker.step}` : 'Not started');
      say('trk-next', names[tracker.step] !== undefined ? names[tracker.step] || `Step ${tracker.step + 1}` : '-');
    }

    // Dash.
    const dash = JSON.stringify(state.dash);
    if (dash !== dashShape) {
      el('dash').replaceChildren(...state.dash.map(slot => {
        const b = document.createElement('button');
        b.textContent = (slot.label || `DASH ${slot.index}`).toUpperCase();
        b.addEventListener('click', () => { buzz('go'); send(`DASH ${slot.index}`); });
        return b;
      }));
      if (!state.dash.length) el('dash').textContent = 'No DASH buttons on the desk yet.';
      dashShape = dash;
    }

    // Show.
    el('blackout').classList.toggle('on', !!state.blackout);
    el('blackout').textContent = state.blackout ? 'BLACKOUT ON' : 'BLACKOUT';
    el('testcard').classList.toggle('on', !!state.testcard);
    el('full').hidden = state.level < 2;
    const outs = JSON.stringify((state.outputs || []).map(o => [o.index, o.name]));
    if (outs !== outShape) {
      el('outputs').replaceChildren(...(state.outputs || []).map(o => {
        const row = document.createElement('div'); row.className = 'item';
        const name = document.createElement('span'); name.textContent = o.name;
        const b = document.createElement('button'); b.className = 'toggle hold'; b.dataset.output = o.index;
        holdToFire(b, () => send(`OUTPUT ${o.index} ${b.classList.contains('on') ? 'OFF' : 'ON'}`));
        row.append(name, b);
        return row;
      }));
      outShape = outs;
    }
    for (const b of el('outputs').querySelectorAll('button')) {
      const o = (state.outputs || []).find(x => x.index === Number(b.dataset.output));
      b.classList.toggle('on', !!(o && o.on)); b.textContent = o && o.on ? 'ON' : 'OFF';
    }
    el('record').classList.toggle('on', !!state.recording);
    el('record').textContent = state.recording ? 'REC' : 'OFF';
    enable();
  }

  function enable() {
    for (const b of el('controls').querySelectorAll('button')) {
      if (b.id === 'lock' || b.parentElement === el('tabs')) continue;
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

  // Hold half a second: a pocket cannot black out the room.
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
  for (const b of el('tabs').querySelectorAll('button')) b.addEventListener('click', () => { buzz('tap'); setTab(b.dataset.tab); });
  el('go').addEventListener('click', () => { buzz('go'); send(`DECK ${deckIndex} GO`); });
  for (const b of document.querySelectorAll('[data-verb]')) {
    b.addEventListener('click', () => { buzz('tap'); send(`DECK ${deckIndex} ${b.dataset.verb}`); });
  }
  el('trk-go').addEventListener('click', () => { buzz('go'); send('TRACKER GO'); });
  for (const b of document.querySelectorAll('[data-tracker]')) {
    b.addEventListener('click', () => { buzz('tap'); send(`TRACKER ${b.dataset.tracker}`); });
  }
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
    const match = /(?:^|[#&])pair=([0-9a-f]{32})/.exec(location.hash);
    if (match) {
      history.replaceState(null, '', location.pathname);
      try {
        const got = await request('/remote/pair', '', { 'X-Deckboy-Pair': match[1] });
        key = got.trim(); store.set(key);
      } catch (error) {
        showLogin(error.message.replace(/^ERR remote: /, ''));
      }
    } else {
      key = store.get();
    }
    if (key) { say('login-status', 'Connecting...'); await refresh(); }
    setTimeout(poll, 750);
  })();
})();
