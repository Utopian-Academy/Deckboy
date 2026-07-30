/*
 * Feedbacks — the half of a control surface that Generic TCP/UDP could never
 * do. Buttons colour themselves from Deckboy's polled STATUS, so the Stream
 * Deck shows deck state, output health and live-cue tally at a glance instead
 * of being a write-only remote.
 */

import { combineRgb } from '@companion-module/base'
import { isOn, remainingSeconds } from './protocol.js'

const RED = combineRgb(200, 0, 0)
const GREEN = combineRgb(0, 160, 60)
const AMBER = combineRgb(210, 130, 0)
const WHITE = combineRgb(255, 255, 255)
const BLACK = combineRgb(0, 0, 0)

export function buildFeedbacks(self) {
	// Deck 0 means "whichever deck has focus", matching the actions.
	const deckOption = {
		type: 'number',
		label: 'Deck (0 = focused deck)',
		id: 'deck',
		default: 0,
		min: 0,
		max: 16,
	}
	const deckFields = (options) => {
		const requested = Number(options?.deck ?? 0)
		const index = requested > 0 ? requested : Number(self.state.global?.focus ?? 1)
		return self.state.decks.get(index)
	}

	return {
		deck_status: {
			type: 'boolean',
			name: 'Deck transport state',
			description: 'Colour a button while a deck is playing, paused or stopped.',
			defaultStyle: { bgcolor: GREEN, color: WHITE },
			options: [
				deckOption,
				{
					type: 'dropdown',
					label: 'State',
					id: 'status',
					default: 'Playing',
					choices: [
						{ id: 'Playing', label: 'Playing' },
						{ id: 'Paused', label: 'Paused' },
						{ id: 'Stopped', label: 'Stopped' },
					],
				},
			],
			callback: ({ options }) => deckFields(options)?.status === options.status,
		},

		deck_has_live_cue: {
			type: 'boolean',
			name: 'Deck has a cue live',
			defaultStyle: { bgcolor: RED, color: WHITE },
			options: [deckOption],
			callback: ({ options }) => {
				const active = deckFields(options)?.active
				return active !== undefined && active !== '' && active !== '0'
			},
		},

		cue_is_live: {
			type: 'boolean',
			name: 'Specific cue is live (tally)',
			description: 'Turns a cue button red while that exact cue is the one on air.',
			defaultStyle: { bgcolor: RED, color: WHITE },
			options: [
				deckOption,
				{ type: 'textinput', label: 'Cue number', id: 'cue', default: '1', useVariables: true },
			],
			callback: async ({ options }) => {
				const cue = await self.parseVariablesInString(options.cue)
				return String(deckFields(options)?.active ?? '') === String(cue).trim()
			},
		},

		cue_is_selected: {
			type: 'boolean',
			name: 'Specific cue is selected (preview)',
			defaultStyle: { bgcolor: GREEN, color: WHITE },
			options: [
				deckOption,
				{ type: 'textinput', label: 'Cue number', id: 'cue', default: '1', useVariables: true },
			],
			callback: async ({ options }) => {
				const cue = await self.parseVariablesInString(options.cue)
				return String(deckFields(options)?.selected ?? '') === String(cue).trim()
			},
		},

		deck_remaining_below: {
			type: 'boolean',
			name: 'Deck time remaining below threshold',
			description: 'The "wrap it up" warning — colour a button when a cue is nearly out.',
			defaultStyle: { bgcolor: AMBER, color: BLACK },
			options: [
				deckOption,
				{ type: 'number', label: 'Seconds', id: 'seconds', default: 20, min: 1, max: 3600 },
			],
			callback: ({ options }) => {
				const fields = deckFields(options)
				if (fields?.status !== 'Playing') return false
				const remaining = remainingSeconds(fields)
				return remaining !== null && remaining <= Number(options.seconds)
			},
		},

		output_enabled: {
			type: 'boolean',
			name: 'Output armed',
			defaultStyle: { bgcolor: GREEN, color: WHITE },
			options: [{ type: 'number', label: 'Output (1-based)', id: 'output', default: 1, min: 1, max: 16 }],
			callback: ({ options }) => isOn(self.state.outputs.get(Number(options.output))?.enabled),
		},

		output_health: {
			type: 'boolean',
			name: 'Output health',
			description: 'Catch an output that has dropped out of fullscreen or lost its display.',
			defaultStyle: { bgcolor: RED, color: WHITE },
			options: [
				{ type: 'number', label: 'Output (1-based)', id: 'output', default: 1, min: 1, max: 16 },
				{
					type: 'dropdown',
					label: 'Health',
					id: 'health',
					default: 'error',
					choices: [
						{ id: 'live', label: 'Live' },
						{ id: 'armed', label: 'Armed' },
						{ id: 'recovering', label: 'Recovering' },
						{ id: 'error', label: 'Error' },
						{ id: 'off', label: 'Off' },
					],
				},
			],
			callback: ({ options }) =>
				String(self.state.outputs.get(Number(options.output))?.health ?? '').toLowerCase() ===
				options.health,
		},

		blackout_active: {
			type: 'boolean',
			name: 'Blackout active',
			defaultStyle: { bgcolor: RED, color: WHITE },
			options: [],
			callback: () => isOn(self.state.global?.blackout),
		},

		connection_lost: {
			type: 'boolean',
			name: 'Deckboy unreachable',
			description: 'So the surface shows the desk is deaf rather than silently doing nothing.',
			defaultStyle: { bgcolor: RED, color: WHITE },
			options: [],
			callback: () => !self.state.connected,
		},
	}
}
