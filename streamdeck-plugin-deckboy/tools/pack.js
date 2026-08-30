// Produce the .streamDeckPlugin file people install by double-clicking.
//
// Without this, installing means finding the Stream Deck plugins folder,
// copying a directory into it and restarting the application -- which is fine
// for someone building from source and no way to hand a plugin to an operator.
// A .streamDeckPlugin is a zip of the .sdPlugin folder under a different
// extension; Stream Deck registers the type and installs it on open.
//
// Written with the platform's own zip rather than a dependency: this repository
// ships a plugin with no dependencies at all, and adding one to the build to
// package the thing would be a poor joke.

import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)))
const folder = 'com.deckboy.streamdeck.sdPlugin'
const source = path.join(root, folder)
if (!fs.existsSync(path.join(source, 'manifest.json'))) {
  console.error('run `node tools/build.js` first: there is no assembled plugin')
  process.exit(1)
}

const version = JSON.parse(
  fs.readFileSync(path.join(source, 'manifest.json'), 'utf8')).Version
const dist = path.join(root, 'dist')
fs.mkdirSync(dist, { recursive: true })
const out = path.join(dist, `Deckboy-${version}.streamDeckPlugin`)
fs.rmSync(out, { force: true })

// The zip has to contain the .sdPlugin FOLDER, not its contents, or Stream
// Deck installs a plugin with no name and no manifest.
const zip = out + '.zip'
fs.rmSync(zip, { force: true })
if (process.platform === 'win32') {
  execFileSync('powershell', ['-NoProfile', '-Command',
    `Compress-Archive -Path '${source}' -DestinationPath '${zip}' -Force`],
    { stdio: 'inherit' })
} else {
  execFileSync('zip', ['-qr', zip, folder], { cwd: root, stdio: 'inherit' })
}
fs.renameSync(zip, out)

const size = (fs.statSync(out).size / 1024).toFixed(0)
console.log(`packed ${path.relative(root, out)} (${size} KB)`)
console.log('double-click it to install, or drag it onto Stream Deck')
