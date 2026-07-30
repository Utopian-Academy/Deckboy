/*
 * Variables — Deckboy's STATUS reply reshaped into $(deckboy:...) tokens for
 * button text, so a Stream Deck key can read out the live cue name and count
 * itself down without any button-level scripting.
 *
 * Deck and output variables are declared for a fixed span rather than for
 * exactly the decks that exist right now: Companion resolves variable names at
 * button-edit time, and definitions that appear and vanish as a show is loaded
 * would leave buttons referencing tokens Companion says are unknown.
 */

import { remainingSeconds, secondsToClock } from './protocol.js'

export const MAX_DECKS = 4
export const MAX_OUTPUTS = 4

export function buildVariableDefinitions() {
	const definitions = [
		{ variableId: 'connected', name: 'Connected to Deckboy (yes/no)' },
		{ variableId: 'version', name: 'Deckboy version' },
		{ variableId: 'focused_deck', name: 'Focused deck number' },
		{ variableId: 'deck_count', name: 'Deck count' },
		{ variableId: 'output_count', name: 'Output count' },
		{ variableId: 'master_volume', name: 'Master volume (%)' },
		{ variableId: 'master_dimmer', name: 'Master dimmer (%)' },
		{ variableId: 'blackout', name: 'Blackout (on/off)' },
		{ variableId: 'panic_profile', name: 'Panic profile' },
		{ variableId: 'find_token', name: 'Find: search token' },
		{ variableId: 'find_matches', name: 'Find: match count' },
	]

	for (let d = 1; d <= MAX_DECKS; d++) {
		definitions.push(
			{ variableId: `deck${d}_name`, name: `Deck ${d}: name` },
			{ variableId: `deck${d}_status`, name: `Deck ${d}: transport status` },
			{ variableId: `deck${d}_cue`, name: `Deck ${d}: live cue name` },
			{ variableId: `deck${d}_cue_id`, name: `Deck ${d}: live cue id` },
			{ variableId: `deck${d}_selected`, name: `Deck ${d}: selected cue number` },
			{ variableId: `deck${d}_active`, name: `Deck ${d}: live cue number` },
			{ variableId: `deck${d}_position`, name: `Deck ${d}: position` },
			{ variableId: `deck${d}_duration`, name: `Deck ${d}: duration` },
			{ variableId: `deck${d}_remaining`, name: `Deck ${d}: time remaining` },
			{ variableId: `deck${d}_remaining_seconds`, name: `Deck ${d}: time remaining (seconds)` },
			{ variableId: `deck${d}_volume`, name: `Deck ${d}: fader (%)` },
			{ variableId: `deck${d}_raster`, name: `Deck ${d}: raster` },
			{ variableId: `deck${d}_audio_device`, name: `Deck ${d}: audio device` },
			{ variableId: `deck${d}_timecode`, name: `Deck ${d}: timecode` }
		)
	}

	for (let o = 1; o <= MAX_OUTPUTS; o++) {
		definitions.push(
			{ variableId: `output${o}_name`, name: `Output ${o}: name` },
			{ variableId: `output${o}_enabled`, name: `Output ${o}: enabled (on/off)` },
			{ variableId: `output${o}_health`, name: `Output ${o}: health` },
			{ variableId: `output${o}_type`, name: `Output ${o}: type` },
			{ variableId: `output${o}_display`, name: `Output ${o}: display number` },
			{ variableId: `output${o}_fps`, name: `Output ${o}: output fps` }
		)
	}

	return definitions
}

/** Map the parsed status into the flat variable object Companion wants. */
export function buildVariableValues(state) {
	const g = state.global ?? {}
	const values = {
		connected: state.connected ? 'yes' : 'no',
		version: g.version ?? '',
		focused_deck: g.focus ?? '',
		deck_count: g.decks ?? '',
		output_count: g.outputs ?? '',
		master_volume: g.master_vol ?? '',
		master_dimmer: g.master_dimmer ?? '',
		blackout: g.blackout ?? '',
		panic_profile: g.panic_profile ?? '',
		find_token: g.find ?? '',
		find_matches: g.find_matches ?? '',
	}

	for (let d = 1; d <= MAX_DECKS; d++) {
		const f = state.decks.get(d) ?? {}
		const remaining = remainingSeconds(f)
		values[`deck${d}_name`] = f.name ?? ''
		values[`deck${d}_status`] = f.status ?? ''
		values[`deck${d}_cue`] = f.cue ?? ''
		values[`deck${d}_cue_id`] = f.cue_id ?? ''
		values[`deck${d}_selected`] = f.selected ?? ''
		values[`deck${d}_active`] = f.active ?? ''
		values[`deck${d}_position`] = f.pos ?? ''
		values[`deck${d}_duration`] = f.dur ?? ''
		values[`deck${d}_remaining`] = secondsToClock(remaining)
		values[`deck${d}_remaining_seconds`] = remaining === null ? '' : Math.round(remaining)
		values[`deck${d}_volume`] = f.vol ?? ''
		values[`deck${d}_raster`] = f.raster ?? ''
		values[`deck${d}_audio_device`] = f.audio ?? ''
		values[`deck${d}_timecode`] = f.tc ?? ''
	}

	for (let o = 1; o <= MAX_OUTPUTS; o++) {
		const f = state.outputs.get(o) ?? {}
		values[`output${o}_name`] = f.name ?? ''
		values[`output${o}_enabled`] = f.enabled ?? ''
		values[`output${o}_health`] = f.health ?? ''
		values[`output${o}_type`] = f.type ?? ''
		values[`output${o}_display`] = f.display ?? ''
		values[`output${o}_fps`] = f.output_fps ?? ''
	}

	return values
}
