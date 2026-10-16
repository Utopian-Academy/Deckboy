// SPDX-License-Identifier: GPL-3.0-or-later
// Invoked by check_phone_remote.py against its isolated running app.
import assert from 'node:assert/strict';
import net from 'node:net';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const [modulePath, executablePath, webPort, controlPort, evidence, host = '127.0.0.1'] = process.argv.slice(2);
const {chromium} = await import(pathToFileURL(path.resolve(modulePath)).href);
// Phone control on, the server listens on the LAN address, not loopback.
const base = `http://${host}:${webPort}`;
const pin = 'remote-test-4826';
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
function command(line) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({host:'127.0.0.1', port:Number(controlPort)});
    let received = '';
    socket.setTimeout(8000, () => socket.destroy(new Error('Control timeout')));
    socket.on('error', reject);
    socket.on('connect', () => socket.write(line+'\n'));
    socket.on('data', bytes => {
      received += bytes.toString();
      if (received.includes('\n')) { socket.destroy(); resolve(received.trim()); }
    });
  });
}
const browser = await chromium.launch({executablePath, headless:true});
try {
  const context = await browser.newContext({viewport:{width:390,height:844}, deviceScaleFactor:1.5,
    isMobile:true, hasTouch:true});
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  let dialogs = 0;
  page.on('dialog', async dialog => { ++dialogs; await dialog.dismiss(); });
  async function unlock() {
    await page.locator('#pin').fill(pin);
    await page.locator('#pin-form button').click();
    await page.waitForFunction(() => document.getElementById('link').textContent === 'LINK OK');
  }
  // A held press: half a second fires the hold action of a tap-or-hold button.
  async function hold(locator, ms = 750) {
    const box = await locator.boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down(); await sleep(ms); await page.mouse.up();
  }
  const cue = n => `#cues button:nth-child(${n})`;
  const hasClass = (selector, name) => page.waitForFunction(([s, c]) => {
    const node = document.querySelector(s); return !!node && node.classList.contains(c);
  }, [selector, name], {timeout: 8000}).catch(async error => {
    // Say what the page showed, not just that it timed out.
    const rows = await page.locator('#cues button').evaluateAll(bs => bs.map(b => `${b.textContent}[${b.className}]`));
    throw new Error(`${selector} never became ${name}; cues: ${rows.join(' ')}; result: ${await page.textContent('#result')}`);
  });
  await page.goto(base+'/remote');
  await unlock();
  // ONE SCREEN: no tabs, TAKE and the whole transport are on it.
  for (const id of ['#go', '#playpause', '[data-verb="RERACK"]', '[data-verb="SKIPBACK"]', '[data-verb="SKIP"]',
                    '[data-seek="-10"]', '[data-seek="10"]', '#fader', '#blackout', '#record']) {
    assert.equal(await page.locator(id).isVisible(), true, `${id} on the one screen`);
  }
  assert.equal(await page.locator('[role="tab"], .tabs').count(), 0, 'no tabs');
  await page.locator('#deck-chips [data-deck="2"]').click();
  assert.equal(await page.locator('#cues button').count(), 0, 'Empty second deck has its own cue list');
  await page.locator('#deck-chips [data-deck="1"]').click();
  // Tap a cue: standby. TAKE: it goes live.
  // (The server checks before this leave cue 2 live.)
  await page.locator(cue(1)).click();
  await hasClass(cue(1), 'standby');
  await page.locator('#go').click();
  await hasClass(cue(1), 'live');
  await page.locator('[data-verb="STOP"]').click();
  await page.waitForFunction(() => document.getElementById('result').textContent.startsWith('OK '));
  // Hold a cue: taken now, with no TAKE.
  await hold(page.locator(cue(2)));
  await hasClass(cue(2), 'live');
  // The fader sets the deck's level.
  await page.locator('#fader').fill('40');
  await page.waitForFunction(() => document.getElementById('result').textContent.startsWith('OK VOLUME') ||
                                    document.getElementById('result').textContent.startsWith('OK DECK'));
  // DASH: tap fires; an error comes back as words.
  await page.getByRole('button', {name:'INVALID DECK',exact:true}).click();
  await page.waitForFunction(() => document.getElementById('result').textContent.includes('ERR DASH') ||
                                    document.getElementById('result').textContent.includes('DECK 99'));
  // Hold a DASH button: the editor opens on it; save renames it on the desk.
  await hold(page.getByRole('button', {name:'SELECT ALPHA',exact:true}));
  assert.equal(await page.locator('#dash-edit').isVisible(), true, 'holding a DASH button opens its editor');
  await page.locator('#dash-label').fill('Pick one');
  await page.locator('#dash-edit button[type="submit"]').click();
  await page.waitForFunction(() => [...document.querySelectorAll('#dash button')].some(b => b.textContent === 'PICK ONE'));
  // + adds a new one.
  await page.getByRole('button', {name:'Add a DASH button',exact:true}).click();
  assert.equal(await page.locator('#dash-edit').isVisible(), true, '+ opens a new DASH button');
  await page.locator('#dash-cancel').click();
  // BLACKOUT is hold-to-fire: a tap does nothing.
  await page.locator('#blackout').click();
  await sleep(1200);
  assert.equal(await page.locator('#blackout').evaluate(b => b.classList.contains('on')), false, 'a tap does not black out');
  await hold(page.locator('#blackout'));
  await hasClass('#blackout', 'on');
  await hold(page.locator('#blackout'));
  await page.waitForFunction(() => !document.getElementById('blackout').classList.contains('on'));
  assert.equal(dialogs, 0, 'Cue names must not execute HTML');
  assert.equal(await page.locator('#cues script').count(), 0);
  assert.equal(await page.evaluate(() => localStorage.length + sessionStorage.length), 0, 'PIN must not be stored');
  assert.equal(page.url(), base+'/remote');
  await context.setOffline(true);
  await page.waitForFunction(() => document.querySelector('#go').disabled);
  await context.setOffline(false);
  await page.waitForFunction(() => !document.querySelector('#go').disabled);
  await page.locator('#lock').click();
  assert.equal(await page.locator('#controls').isVisible(), false);
  await unlock();
  await page.reload();
  assert.equal(await page.locator('#login').isVisible(), true, 'Reload must lock the page');
  // Check both CSS portrait/tablet layout and 150% font scaling. Screenshot
  // names include theme and viewport so the evidence can be reviewed directly.
  for (const theme of ['dark','virtual-boy','ganon']) {
    assert.match(await command('SET theme '+theme), /^OK /);
    await sleep(1200);
    assert.match(await command('UISNAP '+path.join(evidence,`native-${theme}.bmp`)), /^OK /);
    for (const viewport of [{width:390,height:844},{width:1024,height:768}]) {
      await page.setViewportSize(viewport);
      await page.reload(); await unlock();
      await page.addStyleTag({content:':root{font-size:150%}'});
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true,
        `No horizontal overflow: ${theme} ${viewport.width}`);
      // The page's scroll width can hide it: a long cue name once pushed its
      // row off the screen while scrollWidth still matched the viewport.
      const outside = await page.evaluate(() => [...document.querySelectorAll('#controls *')]
        .filter(n => n.offsetParent && n.getBoundingClientRect().right > innerWidth + 1)
        .map(n => n.id || n.className || n.tagName));
      assert.deepEqual(outside, [], `Everything inside the screen: ${theme} ${viewport.width}`);
      const tight = await page.locator('.row5 button, .row4 button, .grid3 button, .row3 button').evaluateAll(buttons =>
        buttons.filter(b => b.offsetParent && b.scrollWidth > b.clientWidth).map(b => `${b.textContent} ${b.scrollWidth}>${b.clientWidth}`));
      assert.deepEqual(tight, [], `Button labels fit: ${theme} ${viewport.width}`);
      const contrast = await page.evaluate(() => {
        const rgb = value => value.match(/[\d.]+/g).slice(0,3).map(Number);
        const luminance = values => values.map(v => v/255).map(v => v <= .04045 ? v/12.92 : ((v+.055)/1.055)**2.4)
          .reduce((sum,v,i) => sum+v*[.2126,.7152,.0722][i],0);
        return ['body','#go','#now-name','.now-next','#cues button','#cues button.live','#dash button']
          .filter(selector => document.querySelector(selector)).map(selector => {
          const style = getComputedStyle(document.querySelector(selector));
          const a = luminance(rgb(style.color)), b = luminance(rgb(style.backgroundColor));
          return (Math.max(a,b)+.05)/(Math.min(a,b)+.05);
        });
      });
      assert.ok(contrast.every(ratio => ratio >= 4.5), `Remote text contrast: ${contrast}`);
      await page.screenshot({path:path.join(evidence,`remote-${theme}-${viewport.width}-150.png`),fullPage:true});
    }
  }
  assert.deepEqual(errors, []);
  console.log('phone-remote-ui: one screen, TAKE, standby/hold-take, fader, DASH fire/edit/add, hold-to-fire blackout, lock, reconnect, HTML escaping, 6 layouts/contrast checks passed');
} finally { await browser.close(); }
