/*
 * Actions — every button press becomes one plain-text Deckboy command.
 *
 * Deckboy's remote vocabulary is much larger than this (see MANUAL.md §20);
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

		custom: {
			name: 'Custom command',
			description:
				'Any Deckboy remote command, sent verbatim. See MANUAL.md section 20 for the full vocabulary.',
			options: [{ type: 'textinput', label: 'Command', id: 'command', default: '', useVariables: true }],
			callback: async ({ options }) => {
				const command = await self.parseVariablesInString(options.command)
				if (command.trim().length > 0) send(command.trim())
			},
		},
	}
}
