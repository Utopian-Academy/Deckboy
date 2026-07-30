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
import { buildPresets } from '../src/presets.js'
import { buildVariableDefinitions } from '../src/variables.js'

function stubInstance() {
	const sent = []
	return {
		sent,
		state: { connected: true, global: { focus: '1' }, decks: new Map(), outputs: new Map() },
		sendCommand: (cmd) => sent.push(cmd),
		parseVariablesInString: async (s) => s,
		log: () => {},
	}
}

const actions = buildActions(stubInstance())
const feedbacks = buildFeedbacks(stubInstance())
const presets = buildPresets()
const variableIds = new Set(buildVariableDefinitions().map((v) => v.variableId))

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
