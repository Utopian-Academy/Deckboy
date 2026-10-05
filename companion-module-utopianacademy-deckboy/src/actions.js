/*
 * Actions — every button press becomes one plain-text Deckboy command.
 *
 * Deckboy's remote vocabulary is much larger than this (see MANUAL.md §22);
 * what is exposed here is the set an operator drives from a Stream Deck during
 * a show. Anything else is reachable through the "Custom command" action at the
 * bottom, so the module never becomes the reason something isn't possible.
 */

import { MAX_DECKS, MAX_OUTPUTS } from './variables.js'

export function buildActions(self) {
	const send = (cmd) => self.sendCommand(cmd)

	const deckOption = {
		type: 'number',
		label: 'Deck (0 = focused deck)',
		id: 'deck',
		default: 0,
		min: 0,
		max: MAX_DECKS,
		asInteger: true,
	}

	// A number option can be switched to an expression, and an expression can
	// come out empty, NaN or 2.5. Any of those used to go out as literal text --
	// MASTERVOL undefined, AUDIOFX 2.5 OFF. Returns the number, or null after
	// saying why the press did nothing.
	const number = (value, what, { integer = false } = {}) => {
		const n = Number(value)
		if (value === '' || value === null || value === undefined || !Number.isFinite(n)) {
			self.log('warn', `${what} is not a number (${JSON.stringify(value)}) — nothing sent`)
			return null
		}
		return integer ? Math.round(n) : n
	}

	// Fixed choices are stored lower case and sent the way Deckboy spells them.
	// String(...) because an action saved before its option existed hands the
	// callback undefined, and undefined.toUpperCase() throws.
	const choice = (value, fallback) => String(value ?? fallback).toUpperCase()

	// A button that names its deck acts on that deck and leaves focus where it
	// was. `DECK n <command>` runs the command on deck n -- but DECK also moves
	// Deckboy's focus there, which then silently retargeted every "focused deck"
	// button on the surface. So focus is put back afterwards.
	//
	// base 2.x resolves variables and expressions in option values BEFORE the
	// callback runs, and removed parseVariablesInString from both the class and
	// the callback context. Option values are read directly from here on.
	const withDeck = async (options, ...commands) => {
		const deck = number(options.deck ?? 0, 'Deck', { integer: true })
		if (deck === null) return
		if (deck <= 0) {
			for (const command of commands) send(command)
			return
		}
		for (const command of commands) send(`DECK ${deck} ${command}`)
		const focus = Number.parseInt(self.state.global?.focus, 10)
		if (Number.isFinite(focus) && focus > 0 && focus !== deck) send(`DECK ${focus}`)
	}

	// Cue numbers are 1-based positions. An empty one -- a cleared field, a
	// variable with nothing in it -- used to send a bare SELECT, which Deckboy
	// refused, and then TAKE anyway: whatever was selected went on air.
	const cueNumber = (value) => {
		const text = String(value ?? '').trim()
		if (!/^[1-9]\d*$/.test(text)) {
			self.log('warn', `Cue number ${JSON.stringify(text)} is not a cue position — nothing sent`)
			return null
		}
		return text
	}

	const simple = (name, command, description) => ({
		name,
		description,
		options: [deckOption],
		callback: async ({ options }) => withDeck(options, command),
	})

	return {
		take: simple('Take (cue selected cue live)', 'TAKE'),
		go: simple('GO (play/pause, or take when idle)', 'GO'),
		play: simple('Play / resume', 'PLAY'),
		pause: simple('Pause', 'PAUSE'),
		stop: simple('Stop', 'STOP'),
		rerack: simple('Rerack (hold first frame)', 'RERACK'),
		skip_next: simple('Skip to next cue', 'SKIP'),
		skip_prev: simple('Skip to previous cue', 'SKIPBACK'),
		select_next: simple('Select next cue', 'NEXT'),
		select_prev: simple('Select previous cue', 'PREV'),

		select_cue: {
			name: 'Select cue by number',
			description: 'Selects without taking it live. Cue numbers are 1-based.',
			options: [deckOption, { type: 'textinput', label: 'Cue number', id: 'cue', default: '1', useVariables: true }],
			callback: async ({ options }) => {
				const cue = cueNumber(options.cue)
				if (cue !== null) await withDeck(options, `SELECT ${cue}`)
			},
		},

		take_cue: {
			name: 'Take cue by number',
			description: 'Selects the cue and takes it live in one press.',
			options: [deckOption, { type: 'textinput', label: 'Cue number', id: 'cue', default: '1', useVariables: true }],
			// One command, so the select and the take land on the same deck.
			callback: async ({ options }) => {
				const cue = cueNumber(options.cue)
				if (cue !== null) await withDeck(options, `TAKE ${cue}`)
			},
		},

		goto_cue: {
			name: 'Goto cue by id or name',
			options: [
				deckOption,
				{ type: 'textinput', label: 'Cue id or name', id: 'token', default: '', useVariables: true },
			],
			callback: async ({ options }) => {
				const token = String(options.token ?? '')
				if (token.trim().length > 0) await withDeck(options, `GOTO ${token}`)
			},
		},

		seek: {
			name: 'Seek (relative or absolute)',
			options: [
				deckOption,
				{
					type: 'dropdown',
					label: 'Mode',
					id: 'mode',
					default: 'rel',
					choices: [
						{ id: 'rel', label: 'Relative (+/- seconds)' },
						{ id: 'abs', label: 'Absolute (seconds from start)' },
					],
				},
				{ type: 'textinput', label: 'Seconds', id: 'seconds', default: '10', useVariables: true },
			],
			callback: async ({ options }) => {
				const secs = String(options.seconds ?? '')
				await withDeck(options, options.mode === 'abs' ? `SEEKPOS ${secs}` : `SEEK ${secs}`)
			},
		},

		clear: { name: 'Clear output to black', options: [], callback: () => send('CLEAR') },
		panic: { name: 'PANIC (run panic profile)', options: [], callback: () => send('PANIC') },
		blackout: {
			name: 'Blackout',
			options: [
				{
					type: 'dropdown',
					label: 'State',
					id: 'state',
					default: 'toggle',
					choices: [
						{ id: 'toggle', label: 'Toggle' },
						{ id: 'on', label: 'On' },
						{ id: 'off', label: 'Off' },
					],
				},
			],
			callback: ({ options }) => send(`BLACKOUT ${choice(options.state, 'toggle')}`),
		},

		master_volume: {
			name: 'Master volume',
			options: [{ type: 'number', label: 'Percent', id: 'value', default: 100, min: 0, max: 200 }],
			callback: ({ options }) => {
				const value = number(options.value, 'Master volume')
				if (value !== null) send(`MASTERVOL ${value}`)
			},
		},
		master_dimmer: {
			name: 'Master dimmer',
			options: [{ type: 'number', label: 'Percent', id: 'value', default: 100, min: 0, max: 100 }],
			callback: ({ options }) => {
				const value = number(options.value, 'Master dimmer')
				if (value !== null) send(`DIMMER ${value}`)
			},
		},
		deck_fader: {
			name: 'Deck fader',
			options: [deckOption, { type: 'number', label: 'Percent', id: 'value', default: 100, min: 0, max: 100 }],
			callback: async ({ options }) => {
				const value = number(options.value, 'Deck fader')
				if (value !== null) await withDeck(options, `VOLUME ${value}`)
			},
		},

		loop: {
			name: 'Toggle loop on selected cue',
			options: [deckOption],
			callback: async ({ options }) => withDeck(options, 'LOOP TOGGLE'),
		},
		shuffle: {
			name: 'Toggle shuffle',
			options: [deckOption],
			callback: async ({ options }) => withDeck(options, 'SHUFFLE TOGGLE'),
		},

		focus_deck: {
			name: 'Focus deck',
			options: [{ type: 'number', label: 'Deck', id: 'deck', default: 1, min: 1, max: MAX_DECKS, asInteger: true }],
			callback: ({ options }) => {
				const deck = number(options.deck, 'Deck', { integer: true })
				if (deck !== null) send(`DECK ${deck}`)
			},
		},

		output_enable: {
			name: 'Output on / off',
			options: [
				{
					type: 'dropdown',
					label: 'State',
					id: 'state',
					default: 'toggle',
					choices: [
						{ id: 'toggle', label: 'Toggle' },
						{ id: 'on', label: 'On' },
						{ id: 'off', label: 'Off' },
					],
				},
			],
			callback: ({ options }) => send(`VIDEO OUTPUT ${choice(options.state, 'toggle')}`),
		},
		output_fullscreen: {
			name: 'Toggle output fullscreen',
			options: [],
			callback: () => send('FULLSCREEN'),
		},
		output_display: {
			name: 'Send focused output to display',
			options: [
				{ type: 'number', label: 'Display (1-based)', id: 'display', default: 1, min: 1, max: 16, asInteger: true },
			],
			callback: ({ options }) => {
				const display = number(options.display, 'Display', { integer: true })
				if (display !== null) send(`DISPLAY ${display}`)
			},
		},

		find: {
			name: 'Find cue (set search token)',
			options: [{ type: 'textinput', label: 'Search text', id: 'token', default: '', useVariables: true }],
			callback: async ({ options }) => {
				const token = String(options.token ?? '')
				send(token.trim().length > 0 ? `FIND ${token}` : 'FINDCLEAR')
			},
		},
		find_next: { name: 'Find: next match', options: [], callback: () => send('FINDNEXT') },
		find_prev: { name: 'Find: previous match', options: [], callback: () => send('FINDPREV') },
		find_take: { name: 'Find: take current match', options: [], callback: () => send('FINDTAKE') },

		// ── VJ mode ─────────────────────────────────────────────────────────
		//
		// These are the ones a hardware surface earns its keep on. A crossfader
		// and a tap tempo are exactly what you do not want to reach for with a
		// mouse, which is the whole argument for the surface.
		vj_mode: {
			name: 'VJ mode',
			options: [
				{
					type: 'dropdown',
					label: 'State',
					id: 'state',
					default: 'toggle',
					choices: [
						{ id: 'on', label: 'On' },
						{ id: 'off', label: 'Off' },
						{ id: 'toggle', label: 'Toggle' },
					],
				},
			],
			callback: ({ options }) => send(`VJ ${choice(options.state, 'toggle')}`),
		},
		vj_mix: {
			name: 'VJ crossfader',
			description: 'Where the fader sits: 0 is all deck A, 1 is all deck B.',
			options: [{ type: 'number', label: 'Position', id: 'value', default: 0.5, min: 0, max: 1, step: 0.01 }],
			callback: ({ options }) => {
				const value = number(options.value, 'Crossfader position')
				if (value !== null) send(`VJ MIX ${value}`)
			},
		},
		vj_blend: {
			name: 'VJ blend mode',
			options: [
				{
					type: 'dropdown',
					label: 'Blend',
					id: 'blend',
					default: 'dissolve',
					choices: [
						{ id: 'dissolve', label: 'Dissolve' },
						{ id: 'add', label: 'Add' },
						{ id: 'multiply', label: 'Multiply' },
					],
				},
			],
			callback: ({ options }) => send(`VJ BLEND ${options.blend ?? 'dissolve'}`),
		},
		vj_tap: {
			name: 'VJ tap tempo',
			description: 'Tap four times or more. Taps over two seconds apart start again.',
			options: [],
			callback: () => send('VJ TAP'),
		},
		vj_bpm: {
			name: 'VJ tempo (BPM)',
			options: [{ type: 'number', label: 'BPM', id: 'value', default: 120, min: 20, max: 300 }],
			callback: ({ options }) => {
				const value = number(options.value, 'BPM')
				if (value !== null) send(`VJ BPM ${value}`)
			},
		},
		vj_quantise: {
			name: 'VJ quantised takes',
			description: 'Hold takes until the next beat, so what you do lands on the music.',
			options: [
				{
					type: 'dropdown',
					label: 'State',
					id: 'state',
					default: 'on',
					choices: [
						{ id: 'on', label: 'On' },
						{ id: 'off', label: 'Off' },
					],
				},
			],
			callback: ({ options }) => send(`VJ QUANTISE ${choice(options.state, 'on')}`),
		},
		vj_decks: {
			name: 'VJ deck assignment',
			description: 'Which decks sit on the A and B sides of the crossfader.',
			options: [
				{ type: 'number', label: 'Deck A', id: 'a', default: 1, min: 1, max: MAX_DECKS, asInteger: true },
				{ type: 'number', label: 'Deck B', id: 'b', default: 2, min: 1, max: MAX_DECKS, asInteger: true },
			],
			callback: ({ options }) => {
				const a = number(options.a, 'Deck A', { integer: true })
				const b = number(options.b, 'Deck B', { integer: true })
				if (a !== null && b !== null) send(`VJ DECKS ${a} ${b}`)
			},
		},

		// ── Effects ─────────────────────────────────────────────────────────
		fx_add: {
			name: 'Add an effect',
			description: 'Adds to the selected cue. The token is the effect name, e.g. schlieren.',
			options: [
				{ type: 'textinput', label: 'Effect', id: 'effect', default: '', useVariables: true },
				{ type: 'number', label: 'Amount', id: 'amount', default: 1, min: 0, max: 1, step: 0.01 },
			],
			callback: async ({ options }) => {
				const effect = String(options.effect ?? '').trim()
				const amount = number(options.amount, 'Amount')
				if (effect.length > 0 && amount !== null) send(`FX ADD ${effect} ${amount}`)
			},
		},
		fx_amount: {
			name: 'Effect amount',
			options: [
				{ type: 'number', label: 'Effect number', id: 'index', default: 1, min: 1, max: 32, asInteger: true },
				{ type: 'number', label: 'Amount', id: 'value', default: 1, min: 0, max: 1, step: 0.01 },
			],
			callback: ({ options }) => {
				const index = number(options.index, 'Effect number', { integer: true })
				const value = number(options.value, 'Amount')
				if (index !== null && value !== null) send(`FX AMOUNT ${index} ${value}`)
			},
		},
		fx_param: {
			name: 'Effect parameter',
			description: "The effect's own shaping controls, A to D.",
			options: [
				{ type: 'number', label: 'Effect number', id: 'index', default: 1, min: 1, max: 32, asInteger: true },
				{
					type: 'dropdown',
					label: 'Parameter',
					id: 'slot',
					default: 'A',
					choices: ['A', 'B', 'C', 'D'].map((id) => ({ id, label: id })),
				},
				{ type: 'number', label: 'Value', id: 'value', default: 0.5, min: 0, max: 1, step: 0.01 },
			],
			callback: ({ options }) => {
				const index = number(options.index, 'Effect number', { integer: true })
				const value = number(options.value, 'Value')
				if (index !== null && value !== null) send(`FX PARAM ${index} ${choice(options.slot, 'A')} ${value}`)
			},
		},
		fx_lfo: {
			name: 'Effect parameter LFO',
			description: 'Hand a parameter to an oscillator. E is the effect amount.',
			options: [
				{ type: 'number', label: 'Effect number', id: 'index', default: 1, min: 1, max: 32, asInteger: true },
				{
					type: 'dropdown',
					label: 'Parameter',
					id: 'slot',
					default: 'A',
					choices: ['A', 'B', 'C', 'D', 'E'].map((id) => ({ id, label: id })),
				},
				{
					type: 'dropdown',
					label: 'Setting',
					id: 'what',
					default: 'on',
					choices: [
						{ id: 'on', label: 'On' },
						{ id: 'off', label: 'Off' },
						{ id: 'shape', label: 'Shape' },
						{ id: 'rate', label: 'Rate (Hz)' },
						{ id: 'depth', label: 'Depth' },
						{ id: 'phase', label: 'Phase' },
						{ id: 'sync', label: 'Follow the tempo' },
						{ id: 'beats', label: 'Cycle length in beats' },
					],
				},
				{
					type: 'textinput',
					label: 'Value (blank for on/off)',
					id: 'value',
					default: '',
					useVariables: true,
				},
			],
			callback: async ({ options }) => {
				const index = number(options.index, 'Effect number', { integer: true })
				if (index === null) return
				const value = String(options.value ?? '').trim()
				send(`FX LFO ${index} ${choice(options.slot, 'A')} ${options.what ?? 'on'}${value ? ' ' + value : ''}`)
			},
		},
		fx_clear: {
			name: 'Clear the effect chain',
			options: [],
			callback: () => send('FX CLEAR'),
		},

		// -- Audio effects ---------------------------------------------------
		// The ear's half of the block above, and deliberately the same shape:
		// one action per control, the slot addressed by its 1-based position,
		// which is what AUDIOFX prints when you read the chain back.
		//
		// Amounts and parameters are PERCENTAGES here, because that is what the
		// verb takes and what the inspector shows. The picture actions above
		// use 0-1 because FX does; mixing the two notations inside one module
		// would be a trap, so each half matches the verb it drives.
		audiofx_add: {
			name: 'Add an audio effect',
			description:
				'Adds to the selected cue. Five of these follow the cue itself: picture, ' +
				'placement, seam, framelock and suspend.',
			options: [
				{
					type: 'dropdown',
					label: 'Effect',
					id: 'effect',
					default: 'comp',
					choices: [
						{ id: 'hpf', label: 'hpf' },
						{ id: 'lpf', label: 'lpf' },
						{ id: 'tilt', label: 'tilt' },
						{ id: 'comp', label: 'comp' },
						{ id: 'gate', label: 'gate' },
						{ id: 'delay', label: 'delay' },
						{ id: 'reverb', label: 'reverb' },
						{ id: 'width', label: 'width' },
						{ id: 'binaural', label: 'binaural' },
						{ id: 'picture', label: 'picture' },
						{ id: 'placement', label: 'placement' },
						{ id: 'seam', label: 'seam' },
						{ id: 'framelock', label: 'framelock' },
						{ id: 'suspend', label: 'suspend' },
					],
				},
				{ type: 'number', label: 'Amount %', id: 'amount', default: 100, min: 0, max: 100 },
			],
			callback: ({ options }) => {
				const amount = number(options.amount, 'Amount')
				if (amount !== null) send(`AUDIOFX ADD ${options.effect ?? 'comp'} ${amount}`)
			},
		},
		audiofx_amount: {
			name: 'Audio effect amount',
			options: [
				{ type: 'number', label: 'Slot', id: 'index', default: 1, min: 1, max: 8, asInteger: true },
				{ type: 'number', label: 'Amount %', id: 'value', default: 100, min: 0, max: 100 },
			],
			callback: ({ options }) => {
				const index = number(options.index, 'Slot', { integer: true })
				const value = number(options.value, 'Amount')
				if (index !== null && value !== null) send(`AUDIOFX ${index} ${value}`)
			},
		},
		audiofx_bypass: {
			name: 'Bypass an audio effect',
			description: 'Takes it out of the chain but keeps its settings.',
			options: [
				{ type: 'number', label: 'Slot', id: 'index', default: 1, min: 1, max: 8, asInteger: true },
				{
					type: 'dropdown',
					label: 'State',
					id: 'state',
					default: 'on',
					choices: [
						{ id: 'on', label: 'Bypassed' },
						{ id: 'off', label: 'Active' },
					],
				},
			],
			callback: ({ options }) => {
				const index = number(options.index, 'Slot', { integer: true })
				if (index !== null) send(`AUDIOFX ${index} BYPASS ${choice(options.state, 'on')}`)
			},
		},
		audiofx_remove: {
			name: 'Remove an audio effect',
			options: [{ type: 'number', label: 'Slot', id: 'index', default: 1, min: 1, max: 8, asInteger: true }],
			callback: ({ options }) => {
				const index = number(options.index, 'Slot', { integer: true })
				if (index !== null) send(`AUDIOFX ${index} OFF`)
			},
		},
		audiofx_clear: {
			name: 'Clear the audio chain',
			options: [],
			callback: () => send('AUDIOFX CLEAR'),
		},
		fx_copy_paste: {
			name: 'Copy or paste an effect chain',
			description: 'The chain only — not geometry, fades or crop.',
			options: [
				{
					type: 'dropdown',
					label: 'Action',
					id: 'action',
					default: 'copy',
					choices: [
						{ id: 'copy', label: 'Copy' },
						{ id: 'paste', label: 'Paste' },
					],
				},
			],
			callback: ({ options }) => send(`FX ${choice(options.action, 'copy')}`),
		},
		code_set: {
			name: 'Set the code source expression',
			description:
				'One expression, or three separated by commas for red, green and blue. Refused if it does not compile.',
			options: [{ type: 'textinput', label: 'Expression', id: 'expression', default: '', useVariables: true }],
			callback: async ({ options }) => {
				const expression = String(options.expression ?? '').trim()
				if (expression.length > 0) send(`CODE SET ${expression}`)
			},
		},

		text_mode: {
			name: 'Video synth: text mode on/off',
			description: 'Switch the selected video synth cue between pixels and a character grid.',
			options: [
				{
					type: 'dropdown',
					label: 'State',
					id: 'state',
					default: 'toggle',
					choices: [
						{ id: 'on', label: 'On' },
						{ id: 'off', label: 'Off' },
						{ id: 'toggle', label: 'Toggle' },
					],
				},
			],
			callback: ({ options }) => send(`ASCII ${choice(options.state, 'toggle')}`),
		},

		text_glyphs: {
			name: 'Video synth: custom glyphs',
			description: 'Characters the picture is built from, darkest first. Empty restores the chosen glyph set.',
			options: [{ type: 'textinput', label: 'Characters', id: 'glyphs', default: '', useVariables: true }],
			callback: async ({ options }) => {
				const glyphs = String(options.glyphs ?? '').trim()
				send(glyphs.length > 0 ? `ASCII GLYPHS ${glyphs}` : 'ASCII GLYPHS')
			},
		},

		text_phrases: {
			name: 'Video synth: phrases',
			description: 'Words to surface in the character field, separated by | . Empty clears them.',
			options: [
				{ type: 'textinput', label: 'Phrases', id: 'phrases', default: '', useVariables: true },
				{ type: 'number', label: 'Hold (seconds, 0 hides)', id: 'hold', default: 2.5, min: 0, max: 60 },
			],
			callback: async ({ options }) => {
				const phrases = String(options.phrases ?? '').trim()
				send(phrases.length > 0 ? `ASCII PHRASES ${phrases}` : 'ASCII PHRASES')
				const hold = number(options.hold, 'Hold')
				if (hold !== null) send(`ASCII HOLD ${hold}`)
			},
		},

		custom: {
			name: 'Custom command',
			description: 'Any Deckboy remote command, sent verbatim. See MANUAL.md section 22 for the full vocabulary.',
			options: [{ type: 'textinput', label: 'Command', id: 'command', default: '', useVariables: true }],
			callback: async ({ options }) => {
				const command = String(options.command ?? '')
				if (command.trim().length > 0) send(command.trim())
			},
		},
	}
}
