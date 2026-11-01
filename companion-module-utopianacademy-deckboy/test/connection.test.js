/*
 * Connection tests: the passphrase and what happens when Deckboy refuses it.
 *
 * The connection methods run against a stand-in for the instance -- a fake
 * socket that records what was sent, and records of status and log calls --
 * so no Companion and no Deckboy are needed. These cover the v1.0.7/v1.0.8
 * review: the passphrase was read from `config`, where Companion 5 never puts
 * a secret-text value, and a refusal cycled the status every few seconds.
 */

import assert from 'node:assert/strict'
import { test } from 'node:test'

import { InstanceStatus } from '@companion-module/base'
import DeckboyInstance from '../src/main.js'
import { buildActions } from '../src/actions.js'

const proto = DeckboyInstance.prototype

function fakeInstance({ secrets = {}, config = { host: '10.0.0.5', port: 5510, pollInterval: 250 } } = {}) {
	const sent = []
	const statuses = []
	const logs = []
	const self = {
		config,
		secrets,
		socket: { isConnected: true, send: (text) => sent.push(text), destroy() {} },
		pollTimer: undefined,
		receiveBuffer: '',
		statusPending: false,
		statusSentAt: 0,
		pendingReport: undefined,
		decoder: undefined,
		stallCount: 0,
		lastErrorMessage: undefined,
		lastAuthMessage: undefined,
		authFailed: false,
		state: { connected: false, global: {}, decks: new Map(), outputs: new Map() },
		sent,
		statuses,
		logs,
		updateStatus: (status, message) => statuses.push([status, message]),
		log: (level, message) => logs.push([level, message]),
		setConnected(connected) {
			this.state.connected = connected
		},
		publishState() {},
		startPolling() {
			this.pollTimer = 'running'
		},
		stopPolling() {
			this.pollTimer = undefined
		},
		closeSocket() {},
	}
	for (const name of ['onConnected', 'requestStatus', 'statusStallMs', 'sendCommand', 'handleData', 'flushReport', 'openConnection']) {
		self[name] = proto[name].bind(self)
	}
	return self
}

test('the passphrase comes from secrets, and AUTH is the first line sent', () => {
	const instance = fakeInstance({ secrets: { passphrase: 'show2026' } })
	instance.onConnected()
	assert.equal(instance.sent[0], 'AUTH show2026\n')
	assert.equal(instance.sent[1], 'STATUS\n')
})

test('no passphrase: no AUTH, straight to STATUS', () => {
	const instance = fakeInstance({ secrets: {} })
	instance.onConnected()
	assert.deepEqual(instance.sent, ['STATUS\n'])
})

test('a passphrase in config is not one: Companion 5 never puts secret-text there', () => {
	const instance = fakeInstance({ secrets: {}, config: { host: '10.0.0.5', passphrase: 'stale' } })
	instance.onConnected()
	assert.ok(!instance.sent.some((line) => line.startsWith('AUTH')))
})

test('a passphrase cannot carry a second command', () => {
	const instance = fakeInstance({ secrets: { passphrase: 'abc\nBLACKOUT' } })
	instance.onConnected()
	assert.equal(instance.sent[0], 'AUTH abcBLACKOUT\n')
})

test('connecting is not OK until Deckboy answers with a report', () => {
	const instance = fakeInstance()
	instance.onConnected()
	assert.ok(!instance.statuses.some(([status]) => status === InstanceStatus.Ok))
	instance.handleData(Buffer.from('DECKBOY version=0.99.424 decks=0 outputs=0\n'))
	assert.equal(instance.statuses.at(-1)[0], InstanceStatus.Ok)
})

test('a refused passphrase stops polling and stays Bad config -- no reconnect cycle', () => {
	const instance = fakeInstance({ secrets: { passphrase: 'wrong' } })
	instance.onConnected()
	instance.handleData(Buffer.from('ERR AUTH wrong passphrase\n'))
	assert.equal(instance.authFailed, true)
	assert.equal(instance.pollTimer, undefined)
	assert.equal(instance.statusPending, false)
	assert.equal(instance.statuses.at(-1)[0], InstanceStatus.BadConfig)
	assert.match(instance.statuses.at(-1)[1], /refused the passphrase/)

	// The watchdog cannot fire: nothing more is asked for, however long.
	const before = instance.sent.length
	for (let i = 0; i < 10; i++) {
		instance.statusSentAt = 0
		instance.requestStatus()
	}
	assert.equal(instance.sent.length, before)
	assert.ok(!instance.statuses.some(([status]) => status === InstanceStatus.ConnectionFailure))
	assert.ok(!instance.logs.some(([, message]) => /reconnecting/.test(message)))
})

test('after a refusal, a reconnect does not send the same passphrase again', () => {
	const instance = fakeInstance({ secrets: { passphrase: 'wrong' } })
	instance.onConnected()
	instance.handleData(Buffer.from('ERR AUTH wrong passphrase\n'))
	const before = instance.sent.length
	instance.onConnected() // TCPHelper reconnecting by itself
	assert.equal(instance.sent.length, before)
})

test('new settings retry, and a later refusal is logged again', () => {
	const instance = fakeInstance({ secrets: { passphrase: 'wrong' } })
	instance.onConnected()
	instance.handleData(Buffer.from('ERR AUTH wrong passphrase\n'))
	assert.equal(instance.logs.filter(([level]) => level === 'error').length, 1)

	// The settings change (openConnection runs; no host here, so no socket opens).
	instance.config = {}
	instance.openConnection()
	assert.equal(instance.authFailed, false)
	assert.equal(instance.lastAuthMessage, undefined)

	// It works for a while, then the passphrase in Deckboy changes.
	const later = fakeInstance({ secrets: { passphrase: 'right' } })
	later.onConnected()
	later.handleData(Buffer.from('ERR AUTH wrong passphrase\n'))
	later.authFailed = false
	later.onConnected()
	later.handleData(Buffer.from('DECKBOY version=0.99.424 decks=0 outputs=0\n'))
	later.handleData(Buffer.from('ERR AUTH wrong passphrase\n'))
	assert.equal(later.logs.filter(([level]) => level === 'error').length, 2)
})

function actionsWithLog() {
	const sent = []
	const logs = []
	const actions = buildActions({
		state: { connected: true, global: { focus: '1' }, decks: new Map(), outputs: new Map() },
		sendCommand: (cmd) => sent.push(cmd),
		log: (level, message) => logs.push([level, message]),
	})
	return { actions, sent, logs }
}

test('the Device fields resolve variables', () => {
	const { actions } = actionsWithLog()
	for (const id of ['limiter', 'output_fx_bypass']) {
		const action = actions[id]
		assert.ok(action, `${id} exists`)
		const device = action.options.find((option) => option.id === 'device')
		assert.equal(device.useVariables, true, `${id} device field`)
	}
})

test('Run dashboard tile says why it sent nothing', () => {
	const { actions, sent, logs } = actionsWithLog()
	actions.dash_tile.callback({ options: { tile: '' } })
	actions.dash_tile.callback({ options: { tile: 0 } })
	assert.deepEqual(sent, [])
	assert.equal(logs.filter(([level]) => level === 'warn').length, 2)
	actions.dash_tile.callback({ options: { tile: 3 } })
	assert.deepEqual(sent, ['DASH 3'])
})
