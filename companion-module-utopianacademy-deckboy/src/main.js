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

import { InstanceBase, InstanceStatus, Regex, TCPHelper } from '@companion-module/base'

import { buildActions } from './actions.js'
import { buildFeedbacks } from './feedbacks.js'
import { buildPresetSections, buildPresets } from './presets.js'
import { buildVariableDefinitions, buildVariableValues } from './variables.js'
import { expectedReportLines, parseStatus } from './protocol.js'
import { StringDecoder } from 'node:string_decoder'

class DeckboyInstance extends InstanceBase {
	constructor(internal) {
		super(internal)
		this.socket = undefined
		this.pollTimer = undefined
		this.receiveBuffer = ''
		this.statusPending = false
		this.statusSentAt = 0
		this.pendingReport = undefined
		this.decoder = undefined
		this.stallCount = 0
		this.lastErrorMessage = undefined
		this.publishedValues = undefined
		this.publishedSignature = undefined
		// Shared with feedbacks.js and variables.js — the last parsed STATUS.
		this.state = { connected: false, global: {}, decks: new Map(), outputs: new Map() }
	}

	async init(config) {
		this.config = config
		this.setActionDefinitions(buildActions(this))
		this.setFeedbackDefinitions(buildFeedbacks(this))
		this.setPresetDefinitions(buildPresetSections(), buildPresets())
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
		this.decoder = new StringDecoder('utf8')
		this.receiveBuffer = ''
		this.pendingReport = undefined
		this.socket = new TCPHelper(this.config.host, this.config.port || 5510)

		this.socket.on('status_change', (status, message) => this.updateStatus(status, message))

		this.socket.on('error', (err) => {
			// TCPHelper reconnects on its own. Polling stops until it does, or a
			// closed Deckboy fills the log with one dropped STATUS per poll.
			this.stopPolling()
			this.setConnected(false)
			// Deckboy binds localhost-only by default, which is the single most
			// common reason a remote Companion sees nothing — say so instead of
			// leaving the operator with a bare ECONNREFUSED.
			const hint =
				this.config.host !== '127.0.0.1' && this.config.host !== 'localhost'
					? ' — check Settings → Network → REMOTE is ON in Deckboy, and that the port is open in the firewall'
					: ''
			this.updateStatus(InstanceStatus.ConnectionFailure, `${err.message}${hint}`)
			// Once per distinct failure, not once per two-second retry.
			if (err.message !== this.lastErrorMessage) {
				this.lastErrorMessage = err.message
				this.log('error', `Deckboy connection error: ${err.message}${hint}`)
			}
		})

		// A clean close (Deckboy quit) raises no error, only an end.
		this.socket.on('end', () => {
			this.stopPolling()
			this.setConnected(false)
			this.updateStatus(InstanceStatus.Disconnected, 'Deckboy closed the connection')
			if (this.lastErrorMessage !== 'end') {
				this.lastErrorMessage = 'end'
				this.log('info', 'Deckboy closed the connection — reconnecting')
			}
		})

		this.socket.on('connect', () => {
			this.lastErrorMessage = undefined
			this.stallCount = 0
			this.statusPending = false
			this.setConnected(true)
			this.updateStatus(InstanceStatus.Ok)
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
		if (!this.socket || !this.socket.isConnected) return

		// One outstanding STATUS at a time: if Deckboy is busy (a big show
		// loading, a slow drive) piling on more requests only makes it worse.
		//
		// But not forever. A request that is never answered is abandoned after
		// a few poll intervals so the next one goes out -- and if that keeps
		// happening, the link is dead even though TCP has not noticed yet (a
		// pulled cable can take minutes to error). Say so on the surface rather
		// than leave the last tally and countdown frozen on the buttons.
		const now = Date.now()
		if (this.statusPending) {
			const waited = now - (this.statusSentAt || 0)
			if (waited < this.statusStallMs()) return
			this.stallCount = (this.stallCount || 0) + 1
			if (this.stallCount >= STALLS_BEFORE_RECONNECT) {
				this.log('warn', `Deckboy has not answered STATUS ${this.stallCount} times — reconnecting`)
				this.openConnection()
				this.updateStatus(InstanceStatus.ConnectionFailure, 'No STATUS reply from Deckboy — reconnecting')
				return
			}
			this.log('debug', `STATUS did not answer in ${waited}ms — asking again`)
		}
		this.statusPending = true
		this.statusSentAt = now
		this.sendCommand('STATUS')
	}

	// How long a STATUS may be outstanding before the next poll is allowed
	// through. Several intervals, so an ordinary slow reply is still waited
	// for, with a floor for very fast poll rates and a ceiling so a surface
	// cannot sit dead for long.
	statusStallMs() {
		const interval = Number(this.config?.pollInterval) || 250
		return Math.min(5000, Math.max(1500, interval * 4))
	}

	// ONE LINE PER SEND, whatever the option said.
	//
	// The protocol is newline-delimited and this is the only place the newline
	// is appended, so it is the only place that has to be sure there is exactly
	// one. Every free-text option resolves Companion variables BEFORE the
	// callback sees the value, so a value carrying a newline -- a typo, a
	// pasted multi-line string, a variable fed in by another module -- turned
	// one button press into two commands. That is command injection into the
	// operator's own desk, and on a cue deck the smuggled one could be
	// anything: BLACKOUT, PANIC, a take.
	//
	// Fixed HERE rather than at the eight options that take free text, because
	// a ninth will be added and would not know to do it.
	//
	// Folded to spaces rather than refused: the button then sends one command
	// with a wrong argument, which Deckboy answers with ERR, instead of
	// silently doing nothing. Either is safe; this one says so.
	sendCommand(command) {
		const raw = String(command)
		const oneLine = raw.replace(/[\r\n]+/g, ' ').trim()
		if (oneLine !== raw.trim()) {
			this.log('warn', `Line break removed from command: ${JSON.stringify(raw)}`)
		}
		if (!oneLine) return
		if (!this.socket || !this.socket.isConnected) {
			this.log('warn', `Not connected — dropped command: ${oneLine}`)
			return
		}
		this.socket.send(`${oneLine}\n`)
	}

	// ── Incoming data ────────────────────────────────────────────────────────

	handleData(chunk) {
		// A StringDecoder keeps a multi-byte character that straddles two TCP
		// chunks whole; decoding each chunk on its own split a non-ASCII cue
		// name in two.
		this.decoder ??= new StringDecoder('utf8')
		this.receiveBuffer += this.decoder.write(chunk)

		const lines = this.receiveBuffer.split(/\r?\n/)
		this.receiveBuffer = lines.pop() ?? ''
		// A peer that never sends a newline -- the wrong port, something that is
		// not Deckboy -- would otherwise grow this for the life of the socket.
		if (this.receiveBuffer.length > MAX_LINE_LENGTH) {
			this.log('warn', `Discarded ${this.receiveBuffer.length} characters with no line break — is this Deckboy's port?`)
			this.receiveBuffer = ''
		}

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
				if (this.pendingReport) {
					this.log('debug', 'A STATUS reply was cut short by the next one — discarded')
				}
				this.pendingReport = { lines: [line], expected: expectedReportLines(line) }
			} else if (this.pendingReport) {
				this.pendingReport.lines.push(line)
			} else {
				continue
			}
			// A REPORT IS COMPLETE WHEN IT SAYS IT IS. The header carries
			// decks=N outputs=M and every deck and output gets exactly one line,
			// so the report is done at 1 + N + M lines -- however TCP chunked it.
			// Flushing at the end of each chunk instead published the first half
			// of a split reply and dropped the rest.
			const report = this.pendingReport
			if (report.expected !== null && report.lines.length >= report.expected) this.flushReport()
			else if (report.lines.length > MAX_REPORT_LINES) this.flushReport()
		}

		// A header without both counts (an older Deckboy, or its "nothing loaded
		// yet" placeholder) cannot say where it ends; take what the burst held.
		if (this.pendingReport && this.pendingReport.expected === null) this.flushReport()
	}

	// Only a real report ends a STATUS request. An acknowledgement-only chunk
	// (the OK for a button press) used to clear the flag too, and a second
	// STATUS went out while the first was still on its way.
	flushReport() {
		const report = this.pendingReport
		this.pendingReport = undefined
		if (!report || report.lines.length === 0) return
		this.statusPending = false
		this.stallCount = 0

		try {
			const parsed = parseStatus(report.lines.join('\n'))
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
			this.pendingReport = undefined
			this.state.decks = new Map()
			this.state.outputs = new Map()
		}
		this.publishState()
	}

	// Four reports a second mostly say what the last one said. Send Companion
	// only the variables that changed, and re-check feedbacks only when the
	// state behind them did -- every feedback here reads the same polled STATUS.
	publishState() {
		const values = buildVariableValues(this.state)
		const last = this.publishedValues ?? {}
		const changed = {}
		for (const [id, value] of Object.entries(values)) {
			if (last[id] !== value) changed[id] = value
		}
		this.publishedValues = values
		if (Object.keys(changed).length > 0) this.setVariableValues(changed)

		const signature = stateSignature(this.state)
		if (signature === this.publishedSignature) return
		this.publishedSignature = signature
		// base 2.x split the no-argument form of checkFeedbacks() out into its
		// own method -- calling checkFeedbacks() with nothing now checks nothing.
		this.checkAllFeedbacks()
	}
}

// Three abandoned STATUS requests in a row is a dead link, not a busy app.
const STALLS_BEFORE_RECONNECT = 3
// No Deckboy line comes near this; a buffer that does is not talking to Deckboy.
const MAX_LINE_LENGTH = 64 * 1024
// One header plus sixteen decks and sixteen outputs, with room to spare.
const MAX_REPORT_LINES = 256

function stateSignature(state) {
	return JSON.stringify([state.connected, state.global, [...state.decks], [...state.outputs]])
}

// base 2.x loads the module from the default export instead of runEntrypoint(),
// and upgrade scripts from the UpgradeScripts named export.
export { UpgradeScripts } from './upgrades.js'
export default DeckboyInstance
