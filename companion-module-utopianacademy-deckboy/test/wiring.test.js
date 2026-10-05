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
					`preset "${presetId}" references unknown action "${action.actionId}"`,
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
				`preset "${presetId}" references unknown feedback "${feedback.feedbackId}"`,
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
			`variable "${variableId}" still carries the 1.x variableId field`,
		)
	}
})

test('presets use the 2.x simple type and carry no category', () => {
	for (const [presetId, preset] of Object.entries(presets)) {
		assert.equal(preset.type, 'simple', `preset "${presetId}" must be type simple`)
		assert.ok(
			!Object.hasOwn(preset, 'category'),
			`preset "${presetId}" still carries a 1.x category; sections replace it`,
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
			assert.ok(variableIds.has(match[1]), `preset "${presetId}" uses undeclared variable "${match[1]}"`)
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
	assert.deepEqual(self.sent, ['DECK 2 TAKE', 'DECK 1'], 'a named deck acts there, then focus goes back')

	self.sent.length = 0
	await built.take.callback({ options: { deck: 1 } })
	assert.deepEqual(self.sent, ['DECK 1 TAKE'], 'no restore needed when the deck already has focus')

	self.sent.length = 0
	await built.take_cue.callback({ options: { deck: 0, cue: '7' } })
	assert.deepEqual(self.sent, ['TAKE 7'])

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
	assert.deepEqual(self.sent, ['VJ TAP', 'VJ BPM 124', 'VJ QUANTISE ON', 'VJ DECKS 1 3'])
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
	await built.audiofx_bypass.callback({ options: { index: 3, state: 'on' } })
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
	const { default: DeckboyInstance } = await import('../src/main.js')
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
	const { default: DeckboyInstance } = await import('../src/main.js')
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

test('an acknowledgement does not end an outstanding STATUS', async () => {
	// The OK for a button press used to clear statusPending, so a second STATUS
	// went out while the first was still on its way. Only a real report -- or
	// the stall timeout -- ends the request.
	const instance = await bareInstance()
	instance.statusPending = true
	instance.handleData(Buffer.from('OK TAKE\n'))
	assert.equal(instance.statusPending, true)
})

async function bareInstance() {
	const { default: DeckboyInstance } = await import('../src/main.js')
	const instance = Object.create(DeckboyInstance.prototype)
	instance.log = () => {}
	instance.receiveBuffer = ''
	instance.state = { connected: true, global: {}, decks: new Map(), outputs: new Map() }
	instance.published = []
	instance.setVariableValues = (values) => instance.published.push(values)
	instance.checkAllFeedbacks = () => {}
	return instance
}

const REPORT =
	'DECKBOY_0.01 focus=1 decks=2 outputs=1 master_vol=100\n' +
	'DECK 1 name="Deck 1" status=Playing active=3 cue="Opener" pos=00:01.0 dur=00:10.0\n' +
	'DECK 2 name="Deck 2" status=Stopped active=0 cue="" pos=00:00.0 dur=00:00.0\n' +
	'OUTPUT 1 name="Output 1" enabled=on health=live\n'

test('a STATUS reply split across TCP chunks is published whole', async () => {
	// Flushing at the end of every chunk published the first half of a split
	// reply and dropped the rest. The header says how many lines follow.
	const instance = await bareInstance()
	const bytes = Buffer.from(REPORT)
	for (let cut = 1; cut < bytes.length; cut += 7) {
		instance.state.decks = new Map()
		instance.state.outputs = new Map()
		instance.statusPending = true
		instance.handleData(bytes.subarray(0, cut))
		instance.handleData(bytes.subarray(cut))
		assert.equal(instance.state.decks.size, 2, `cut at ${cut}: both decks`)
		assert.equal(instance.state.outputs.size, 1, `cut at ${cut}: the output`)
		assert.equal(instance.statusPending, false, `cut at ${cut}: request ended`)
	}
})

test('a non-ASCII cue name split mid-character survives', async () => {
	const instance = await bareInstance()
	const bytes = Buffer.from(REPORT.replace('Opener', 'Ouverture é'))
	const at = bytes.indexOf(Buffer.from('é')) + 1
	instance.handleData(bytes.subarray(0, at))
	instance.handleData(bytes.subarray(at))
	assert.equal(instance.state.decks.get(1).cue, 'Ouverture é')
})

test('a peer that never sends a newline cannot grow the buffer without limit', async () => {
	const instance = await bareInstance()
	instance.handleData(Buffer.alloc(70 * 1024, 0x41))
	assert.equal(instance.receiveBuffer, '')
})

test('a link that stops answering is marked failed and reconnected', async () => {
	// A pulled cable raises no error for minutes; the buttons kept showing the
	// last tally as if all was well.
	const instance = await bareInstance()
	instance.config = { pollInterval: 250 }
	instance.socket = { isConnected: true, send: () => {} }
	let reconnects = 0
	const statuses = []
	instance.openConnection = () => reconnects++
	instance.updateStatus = (status) => statuses.push(status)
	instance.statusPending = false
	instance.requestStatus()
	for (let i = 0; i < 3; i++) {
		instance.statusSentAt = Date.now() - (instance.statusStallMs() + 50)
		instance.requestStatus()
	}
	assert.equal(reconnects, 1)
	assert.equal(statuses.at(-1), 'connection_failure')
})

test('polling sends nothing while disconnected', async () => {
	const instance = await bareInstance()
	const sends = []
	instance.config = { pollInterval: 250 }
	const logs = []
	instance.log = (level, text) => logs.push(`${level} ${text}`)
	instance.socket = { isConnected: false, send: (s) => sends.push(s) }
	for (let i = 0; i < 5; i++) instance.requestStatus()
	assert.deepEqual(sends, [])
	assert.deepEqual(logs, [], 'a closed Deckboy must not fill the log, one line per poll')
})

test('only changed variables are republished', async () => {
	const instance = await bareInstance()
	instance.handleData(Buffer.from(REPORT))
	const first = Object.keys(instance.published.at(-1)).length
	instance.handleData(Buffer.from(REPORT.replace('pos=00:01.0', 'pos=00:02.0')))
	const second = instance.published.at(-1)
	assert.ok(first > 10)
	assert.ok(Object.keys(second).length < 5, 'a moving playhead changes a handful of values')
	const count = instance.published.length
	instance.handleData(Buffer.from(REPORT.replace('pos=00:01.0', 'pos=00:02.0')))
	assert.equal(instance.published.length, count, 'an identical report publishes nothing')
})

test('a cue number that is empty or not a position sends nothing', async () => {
	// SELECT with nothing after it was refused, and the TAKE that followed put
	// whatever was selected on air.
	const self = stubInstance()
	const built = buildActions(self)
	for (const cue of ['', '  ', 'abc', '0', '-2', '1.5', undefined]) {
		await built.take_cue.callback({ options: { deck: 0, cue } })
		await built.select_cue.callback({ options: { deck: 0, cue } })
	}
	assert.deepEqual(self.sent, [])
})

test('number options that are not numbers send nothing', async () => {
	const self = stubInstance()
	const built = buildActions(self)
	await built.master_volume.callback({ options: { value: undefined } })
	await built.master_dimmer.callback({ options: { value: 'abc' } })
	await built.audiofx_remove.callback({ options: { index: '' } })
	await built.take.callback({ options: { deck: 'x' } })
	assert.deepEqual(self.sent, [])
	await built.audiofx_remove.callback({ options: { index: 2.6 } })
	assert.deepEqual(self.sent, ['AUDIOFX 3 OFF'], 'an index is sent as a whole number')
})

test('an option saved before it existed does not throw', async () => {
	const self = stubInstance()
	const built = buildActions(self)
	await built.vj_mode.callback({ options: {} })
	await built.fx_copy_paste.callback({ options: {} })
	assert.deepEqual(self.sent, ['VJ TOGGLE', 'FX COPY'])
})

test('every index-style number option is integer-only', () => {
	for (const defs of [actions, feedbacks]) {
		for (const [id, def] of Object.entries(defs)) {
			for (const opt of def.options ?? []) {
				if (opt.type !== 'number') continue
				if (['deck', 'output', 'display', 'index', 'a', 'b'].includes(opt.id)) {
					assert.equal(opt.asInteger, true, `${id}.${opt.id} needs asInteger`)
				}
			}
		}
	}
})

test('fixed-choice dropdown ids are lower case', () => {
	for (const [id, def] of Object.entries(actions)) {
		for (const opt of def.options ?? []) {
			if (opt.type !== 'dropdown' || opt.id === 'slot') continue
			for (const c of opt.choices) assert.equal(c.id, String(c.id).toLowerCase(), `${id}.${opt.id} ${c.id}`)
		}
	}
})
