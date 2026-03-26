/**
 * Variable definitions and updater for Deckboy Companion module.
 *
 * These are populated from the STATUS JSON response.
 * Companion variables appear as $(deckboy:variable_name) on buttons.
 */

export const VARIABLES = [
	// Global
	{ variableId: 'app_version', name: 'App version' },
	{ variableId: 'deck_count', name: 'Number of decks' },
	{ variableId: 'output_count', name: 'Number of outputs' },
	{ variableId: 'focused_deck', name: 'Focused deck number' },
	{ variableId: 'focused_output', name: 'Focused output number' },
	{ variableId: 'panic_profile', name: 'Panic profile' },
	{ variableId: 'master_dimmer', name: 'Master dimmer (0-100)' },
	{ variableId: 'blackout', name: 'Blackout state (on/off)' },
	{ variableId: 'master_volume', name: 'Master volume (0-200)' },

	// Focused deck
	{ variableId: 'deck_name', name: 'Focused deck name' },
	{ variableId: 'deck_status', name: 'Focused deck transport state' },
	{ variableId: 'cue_name', name: 'Active cue name' },
	{ variableId: 'cue_number', name: 'Active cue number' },
	{ variableId: 'cue_id', name: 'Active cue ID' },
	{ variableId: 'selected_cue_name', name: 'Selected (armed) cue name' },
	{ variableId: 'selected_cue_number', name: 'Selected cue number' },
	{ variableId: 'selected_cue_id', name: 'Selected cue ID' },
	{ variableId: 'position', name: 'Playback position (MM:SS.s)' },
	{ variableId: 'duration', name: 'Cue duration (MM:SS.s)' },
	{ variableId: 'volume', name: 'Deck volume (0-100)' },
	{ variableId: 'timecode', name: 'Timecode (HH:MM:SS:FF)' },
	{ variableId: 'transition_style', name: 'Transition style' },
	{ variableId: 'transition_seconds', name: 'Transition duration' },

	// Per-deck (deck 1-4)
	...makeDeckVariables(1),
	...makeDeckVariables(2),
	...makeDeckVariables(3),
	...makeDeckVariables(4),

	// Focused output
	{ variableId: 'output_name', name: 'Focused output name' },
	{ variableId: 'output_enabled', name: 'Focused output enabled' },
	{ variableId: 'output_type', name: 'Focused output type' },
	{ variableId: 'output_health', name: 'Focused output health' },
	{ variableId: 'output_ndi', name: 'NDI enabled' },
	{ variableId: 'output_ndi_name', name: 'NDI source name' },
	{ variableId: 'output_ndi_receivers', name: 'NDI receiver count' },
	{ variableId: 'output_stream', name: 'Stream enabled' },
	{ variableId: 'output_fps', name: 'Output FPS' },
	{ variableId: 'stream_fps', name: 'Stream FPS' },
]

function makeDeckVariables(n) {
	return [
		{ variableId: `deck${n}_name`, name: `Deck ${n} name` },
		{ variableId: `deck${n}_status`, name: `Deck ${n} status` },
		{ variableId: `deck${n}_cue`, name: `Deck ${n} active cue` },
		{ variableId: `deck${n}_cue_number`, name: `Deck ${n} active cue number` },
		{ variableId: `deck${n}_position`, name: `Deck ${n} position` },
		{ variableId: `deck${n}_duration`, name: `Deck ${n} duration` },
		{ variableId: `deck${n}_volume`, name: `Deck ${n} volume` },
		{ variableId: `deck${n}_timecode`, name: `Deck ${n} timecode` },
	]
}

/**
 * Update variable values from a parsed STATUS JSON object.
 */
export function updateVariables(instance, state) {
	const vars = {}

	vars['app_version'] = state.app || ''
	vars['deck_count'] = state.deckCount ?? 0
	vars['output_count'] = state.outputCount ?? 0
	vars['focused_deck'] = state.focusedDeck ?? 0
	vars['focused_output'] = state.focusedOutput ?? 0
	vars['panic_profile'] = state.panicProfile || 'none'
	vars['master_dimmer'] = state.masterDimmer ?? 100
	vars['blackout'] = state.blackout ? 'on' : 'off'
	vars['master_volume'] = state.masterVolume ?? 100

	// Focused deck (1-indexed)
	const focusedIdx = (state.focusedDeck ?? 1) - 1
	const decks = state.decks || []
	const focusedDeck = decks[focusedIdx]

	if (focusedDeck) {
		vars['deck_name'] = focusedDeck.name || ''
		vars['deck_status'] = focusedDeck.status || 'stopped'
		vars['cue_name'] = focusedDeck.cue || ''
		vars['cue_number'] = focusedDeck.activeCueNumber || ''
		vars['cue_id'] = focusedDeck.activeCueId || ''
		vars['selected_cue_name'] = '' // Not in JSON (selection name not exposed separately)
		vars['selected_cue_number'] = focusedDeck.selectedCueNumber || ''
		vars['selected_cue_id'] = focusedDeck.selectedCueId || ''
		vars['position'] = focusedDeck.position || '00:00.0'
		vars['duration'] = focusedDeck.duration || '00:00.0'
		vars['volume'] = focusedDeck.volume ?? 100
		vars['timecode'] = focusedDeck.timecode || '00:00:00:00'
		vars['transition_style'] = focusedDeck.transitionStyle || ''
		vars['transition_seconds'] = focusedDeck.transitionSeconds ?? 0
	}

	// Per-deck variables (up to 4 decks)
	for (let i = 0; i < Math.min(decks.length, 4); i++) {
		const d = decks[i]
		const n = i + 1
		vars[`deck${n}_name`] = d.name || ''
		vars[`deck${n}_status`] = d.status || 'stopped'
		vars[`deck${n}_cue`] = d.cue || ''
		vars[`deck${n}_cue_number`] = d.activeCueNumber || ''
		vars[`deck${n}_position`] = d.position || '00:00.0'
		vars[`deck${n}_duration`] = d.duration || '00:00.0'
		vars[`deck${n}_volume`] = d.volume ?? 100
		vars[`deck${n}_timecode`] = d.timecode || '00:00:00:00'
	}

	// Focused output
	const outputs = state.outputs || []
	const focusedOutIdx = (state.focusedOutput ?? 1) - 1
	const focusedOutput = outputs[focusedOutIdx]

	if (focusedOutput) {
		vars['output_name'] = focusedOutput.name || ''
		vars['output_enabled'] = focusedOutput.enabled ? 'on' : 'off'
		vars['output_type'] = focusedOutput.type || ''
		vars['output_health'] = focusedOutput.health || ''
		vars['output_ndi'] = focusedOutput.ndiEnabled ? 'on' : 'off'
		vars['output_ndi_name'] = focusedOutput.ndiName || ''
		vars['output_ndi_receivers'] = focusedOutput.ndiReceivers ?? 0
		vars['output_stream'] = focusedOutput.streamEnabled ? 'on' : 'off'
		vars['output_fps'] = Math.round(focusedOutput.outputFps ?? 0)
		vars['stream_fps'] = Math.round(focusedOutput.streamFps ?? 0)
	}

	instance.setVariableValues(vars)
}
