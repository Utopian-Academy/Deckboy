/*
 * Live check — point the module's protocol layer at a running Deckboy and
 * print what Companion would show. Not part of `npm test` (it needs a real
 * app); run it when changing the parser or after a Deckboy status-format
 * change:
 *
 *   node tools/live-check.js [host] [port]
 *
 * Exits non-zero if Deckboy answers but nothing parses, which is the failure
 * mode that would otherwise show up as silently blank Companion variables.
 */

import net from 'node:net'

import { parseStatus } from '../src/protocol.js'
import { buildVariableValues } from '../src/variables.js'

const host = process.argv[2] ?? '127.0.0.1'
const port = Number.parseInt(process.argv[3] ?? '5510', 10)

const socket = net.createConnection({ host, port }, () => {
	socket.write('STATUS\n')
})

let buffer = ''
const timer = setTimeout(() => {
	console.error(`No reply from ${host}:${port} within 3s.`)
	console.error('Is Deckboy running, and is REMOTE on if this is not localhost?')
	socket.destroy()
	process.exit(2)
}, 3000)

socket.on('data', (chunk) => {
	buffer += chunk.toString('utf8')
	if (!buffer.includes('OUTPUT')) return // wait for the whole report
	clearTimeout(timer)

	const state = parseStatus(buffer)
	state.connected = true
	const values = buildVariableValues(state)

	console.log(`Connected to Deckboy ${state.global.version || '?'} at ${host}:${port}\n`)
	console.log(`decks parsed:   ${state.decks.size}`)
	console.log(`outputs parsed: ${state.outputs.size}\n`)

	const interesting = Object.entries(values).filter(([, v]) => v !== '' && v !== undefined)
	for (const [key, value] of interesting) {
		console.log(`  $(deckboy:${key}) = ${value}`)
	}

	socket.destroy()
	if (state.decks.size === 0) {
		console.error('\nDeckboy replied but no DECK line parsed — the status format may have changed.')
		process.exit(1)
	}
	console.log('\nOK')
	process.exit(0)
})

socket.on('error', (err) => {
	clearTimeout(timer)
	console.error(`Connection to ${host}:${port} failed: ${err.message}`)
	process.exit(2)
})
