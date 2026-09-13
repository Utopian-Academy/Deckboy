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
const plugin = path.join(root, 'com.utopian-academy.deckboy.sdPlugin')
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
const wrongSize = []

// Elgato's Marketplace guidelines fix each icon's size, and the sizes differ
// by ROLE, not by taste: the picture in the actions list is not the picture on
// the key. The plugin shipped for a while with one 72x72 file doing both jobs
// and a 28x28 plugin icon where the listing wants 256x256 -- all of which
// loads fine on a deck and none of which passes review. Sizes in px, @1x.
const iconSizes = {
	plugin: 256,    // the Marketplace listing
	category: 28,
	action: 20,     // white stroke, transparent, in the actions list
	key: 72,        // the button the operator presses
}

// PNG puts width and height in the IHDR chunk, which is always the first one,
// so the first 24 bytes are enough and no image library is needed.
const pngSize = (file) => {
	const head = Buffer.alloc(24)
	const fd = fs.openSync(file, 'r')
	try {
		if (fs.readSync(fd, head, 0, 24, 0) < 24) return null
	} finally {
		fs.closeSync(fd)
	}
	if (head.toString('ascii', 1, 4) !== 'PNG') return null
	return { width: head.readUInt32BE(16), height: head.readUInt32BE(20) }
}

const need = (relative, what) => {
	if (!relative) return
	// Icons are named without their extension and come in two sizes.
	const candidates = what === 'file'
		? [[relative, 0]]
		: [[relative + '.png', iconSizes[what]],
		   [relative + '@2x.png', iconSizes[what] * 2]]
	for (const [candidate, expected] of candidates) {
		const full = path.join(plugin, candidate)
		if (!fs.existsSync(full)) {
			missing.push(candidate)
			continue
		}
		if (!expected) continue
		const size = pngSize(full)
		if (!size) continue
		if (size.width !== expected || size.height !== expected) {
			wrongSize.push(
				`${candidate} is ${size.width}x${size.height}, wants ${expected}x${expected}`)
		}
	}
}
need(manifest.CodePath, 'file')
need(manifest.Icon, 'plugin')
need(manifest.CategoryIcon, 'category')
for (const action of manifest.Actions || []) {
	need(action.Icon, 'action')
	need(action.PropertyInspectorPath, 'file')
	for (const state of action.States || []) need(state.Image, 'key')
}
if (missing.length) {
	console.error('the manifest names files that are not there:')
	for (const name of missing) console.error('  ' + name)
	process.exit(1)
}
if (wrongSize.length) {
	console.error('artwork is not the size the guidelines ask for:')
	for (const line of wrongSize) console.error('  ' + line)
	console.error('regenerate it with: python tools/make_icons.py')
	process.exit(1)
}
console.log('every path the manifest names is present, at the right size')
