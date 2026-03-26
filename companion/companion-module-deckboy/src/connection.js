import { TCPHelper } from '@companion-module/base'

/**
 * Manages the TCP socket to Deckboy and polls STATUS JSON for state.
 */
export class DeckboyConnection {
	constructor(host, port, pollInterval, callbacks) {
		this.host = host
		this.port = port
		this.pollInterval = pollInterval
		this.callbacks = callbacks
		this.socket = null
		this.pollTimer = null
		this.buffer = ''
		this.destroyed = false

		this.connect()
	}

	connect() {
		if (this.destroyed) return

		this.socket = new TCPHelper(this.host, this.port)

		this.socket.on('connect', () => {
			this.callbacks.onConnected()
			this.startPolling()
		})

		this.socket.on('data', (data) => {
			this.handleData(data.toString())
		})

		this.socket.on('error', (err) => {
			this.callbacks.onError(err.message || String(err))
		})

		this.socket.on('close', () => {
			this.stopPolling()
			this.callbacks.onDisconnected()
		})
	}

	handleData(chunk) {
		this.buffer += chunk

		// The JSON response is a single line terminated by newline.
		// It starts with '{' and ends with '}\n'.
		// Status text responses start with 'DECKBOY_' — we only parse JSON.
		const lines = this.buffer.split('\n')
		// Keep the last incomplete fragment in the buffer.
		this.buffer = lines.pop() || ''

		for (const line of lines) {
			const trimmed = line.trim()
			if (!trimmed.startsWith('{')) continue
			try {
				const state = JSON.parse(trimmed)
				if (state.app) {
					this.callbacks.onState(state)
				}
			} catch {
				// Not valid JSON — ignore (could be a text status line).
			}
		}
	}

	startPolling() {
		this.stopPolling()
		// Initial poll immediately.
		this.send('STATUS JSON')
		this.pollTimer = setInterval(() => {
			this.send('STATUS JSON')
		}, this.pollInterval)
	}

	stopPolling() {
		if (this.pollTimer) {
			clearInterval(this.pollTimer)
			this.pollTimer = null
		}
	}

	send(cmd) {
		if (this.socket) {
			this.socket.send(cmd + '\n').catch(() => {})
		}
	}

	destroy() {
		this.destroyed = true
		this.stopPolling()
		if (this.socket) {
			this.socket.destroy()
			this.socket = null
		}
	}
}
