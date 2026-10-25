R"JS(
'use strict';
(() => {
  const el = id => document.getElementById(id);
  let key = '';
  try { key = localStorage.getItem('deckboy-phone-key') || ''; } catch {}
  let st = null, mapShape = '', planShape = '', tacShape = '', manualShape = '', elementShape = '', secShape = '', missionShape = '';
  // MEASURING: a mode, then taps on the map. A ruler takes two taps.
  let measure = '', first = null, move = 5, ringSet = 0;
  const ringSets = [['3-12', '3,6,9,12'], ['6-24', '6,12,18,24'], ['1-6', '1,2,3,6']];
  async function send(cmd) {
    if (!key) { el('pair').hidden = false; return null; }
    try {
      const r = await fetch('/remote/command', {method:'POST', cache:'no-store',
        headers:{'Content-Type':'text/plain','X-Deckboy-Key':key}, body:cmd});
      const t = (await r.text()).trim();
      if (r.status === 401) { el('pair').hidden = false; return null; }
      if (!cmd.endsWith('STATUS')) { el('result').textContent = t; el('result').classList.toggle('err', !r.ok); }
      return r.ok ? t : null;
    } catch { el('link').textContent = 'NO LINK'; return null; }
  }
  function hold(b, tap, held) {
    let timer = 0, fired = false;
    b.addEventListener('pointerdown', () => { fired = false; timer = setTimeout(() => { fired = true; held(); if (navigator.vibrate) navigator.vibrate(30); }, 500); });
    for (const e of ['pointerup','pointerleave','pointercancel']) b.addEventListener(e, () => clearTimeout(timer));
    b.addEventListener('click', e => { e.preventDefault(); if (!fired) tap(); });
    b.addEventListener('contextmenu', e => e.preventDefault());
  }
  // Scoring lines the table cannot judge: a button per side.
  function drawManual() {
    const lines = (st.scores || []).map((sc, i) => [sc, i]).filter(([sc]) => sc[1] === 'MANUAL');
    const shape = JSON.stringify([st.plan, lines.length, st.p1, st.p2]);
    if (shape === manualShape) return;
    manualShape = shape;
    el('manual').replaceChildren(...lines.flatMap(([sc, i]) => {
      const p = document.createElement('p'); p.textContent = sc[2];
      const row = document.createElement('div'); row.className = 'row2';
      for (const [who, name] of [['P1', st.p1], ['P2', st.p2]]) {
        const b = document.createElement('button'); b.textContent = name.slice(0, 3) + ' +' + sc[0];
        b.addEventListener('click', () => send('TABLE SCORE ' + (i + 1) + ' ' + who).then(poll));
        row.appendChild(b);
      }
      return [p, row];
    }));
  }
  // Battle tactics: each player's two cards (tap to change), the three
  // tactics on each, the one being gone for, and COMPLETED to bank it.
  function drawTactics() {
    const noTactics = !!st.pack || st.system === '40k';
    el('tactics').hidden = el('tacticslabel').hidden = noTactics;
    if (noTactics) return;
    const shape = JSON.stringify([st.tac, st.p1, st.p2]);
    if (shape === tacShape) return;
    tacShape = shape;
    el('tactics').replaceChildren(...[0, 1].map(pl => {
      const t = st.tac[pl], P = pl ? 'P2' : 'P1', box = document.createElement('div'); box.className = 'tac';
      const who = document.createElement('p'); who.className = 'who';
      who.textContent = (pl ? st.p2 : st.p1) + ' - ' + t.done + ' COMPLETED';
      box.appendChild(who);
      const cards = document.createElement('div'); cards.className = 'row2';
      [0, 1].forEach(slot => {
        const b = document.createElement('button');
        b.textContent = 'CARD ' + (slot + 1) + ': ' + (t.card[slot] >= 0 ? st.cards[t.card[slot]].name : 'NONE');
        b.addEventListener('click', () => send('TABLE TACTIC CARD ' + P + ' ' + (slot + 1) + ' NEXT').then(poll));
        cards.appendChild(b);
      });
      box.appendChild(cards);
      [0, 1].forEach(slot => {
        if (t.card[slot] < 0) return;
        const row = document.createElement('div'); row.className = 'row3'; row.style.marginBottom = '6px';
        st.cards[t.card[slot]].t.forEach((tc, k) => {
          const b = document.createElement('button');
          b.textContent = tc[0] + ' (' + tc[2] + ')';
          if (t.slot === slot && t.pick === k) b.className = 'on';
          b.addEventListener('click', () => send('TABLE TACTIC PICK ' + P + ' ' + (slot + 1) + ' ' + (k + 1)).then(poll));
          row.appendChild(b);
        });
        box.appendChild(row);
      });
      if (t.slot >= 0 && t.pick >= 0 && t.card[t.slot] >= 0) {
        const tc = st.cards[t.card[t.slot]].t[t.pick];
        const cond = document.createElement('p'); cond.className = 'cond'; cond.textContent = tc[3];
        const row = document.createElement('div'); row.className = 'row2';
        const done = document.createElement('button'); done.className = 'on'; done.textContent = 'COMPLETED +' + tc[1];
        done.addEventListener('click', () => send('TABLE TACTIC DONE ' + P).then(poll));
        const clear = document.createElement('button'); clear.textContent = 'CLEAR';
        clear.addEventListener('click', () => send('TABLE TACTIC PICK ' + P + ' NONE').then(poll));
        row.append(done, clear);
        box.append(cond, row);
      }
      return box;
    }));
  }

  // 40k secondaries: each player's hand, by name. SCORE asks the points.
  function drawSecondaries() {
    const shape = JSON.stringify([st.sec, st.p1, st.p2]);
    if (shape === secShape) return;
    secShape = shape;
    el('sec').replaceChildren(...[0, 1].map(pl => {
      const P = pl ? 'P2' : 'P1', s = st.sec[pl], box = document.createElement('div'); box.className = 'sec';
      const who = document.createElement('p'); who.className = 'who';
      const total = s.scored.reduce((a, c) => a + c[1], 0);
      who.textContent = (pl ? st.p2 : st.p1) + ' - ' + s.scored.length + ' SCORED, ' + total + ' VP';
      who.style.color = st.colors[pl];
      box.appendChild(who);
      s.active.forEach((name, i) => {
        const row = document.createElement('div'); row.className = 'card';
        const label = document.createElement('span'); label.textContent = name;
        const score = document.createElement('button'); score.className = 'on'; score.textContent = 'SCORE';
        score.addEventListener('click', () => {
          const vp = prompt('Points for ' + name + '?', '');
          if (vp !== null && /^\d+$/.test(vp.trim())) send('TABLE SECONDARY ' + P + ' SCORE ' + (i + 1) + ' ' + vp.trim()).then(poll);
        });
        const drop = document.createElement('button'); drop.textContent = 'DISCARD';
        drop.addEventListener('click', () => send('TABLE SECONDARY ' + P + ' DISCARD ' + (i + 1)).then(poll));
        row.append(label, score, drop);
        box.appendChild(row);
      });
      const add = document.createElement('button'); add.textContent = '+ DRAW A CARD (TYPE ITS NAME)';
      add.addEventListener('click', () => {
        const name = prompt('Card name?', '');
        if (name && name.trim()) send('TABLE SECONDARY ' + P + ' ADD ' + name.trim()).then(poll);
      });
      box.appendChild(add);
      return box;
    }));
  }
  // Mission setup: the owner's own deployment, primary and rule files.
  function drawMissions() {
    const m = st.missions || {deployments: [], primaries: [], rules: [], layouts: []};
    const shape = JSON.stringify([m, st.layout]);
    if (shape === missionShape) return;
    missionShape = shape;
    // Refilled when the files change; what the player had picked stays picked.
    const fill = (id, names, none) => {
      const keep = el(id).value;
      const opts = (none ? [['-', none]] : []).concat(names.map(n => [n, n.toUpperCase()]));
      el(id).replaceChildren(...opts.map(([v, t]) => { const o = document.createElement('option'); o.value = v; o.textContent = t; return o; }));
      if (opts.some(([v]) => v === keep)) el(id).value = keep;
    };
    fill('m-dep', m.deployments); fill('m-pri', m.primaries); fill('m-rule', m.rules, 'NO MISSION RULE');
    fill('m-layout', m.layouts, 'NO TERRAIN LAYOUT');
    el('m-layout').value = st.layout || '-';
    const ready = m.deployments.length && m.primaries.length;
    el('m-go').disabled = !ready;
    el('m-hint').textContent = ready ? '' : 'Add your cards as text files in ' + st.folder40k + ': deployments, primaries, rules and layouts folders.';
  }

)JS" R"JS(  function draw() {
    el('round').textContent = 'ROUND ' + st.round;
    el('vp').textContent = st.p1 + ' ' + st.vp[0] + ' - ' + st.vp[1] + ' ' + st.p2;
    el('turn').textContent = (st.turn === 2 ? st.p2 : st.p1) + ' TURN';
    el('phase').textContent = st.phase || 'PHASE';
    const s3 = n => n.slice(0, 3);
    el('c1m').textContent = s3(st.p1) + ' CP -'; el('c1p').textContent = s3(st.p1) + ' CP ' + st.cp[0] + ' +';
    el('c2m').textContent = s3(st.p2) + ' CP -'; el('c2p').textContent = s3(st.p2) + ' CP ' + st.cp[1] + ' +';
    el('clock').classList.toggle('on', !!st.clock);
    el('clock').textContent = st.clock ? s3(st.p1) + ' ' + st.clocks[0] + ' | ' + s3(st.p2) + ' ' + st.clocks[1] : 'CLOCK';
    el('clockpause').textContent = st.clockPaused ? 'RESUME' : 'PAUSE';
    const hasScores = !!st.scores?.length;
    el('autoscore').disabled = !hasScores;
    el('autoscore').classList.toggle('on', hasScores && !!st.auto);
    el('autoscore').textContent = !hasScores ? 'MANUAL VP' : st.auto ? 'AUTO-SCORE ON' : 'AUTO-SCORE OFF';
    el('scoringhint').textContent = hasScores ? 'AUTOMATIC AT THE END OF EACH TURN' : "USE THE VP BUTTONS AND YOUR BATTLEPACK'S CARDS";
    el('lastscore').textContent = st.lastScore || '-';
    drawManual(); drawTactics();
    el('w40k').hidden = st.system !== '40k';
    if (st.system === '40k') { drawSecondaries(); drawMissions(); }
    el('m-setup').classList.toggle('on', !!st.setup); el('m-battle').classList.toggle('on', !st.setup);
    if (st.over) el('turn').textContent = 'GAME OVER';
    el('plan').textContent = st.title + '  (' + st.w + '" x ' + st.h + '")';
    el('boardnote').textContent = st.note || '';
    el('boardnote').hidden = !st.note;
    const short = n => n.slice(0, 3);
    el('v1m').textContent = short(st.p1) + ' -'; el('v1p').textContent = short(st.p1) + ' +';
    el('v2m').textContent = short(st.p2) + ' -'; el('v2p').textContent = short(st.p2) + ' +';
    for (const b of document.querySelectorAll('[data-style]')) b.classList.toggle('on', Number(b.dataset.style) === st.style);
    for (const b of document.querySelectorAll('[data-ground]')) b.classList.toggle('on', b.dataset.ground === st.ground);
    el('b60').classList.toggle('on', !st.pack && !!st.board60); el('b44').classList.toggle('on', !st.pack && !st.board60);
    el('b60').disabled = el('b44').disabled = !!st.pack;
    el('speed').textContent = 'SPEED ' + Math.round(st.speed * 100) + '%';
    el('bright').textContent = 'BRIGHT ' + Math.round(st.bright * 100) + '%';
    ['c1', 'c2', 'c3', 'c4'].forEach((id, i) => { if (document.activeElement !== el(id)) el(id).value = st.colors[i]; });
    el('map').style.aspectRatio = `${st.w} / ${st.h}`;
    const svg = el('map').querySelector('.geometry'), ns = 'http://www.w3.org/2000/svg';
    svg.setAttribute('viewBox', `0 0 ${st.w} ${st.h}`);
    const outlines = [];
    if (st.layers.guides) for (const line of st.lines || []) {
      if (line[4]) continue;
      const shape = document.createElementNS(ns, 'line');
      ['x1','y1','x2','y2'].forEach((name,i)=>shape.setAttribute(name,line[i]));
      shape.setAttribute('stroke','#aaa'); shape.setAttribute('stroke-width','.06');
      shape.setAttribute('stroke-dasharray','.3 .3'); outlines.push(shape);
    }
    if (st.layers.objectives) st.obj.forEach((o,i)=> {
      if (o[6]) return;
      const polygon = o[5]?.length, shape = document.createElementNS(ns,polygon?'polygon':'circle');
      if (polygon) shape.setAttribute('points',o[5].map(p=>p.join(',')).join(' '));
      else { shape.setAttribute('cx',o[0]); shape.setAttribute('cy',o[1]); shape.setAttribute('r',o[4] ?? 3); }
      const colour = o[2] === 3 ? '#ddd' : o[2] ? st.colors[o[2]-1] : '#fff';
      shape.setAttribute('stroke',colour); shape.setAttribute('stroke-width',st.hl===i+1?'.18':'.09');
      if (o[2] === 3) shape.setAttribute('stroke-dasharray','.5 .5');
      shape.setAttribute('fill',o[2] && o[2] !== 3?colour:'none'); shape.setAttribute('fill-opacity','.12');
      outlines.push(shape);
    });
    svg.replaceChildren(...outlines);
    const shape = JSON.stringify([st.w, st.h, st.obj.map(o => [o[0], o[1], o[3]]), (st.places || []).map(p => [p[0], p[1]])]);
    if (shape !== mapShape) {
      const map = el('map');
      for (const b of [...map.querySelectorAll('button')]) b.remove();
      st.obj.forEach((o, i) => {
        const b = document.createElement('button');
        b.textContent = o[3]; b.style.left = (100 * o[0] / st.w) + '%'; b.style.top = (100 * o[1] / st.h) + '%';
        hold(b, () => send('TABLE OBJ ' + (i + 1) + ' NEXT').then(poll), () => send('TABLE HIGHLIGHT ' + (i + 1)).then(poll));
        map.appendChild(b);
      });
      (st.places || []).forEach((p, i) => {
        const b = document.createElement('button');
        b.className = 'place'; b.dataset.place = i;
        b.style.left = (100 * p[0] / st.w) + '%'; b.style.top = (100 * p[1] / st.h) + '%';
        b.setAttribute('aria-label', 'Place of power ' + (i + 1));
        b.addEventListener('click', e => { e.preventDefault(); e.stopPropagation(); if (!measure) send('TABLE PLACE ' + (i + 1) + ' NEXT').then(poll); });
        map.appendChild(b);
      });
      mapShape = shape;
    }
    [...el('map').querySelectorAll('button.place')].forEach(b => {
      const p = st.places[Number(b.dataset.place)]; if (!p) return;
      b.hidden = b.disabled = !!p[4] || !st.layers.places;
      b.style.borderColor = p[2] === 1 ? '#e84a3a' : p[2] === 2 ? '#4f8dff' : '#e7d36a';
    });
    [...el('map').querySelectorAll('button:not(.place)')].forEach((b, i) => {
      const o = st.obj[i]; if (!o) return;
      b.hidden = b.disabled = !!o[6] || !st.layers.objectives;
      b.className = (o[2] === 1 ? 'p1' : o[2] === 2 ? 'p2' : o[2] === 3 ? 'p3' : '') + (st.hl === i + 1 ? ' hl' : '');
      b.style.color = o[2] === 1 || o[2] === 2 ? st.colors[o[2] - 1] : '';
      b.setAttribute('aria-label', 'Objective ' + o[3] + ', ' + (o[2] === 3 ? 'contested' : 'held by ' + (o[2] ? 'player ' + o[2] : 'nobody')));
    });
    for (const b of document.querySelectorAll('[data-l]')) b.classList.toggle('on', !!st.layers[b.dataset.l]);
    el('anim').classList.toggle('on', !!st.anim);
    el('cal').classList.toggle('on', !!st.cal);
    const elements = st.elements || [];
    const elementKey = JSON.stringify([st.plan, elements.map(e => e.slice(0,3))]);
    if (elementKey !== elementShape) {
      el('elements').replaceChildren(...elements.map(([kind, index]) => {
        const b = document.createElement('button'); b.dataset.kind = kind; b.dataset.index = index;
        b.addEventListener('click', () => send('TABLE MUTE ' + kind + ' ' + index + ' TOGGLE').then(poll));
        return b;
      }));
      elementShape = elementKey;
    }
    [...el('elements').children].forEach((b, i) => {
      const [, , label, muted] = elements[i];
      b.textContent = (muted ? 'RESTORE ' : 'MUTE ') + label;
      b.classList.toggle('on', !!muted); b.setAttribute('aria-pressed', String(!!muted));
    });
    const plans = JSON.stringify(st.plans);
    if (plans !== planShape) {
      for (const id of ['pitched-plans', 'spearhead-plans', 'custom-plans', 'w40k-plans']) el(id).replaceChildren();
      const packs = new Map();
      el('custom-group').hidden = true;
      for (const [slug, title, pack, family] of st.plans) {
        const b = document.createElement('button'); b.textContent = title.toUpperCase(); b.dataset.plan = slug;
        b.addEventListener('click', () => send('TABLE PLAN ' + slug).then(poll));
        let target = el('pitched-plans');
        if (family === 'custom') { target = el('custom-plans'); el('custom-group').hidden = false; }
        else if (family === '40k') target = el('w40k-plans');
        else if (family === 'spearhead') {
          if (!packs.has(pack)) {
            const label = document.createElement('p'); label.className = 'hint'; label.textContent = pack;
            const group = document.createElement('div'); group.className = 'chips outs'; group.style.flexWrap = 'wrap';
            el('spearhead-plans').append(label, group); packs.set(pack, group);
          }
          target = packs.get(pack);
        }
        target.appendChild(b);
      }
      planShape = plans;
    }
    for (const b of el('plans').querySelectorAll('[data-plan]')) b.classList.toggle('on', b.dataset.plan === st.plan);
  }
  async function poll() {
    const t = await send('TABLE STATUS');
    if (!t) return;
    const at = t.indexOf('{');
    if (at < 0) return;
    try { st = JSON.parse(t.slice(at)); } catch { return; }
    el('link').textContent = 'TABLE';
    try { draw(); } catch (e) { el('result').textContent = 'page error: ' + e.message; }
  }
  for (const b of document.querySelectorAll('[data-c]')) b.addEventListener('click', () => send(b.dataset.c).then(poll));
  for (const b of document.querySelectorAll('[data-l]')) b.addEventListener('click', () => send('TABLE LAYER ' + b.dataset.l + ' TOGGLE').then(poll));
  // Measuring: tap the map (not a button on it) to place.
  const inches = e => {
    const map = el('map'), r = map.getBoundingClientRect();
    const x = (e.clientX - r.left - map.clientLeft) / map.clientWidth * st.w;
    const y = (e.clientY - r.top - map.clientTop) / map.clientHeight * st.h;
    return [Math.max(0, Math.min(st.w, x)).toFixed(1), Math.max(0, Math.min(st.h, y)).toFixed(1)];
  };
  function setMeasure(m) {
    measure = measure === m ? '' : m; first = null;
    for (const b of document.querySelectorAll('[data-measure]')) b.classList.toggle('on', b.dataset.measure === measure);
    el('map').classList.toggle('measuring', !!measure);
    for (const d of el('map').querySelectorAll('.dot')) d.remove();
    el('measurehint').textContent = !measure ? 'PICK ONE, THEN TAP THE MAP' : measure === 'RULER' ? 'TAP THE FIRST END' : 'TAP WHERE IT GOES';
  }
  for (const b of document.querySelectorAll('[data-measure]')) b.addEventListener('click', () => setMeasure(b.dataset.measure));
  el('map').addEventListener('click', e => {
    if (!measure || !st || e.target.closest('button')) return;
    const [x, y] = inches(e);
    if (measure === 'RULER') {
      if (!first) {
        first = [x, y];
        const d = document.createElement('div'); d.className = 'dot';
        d.style.left = (100 * x / st.w) + '%'; d.style.top = (100 * y / st.h) + '%';
        el('map').appendChild(d);
        el('measurehint').textContent = 'NOW THE OTHER END';
        return;
      }
      send('TABLE RULER ' + first.join(' ') + ' ' + x + ' ' + y).then(poll);
      setMeasure('');
      return;
    }
    if (measure === 'DS') send('TABLE DEEPSTRIKE ' + x + ' ' + y).then(poll);
    else if (measure === 'R6' || measure === 'R12' || measure === 'R24') send('TABLE RINGS ' + x + ' ' + y + ' ' + measure.slice(1)).then(poll);
    else if (measure === 'MOVE') send('TABLE MOVE ' + x + ' ' + y + ' ' + move).then(poll);
    else send('TABLE RINGS ' + x + ' ' + y + ' ' + ringSets[ringSet][1]).then(poll);
  });
  el('movem').addEventListener('click', () => { move = Math.max(1, move - 1); el('moveval').textContent = 'MOVE ' + move + '"'; });
  el('movep').addEventListener('click', () => { move = Math.min(20, move + 1); el('moveval').textContent = 'MOVE ' + move + '"'; });
  el('ringset').addEventListener('click', () => { ringSet = (ringSet + 1) % ringSets.length; el('ringset').textContent = 'RINGS ' + ringSets[ringSet][0]; });
  el('anim').addEventListener('click', () => send('TABLE ANIMATE TOGGLE').then(poll));
  for (const b of document.querySelectorAll('[data-ground]')) b.addEventListener('click', () => send('TABLE GROUND ' + b.dataset.ground.toUpperCase()).then(poll));
  for (const b of document.querySelectorAll('[data-style]')) b.addEventListener('click', () => send('TABLE STYLE ' + ['CLEAN', 'ARCANE', 'EMBER'][Number(b.dataset.style)]).then(poll));
  for (const id of ['c1', 'c2', 'c3', 'c4']) el(id).addEventListener('change', () => send('TABLE COLOR ' + el(id).dataset.who + ' ' + el(id).value).then(poll));
  el('cal').addEventListener('click', () => send('TABLE CALIBRATE TOGGLE').then(poll));
  // A new mission opens in the setup view: the card is what the players need first.
  el('m-go').addEventListener('click', () => send('TABLE MISSION ' + el('m-dep').value + ' ' + el('m-pri').value + ' ' + el('m-rule').value)
    .then(t => t && send('TABLE SETUP ON')).then(poll));
  el('m-layout').addEventListener('change', () => send('TABLE LAYOUT ' + (el('m-layout').value === '-' ? 'OFF' : el('m-layout').value)).then(poll));
  poll();
  setInterval(() => { if (!document.hidden) poll(); }, 1500);
})();
)JS"
