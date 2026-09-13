/*
 * companion-module-deckboy — Bitfocus Companion module for Deckboy.
 *
 * Replaces the Generic TCP/UDP recipe in docs/streamdeck/. That recipe could
 * only push commands one way; this module also polls Deckboy's STATUS reply,
 * so buttons carry tally, transport state, output health and a countdown.
 *
 * Connection model: one long-lived TCP socket on Deckboy's Companion port
 * (5510 by default). Commands are newline-terminated plain text; STATUS is
 * re-sent on a timer and every reply refreshes variables and feedbacks.
 */

import { InstanceBase, InstanceStatus, Regex, TCPHelper, runEntrypoint } from '@companion-module/base'

import { buildActions } from './src/actions.js'
import { buildFeedbacks } from './src/feedbacks.js'
import { buildPresets } from './src/presets.js'
import { buildVariableDefinitions, buildVariableValues } from './src/variables.js'
import { parseStatus } from './src/protocol.js'

class DeckboyInstance extends InstanceBase {
	constructor(internal) {
		super(internal)
		this.socket = undefined
		this.pollTimer = undefined
		this.receiveBuffer = ''
		this.statusPending = false
		this.pendingReport = undefined
		// Shared with feedbacks.js and variables.js — the last parsed STATUS.
		this.state = { connected: false, global: {}, decks: new Map(), outputs: new Map() }
	}

	async init(config) {
		this.config = config
		this.setActionDefinitions(buildActions(this))
		this.setFeedbackDefinitions(buildFeedbacks(this))
		this.setPresetDefinitions(buildPresets())
		this.setVariableDefinitions(buildVariableDefinitions())
		this.publishState()
		this.openConnection()
	}

	async configUpdated(config) {
		this.config = config
		this.openConnection()
	}

	async destroy() {
		this.stopPolling()
		this.closeSocket()
	}

	getConfigFields() {
		return [
			{
				type: 'static-text',
				id: 'intro',
				width: 12,
				label: 'Deckboy',
				value:
					'Enter the address of the machine running Deckboy. The port must match ' +
					'Settings → Network → Companion port (5510 by default).<br><br>' +
					'<b>Deckboy listens on localhost only until you turn on ' +
					'Settings → Network → REMOTE.</b> Leave it off and only Companion running ' +
					'on the same machine can connect.',
			},
			{
				type: 'textinput',
				id: 'host',
				label: 'Deckboy IP address',
				width: 8,
				default: '127.0.0.1',
				regex: Regex.HOSTNAME,
			},
			{ type: 'number', id: 'port', label: 'Port', width: 4, default: 5510, min: 1, max: 65535 },
			{
				type: 'number',
				id: 'pollInterval',
				label: 'Status poll interval (ms)',
				tooltip:
					'How often to ask Deckboy for state. 250ms keeps countdowns smooth; raise it ' +
					'if you are running many surfaces against one machine.',
				width: 6,
				default: 250,
				min: 100,
				max: 5000,
			},
		]
	}

	// ── Connection ───────────────────────────────────────────────────────────

	openConnection() {
		this.stopPolling()
		this.closeSocket()

		if (!this.config?.host) {
			this.updateStatus(InstanceStatus.BadConfig, 'No Deckboy address set')
			return
		}

		this.updateStatus(InstanceStatus.Connecting)
		this.socket = new TCPHelper(this.config.host, this.config.port || 5510)

		this.socket.on('status_change', (status, message) => this.updateStatus(status, message))

		this.socket.on('error', (err) => {
			this.setConnected(false)
			// Deckboy binds localhost-only by default, which is the single most
			// common reason a remote Companion sees nothing — say so instead of
			// leaving the operator with a bare ECONNREFUSED.
			const hint =
				this.config.host !== '127.0.0.1' && this.config.host !== 'localhost'
					? ' — check Settings → Network → REMOTE is ON in Deckboy, and that the port is open in the firewall'
					: ''
			this.updateStatus(InstanceStatus.ConnectionFailure, `${err.message}${hint}`)
			this.log('error', `Deckboy connection error: ${err.message}${hint}`)
		})

		this.socket.on('connect', () => {
			this.setConnected(true)
			this.updateStatus(InstanceStatus.Ok)
			this.receiveBuffer = ''
			this.requestStatus()
			this.startPolling()
		})

		this.socket.on('data', (chunk) => this.handleData(chunk))
	}

	closeSocket() {
		if (this.socket) {
			this.socket.destroy()
			this.socket = undefined
		}
		this.setConnected(false)
	}

	startPolling() {
		this.stopPolling()
		const interval = Math.max(100, Number(this.config?.pollInterval) || 250)
		this.pollTimer = setInterval(() => this.requestStatus(), interval)
	}

	stopPolling() {
		if (this.pollTimer) {
			clearInterval(this.pollTimer)
			this.pollTimer = undefined
		}
	}

	requestStatus() {
		// One outstanding STATUS at a time: if Deckboy is busy (a big show
		// loading, a slow drive) piling on more requests only makes it worse.
		if (this.statusPending) return
		this.statusPending = true
		this.sendCommand('STATUS')
	}

	sendCommand(command) {
		if (!this.socket || !this.socket.isConnected) {
			this.log('warn', `Not connected — dropped command: ${command}`)
			return
		}
		this.socket.send(`${command}\n`)
	}

	// ── Incoming data ────────────────────────────────────────────────────────

	handleData(chunk) {
		this.receiveBuffer += chunk.toString('utf8')

		// A STATUS reply is several lines with no terminator of its own. Treat a
		// DECKBOY header line as the start of a report and flush the previous
		// one when the next header (or a quiet gap) arrives.
		const lines = this.receiveBuffer.split(/\r?\n/)
		this.receiveBuffer = lines.pop() ?? ''
		if (lines.length === 0) return

		for (const line of lines) {
			// Command acknowledgements are not part of a report and may land in
			// the same chunk as one. ERR is worth surfacing — it means the verb or
			// its arguments were wrong, which used to be silent.
			if (line.startsWith('OK ')) continue
			if (line.startsWith('ERR ')) {
				this.log('warn', `Deckboy rejected a command: ${line.slice(4)}`)
				continue
			}
			if (line.startsWith('DECKBOY')) {
				this.flushReport()
				this.pendingReport = [line]
			} else if (this.pendingReport) {
				this.pendingReport.push(line)
			}
		}
		// Deckboy sends the whole report in one burst; flush on the same tick so
		// feedbacks update immediately rather than one poll late.
		this.flushReport()
	}

	flushReport() {
		if (!this.pendingReport || this.pendingReport.length === 0) {
			this.pendingReport = undefined
			return
		}
		const payload = this.pendingReport.join('\n')
		this.pendingReport = undefined
		this.statusPending = false

		try {
			const parsed = parseStatus(payload)
			this.state.global = parsed.global
			this.state.decks = parsed.decks
			this.state.outputs = parsed.outputs
			this.publishState()
		} catch (err) {
			this.log('warn', `Could not parse Deckboy status: ${err.message}`)
		}
	}

	setConnected(connected) {
		if (this.state.connected === connected) return
		this.state.connected = connected
		if (!connected) {
			this.statusPending = false
			this.state.decks = new Map()
			this.state.outputs = new Map()
		}
		this.publishState()
	}

	publishState() {
		this.setVariableValues(buildVariableValues(this.state))
		this.checkFeedbacks()
	}
}

runEntrypoint(DeckboyInstance, [])
