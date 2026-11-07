// Video University: questions, progress and the simulators. Static site, no
// server, no account: progress stays in this browser.
(() => {
  const KEY = 'deckboy-university-v1';
  const load = () => { try { return JSON.parse(localStorage.getItem(KEY)) || {}; } catch (e) { return {}; } };
  const save = (p) => { try { localStorage.setItem(KEY, JSON.stringify(p)); } catch (e) { /* private window */ } };
  const $ = (id) => document.getElementById(id);

  // ── Questions ─ a set is passed when every question has the right answer.
  document.querySelectorAll('[data-quiz]').forEach((quiz) => {
    const qs = [...quiz.querySelectorAll('.q')];
    const score = quiz.querySelector('.uni-score');
    const solved = new Set();
    // A reward stays hidden until you get every answer right.
    const reveal = () => {
      const r = document.querySelector('[data-reward="' + quiz.dataset.quiz + '"]');
      if (r) r.hidden = false;
    };
    const report = () => {
      if (score) score.textContent = solved.size + ' of ' + qs.length + ' right';
      if (solved.size === qs.length) {
        const p = load(); p[quiz.dataset.quiz] = true; save(p);
        if (score) score.textContent = 'All ' + qs.length + ' right. This is marked as done.';
        reveal();
      }
    };
    qs.forEach((q, i) => {
      const why = q.querySelector('.why');
      q.querySelectorAll('button').forEach((b) => b.addEventListener('click', () => {
        const right = b.hasAttribute('data-right');
        why.textContent = (right ? 'Right. ' : 'Not that one. ') + (b.dataset.why || '');
        why.classList.toggle('right', right);
        if (right) solved.add(i);
        report();
      }));
    });
    report();
    if (load()[quiz.dataset.quiz]) reveal();
    if (load()[quiz.dataset.quiz] && score) score.textContent = 'You finished this before. You can do it again.';
  });

  // ── Marks on the lesson lists and the catalogue.
  const progress = load();
  document.querySelectorAll('[data-done-key]').forEach((el) => {
    el.textContent = progress[el.dataset.doneKey] ? 'done' : '';
  });

  // ── A test picture, drawn once, used by the simulators.
  function testPicture(w, h) {
    const c = document.createElement('canvas'); c.width = w; c.height = h;
    const g = c.getContext('2d');
    const sky = g.createLinearGradient(0, 0, 0, h * 0.7);
    sky.addColorStop(0, '#1b4f9c'); sky.addColorStop(1, '#f6c177');
    g.fillStyle = sky; g.fillRect(0, 0, w, h);
    g.fillStyle = '#ffe9a8'; g.beginPath(); g.arc(w * 0.72, h * 0.34, h * 0.12, 0, 7); g.fill();
    g.fillStyle = '#20563a'; g.beginPath(); g.moveTo(0, h * 0.72);
    g.lineTo(w * 0.28, h * 0.42); g.lineTo(w * 0.52, h * 0.7); g.lineTo(w * 0.78, h * 0.5);
    g.lineTo(w, h * 0.7); g.lineTo(w, h); g.lineTo(0, h); g.fill();
    g.fillStyle = '#0d2b1d'; g.fillRect(0, h * 0.82, w, h * 0.18);
    g.fillStyle = '#fcfe1f'; g.font = 'bold ' + Math.round(h * 0.1) + 'px monospace';
    g.textAlign = 'center'; g.fillText('DECKBOY', w / 2, h * 0.95);
    return c;
  }

  // ── Lecture 1: frames per second.
  if ($('sim-fps')) {
    const cv = $('sim-fps'), g = cv.getContext('2d');
    const W = cv.width, H = cv.height;
    let fps = 2, frame = 0, last = 0, paused = false;
    const out = $('fps-out'), count = $('fps-count');
    const draw = (t) => {
      g.fillStyle = '#071a07'; g.fillRect(0, 0, W, H);
      g.strokeStyle = '#2c7a2c'; g.beginPath(); g.moveTo(0, H * 0.8); g.lineTo(W, H * 0.8); g.stroke();
      const x = W * (0.5 + 0.42 * Math.sin(t * 2 * Math.PI / 4));
      const y = H * 0.8 - 22 - Math.abs(Math.sin(t * 2 * Math.PI / 1.6)) * H * 0.5;
      g.fillStyle = '#fcfe1f'; g.beginPath(); g.arc(x, y, 22, 0, 7); g.fill();
    };
    const tick = (now) => {
      if (!paused && now - last >= 1000 / fps) {
        last = now; frame++;
        draw(now / 1000);
        count.textContent = frame;
      }
      requestAnimationFrame(tick);
    };
    const set = () => { fps = +$('fps').value; out.textContent = fps + ' frames per second'; };
    $('fps').addEventListener('input', set); set();
    $('fps-pause').addEventListener('click', (e) => {
      paused = !paused; e.target.setAttribute('aria-pressed', String(paused));
      e.target.textContent = paused ? 'Resume' : 'Stop on one frame';
    });
    draw(0);
    if (matchMedia('(prefers-reduced-motion: reduce)').matches) { paused = true; $('fps-pause').textContent = 'Resume'; $('fps-pause').setAttribute('aria-pressed', 'true'); }
    requestAnimationFrame(tick);
  }

  // ── Lecture 2: pixels. The picture at a chosen width, and the pixel you point at.
  if ($('sim-pixels')) {
    const cv = $('sim-pixels'), g = cv.getContext('2d');
    const W = cv.width, H = cv.height;
    const src = testPicture(W, H);
    const small = document.createElement('canvas'), sg = small.getContext('2d');
    let across = 16, down = 9;
    const render = () => {
      across = +$('px-across').value; down = Math.max(1, Math.round(across * 9 / 16));
      small.width = across; small.height = down;
      sg.imageSmoothingEnabled = true; sg.drawImage(src, 0, 0, across, down);
      g.imageSmoothingEnabled = false; g.drawImage(small, 0, 0, W, H);
      $('px-out').textContent = across + ' x ' + down + ' = ' + (across * down).toLocaleString('en') + ' pixels';
    };
    const point = (e) => {
      const r = cv.getBoundingClientRect();
      const x = Math.min(across - 1, Math.max(0, Math.floor((e.clientX - r.left) / r.width * across)));
      const y = Math.min(down - 1, Math.max(0, Math.floor((e.clientY - r.top) / r.height * down)));
      const d = sg.getImageData(x, y, 1, 1).data;
      $('px-read').innerHTML = 'Pixel <b>' + x + ', ' + y + '</b>: red <b>' + d[0] + '</b>, green <b>' + d[1] + '</b>, blue <b>' + d[2] + '</b>';
      $('px-chip').style.background = 'rgb(' + d[0] + ',' + d[1] + ',' + d[2] + ')';
    };
    $('px-across').addEventListener('input', render);
    cv.addEventListener('pointermove', point); cv.addEventListener('pointerdown', point);
    render();
  }

  // ── Lecture 3: three lights.
  if ($('sim-rgb')) {
    const cv = $('sim-rgb'), g = cv.getContext('2d');
    const W = cv.width, H = cv.height;
    const render = () => {
      const r = +$('rgb-r').value, gr = +$('rgb-g').value, b = +$('rgb-b').value;
      $('rgb-r-out').textContent = r; $('rgb-g-out').textContent = gr; $('rgb-b-out').textContent = b;
      // Left: one pixel seen close, its three lights. Right: seen from a distance.
      g.fillStyle = '#000'; g.fillRect(0, 0, W, H);
      const cell = W / 2 / 3, gap = 8;
      [[r, 0, 0], [0, gr, 0], [0, 0, b]].forEach((c, i) => {
        g.fillStyle = 'rgb(' + c.join(',') + ')';
        g.fillRect(i * cell + gap, gap, cell - gap * 2, H - gap * 2);
      });
      g.fillStyle = 'rgb(' + r + ',' + gr + ',' + b + ')';
      g.fillRect(W / 2 + gap, gap, W / 2 - gap * 2, H - gap * 2);
      $('rgb-hex').textContent = '#' + [r, gr, b].map((v) => v.toString(16).padStart(2, '0')).join('');
    };
    ['rgb-r', 'rgb-g', 'rgb-b'].forEach((id) => $(id).addEventListener('input', render));
    document.querySelectorAll('[data-rgb]').forEach((btn) => btn.addEventListener('click', () => {
      const v = btn.dataset.rgb.split(',');
      $('rgb-r').value = v[0]; $('rgb-g').value = v[1]; $('rgb-b').value = v[2]; render();
    }));
    render();
  }

  // ── Lecture 4: resolution and shape.
  if ($('sim-res')) {
    const cv = $('sim-res'), g = cv.getContext('2d');
    const W = cv.width, H = cv.height;
    const FORMATS = [['SD (PAL)', 720, 576], ['720p', 1280, 720], ['1080p (HD)', 1920, 1080], ['2160p (UHD 4K)', 3840, 2160], ['4320p (UHD 8K)', 7680, 4320]];
    const scale = Math.min((W - 8) / 7680, (H - 8) / 4320);
    const ramp = ['#94ae79', '#f3fdca', '#fcfe1f', '#ffb3d1', '#9fe6ff'];
    FORMATS.slice().reverse().forEach((f, i) => {
      g.strokeStyle = ramp[4 - i]; g.lineWidth = 2;
      g.strokeRect(3, H - 3 - f[2] * scale, f[1] * scale, f[2] * scale);
    });
    g.font = '13px monospace';
    FORMATS.forEach((f, i) => {
      g.fillStyle = ramp[i];
      if (i >= 2) g.fillText(f[0], 9, H - f[2] * scale + 14);
    });
    const sel = $('res-format');
    FORMATS.forEach((f, i) => { const o = document.createElement('option'); o.value = i; o.textContent = f[0]; sel.appendChild(o); });
    sel.value = 2;
    const read = () => {
      const f = FORMATS[+sel.value], hd = 1920 * 1080;
      const n = f[1] * f[2];
      $('res-read').innerHTML = '<b>' + f[1] + ' x ' + f[2] + '</b> = <b>' + n.toLocaleString('en') + '</b> pixels. That is <b>' + (n / hd).toFixed(n < hd ? 2 : 0) + '</b> x the pixels of 1080p.';
    };
    sel.addEventListener('change', read); read();

    // Shape: a 16:9 picture on a screen of another shape.
    const cs = $('sim-shape'), s = cs.getContext('2d');
    const pic = testPicture(640, 360);
    const SW = cs.width, SH = cs.height;
    const SCREENS = { '16:9': 16 / 9, '4:3': 4 / 3, '5:4': 5 / 4, '21:9': 21 / 9, '9:16': 9 / 16 };
    let screen = '4:3', fit = 'fit';
    const drawShape = () => {
      s.fillStyle = '#143f14'; s.fillRect(0, 0, SW, SH);
      const a = SCREENS[screen];
      const sw = Math.min(SW, SH * a), sh = sw / a;
      const sx = (SW - sw) / 2, sy = (SH - sh) / 2;
      s.save(); s.beginPath(); s.rect(sx, sy, sw, sh); s.clip();
      s.fillStyle = '#000'; s.fillRect(sx, sy, sw, sh);
      let pw, ph;
      if (fit === 'fit') { const k = Math.min(sw / 16, sh / 9); pw = 16 * k; ph = 9 * k; }
      else if (fit === 'fill') { const k = Math.max(sw / 16, sh / 9); pw = 16 * k; ph = 9 * k; }
      else { pw = sw; ph = sh; }
      s.drawImage(pic, sx + (sw - pw) / 2, sy + (sh - ph) / 2, pw, ph);
      s.restore();
      s.strokeStyle = '#fcfe1f'; s.lineWidth = 2; s.strokeRect(sx, sy, sw, sh);
      const text = { fit: 'Fit: you see the whole picture. Black bars fill the rest of the screen.',
                     fill: 'Fill: there are no bars. You lose the edges of the picture.',
                     stretch: 'Stretch: there are no bars and you keep the whole picture, but circles become ovals. Do not do this.' };
      $('shape-read').textContent = screen === '16:9' ? 'The shapes are the same. The picture fills the screen.' : text[fit];
    };
    document.querySelectorAll('[data-screen]').forEach((b) => b.addEventListener('click', () => {
      screen = b.dataset.screen;
      document.querySelectorAll('[data-screen]').forEach((x) => x.setAttribute('aria-pressed', String(x === b)));
      drawShape();
    }));
    document.querySelectorAll('[data-fit]').forEach((b) => b.addEventListener('click', () => {
      fit = b.dataset.fit;
      document.querySelectorAll('[data-fit]').forEach((x) => x.setAttribute('aria-pressed', String(x === b)));
      drawShape();
    }));
    drawShape();
  }

  // ── Lecture 5: how much data.
  if ($('data-w')) {
    const human = (bytes) => {
      const units = ['bytes', 'KB', 'MB', 'GB', 'TB']; let i = 0;
      while (bytes >= 1000 && i < units.length - 1) { bytes /= 1000; i++; }
      return (bytes >= 100 ? bytes.toFixed(0) : bytes.toFixed(1)) + ' ' + units[i];
    };
    const render = () => {
      const [w, h] = $('data-w').value.split('x').map(Number);
      const fps = +$('data-fps').value, bytes = +$('data-depth').value;
      const frame = w * h * bytes, sec = frame * fps, hour = sec * 3600;
      $('data-frame').textContent = human(frame);
      $('data-sec').textContent = human(sec) + ' (' + (sec * 8 / 1e9).toFixed(2) + ' gigabits)';
      $('data-hour').textContent = human(hour);
      $('data-sum').textContent = w + ' x ' + h + ' x ' + bytes + ' bytes x ' + fps + ' frames per second';
      const mbps = +$('data-link').value;
      const ratio = sec * 8 / (mbps * 1e6);
      $('data-ratio').textContent = 'To fit a ' + mbps + ' megabit stream, a codec must make this ' + Math.round(ratio) + ' times smaller.';
    };
    ['data-w', 'data-fps', 'data-depth', 'data-link'].forEach((id) => $(id).addEventListener('input', render));
    render();
  }

  // ── Lecture 6: the chain. Break a link and see what the screen shows.
  if ($('chain')) {
    const LINKS = {
      none: ['The picture is on the screen.', 'Every link works.'],
      source: ['The screen is black, and it may say "no signal".', 'The source sends nothing. Check first that the source plays. Check its own monitor.'],
      out: ['The screen says "no signal".', 'The source plays, but its output is off. Turn the output on.'],
      cable: ['The screen says "no signal", or the picture drops out.', 'Change the cable. Use a short cable that you know is good. That one test tells you a lot.'],
      input: ['The screen shows its menu, or a different picture.', 'The screen looks at the wrong input. Select the correct input on the screen.'],
      format: ['The screen says "format not supported", or it stays black.', 'The source sends a format the screen does not accept. Set the source to a format in the screen\'s list.']
    };
    const cv = $('sim-chain'), g = cv.getContext('2d');
    const pic = testPicture(cv.width, cv.height);
    let fault = 'none';
    const render = () => {
      if (fault === 'none') { g.drawImage(pic, 0, 0); }
      else {
        g.fillStyle = '#000'; g.fillRect(0, 0, cv.width, cv.height);
        g.fillStyle = '#f3fdca'; g.font = '20px monospace'; g.textAlign = 'center';
        const words = { source: 'NO SIGNAL', out: 'NO SIGNAL', cable: 'NO SIGNAL', input: 'HDMI 2: NO SIGNAL', format: 'FORMAT NOT SUPPORTED' };
        g.fillText(words[fault], cv.width / 2, cv.height / 2);
      }
      $('chain-see').textContent = LINKS[fault][0];
      $('chain-do').textContent = LINKS[fault][1];
    };
    document.querySelectorAll('#chain [data-fault]').forEach((b) => b.addEventListener('click', () => {
      fault = b.dataset.fault;
      document.querySelectorAll('#chain [data-fault]').forEach((x) => x.setAttribute('aria-pressed', String(x === b)));
      render();
    }));
    render();
  }

  // ── The troubleshooter.
  if ($('stage') && window.UNI_TREE) {
    const T = window.UNI_TREE, stage = $('stage'), trail = $('trail');
    let path = ['start'], said = [];
    const render = () => {
      const n = T[path[path.length - 1]];
      trail.textContent = said.join('  >  ');
      stage.textContent = '';
      if (n.q) {
        const box = document.createElement('div'); box.className = 'ask';
        const q = document.createElement('p'); q.textContent = n.q; box.appendChild(q);
        const list = document.createElement('div'); list.className = 'answers';
        n.a.forEach(([label, next]) => {
          const b = document.createElement('button'); b.textContent = label;
          b.addEventListener('click', () => { path.push(next); said.push(label); render(); });
          list.appendChild(b);
        });
        box.appendChild(list); stage.appendChild(box);
      } else {
        const f = document.createElement('div'); f.className = 'fix';
        const h = document.createElement('h3'); h.textContent = n.t; f.appendChild(h);
        const p = document.createElement('p'); p.textContent = n.p; f.appendChild(p);
        const l = document.createElement('p'); l.className = 'sources'; l.textContent = 'Course: ' + n.l; f.appendChild(l);
        stage.appendChild(f);
      }
    };
    $('startover').addEventListener('click', () => { path = ['start']; said = []; render(); });
    $('back').addEventListener('click', () => { if (path.length > 1) { path.pop(); said.pop(); render(); } });
    render();
  }
})();
