/*
 * Actions — every button press becomes one plain-text Deckboy command.
 *
 * Deckboy's remote vocabulary is much larger than this (see MANUAL.md §22);
 * what is exposed here is the set an operator drives from a Stream Deck during
 * a show. Anything else is reachable through the "Custom command" action at the
 * bottom, so the module never becomes the reason something isn't possible.
 */

export function buildActions(self) {
	const send = (cmd) => self.sendCommand(cmd)

	// Deck-targeted commands are prefixed with `DECK n` so a button always acts
	// on the deck it names, regardless of which deck currently has focus.
	const deckOption = {
		type: 'number',
		label: 'Deck (0 = focused deck)',
		id: 'deck',
		default: 0,
		min: 0,
		max: 16,
	}
	const withDeck = async (options, command) => {
		const deck = Number(await self.parseVariablesInString(String(options.deck ?? 0)))
		if (Number.isFinite(deck) && deck > 0) {
			send(`DECK ${deck}`)
		}
		send(command)
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
			options: [
				deckOption,
				{ type: 'textinput', label: 'Cue number', id: 'cue', default: '1', useVariables: true },
			],
			callback: async ({ options }) => {
				const cue = await self.parseVariablesInString(options.cue)
				await withDeck(options, `SELECT ${cue}`)
			},
		},

		take_cue: {
			name: 'Take cue by number',
			description: 'Selects the cue and takes it live in one press.',
			options: [
				deckOption,
				{ type: 'textinput', label: 'Cue number', id: 'cue', default: '1', useVariables: true },
			],
			callback: async ({ options }) => {
				const cue = await self.parseVariablesInString(options.cue)
				await withDeck(options, `SELECT ${cue}`)
				send('TAKE')
			},
		},

		goto_cue: {
			name: 'Goto cue by id or name',
			options: [
				deckOption,
				{ type: 'textinput', label: 'Cue id or name', id: 'token', default: '', useVariables: true },
			],
			callback: async ({ options }) => {
				const token = await self.parseVariablesInString(options.token)
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
				const secs = await self.parseVariablesInString(options.seconds)
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
			callback: ({ options }) => send(`BLACKOUT ${String(options.state).toUpperCase()}`),
		},

		master_volume: {
			name: 'Master volume',
			options: [{ type: 'number', label: 'Percent', id: 'value', default: 100, min: 0, max: 200 }],
			callback: ({ options }) => send(`MASTERVOL ${options.value}`),
		},
		master_dimmer: {
			name: 'Master dimmer',
			options: [{ type: 'number', label: 'Percent', id: 'value', default: 100, min: 0, max: 100 }],
			callback: ({ options }) => send(`DIMMER ${options.value}`),
		},
		deck_fader: {
			name: 'Deck fader',
			options: [deckOption, { type: 'number', label: 'Percent', id: 'value', default: 100, min: 0, max: 100 }],
			callback: async ({ options }) => withDeck(options, `VOLUME ${options.value}`),
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
			options: [{ type: 'number', label: 'Deck', id: 'deck', default: 1, min: 1, max: 16 }],
			callback: ({ options }) => send(`DECK ${options.deck}`),
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
			callback: ({ options }) => send(`VIDEO OUTPUT ${String(options.state).toUpperCase()}`),
		},
		output_fullscreen: {
			name: 'Toggle output fullscreen',
			options: [],
			callback: () => send('FULLSCREEN'),
		},
		output_display: {
			name: 'Send focused output to display',
			options: [{ type: 'number', label: 'Display (1-based)', id: 'display', default: 1, min: 1, max: 16 }],
			callback: ({ options }) => send(`DISPLAY ${options.display}`),
		},

		find: {
			name: 'Find cue (set search token)',
			options: [{ type: 'textinput', label: 'Search text', id: 'token', default: '', useVariables: true }],
			callback: async ({ options }) => {
				const token = await self.parseVariablesInString(options.token)
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
			callback: ({ options }) => send(`VJ ${options.state.toUpperCase()}`),
		},
		vj_mix: {
			name: 'VJ crossfader',
			description: 'Where the fader sits: 0 is all deck A, 1 is all deck B.',
			options: [
				{ type: 'number', label: 'Position', id: 'value', default: 0.5, min: 0, max: 1, step: 0.01 },
			],
			callback: ({ options }) => send(`VJ MIX ${options.value}`),
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
			callback: ({ options }) => send(`VJ BLEND ${options.blend}`),
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
			callback: ({ options }) => send(`VJ BPM ${options.value}`),
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
			callback: ({ options }) => send(`VJ QUANTISE ${options.state}`),
		},
		vj_decks: {
			name: 'VJ deck assignment',
			description: 'Which decks sit on the A and B sides of the crossfader.',
			options: [
				{ type: 'number', label: 'Deck A', id: 'a', default: 1, min: 1, max: 8 },
				{ type: 'number', label: 'Deck B', id: 'b', default: 2, min: 1, max: 8 },
			],
			callback: ({ options }) => send(`VJ DECKS ${options.a} ${options.b}`),
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
				const effect = (await self.parseVariablesInString(options.effect)).trim()
				if (effect.length > 0) send(`FX ADD ${effect} ${options.amount}`)
			},
		},
		fx_amount: {
			name: 'Effect amount',
			options: [
				{ type: 'number', label: 'Effect number', id: 'index', default: 1, min: 1, max: 32 },
				{ type: 'number', label: 'Amount', id: 'value', default: 1, min: 0, max: 1, step: 0.01 },
			],
			callback: ({ options }) => send(`FX AMOUNT ${options.index} ${options.value}`),
		},
		fx_param: {
			name: 'Effect parameter',
			description: "The effect's own shaping controls, A to D.",
			options: [
				{ type: 'number', label: 'Effect number', id: 'index', default: 1, min: 1, max: 32 },
				{
					type: 'dropdown',
					label: 'Parameter',
					id: 'slot',
					default: 'A',
					choices: ['A', 'B', 'C', 'D'].map((id) => ({ id, label: id })),
				},
				{ type: 'number', label: 'Value', id: 'value', default: 0.5, min: 0, max: 1, step: 0.01 },
			],
			callback: ({ options }) => send(`FX PARAM ${options.index} ${options.slot} ${options.value}`),
		},
		fx_lfo: {
			name: 'Effect parameter LFO',
			description: 'Hand a parameter to an oscillator. E is the effect amount.',
			options: [
				{ type: 'number', label: 'Effect number', id: 'index', default: 1, min: 1, max: 32 },
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
				const value = (await self.parseVariablesInString(options.value)).trim()
				send(`FX LFO ${options.index} ${options.slot} ${options.what}${value ? ' ' + value : ''}`)
			},
		},
		fx_clear: {
			name: 'Clear the effect chain',
			options: [],
			callback: () => send('FX CLEAR'),
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
			callback: ({ options }) => send(`FX ${options.action.toUpperCase()}`),
		},
		code_set: {
			name: 'Set the code source expression',
			description:
				'One expression, or three separated by commas for red, green and blue. Refused if it does not compile.',
			options: [
				{ type: 'textinput', label: 'Expression', id: 'expression', default: '', useVariables: true },
			],
			callback: async ({ options }) => {
				const expression = (await self.parseVariablesInString(options.expression)).trim()
				if (expression.length > 0) send(`CODE SET ${expression}`)
			},
		},

		custom: {
			name: 'Custom command',
			description:
				'Any Deckboy remote command, sent verbatim. See MANUAL.md section 22 for the full vocabulary.',
			options: [{ type: 'textinput', label: 'Command', id: 'command', default: '', useVariables: true }],
			callback: async ({ options }) => {
				const command = await self.parseVariablesInString(options.command)
				if (command.trim().length > 0) send(command.trim())
			},
		},
	}
}
