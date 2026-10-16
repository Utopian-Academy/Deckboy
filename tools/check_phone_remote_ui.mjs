// SPDX-License-Identifier: GPL-3.0-or-later
// Invoked by check_phone_remote.py against its isolated running app.
import assert from 'node:assert/strict';
import net from 'node:net';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const [modulePath, executablePath, webPort, controlPort, evidence] = process.argv.slice(2);
const {chromium} = await import(pathToFileURL(path.resolve(modulePath)).href);
const base = `http://127.0.0.1:${webPort}`;
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
    await page.waitForFunction(() => document.getElementById('connection').textContent === 'Connected');
  }
  await page.goto(base+'/remote');
  await unlock();
  await page.locator('#deck-chips [data-deck="2"]').click();
  assert.equal(await page.locator('#cues button').count(), 0, 'Empty second deck has its own cue list');
  await page.locator('#deck-chips [data-deck="1"]').click();
  await page.locator('#cues button').nth(1).click();
  await page.waitForFunction(() => document.querySelector('#cues button:nth-child(2)').getAttribute('aria-pressed') === 'true');
  await page.locator('#go').click();
  await page.waitForFunction(() => document.getElementById('transport').textContent.includes('active 2'));
  await page.locator('[data-verb="STOP"]').click();
  await page.waitForFunction(() => document.getElementById('result').textContent.startsWith('OK '));
  await page.getByRole('button', {name:'Select Alpha',exact:true}).click();
  await page.waitForFunction(() => document.querySelector('#cues button').getAttribute('aria-pressed') === 'true');
  await page.locator('[data-verb="NEXT"]').click();
  await page.waitForFunction(() => document.querySelector('#cues button:nth-child(2)').getAttribute('aria-pressed') === 'true');
  await page.getByRole('button', {name:'Invalid deck',exact:true}).click();
  await page.waitForFunction(() => document.getElementById('result').textContent.includes('ERR DASH'));
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
      assert.equal(await page.locator('.transport button').evaluateAll(buttons =>
        buttons.every(button => button.scrollWidth <= button.clientWidth)), true, 'Transport labels fit');
      const contrast = await page.evaluate(() => {
        const rgb = value => value.match(/[\d.]+/g).slice(0,3).map(Number);
        const luminance = values => values.map(v => v/255).map(v => v <= .04045 ? v/12.92 : ((v+.055)/1.055)**2.4)
          .reduce((sum,v,i) => sum+v*[.2126,.7152,.0722][i],0);
        return ['body','#go','#cues button[aria-pressed="true"]'].map(selector => {
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
  console.log('phone-remote-ui: transport, cue selection, DASH, errors, lock, reconnect, HTML escaping, 6 layouts/contrast checks passed');
} finally { await browser.close(); }
