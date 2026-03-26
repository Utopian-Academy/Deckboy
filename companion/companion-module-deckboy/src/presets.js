import { combineRgb } from '@companion-module/base'

/**
 * Preset definitions for the Deckboy Companion module.
 *
 * Presets are pre-built button configurations that users can drag onto
 * their Stream Deck surfaces. Each preset bundles an action, optional
 * feedbacks, and button styling.
 */

const GREEN = combineRgb(0, 180, 0)
const RED = combineRgb(200, 0, 0)
const AMBER = combineRgb(200, 160, 0)
const BLUE = combineRgb(0, 80, 200)
const WHITE = combineRgb(255, 255, 255)
const DIM = combineRgb(60, 60, 60)
const BLACK = combineRgb(0, 0, 0)
const DARK_GREEN = combineRgb(0, 60, 0)
const DARK_RED = combineRgb(80, 0, 0)
const DARK_BLUE = combineRgb(0, 30, 100)

export function getPresets() {
	const presets = {}

	// ── Transport ────────────────────────────────────────────────

	presets['play'] = {
		type: 'button',
		category: 'Transport',
		name: 'Play',
		style: {
			text: 'PLAY',
			size: '18',
			color: WHITE,
			bgcolor: DARK_GREEN,
		},
		steps: [{ down: [{ actionId: 'play' }], up: [] }],
		feedbacks: [
			{
				feedbackId: 'playing',
				options: { deck: 0 },
				style: { bgcolor: GREEN, color: BLACK },
			},
		],
	}

	presets['pause'] = {
		type: 'button',
		category: 'Transport',
		name: 'Pause',
		style: {
			text: 'PAUSE',
			size: '18',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'pause' }], up: [] }],
		feedbacks: [
			{
				feedbackId: 'paused',
				options: { deck: 0 },
				style: { bgcolor: AMBER, color: BLACK },
			},
		],
	}

	presets['stop'] = {
		type: 'button',
		category: 'Transport',
		name: 'Stop',
		style: {
			text: 'STOP',
			size: '18',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'stop' }], up: [] }],
		feedbacks: [
			{
				feedbackId: 'stopped',
				options: { deck: 0 },
				style: { bgcolor: DIM, color: WHITE },
			},
		],
	}

	presets['toggle'] = {
		type: 'button',
		category: 'Transport',
		name: 'Toggle Play/Pause',
		style: {
			text: 'GO',
			size: '24',
			color: WHITE,
			bgcolor: DARK_GREEN,
		},
		steps: [{ down: [{ actionId: 'toggle' }], up: [] }],
		feedbacks: [
			{
				feedbackId: 'playing',
				options: { deck: 0 },
				style: { bgcolor: GREEN, color: BLACK },
			},
			{
				feedbackId: 'paused',
				options: { deck: 0 },
				style: { bgcolor: AMBER, color: BLACK },
			},
		],
	}

	presets['rerack'] = {
		type: 'button',
		category: 'Transport',
		name: 'Rerack',
		style: {
			text: 'RACK',
			size: '18',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'rerack', options: { target: '' } }], up: [] }],
		feedbacks: [],
	}

	presets['allStop'] = {
		type: 'button',
		category: 'Transport',
		name: 'All Stop',
		style: {
			text: 'ALL\\nSTOP',
			size: '14',
			color: WHITE,
			bgcolor: DARK_RED,
		},
		steps: [{ down: [{ actionId: 'allStop' }], up: [] }],
		feedbacks: [],
	}

	// ── Cue navigation ───────────────────────────────────────────

	presets['next'] = {
		type: 'button',
		category: 'Cue Navigation',
		name: 'Next Cue',
		style: {
			text: 'NEXT',
			size: '18',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'next' }], up: [] }],
		feedbacks: [],
	}

	presets['prev'] = {
		type: 'button',
		category: 'Cue Navigation',
		name: 'Previous Cue',
		style: {
			text: 'PREV',
			size: '18',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'prev' }], up: [] }],
		feedbacks: [],
	}

	// Goto cue presets (1-12)
	for (let i = 1; i <= 12; i++) {
		presets[`goto_${i}`] = {
			type: 'button',
			category: 'Cue Navigation',
			name: `Go To Cue ${i}`,
			style: {
				text: `CUE ${i}`,
				size: '14',
				color: WHITE,
				bgcolor: DIM,
			},
			steps: [{ down: [{ actionId: 'goto', options: { cue: String(i) } }], up: [] }],
			feedbacks: [],
		}
	}

	// ── Deck selection ───────────────────────────────────────────

	for (let i = 1; i <= 4; i++) {
		presets[`deck_${i}`] = {
			type: 'button',
			category: 'Deck Selection',
			name: `Focus Deck ${i}`,
			style: {
				text: `DECK ${i}`,
				size: '18',
				color: WHITE,
				bgcolor: DIM,
			},
			steps: [{ down: [{ actionId: 'deckSelect', options: { deck: i } }], up: [] }],
			feedbacks: [
				{
					feedbackId: 'deckFocused',
					options: { deck: i },
					style: { bgcolor: WHITE, color: BLACK },
				},
			],
		}
	}

	// ── Blackout ─────────────────────────────────────────────────

	presets['blackout'] = {
		type: 'button',
		category: 'Master',
		name: 'Blackout Toggle',
		style: {
			text: 'BLK',
			size: '24',
			color: WHITE,
			bgcolor: DARK_RED,
		},
		steps: [{ down: [{ actionId: 'blackout', options: { mode: 'TOGGLE' } }], up: [] }],
		feedbacks: [
			{
				feedbackId: 'blackoutActive',
				options: {},
				style: { bgcolor: RED, color: WHITE },
			},
		],
	}

	presets['panic'] = {
		type: 'button',
		category: 'Master',
		name: 'Panic',
		style: {
			text: 'PANIC',
			size: '18',
			color: WHITE,
			bgcolor: RED,
		},
		steps: [{ down: [{ actionId: 'panic', options: { profile: '' } }], up: [] }],
		feedbacks: [],
	}

	// ── NDI & Stream ─────────────────────────────────────────────

	presets['ndi'] = {
		type: 'button',
		category: 'Output',
		name: 'NDI Toggle',
		style: {
			text: 'NDI',
			size: '18',
			color: WHITE,
			bgcolor: DARK_BLUE,
		},
		steps: [{ down: [{ actionId: 'ndiToggle', options: { mode: 'TOGGLE' } }], up: [] }],
		feedbacks: [
			{
				feedbackId: 'ndiEnabled',
				options: { output: 0 },
				style: { bgcolor: BLUE, color: WHITE },
			},
		],
	}

	presets['stream'] = {
		type: 'button',
		category: 'Output',
		name: 'Stream Toggle',
		style: {
			text: 'STREAM',
			size: '14',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'streamToggle', options: { mode: 'TOGGLE' } }], up: [] }],
		feedbacks: [
			{
				feedbackId: 'streamEnabled',
				options: { output: 0 },
				style: { bgcolor: RED, color: WHITE },
			},
		],
	}

	presets['outputToggle'] = {
		type: 'button',
		category: 'Output',
		name: 'Output Toggle',
		style: {
			text: 'OUTPUT',
			size: '14',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'outputToggle', options: { mode: 'TOGGLE' } }], up: [] }],
		feedbacks: [
			{
				feedbackId: 'outputEnabled',
				options: { output: 0 },
				style: { bgcolor: GREEN, color: BLACK },
			},
		],
	}

	presets['testCard'] = {
		type: 'button',
		category: 'Output',
		name: 'Test Card',
		style: {
			text: 'TEST',
			size: '18',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'outputTestCard', options: { mode: 'TOGGLE' } }], up: [] }],
		feedbacks: [
			{
				feedbackId: 'testCardActive',
				options: { output: 0 },
				style: { bgcolor: AMBER, color: BLACK },
			},
		],
	}

	// ── Status display ───────────────────────────────────────────

	presets['cueName'] = {
		type: 'button',
		category: 'Status',
		name: 'Cue Name',
		style: {
			text: '$(deckboy:cue_name)',
			size: '14',
			color: WHITE,
			bgcolor: BLACK,
		},
		steps: [],
		feedbacks: [],
	}

	presets['position'] = {
		type: 'button',
		category: 'Status',
		name: 'Position',
		style: {
			text: '$(deckboy:position)',
			size: '18',
			color: GREEN,
			bgcolor: BLACK,
		},
		steps: [],
		feedbacks: [],
	}

	presets['timecode'] = {
		type: 'button',
		category: 'Status',
		name: 'Timecode',
		style: {
			text: '$(deckboy:timecode)',
			size: '14',
			color: AMBER,
			bgcolor: BLACK,
		},
		steps: [],
		feedbacks: [],
	}

	presets['deckStatus'] = {
		type: 'button',
		category: 'Status',
		name: 'Deck Status',
		style: {
			text: '$(deckboy:deck_status)',
			size: '18',
			color: WHITE,
			bgcolor: BLACK,
		},
		steps: [],
		feedbacks: [
			{
				feedbackId: 'playing',
				options: { deck: 0 },
				style: { bgcolor: GREEN, color: BLACK },
			},
			{
				feedbackId: 'paused',
				options: { deck: 0 },
				style: { bgcolor: AMBER, color: BLACK },
			},
		],
	}

	// ── Transitions ──────────────────────────────────────────────

	presets['transCut'] = {
		type: 'button',
		category: 'Transitions',
		name: 'Cut',
		style: {
			text: 'CUT',
			size: '18',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'transitionStyle', options: { style: 'cut' } }], up: [] }],
		feedbacks: [],
	}

	presets['transCrossfade'] = {
		type: 'button',
		category: 'Transitions',
		name: 'Crossfade',
		style: {
			text: 'XFADE',
			size: '14',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'transitionStyle', options: { style: 'crossfade' } }], up: [] }],
		feedbacks: [],
	}

	presets['transDip'] = {
		type: 'button',
		category: 'Transitions',
		name: 'Dip to Black',
		style: {
			text: 'DIP',
			size: '18',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'transitionStyle', options: { style: 'dip' } }], up: [] }],
		feedbacks: [],
	}

	// ── Fullscreen ───────────────────────────────────────────────

	presets['fullscreen'] = {
		type: 'button',
		category: 'Master',
		name: 'Fullscreen',
		style: {
			text: 'FS',
			size: '24',
			color: WHITE,
			bgcolor: DIM,
		},
		steps: [{ down: [{ actionId: 'fullscreen' }], up: [] }],
		feedbacks: [],
	}

	return presets
}
