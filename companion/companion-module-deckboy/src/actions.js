/**
 * Action definitions for the Deckboy Companion module.
 *
 * Each action maps to one or more TCP commands sent to Deckboy on port 5510.
 */

export function getActions(instance) {
	return {
		// ── Transport ────────────────────────────────────────────────
		play: {
			name: 'Play',
			options: [],
			callback: () => instance.sendCommand('PLAY'),
		},
		pause: {
			name: 'Pause',
			options: [],
			callback: () => instance.sendCommand('PAUSE'),
		},
		stop: {
			name: 'Stop',
			options: [],
			callback: () => instance.sendCommand('STOP'),
		},
		toggle: {
			name: 'Toggle Play/Pause',
			options: [],
			callback: () => instance.sendCommand('TOGGLE'),
		},
		rerack: {
			name: 'Rerack',
			description: 'Seek to start and pause',
			options: [
				{
					type: 'dropdown',
					id: 'target',
					label: 'Target',
					default: '',
					choices: [
						{ id: '', label: 'Focused deck' },
						{ id: 'ALL', label: 'All decks' },
					],
				},
			],
			callback: (action) => {
				const t = action.options.target
				instance.sendCommand(t ? `RERACK ${t}` : 'RERACK')
			},
		},
		allPlay: {
			name: 'All Play',
			options: [],
			callback: () => instance.sendCommand('ALLPLAY'),
		},
		allPause: {
			name: 'All Pause',
			options: [],
			callback: () => instance.sendCommand('ALLPAUSE'),
		},
		allStop: {
			name: 'All Stop',
			options: [],
			callback: () => instance.sendCommand('ALLSTOP'),
		},

		// ── Cue selection ────────────────────────────────────────────
		next: {
			name: 'Next Cue',
			options: [],
			callback: () => instance.sendCommand('NEXT'),
		},
		prev: {
			name: 'Previous Cue',
			options: [],
			callback: () => instance.sendCommand('PREV'),
		},
		goto: {
			name: 'Go To Cue',
			description: 'Select and play a cue by number, token, or name',
			options: [
				{
					type: 'textinput',
					id: 'cue',
					label: 'Cue (number, ID, or name)',
					default: '',
				},
			],
			callback: (action) => {
				const cue = action.options.cue?.trim()
				if (cue) instance.sendCommand(`GOTO ${cue}`)
			},
		},
		take: {
			name: 'Take',
			description: 'Arm and play a cue (optional auto-advance)',
			options: [
				{
					type: 'textinput',
					id: 'cue',
					label: 'Cue (blank = take selected)',
					default: '',
				},
				{
					type: 'checkbox',
					id: 'auto',
					label: 'Auto-advance after take',
					default: false,
				},
			],
			callback: (action) => {
				const cue = action.options.cue?.trim()
				const auto = action.options.auto ? ' AUTO' : ''
				instance.sendCommand(cue ? `TAKE ${cue}${auto}` : `TAKE${auto}`)
			},
		},
		select: {
			name: 'Select (Arm) Cue',
			options: [
				{
					type: 'textinput',
					id: 'cue',
					label: 'Cue (number, ID, or name)',
					default: '',
				},
			],
			callback: (action) => {
				const cue = action.options.cue?.trim()
				if (cue) instance.sendCommand(`SELECT ${cue}`)
			},
		},

		// ── Deck focus ───────────────────────────────────────────────
		deckSelect: {
			name: 'Focus Deck',
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
			callback: (action) => instance.sendCommand(`DECK ${action.options.deck}`),
		},
		deckNext: {
			name: 'Next Deck',
			options: [],
			callback: () => instance.sendCommand('DECKNEXT'),
		},
		deckPrev: {
			name: 'Previous Deck',
			options: [],
			callback: () => instance.sendCommand('DECKPREV'),
		},

		// ── Seek & position ──────────────────────────────────────────
		seek: {
			name: 'Seek to Position',
			options: [
				{
					type: 'textinput',
					id: 'seconds',
					label: 'Position (seconds or timecode)',
					default: '0',
				},
			],
			callback: (action) => {
				const s = action.options.seconds?.trim()
				if (s) instance.sendCommand(`SEEK ${s}`)
			},
		},
		gotoLastN: {
			name: 'Go To Last N Seconds',
			options: [
				{
					type: 'dropdown',
					id: 'offset',
					label: 'Offset from end',
					default: '10',
					choices: [
						{ id: '10', label: '10 seconds' },
						{ id: '20', label: '20 seconds' },
						{ id: '30', label: '30 seconds' },
					],
				},
			],
			callback: (action) => {
				// Compute from duration — but we don't know it client-side easily.
				// Deckboy has GOTO-10/20/30 buttons but no single command; use SEEK with state.
				const deck = getFocusedDeck(instance)
				if (deck?.duration) {
					const durMatch = deck.duration.match(/(\d+):(\d+\.?\d*)/)
					if (durMatch) {
						const durSec = parseInt(durMatch[1]) * 60 + parseFloat(durMatch[2])
						const target = Math.max(0, durSec - parseInt(action.options.offset))
						instance.sendCommand(`SEEK ${target}`)
					}
				}
			},
		},

		// ── Volume & levels ──────────────────────────────────────────
		volume: {
			name: 'Set Deck Volume',
			options: [
				{
					type: 'number',
					id: 'level',
					label: 'Volume (0-100)',
					default: 100,
					min: 0,
					max: 100,
				},
			],
			callback: (action) => instance.sendCommand(`VOLUME ${action.options.level}`),
		},
		masterVolume: {
			name: 'Set Master Volume',
			options: [
				{
					type: 'number',
					id: 'level',
					label: 'Volume (0-200, 100 = unity)',
					default: 100,
					min: 0,
					max: 200,
				},
			],
			callback: (action) => {
				const normalized = action.options.level / 100.0
				instance.sendCommand(`MASTERVOL ${normalized}`)
			},
		},
		dimmer: {
			name: 'Set Master Dimmer',
			options: [
				{
					type: 'number',
					id: 'level',
					label: 'Dimmer (0-100)',
					default: 100,
					min: 0,
					max: 100,
				},
			],
			callback: (action) => instance.sendCommand(`DIMMER ${action.options.level}`),
		},

		// ── Blackout ─────────────────────────────────────────────────
		blackout: {
			name: 'Blackout',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Mode',
					default: 'TOGGLE',
					choices: [
						{ id: 'ON', label: 'On' },
						{ id: 'OFF', label: 'Off' },
						{ id: 'TOGGLE', label: 'Toggle' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`BLACKOUT ${action.options.mode}`),
		},

		// ── Transitions ──────────────────────────────────────────────
		transitionStyle: {
			name: 'Set Transition Style',
			options: [
				{
					type: 'dropdown',
					id: 'style',
					label: 'Style',
					default: 'crossfade',
					choices: [
						{ id: 'crossfade', label: 'Crossfade' },
						{ id: 'cut', label: 'Cut' },
						{ id: 'dip', label: 'Dip to Black' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`TRANSITIONSTYLE ${action.options.style}`),
		},
		transition: {
			name: 'Set Transition',
			options: [
				{
					type: 'dropdown',
					id: 'style',
					label: 'Style',
					default: 'crossfade',
					choices: [
						{ id: 'crossfade', label: 'Crossfade' },
						{ id: 'cut', label: 'Cut' },
						{ id: 'dip', label: 'Dip to Black' },
					],
				},
				{
					type: 'number',
					id: 'seconds',
					label: 'Duration (seconds)',
					default: 1.0,
					min: 0,
					max: 30,
					step: 0.25,
				},
			],
			callback: (action) => {
				instance.sendCommand(`TRANSITION ${action.options.style} ${action.options.seconds}`)
			},
		},

		// ── Cue properties ───────────────────────────────────────────
		loop: {
			name: 'Loop',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Mode',
					default: 'TOGGLE',
					choices: [
						{ id: 'ON', label: 'On' },
						{ id: 'OFF', label: 'Off' },
						{ id: 'TOGGLE', label: 'Toggle' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`LOOP ${action.options.mode}`),
		},
		endAction: {
			name: 'Set End Action',
			options: [
				{
					type: 'dropdown',
					id: 'action',
					label: 'End action',
					default: 'inherit',
					choices: [
						{ id: 'inherit', label: 'Inherit' },
						{ id: 'stop', label: 'Stop' },
						{ id: 'loop', label: 'Loop' },
						{ id: 'hold', label: 'Hold last frame' },
						{ id: 'next', label: 'Auto next' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`ENDACTION ${action.options.action}`),
		},
		speed: {
			name: 'Set Playback Speed',
			options: [
				{
					type: 'number',
					id: 'speed',
					label: 'Speed (0.25 - 4.0)',
					default: 1.0,
					min: 0.25,
					max: 4.0,
					step: 0.25,
				},
			],
			callback: (action) => instance.sendCommand(`SPEED ${action.options.speed}`),
		},
		colorTag: {
			name: 'Set Color Tag',
			options: [
				{
					type: 'dropdown',
					id: 'color',
					label: 'Color',
					default: '',
					choices: [
						{ id: '', label: 'None' },
						{ id: 'red', label: 'Red' },
						{ id: 'orange', label: 'Orange' },
						{ id: 'yellow', label: 'Yellow' },
						{ id: 'cyan', label: 'Cyan' },
						{ id: 'blue', label: 'Blue' },
						{ id: 'purple', label: 'Purple' },
						{ id: 'pink', label: 'Pink' },
					],
				},
			],
			callback: (action) => {
				const c = action.options.color
				instance.sendCommand(c ? `COLOR ${c}` : 'COLOR none')
			},
		},

		// ── In/out points ────────────────────────────────────────────
		setInPoint: {
			name: 'Set In Point',
			options: [
				{
					type: 'textinput',
					id: 'seconds',
					label: 'In point (seconds or timecode)',
					default: '0',
				},
			],
			callback: (action) => instance.sendCommand(`IN ${action.options.seconds}`),
		},
		setOutPoint: {
			name: 'Set Out Point',
			options: [
				{
					type: 'textinput',
					id: 'seconds',
					label: 'Out point (seconds or timecode)',
					default: '',
				},
			],
			callback: (action) => instance.sendCommand(`OUT ${action.options.seconds}`),
		},
		trimClear: {
			name: 'Clear Trim Points',
			options: [],
			callback: () => instance.sendCommand('TRIM CLEAR'),
		},

		// ── Overlays ─────────────────────────────────────────────────
		clearOverlay: {
			name: 'Clear Overlays',
			options: [],
			callback: () => instance.sendCommand('CLEAROVERLAY'),
		},
		overlayPush: {
			name: 'Push Overlay',
			options: [
				{
					type: 'textinput',
					id: 'cue',
					label: 'Cue number or name',
					default: '',
				},
			],
			callback: (action) => {
				const cue = action.options.cue?.trim()
				if (cue) instance.sendCommand(`OVERLAY PUSH ${cue}`)
			},
		},
		overlayPop: {
			name: 'Pop Overlay',
			options: [],
			callback: () => instance.sendCommand('OVERLAY POP'),
		},

		// ── Output management ────────────────────────────────────────
		outputSelect: {
			name: 'Focus Output',
			options: [
				{
					type: 'number',
					id: 'output',
					label: 'Output number',
					default: 1,
					min: 1,
					max: 16,
				},
			],
			callback: (action) => instance.sendCommand(`VIDEO OUTPUT ${action.options.output}`),
		},
		outputToggle: {
			name: 'Toggle Output',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Mode',
					default: 'TOGGLE',
					choices: [
						{ id: 'ON', label: 'On' },
						{ id: 'OFF', label: 'Off' },
						{ id: 'TOGGLE', label: 'Toggle' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`VIDEO OUTPUT ${action.options.mode}`),
		},
		outputTestCard: {
			name: 'Output Test Card',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Mode',
					default: 'TOGGLE',
					choices: [
						{ id: 'ON', label: 'On' },
						{ id: 'OFF', label: 'Off' },
						{ id: 'TOGGLE', label: 'Toggle' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`VIDEO OUTPUT TESTCARD ${action.options.mode}`),
		},

		// ── NDI ──────────────────────────────────────────────────────
		ndiToggle: {
			name: 'NDI Output',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Mode',
					default: 'TOGGLE',
					choices: [
						{ id: 'ON', label: 'On' },
						{ id: 'OFF', label: 'Off' },
						{ id: 'TOGGLE', label: 'Toggle' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`NDI ${action.options.mode}`),
		},
		ndiKeyToggle: {
			name: 'NDI Key Output',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Mode',
					default: 'TOGGLE',
					choices: [
						{ id: 'ON', label: 'On' },
						{ id: 'OFF', label: 'Off' },
						{ id: 'TOGGLE', label: 'Toggle' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`NDI KEY ${action.options.mode}`),
		},

		// ── Streaming ────────────────────────────────────────────────
		streamToggle: {
			name: 'Stream Output',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Mode',
					default: 'TOGGLE',
					choices: [
						{ id: 'ON', label: 'On' },
						{ id: 'OFF', label: 'Off' },
						{ id: 'TOGGLE', label: 'Toggle' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`VIDEO STREAM ${action.options.mode}`),
		},

		// ── Timecode ─────────────────────────────────────────────────
		tcChase: {
			name: 'Timecode Chase',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Mode',
					default: 'TOGGLE',
					choices: [
						{ id: 'ON', label: 'On' },
						{ id: 'OFF', label: 'Off' },
						{ id: 'TOGGLE', label: 'Toggle' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`TC CHASE ${action.options.mode}`),
		},
		tcRun: {
			name: 'Timecode Run',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Mode',
					default: 'TOGGLE',
					choices: [
						{ id: 'ON', label: 'On' },
						{ id: 'OFF', label: 'Off' },
						{ id: 'TOGGLE', label: 'Toggle' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`TC RUN ${action.options.mode}`),
		},

		// ── Panic ────────────────────────────────────────────────────
		panic: {
			name: 'Panic',
			description: 'Trigger panic profile (emergency fade-out)',
			options: [
				{
					type: 'textinput',
					id: 'profile',
					label: 'Profile (blank = current)',
					default: '',
				},
			],
			callback: (action) => {
				const p = action.options.profile?.trim()
				instance.sendCommand(p ? `PANIC ${p}` : 'PANIC')
			},
		},

		// ── Shuffle ──────────────────────────────────────────────────
		shuffle: {
			name: 'Shuffle',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Mode',
					default: 'TOGGLE',
					choices: [
						{ id: 'ON', label: 'On' },
						{ id: 'OFF', label: 'Off' },
						{ id: 'TOGGLE', label: 'Toggle' },
					],
				},
			],
			callback: (action) => instance.sendCommand(`SHUFFLE ${action.options.mode}`),
		},

		// ── Fullscreen ───────────────────────────────────────────────
		fullscreen: {
			name: 'Toggle Fullscreen',
			options: [],
			callback: () => instance.sendCommand('FULLSCREEN'),
		},

		// ── Raw command ──────────────────────────────────────────────
		raw: {
			name: 'Send Raw Command',
			description: 'Send any Deckboy TCP command verbatim',
			options: [
				{
					type: 'textinput',
					id: 'command',
					label: 'Command',
					default: '',
				},
			],
			callback: (action) => {
				const cmd = action.options.command?.trim()
				if (cmd) instance.sendCommand(cmd)
			},
		},
	}
}

function getFocusedDeck(instance) {
	const state = instance.state
	if (!state?.decks) return null
	const idx = (state.focusedDeck ?? 1) - 1
	return state.decks[idx] || null
}
