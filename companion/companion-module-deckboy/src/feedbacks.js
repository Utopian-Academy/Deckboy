import { combineRgb } from '@companion-module/base'

/**
 * Feedback definitions for Deckboy Companion module.
 *
 * Feedbacks change button appearance based on Deckboy state,
 * polled via STATUS JSON.
 */

// Color constants
const GREEN = combineRgb(0, 180, 0)
const RED = combineRgb(200, 0, 0)
const AMBER = combineRgb(200, 160, 0)
const BLUE = combineRgb(0, 80, 200)
const WHITE = combineRgb(255, 255, 255)
const DIM = combineRgb(60, 60, 60)
const BLACK = combineRgb(0, 0, 0)

export function getFeedbacks(instance) {
	return {
		// ── Transport state ──────────────────────────────────────────
		playing: {
			type: 'boolean',
			name: 'Deck is Playing',
			description: 'True when the focused deck transport is playing',
			options: [
				{
					type: 'number',
					id: 'deck',
					label: 'Deck (0 = focused)',
					default: 0,
					min: 0,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: GREEN,
				color: BLACK,
			},
			callback: (feedback) => {
				return getDeckStatus(instance, feedback.options.deck) === 'playing'
			},
		},
		paused: {
			type: 'boolean',
			name: 'Deck is Paused',
			description: 'True when the deck transport is paused',
			options: [
				{
					type: 'number',
					id: 'deck',
					label: 'Deck (0 = focused)',
					default: 0,
					min: 0,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: AMBER,
				color: BLACK,
			},
			callback: (feedback) => {
				return getDeckStatus(instance, feedback.options.deck) === 'paused'
			},
		},
		stopped: {
			type: 'boolean',
			name: 'Deck is Stopped',
			description: 'True when the deck transport is stopped',
			options: [
				{
					type: 'number',
					id: 'deck',
					label: 'Deck (0 = focused)',
					default: 0,
					min: 0,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: DIM,
				color: WHITE,
			},
			callback: (feedback) => {
				const s = getDeckStatus(instance, feedback.options.deck)
				return s === 'stopped' || s === ''
			},
		},

		// ── Blackout ─────────────────────────────────────────────────
		blackoutActive: {
			type: 'boolean',
			name: 'Blackout Active',
			description: 'True when master blackout is engaged',
			options: [],
			defaultStyle: {
				bgcolor: RED,
				color: WHITE,
			},
			callback: () => {
				return instance.state?.blackout === true
			},
		},

		// ── Output health ────────────────────────────────────────────
		outputHealthy: {
			type: 'boolean',
			name: 'Output Healthy',
			description: 'True when the focused output health is OK',
			options: [
				{
					type: 'number',
					id: 'output',
					label: 'Output (0 = focused)',
					default: 0,
					min: 0,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: GREEN,
				color: BLACK,
			},
			callback: (feedback) => {
				const out = getOutput(instance, feedback.options.output)
				return out?.health === 'ok' || out?.health === 'active'
			},
		},
		outputUnhealthy: {
			type: 'boolean',
			name: 'Output Unhealthy',
			description: 'True when the focused output has a problem',
			options: [
				{
					type: 'number',
					id: 'output',
					label: 'Output (0 = focused)',
					default: 0,
					min: 0,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: RED,
				color: WHITE,
			},
			callback: (feedback) => {
				const out = getOutput(instance, feedback.options.output)
				if (!out) return false
				return out.health !== 'ok' && out.health !== 'active' && out.health !== ''
			},
		},

		// ── NDI ──────────────────────────────────────────────────────
		ndiEnabled: {
			type: 'boolean',
			name: 'NDI Enabled',
			description: 'True when NDI output is enabled on the focused output',
			options: [
				{
					type: 'number',
					id: 'output',
					label: 'Output (0 = focused)',
					default: 0,
					min: 0,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: BLUE,
				color: WHITE,
			},
			callback: (feedback) => {
				const out = getOutput(instance, feedback.options.output)
				return out?.ndiEnabled === true
			},
		},
		ndiHasReceivers: {
			type: 'boolean',
			name: 'NDI Has Receivers',
			description: 'True when at least one NDI receiver is connected',
			options: [
				{
					type: 'number',
					id: 'output',
					label: 'Output (0 = focused)',
					default: 0,
					min: 0,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: GREEN,
				color: BLACK,
			},
			callback: (feedback) => {
				const out = getOutput(instance, feedback.options.output)
				return (out?.ndiReceivers ?? 0) > 0
			},
		},

		// ── Streaming ────────────────────────────────────────────────
		streamEnabled: {
			type: 'boolean',
			name: 'Stream Enabled',
			description: 'True when streaming is enabled on the focused output',
			options: [
				{
					type: 'number',
					id: 'output',
					label: 'Output (0 = focused)',
					default: 0,
					min: 0,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: RED,
				color: WHITE,
			},
			callback: (feedback) => {
				const out = getOutput(instance, feedback.options.output)
				return out?.streamEnabled === true
			},
		},

		// ── Output enabled ───────────────────────────────────────────
		outputEnabled: {
			type: 'boolean',
			name: 'Output Enabled',
			description: 'True when the output is enabled',
			options: [
				{
					type: 'number',
					id: 'output',
					label: 'Output (0 = focused)',
					default: 0,
					min: 0,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: GREEN,
				color: BLACK,
			},
			callback: (feedback) => {
				const out = getOutput(instance, feedback.options.output)
				return out?.enabled === true
			},
		},

		// ── Test card ────────────────────────────────────────────────
		testCardActive: {
			type: 'boolean',
			name: 'Test Card Active',
			description: 'True when the output test card is showing',
			options: [
				{
					type: 'number',
					id: 'output',
					label: 'Output (0 = focused)',
					default: 0,
					min: 0,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: AMBER,
				color: BLACK,
			},
			callback: (feedback) => {
				const out = getOutput(instance, feedback.options.output)
				return out?.outputTestCard === true
			},
		},

		// ── Focused deck ─────────────────────────────────────────────
		deckFocused: {
			type: 'boolean',
			name: 'Deck is Focused',
			description: 'True when a specific deck is the focused deck',
			options: [
				{
					type: 'number',
					id: 'deck',
					label: 'Deck number',
					default: 1,
					min: 1,
					max: 16,
				},
			],
			defaultStyle: {
				bgcolor: WHITE,
				color: BLACK,
			},
			callback: (feedback) => {
				return (instance.state?.focusedDeck ?? 0) === feedback.options.deck
			},
		},
	}
}

// ── Helpers ──────────────────────────────────────────────────────────

function getDeckStatus(instance, deckNum) {
	const state = instance.state
	if (!state?.decks) return ''
	const idx = deckNum > 0 ? deckNum - 1 : (state.focusedDeck ?? 1) - 1
	return state.decks[idx]?.status || ''
}

function getOutput(instance, outputNum) {
	const state = instance.state
	if (!state?.outputs) return null
	const idx = outputNum > 0 ? outputNum - 1 : (state.focusedOutput ?? 1) - 1
	return state.outputs[idx] || null
}
