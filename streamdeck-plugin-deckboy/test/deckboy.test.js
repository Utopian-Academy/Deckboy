// What can be proved without a Stream Deck and without Deckboy running.
//
// The plugin's own WebSocket half needs Stream Deck to exercise it, but the
// parts that decide WHAT to send and WHAT a key should say are ordinary
// functions, and those are where the mistakes live: a wrong command string is
// a key that quietly does nothing on a show.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import net from 'node:net'
import { parseStatus, transportTitle, DeckboyLink } from '../src/deckboy.js'

test('a STATUS line becomes fields', () => {
	const s = parseStatus('state=playing deck=1 tc=00:01:02:03 output_fps=59.9')
	assert.equal(s.state, 'playing')
	assert.equal(s.deck, '1')
	// Timecode keeps its text: turned into a number it loses the frames.
	assert.equal(s.tc, '00:01:02:03')
	assert.equal(s.output_fps, '59.9')
})

test('rubbish in a STATUS line is skipped, not guessed at', () => {
	const s = parseStatus('state=playing garbage =novalue also')
	assert.equal(s.state, 'playing')
	assert.equal(Object.keys(s).length, 1)
})

test('a key says what the deck is doing', () => {
	assert.equal(transportTitle({ state: 'playing' }), 'LIVE')
	assert.equal(transportTitle({ state: 'paused' }), 'HELD')
	assert.equal(transportTitle({ state: 'stopped' }), 'STOP')
	// Nothing known is not the same as stopped, and must not read as it.
	assert.equal(transportTitle({}), 'offline')
})

test('a command is refused rather than queued when nothing is connected', () => {
	const link = new DeckboyLink()
	assert.equal(link.send('TAKE'), false)
	link.shutdown()
})

test('it connects, sends, and reads a status line back', async () => {
	const received = []
	const accepted = []
	const server = net.createServer((socket) => {
		accepted.push(socket)
		socket.setEncoding('utf8')
		socket.on('data', (chunk) => {
			for (const line of chunk.split('\n')) {
				const trimmed = line.trim()
				if (!trimmed) continue
				received.push(trimmed)
				if (trimmed === 'STATUS') {
					socket.write('state=playing deck=1\n')
				}
			}
		})
	})
	await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
	const port = server.address().port

	const link = new DeckboyLink()
	const status = new Promise((resolve) => link.once('status', resolve))
	link.configure('127.0.0.1', port)
	await new Promise((resolve) => link.once('connected', resolve))

	assert.equal(link.send('TAKE 3'), true)
	const fields = await status
	assert.equal(fields.state, 'playing')
	assert.ok(received.includes('STATUS'), 'polls for status on connect')
	assert.ok(received.includes('TAKE 3'), 'sends the command it was given')

	link.shutdown()
	// The server's own sockets have to go too: net.Server.close() waits for
	// live connections, and the reconnect test deliberately leaves one open.
	for (const s of accepted) s.destroy()
	await new Promise((resolve) => server.close(resolve))
})

test('it comes back on its own after Deckboy restarts', async () => {
	const accepted = []
	const server = net.createServer((socket) => accepted.push(socket))
	await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
	const port = server.address().port

	const link = new DeckboyLink()
	link.configure('127.0.0.1', port)
	await new Promise((resolve) => link.once('connected', resolve))

	const reconnected = new Promise((resolve) => link.once('connected', resolve))
	const dropped = new Promise((resolve) => link.once('disconnected', resolve))
	link.socket.destroy()
	await dropped
	assert.equal(link.connected, false)
	// The retry is on a timer; an operator restarting Deckboy between shows
	// should not have to touch the deck.
	await reconnected
	assert.equal(link.connected, true)

	link.shutdown()
	// The server's own sockets have to go too: net.Server.close() waits for
	// live connections, and the reconnect test deliberately leaves one open.
	for (const s of accepted) s.destroy()
	await new Promise((resolve) => server.close(resolve))
})
