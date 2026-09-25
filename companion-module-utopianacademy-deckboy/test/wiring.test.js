/*
 * Wiring tests.
 *
 * Companion itself isn't needed to catch the mistakes that actually happen in
 * a module: a preset referencing an action or feedback id that no longer
 * exists, button text using a variable that was never declared, or an option
 * missing the id its callback reads. Those fail silently at runtime — the
 * button just does nothing — so they are asserted here instead.
 *
 * The builders are driven with a stub instance, which also proves they don't
 * touch Companion internals at definition time.
 */

import assert from 'node:assert/strict'
import { test } from 'node:test'

import { buildActions } from '../src/actions.js'
import { buildFeedbacks } from '../src/feedbacks.js'
import { buildPresetSections, buildPresets } from '../src/presets.js'
import { buildVariableDefinitions } from '../src/variables.js'

function stubInstance() {
	const sent = []
	return {
		sent,
		state: { connected: true, global: { focus: '1' }, decks: new Map(), outputs: new Map() },
		sendCommand: (cmd) => sent.push(cmd),
		// No parseVariablesInString: base 2.x resolves option values before the
		// callback sees them, and removed the method. A stub that still offered
		// it would let a re-introduced call pass here and fail in Companion.
		log: () => {},
	}
}

const actions = buildActions(stubInstance())
const feedbacks = buildFeedbacks(stubInstance())
const presets = buildPresets()
const variableDefinitions = buildVariableDefinitions()
const variableIds = new Set(Object.keys(variableDefinitions))

test('every action has a callback and well-formed options', () => {
	for (const [id, action] of Object.entries(actions)) {
		assert.equal(typeof action.callback, 'function', `${id} callback`)
		assert.ok(action.name, `${id} needs a name`)
		for (const option of action.options ?? []) {
			assert.ok(option.id, `${id} has an option with no id`)
			assert.ok(option.type, `${id} option ${option.id} has no type`)
		}
	}
})

test('every feedback has a callback, a type and well-formed options', () => {
	for (const [id, feedback] of Object.entries(feedbacks)) {
		assert.equal(typeof feedback.callback, 'function', `${id} callback`)
		assert.ok(['boolean', 'advanced'].includes(feedback.type), `${id} type`)
		if (feedback.type === 'boolean') {
			assert.ok(feedback.defaultStyle, `${id} boolean feedback needs a defaultStyle`)
		}
		for (const option of feedback.options ?? []) {
			assert.ok(option.id, `${id} has an option with no id`)
		}
	}
})

test('presets only reference actions that exist', () => {
	for (const [presetId, preset] of Object.entries(presets)) {
		for (const step of preset.steps ?? []) {
			for (const action of [...(step.down ?? []), ...(step.up ?? [])]) {
				assert.ok(
					Object.hasOwn(actions, action.actionId),
					`preset "${presetId}" references unknown action "${action.actionId}"`
				)
			}
		}
	}
})

test('presets only reference feedbacks that exist', () => {
	for (const [presetId, preset] of Object.entries(presets)) {
		for (const feedback of preset.feedbacks ?? []) {
			assert.ok(
				Object.hasOwn(feedbacks, feedback.feedbackId),
				`preset "${presetId}" references unknown feedback "${feedback.feedbackId}"`
			)
		}
	}
})

test('variable definitions are keyed by id, as base 2.x expects', () => {
	assert.ok(!Array.isArray(variableDefinitions), 'definitions must be an object, not an array')
	for (const [variableId, definition] of Object.entries(variableDefinitions)) {
		assert.ok(variableId.length > 0, 'a variable has an empty id')
		assert.ok(definition?.name, `variable "${variableId}" needs a name`)
		assert.ok(
			!Object.hasOwn(definition, 'variableId'),
			`variable "${variableId}" still carries the 1.x variableId field`
		)
	}
})

test('presets use the 2.x simple type and carry no category', () => {
	for (const [presetId, preset] of Object.entries(presets)) {
		assert.equal(preset.type, 'simple', `preset "${presetId}" must be type simple`)
		assert.ok(
			!Object.hasOwn(preset, 'category'),
			`preset "${presetId}" still carries a 1.x category; sections replace it`
		)
	}
})

test('every preset appears in exactly one section, and every section entry exists', () => {
	const sections = buildPresetSections()
	const seen = new Map()
	for (const section of sections) {
		assert.ok(section.id, 'a section has no id')
		assert.ok(section.name, `section "${section.id}" has no name`)
		for (const presetId of section.definitions) {
			assert.ok(Object.hasOwn(presets, presetId), `section "${section.id}" lists unknown preset "${presetId}"`)
			assert.ok(!seen.has(presetId), `preset "${presetId}" is listed in two sections`)
			seen.set(presetId, section.id)
		}
	}
	for (const presetId of Object.keys(presets)) {
		// A preset missing from the structure is defined but unreachable: it
		// simply never appears in Companion's preset browser.
		assert.ok(seen.has(presetId), `preset "${presetId}" is in no section and would be invisible`)
	}
})

test('preset option keys match the definition they target', () => {
	for (const [presetId, preset] of Object.entries(presets)) {
		for (const step of preset.steps ?? []) {
			for (const used of step.down ?? []) {
				const known = new Set((actions[used.actionId].options ?? []).map((o) => o.id))
				for (const key of Object.keys(used.options ?? {})) {
					assert.ok(known.has(key), `preset "${presetId}" sets unknown action option "${key}"`)
				}
			}
		}
		for (const used of preset.feedbacks ?? []) {
			const known = new Set((feedbacks[used.feedbackId].options ?? []).map((o) => o.id))
			for (const key of Object.keys(used.options ?? {})) {
				assert.ok(known.has(key), `preset "${presetId}" sets unknown feedback option "${key}"`)
			}
		}
	}
})

test('every $(deckboy:...) in preset text is a declared variable', () => {
	for (const [presetId, preset] of Object.entries(presets)) {
		const text = preset.style?.text ?? ''
		for (const match of text.matchAll(/\$\(deckboy:([a-zA-Z0-9_]+)\)/g)) {
			assert.ok(
				variableIds.has(match[1]),
				`preset "${presetId}" uses undeclared variable "${match[1]}"`
			)
		}
	}
})

test('actions emit the expected Deckboy commands', async () => {
	const self = stubInstance()
	const built = buildActions(self)
	await built.take.callback({ options: { deck: 0 } })
	assert.deepEqual(self.sent, ['TAKE'], 'deck 0 must not emit a DECK prefix')

	self.sent.length = 0
	await built.take.callback({ options: { deck: 2 } })
	assert.deepEqual(self.sent, ['DECK 2', 'TAKE'], 'an explicit deck must be selected first')

	self.sent.length = 0
	await built.take_cue.callback({ options: { deck: 0, cue: '7' } })
	assert.deepEqual(self.sent, ['SELECT 7', 'TAKE'])

	self.sent.length = 0
	await built.seek.callback({ options: { deck: 0, mode: 'abs', seconds: '30' } })
	assert.deepEqual(self.sent, ['SEEKPOS 30'])

	self.sent.length = 0
	await built.custom.callback({ options: { command: '  PANIC  ' } })
	assert.deepEqual(self.sent, ['PANIC'], 'custom commands are trimmed')

	self.sent.length = 0
	await built.custom.callback({ options: { command: '   ' } })
	assert.deepEqual(self.sent, [], 'an empty custom command sends nothing')
})

test('VJ actions emit the expected commands', async () => {
	const self = stubInstance()
	const built = buildActions(self)

	// A bare VJ reports STATUS rather than toggling, so a toggle button needs a
	// verb of its own -- otherwise the surface has to know which state the app
	// is in before it can pick between ON and OFF, which is the thing the
	// surface exists to save you.
	await built.vj_mode.callback({ options: { state: 'toggle' } })
	assert.deepEqual(self.sent, ['VJ TOGGLE'])

	self.sent.length = 0
	await built.vj_mode.callback({ options: { state: 'on' } })
	assert.deepEqual(self.sent, ['VJ ON'])

	self.sent.length = 0
	await built.vj_mix.callback({ options: { value: 0.35 } })
	assert.deepEqual(self.sent, ['VJ MIX 0.35'])

	self.sent.length = 0
	await built.vj_blend.callback({ options: { blend: 'multiply' } })
	assert.deepEqual(self.sent, ['VJ BLEND multiply'])

	self.sent.length = 0
	await built.vj_tap.callback({ options: {} })
	await built.vj_bpm.callback({ options: { value: 124 } })
	await built.vj_quantise.callback({ options: { state: 'on' } })
	await built.vj_decks.callback({ options: { a: 1, b: 3 } })
	assert.deepEqual(self.sent, ['VJ TAP', 'VJ BPM 124', 'VJ QUANTISE on', 'VJ DECKS 1 3'])
})

test('audio effect actions emit the expected commands', async () => {
	const self = stubInstance()
	const built = buildActions(self)

	await built.audiofx_add.callback({ options: { effect: 'comp', amount: 80 } })
	assert.deepEqual(self.sent, ['AUDIOFX ADD comp 80'], 'add names the effect and a percent')

	self.sent.length = 0
	await built.audiofx_amount.callback({ options: { index: 2, value: 45 } })
	assert.deepEqual(self.sent, ['AUDIOFX 2 45'], 'the slot is 1-based, as the read-back prints it')

	self.sent.length = 0
	await built.audiofx_bypass.callback({ options: { index: 3, state: 'ON' } })
	assert.deepEqual(self.sent, ['AUDIOFX 3 BYPASS ON'])

	self.sent.length = 0
	await built.audiofx_remove.callback({ options: { index: 1 } })
	assert.deepEqual(self.sent, ['AUDIOFX 1 OFF'])

	self.sent.length = 0
	await built.audiofx_clear.callback({ options: {} })
	assert.deepEqual(self.sent, ['AUDIOFX CLEAR'])
})

test('effect actions emit the expected commands', async () => {
	const self = stubInstance()
	const built = buildActions(self)

	await built.fx_add.callback({ options: { effect: ' schlieren ', amount: 0.9 } })
	assert.deepEqual(self.sent, ['FX ADD schlieren 0.9'], 'the effect name is trimmed')

	self.sent.length = 0
	await built.fx_add.callback({ options: { effect: '   ', amount: 1 } })
	assert.deepEqual(self.sent, [], 'no effect name sends nothing')

	self.sent.length = 0
	await built.fx_amount.callback({ options: { index: 2, value: 0.4 } })
	await built.fx_param.callback({ options: { index: 1, slot: 'C', value: 0.7 } })
	assert.deepEqual(self.sent, ['FX AMOUNT 2 0.4', 'FX PARAM 1 C 0.7'])

	// on/off carry no value, and must not emit a trailing space -- the parser
	// splits on whitespace and an empty final token is not the same as none.
	self.sent.length = 0
	await built.fx_lfo.callback({ options: { index: 1, slot: 'A', what: 'on', value: '' } })
	assert.deepEqual(self.sent, ['FX LFO 1 A on'])

	self.sent.length = 0
	await built.fx_lfo.callback({ options: { index: 1, slot: 'E', what: 'beats', value: '4' } })
	assert.deepEqual(self.sent, ['FX LFO 1 E beats 4'])

	self.sent.length = 0
	await built.fx_clear.callback({ options: {} })
	await built.fx_copy_paste.callback({ options: { action: 'paste' } })
	assert.deepEqual(self.sent, ['FX CLEAR', 'FX PASTE'])

	self.sent.length = 0
	await built.code_set.callback({ options: { expression: ' r, y, 0.5 ' } })
	assert.deepEqual(self.sent, ['CODE SET r, y, 0.5'])
})

test('feedbacks read the polled state', async () => {
	const self = stubInstance()
	const built = buildFeedbacks(self)
	self.state.decks.set(1, { status: 'Playing', active: '3', selected: '4', pos: '00:10.0', dur: '00:15.0' })
	self.state.outputs.set(1, { enabled: 'on', health: 'live' })

	assert.equal(built.deck_status.callback({ options: { deck: 1, status: 'Playing' } }), true)
	assert.equal(built.deck_status.callback({ options: { deck: 1, status: 'Paused' } }), false)
	// deck 0 resolves through global focus
	assert.equal(built.deck_status.callback({ options: { deck: 0, status: 'Playing' } }), true)
	assert.equal(await built.cue_is_live.callback({ options: { deck: 1, cue: '3' } }), true)
	assert.equal(await built.cue_is_live.callback({ options: { deck: 1, cue: '4' } }), false)
	assert.equal(await built.cue_is_selected.callback({ options: { deck: 1, cue: '4' } }), true)
	assert.equal(built.deck_remaining_below.callback({ options: { deck: 1, seconds: 10 } }), true)
	assert.equal(built.deck_remaining_below.callback({ options: { deck: 1, seconds: 3 } }), false)
	assert.equal(built.output_enabled.callback({ options: { output: 1 } }), true)
	assert.equal(built.output_health.callback({ options: { output: 1, health: 'live' } }), true)
	assert.equal(built.connection_lost.callback({ options: {} }), false)
})

/*
 * The three changes requested on the v1.0.1 review. Each of these fails
 * silently in the field -- an empty variable, a surface that stops updating, a
 * second command nobody typed -- so each is asserted rather than trusted.
 */

test('every deck and output the options offer has variables behind it', () => {
	// The options used to offer 1-16 while variables were built for four, so a
	// button on deck 7 drove a real playlist and every variable about it was
	// empty. Read the ranges out of the built options rather than restating
	// them, so this fails if they drift apart again.
	const maxOf = (defs, label) => {
		let seen = 0
		for (const def of Object.values(defs)) {
			for (const opt of def.options ?? []) {
				if (String(opt.label ?? '').startsWith(label) && Number.isFinite(opt.max)) {
					seen = Math.max(seen, opt.max)
				}
			}
		}
		return seen
	}
	const deckMax = Math.max(maxOf(actions, 'Deck'), maxOf(feedbacks, 'Deck'))
	const outputMax = Math.max(maxOf(actions, 'Output'), maxOf(feedbacks, 'Output'))
	assert.ok(deckMax > 0, 'no deck option found to check')
	for (let d = 1; d <= deckMax; d++) {
		assert.ok(variableIds.has(`deck${d}_cue`), `deck ${d} is selectable but has no variables`)
	}
	for (let o = 1; o <= outputMax; o++) {
		assert.ok(variableIds.has(`output${o}_enabled`), `output ${o} is selectable but has no variables`)
	}
})

test('a newline in an option cannot smuggle a second command', async () => {
	// The protocol is newline-delimited, and option values have their variables
	// resolved before the callback sees them. A value carrying a newline used to
	// become two commands in one send -- the second of which could be anything.
	const { default: DeckboyInstance } = await import('../main.js')
	const sends = []
	const instance = Object.create(DeckboyInstance.prototype)
	instance.socket = { isConnected: true, send: (s) => sends.push(s) }
	instance.log = () => {}

	instance.sendCommand('GOTO My Cue\nBLACKOUT')
	assert.equal(sends.length, 1, 'one press must be one send')
	assert.equal(sends[0].match(/\n/g).length, 1, 'exactly one newline, at the end')
	assert.ok(!/\nBLACKOUT/.test(sends[0]), 'the smuggled command must not survive')

	sends.length = 0
	instance.sendCommand('SELECT 3\r\nPANIC')
	assert.equal(sends.length, 1)
	assert.ok(!/PANIC\n/.test(sends[0].replace(/ PANIC/, '')), 'CRLF must not split either')
})

test('a STATUS that never answers does not stop polling', async () => {
	// statusPending was cleared in exactly one place, after a reply carrying a
	// DECKBOY line. A reply that never arrived left it true for the life of the
	// connection and the surface quietly stopped updating.
	const { default: DeckboyInstance } = await import('../main.js')
	const sends = []
	const instance = Object.create(DeckboyInstance.prototype)
	instance.socket = { isConnected: true, send: (s) => sends.push(s) }
	instance.log = () => {}
	instance.config = { pollInterval: 250 }
	instance.statusPending = false
	instance.statusSentAt = 0

	instance.requestStatus()
	assert.equal(sends.length, 1, 'the first poll goes out')
	instance.requestStatus()
	assert.equal(sends.length, 1, 'a second is held while one is outstanding')

	// Nothing ever answers. Wind the clock past the stall window.
	instance.statusSentAt = Date.now() - (instance.statusStallMs() + 50)
	instance.requestStatus()
	assert.equal(sends.length, 2, 'a stalled request is abandoned and polling resumes')
})

test('a reply with no DECKBOY line still ends the request', async () => {
	// The other half of the stall, and the likelier one: Deckboy DID answer, the
	// answer carried nothing parseable, and flushReport's empty path returned
	// without clearing the flag. Polling then stopped for the rest of the
	// session with no error, because nothing had failed.
	const { default: DeckboyInstance } = await import('../main.js')
	const instance = Object.create(DeckboyInstance.prototype)
	instance.log = () => {}
	instance.pendingReport = []
	instance.statusPending = true

	instance.flushReport()
	assert.equal(instance.statusPending, false, 'an empty reply must end the request')
	assert.equal(instance.pendingReport, undefined)
})
