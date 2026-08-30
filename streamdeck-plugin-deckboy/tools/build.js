// Assemble the .sdPlugin folder Stream Deck actually loads.
//
// Stream Deck runs the plugin from a folder named after its UUID, with the
// code at the path the manifest names. The sources live in src/ so they can be
// tested and read; this copies them where Stream Deck expects to find them.
//
// No bundler, because there is nothing to bundle: the plugin has no
// dependencies, so the files that run are the files that were written.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)))
const plugin = path.join(root, 'com.deckboy.streamdeck.sdPlugin')
const bin = path.join(plugin, 'bin')

fs.mkdirSync(bin, { recursive: true })
let copied = 0
for (const name of fs.readdirSync(path.join(root, 'src'))) {
	if (!name.endsWith('.js')) continue
	fs.copyFileSync(path.join(root, 'src', name), path.join(bin, name))
	copied += 1
}
console.log(`copied ${copied} file(s) into ${path.relative(root, bin)}`)

// The property inspectors, which is how a key gets a host, a port and its
// command. They are plain pages, so they are copied rather than built.
const ui = path.join(plugin, 'ui')
fs.mkdirSync(ui, { recursive: true })
let uiCopied = 0
for (const name of fs.readdirSync(path.join(root, 'src', 'ui'))) {
	fs.copyFileSync(path.join(root, 'src', 'ui', name), path.join(ui, name))
	uiCopied += 1
}
console.log(`copied ${uiCopied} inspector file(s) into ui/`)

// EVERY PATH THE MANIFEST NAMES, checked. Stream Deck's answer to a missing
// file is to load nothing and say very little about why, so a broken path is
// far cheaper to find here than on a deck.
const manifest = JSON.parse(
	fs.readFileSync(path.join(plugin, 'manifest.json'), 'utf8'))
const missing = []
const need = (relative, what) => {
	if (!relative) return
	// Icons are named without their extension and come in two sizes.
	const candidates = what === 'icon'
		? [relative + '.png', relative + '@2x.png']
		: [relative]
	for (const candidate of candidates) {
		if (!fs.existsSync(path.join(plugin, candidate))) missing.push(candidate)
	}
}
need(manifest.CodePath, 'file')
need(manifest.Icon, 'icon')
need(manifest.CategoryIcon, 'icon')
for (const action of manifest.Actions || []) {
	need(action.Icon, 'icon')
	need(action.PropertyInspectorPath, 'file')
	for (const state of action.States || []) need(state.Image, 'icon')
}
if (missing.length) {
	console.error('the manifest names files that are not there:')
	for (const name of missing) console.error('  ' + name)
	process.exit(1)
}
console.log('every path the manifest names is present')
