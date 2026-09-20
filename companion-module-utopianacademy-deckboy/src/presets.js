/*
 * Presets — drag-and-drop buttons that already carry their feedback wiring.
 * The point is that a new user gets a working, self-colouring transport page
 * without having to know which feedback goes with which action.
 *
 * base 2.x split presets in two. A preset no longer carries a `category`
 * string; the grouping is a SEPARATE structure passed alongside the
 * definitions, and the preset type is 'simple' rather than 'button'. The
 * sections below are the old categories, in the order an operator meets them.
 */

import { combineRgb } from '@companion-module/base'

const WHITE = combineRgb(255, 255, 255)
const BLACK = combineRgb(0, 0, 0)
const DARK = combineRgb(20, 20, 20)
const RED = combineRgb(200, 0, 0)
const GREEN = combineRgb(0, 160, 60)
const AMBER = combineRgb(210, 130, 0)

// Both exports are thin wrappers over this: the presets and the sections are
// built together from one pass, so neither can go stale against the other, and
// nothing is kept in module state between calls.
function buildAll() {
	const presets = {}
	const sections = new Map()

	// `category` is still an argument: it is what buildPresetSections() groups
	// on, so the two cannot drift apart the way a separate hand-written list
	// would. It is recorded here and stripped from the definition itself.
	const button = (id, category, name, text, actionId, options = {}, feedbacks = [], size = '18') => {
		sections.set(category, [...(sections.get(category) ?? []), id])
		presets[id] = {
			type: 'simple',
			name,
			style: { text, size, color: WHITE, bgcolor: DARK },
			steps: [{ down: [{ actionId, options }], up: [] }],
			feedbacks,
		}
	}

	// ── Transport ──
	button('take', 'Transport', 'Take', 'TAKE', 'take', { deck: 0 }, [
		{ feedbackId: 'deck_has_live_cue', options: { deck: 0 }, style: { bgcolor: RED, color: WHITE } },
	])
	button('go', 'Transport', 'GO', 'GO', 'go', { deck: 0 }, [
		{ feedbackId: 'deck_status', options: { deck: 0, status: 'Playing' }, style: { bgcolor: GREEN, color: WHITE } },
	])
	button('pause', 'Transport', 'Pause', 'PAUSE', 'pause', { deck: 0 }, [
		{ feedbackId: 'deck_status', options: { deck: 0, status: 'Paused' }, style: { bgcolor: AMBER, color: BLACK } },
	])
	button('stop', 'Transport', 'Stop', 'STOP', 'stop', { deck: 0 })
	button('rerack', 'Transport', 'Rerack', 'RERACK', 'rerack', { deck: 0 })
	button('skip_next', 'Transport', 'Skip forward', 'SKIP\\n>|', 'skip_next', { deck: 0 })
	button('skip_prev', 'Transport', 'Skip back', 'SKIP\\n|<', 'skip_prev', { deck: 0 })
	button('select_next', 'Transport', 'Select next', 'SEL\\n▼', 'select_next', { deck: 0 })
	button('select_prev', 'Transport', 'Select previous', 'SEL\\n▲', 'select_prev', { deck: 0 })

	// ── Status readouts ──
	sections.set('Status', [...(sections.get('Status') ?? []), 'now_playing'])
	presets['now_playing'] = {
		type: 'simple',
		name: 'Now playing (cue name + remaining)',
		style: {
			text: '$(deckboy:deck1_cue)\\n$(deckboy:deck1_remaining)',
			size: '14',
			color: WHITE,
			bgcolor: DARK,
		},
		steps: [{ down: [], up: [] }],
		feedbacks: [
			{ feedbackId: 'deck_status', options: { deck: 1, status: 'Playing' }, style: { bgcolor: GREEN, color: WHITE } },
			{
				feedbackId: 'deck_remaining_below',
				options: { deck: 1, seconds: 20 },
				style: { bgcolor: AMBER, color: BLACK },
			},
		],
	}
	sections.set('Status', [...(sections.get('Status') ?? []), 'connection'])
	presets['connection'] = {
		type: 'simple',
		name: 'Connection watchdog',
		style: { text: 'DECKBOY\\n$(deckboy:connected)', size: '14', color: WHITE, bgcolor: DARK },
		steps: [{ down: [], up: [] }],
		feedbacks: [{ feedbackId: 'connection_lost', options: {}, style: { bgcolor: RED, color: WHITE } }],
	}

	// ── Output ──
	button('output_toggle', 'Output', 'Output on/off', 'OUT\\nON/OFF', 'output_enable', { state: 'toggle' }, [
		{ feedbackId: 'output_enabled', options: { output: 1 }, style: { bgcolor: GREEN, color: WHITE } },
		{ feedbackId: 'output_health', options: { output: 1, health: 'error' }, style: { bgcolor: RED, color: WHITE } },
	])
	button('fullscreen', 'Output', 'Toggle fullscreen', 'FULL\\nSCREEN', 'output_fullscreen', {})
	button('clear', 'Output', 'Clear to black', 'CLEAR', 'clear', {})
	button('blackout', 'Output', 'Blackout', 'BLACK\\nOUT', 'blackout', { state: 'toggle' }, [
		{ feedbackId: 'blackout_active', options: {}, style: { bgcolor: RED, color: WHITE } },
	])
	sections.set('Output', [...(sections.get('Output') ?? []), 'panic'])
	presets['panic'] = {
		type: 'simple',
		name: 'PANIC',
		style: { text: 'PANIC', size: '18', color: WHITE, bgcolor: RED },
		steps: [{ down: [{ actionId: 'panic', options: {} }], up: [] }],
		feedbacks: [],
	}

	// ── Cue tally: one preset the operator duplicates per cue ──
	sections.set('Cues', [...(sections.get('Cues') ?? []), 'cue_tally'])
	presets['cue_tally'] = {
		type: 'simple',
		name: 'Cue button with tally (edit the cue number)',
		style: { text: 'CUE 1', size: '18', color: WHITE, bgcolor: DARK },
		steps: [{ down: [{ actionId: 'take_cue', options: { deck: 0, cue: '1' } }], up: [] }],
		feedbacks: [
			{ feedbackId: 'cue_is_selected', options: { deck: 0, cue: '1' }, style: { bgcolor: GREEN, color: WHITE } },
			{ feedbackId: 'cue_is_live', options: { deck: 0, cue: '1' }, style: { bgcolor: RED, color: WHITE } },
		],
	}

	const structure = [...sections.entries()].map(([name, presetIds]) => ({
		id: name.toLowerCase(),
		name,
		definitions: presetIds,
	}))
	return { presets, structure }
}

export function buildPresets() {
	return buildAll().presets
}

/**
 * The section structure base 2.x wants as setPresetDefinitions' first argument.
 * Derived from the same `category` labels the presets declare, so a preset can
 * never end up defined but unreachable in the UI.
 */
export function buildPresetSections() {
	return buildAll().structure
}
