// deckboy.js — one long-lived TCP connection to Deckboy, shared by every key.
//
// Every key on the deck talks to the same instance, so they share one socket
// rather than opening their own. A Stream Deck page can hold thirty keys and
// thirty sockets to the same port is thirty times the reconnect storm when the
// application restarts between shows.
//
// RECONNECTS ON ITS OWN, because Deckboy is the thing being restarted. An
// operator relaunches it mid-setup and expects the deck to come back without
// touching anything; a plugin that gave up on the first refusal would need
// unplugging to recover.

import net from 'node:net'
import { EventEmitter } from 'node:events'

const RETRY_MS = 2000
const STATUS_MS = 500

export class DeckboyLink extends EventEmitter {
	constructor() {
		super()
		this.host = '127.0.0.1'
		this.port = 5510
		this.socket = null
		this.connected = false
		this.buffer = ''
		this.retryTimer = null
		this.statusTimer = null
		this.status = {}
		this.stopped = false
	}

	// Called whenever a key's settings name a different machine. Reconnects
	// only when something actually changed, so retyping a port in the property
	// inspector does not drop a live connection on every keystroke.
	configure(host, port) {
		const nextHost = (host || '127.0.0.1').trim() || '127.0.0.1'
		const nextPort = Number(port) || 5510
		if (nextHost === this.host && nextPort === this.port && this.socket) return
		this.host = nextHost
		this.port = nextPort
		this.connect()
	}

	connect() {
		this.stopped = false
		this.close()
		const socket = net.createConnection({ host: this.host, port: this.port })
		this.socket = socket
		socket.setEncoding('utf8')
		socket.on('connect', () => {
			this.connected = true
			this.emit('connected')
			this.startStatusPolling()
		})
		socket.on('data', (chunk) => this.onData(chunk))
		// error and close both land here; one retry timer, not two.
		socket.on('error', () => this.dropped())
		socket.on('close', () => this.dropped())
	}

	dropped() {
		if (!this.connected && !this.socket) return
		this.connected = false
		this.socket = null
		this.stopStatusPolling()
		this.emit('disconnected')
		if (this.stopped) return
		clearTimeout(this.retryTimer)
		this.retryTimer = setTimeout(() => this.connect(), RETRY_MS)
	}

	close() {
		clearTimeout(this.retryTimer)
		this.stopStatusPolling()
		if (this.socket) {
			this.socket.removeAllListeners()
			this.socket.destroy()
			this.socket = null
		}
		this.connected = false
	}

	shutdown() {
		this.stopped = true
		this.close()
	}

	send(command) {
		const line = String(command || '').trim()
		if (!line) return false
		if (!this.connected || !this.socket) return false
		this.socket.write(line + '\n')
		return true
	}

	// Deckboy answers line by line. STATUS comes back as one line of
	// space-separated key=value pairs, which is what drives the key titles.
	onData(chunk) {
		this.buffer += chunk
		let cut
		while ((cut = this.buffer.indexOf('\n')) >= 0) {
			const line = this.buffer.slice(0, cut).trim()
			this.buffer = this.buffer.slice(cut + 1)
			if (!line) continue
			if (line.includes('=')) {
				this.status = parseStatus(line)
				this.emit('status', this.status)
			} else {
				this.emit('reply', line)
			}
		}
	}

	startStatusPolling() {
		this.stopStatusPolling()
		this.statusTimer = setInterval(() => this.send('STATUS'), STATUS_MS)
		this.send('STATUS')
	}

	stopStatusPolling() {
		clearInterval(this.statusTimer)
		this.statusTimer = null
	}
}

// One line of `key=value key=value` into an object. Values keep their text:
// a timecode is not a number and turning it into one loses the frames.
export function parseStatus(line) {
	const out = {}
	for (const token of String(line).split(/\s+/)) {
		const eq = token.indexOf('=')
		if (eq <= 0) continue
		out[token.slice(0, eq)] = token.slice(eq + 1)
	}
	return out
}

// What a transport key should say under it. Deliberately short: a Stream Deck
// key is 72 pixels and a title that wraps to three lines says less than one
// that says "LIVE".
export function transportTitle(status) {
	if (!status || Object.keys(status).length === 0) return 'offline'
	const state = status.state || status.deck1_state || ''
	if (state === 'playing') return 'LIVE'
	if (state === 'paused') return 'HELD'
	if (state === 'stopped') return 'STOP'
	return state || 'ready'
}
