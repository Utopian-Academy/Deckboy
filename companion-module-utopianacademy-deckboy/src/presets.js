/*
 * Presets — drag-and-drop buttons that already carry their feedback wiring.
 * The point is that a new user gets a working, self-colouring transport page
 * without having to know which feedback goes with which action.
 */

import { combineRgb } from '@companion-module/base'

const WHITE = combineRgb(255, 255, 255)
const BLACK = combineRgb(0, 0, 0)
const DARK = combineRgb(20, 20, 20)
const RED = combineRgb(200, 0, 0)
const GREEN = combineRgb(0, 160, 60)
const AMBER = combineRgb(210, 130, 0)

export function buildPresets() {
	const presets = {}

	const button = (id, category, name, text, actionId, options = {}, feedbacks = [], size = '18') => {
		presets[id] = {
			type: 'button',
			category,
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
	presets['now_playing'] = {
		type: 'button',
		category: 'Status',
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
	presets['connection'] = {
		type: 'button',
		category: 'Status',
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
	presets['panic'] = {
		type: 'button',
		category: 'Output',
		name: 'PANIC',
		style: { text: 'PANIC', size: '18', color: WHITE, bgcolor: RED },
		steps: [{ down: [{ actionId: 'panic', options: {} }], up: [] }],
		feedbacks: [],
	}

	// ── Cue tally: one preset the operator duplicates per cue ──
	presets['cue_tally'] = {
		type: 'button',
		category: 'Cues',
		name: 'Cue button with tally (edit the cue number)',
		style: { text: 'CUE 1', size: '18', color: WHITE, bgcolor: DARK },
		steps: [{ down: [{ actionId: 'take_cue', options: { deck: 0, cue: '1' } }], up: [] }],
		feedbacks: [
			{ feedbackId: 'cue_is_selected', options: { deck: 0, cue: '1' }, style: { bgcolor: GREEN, color: WHITE } },
			{ feedbackId: 'cue_is_live', options: { deck: 0, cue: '1' }, style: { bgcolor: RED, color: WHITE } },
		],
	}

	return presets
}
