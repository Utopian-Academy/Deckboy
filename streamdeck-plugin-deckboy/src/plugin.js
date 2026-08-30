// plugin.js — the Stream Deck side.
//
// NO DEPENDENCIES. Stream Deck talks to a plugin over a plain WebSocket on
// localhost, and Node has had a WebSocket client built in since 22 — so this
// needs nothing from npm at all. For a plugin that runs on an operator's show
// machine that is worth having: nothing to audit, nothing to update, and
// nothing that can go missing between installing and opening the deck.
//
// Stream Deck launches us with -port, -pluginUUID, -registerEvent and -info,
// and expects the register event back on the socket before it will speak.

import { DeckboyLink, transportTitle } from './deckboy.js'

function parseArgs(argv) {
	const args = {}
	for (let i = 0; i < argv.length; i += 1) {
		const key = argv[i]
		if (key.startsWith('-') && i + 1 < argv.length) {
			args[key.slice(1)] = argv[i + 1]
			i += 1
		}
	}
	return args
}

const args = parseArgs(process.argv.slice(2))
const link = new DeckboyLink()

// Every visible key, so state changes can be pushed to all of them at once.
const contexts = new Map()

let ws = null

function toDeck(payload) {
	if (ws && ws.readyState === 1) ws.send(JSON.stringify(payload))
}

function setTitle(context, title) {
	toDeck({ event: 'setTitle', context, payload: { title, target: 0 } })
}

function showAlert(context) {
	toDeck({ event: 'showAlert', context })
}

function showOk(context) {
	toDeck({ event: 'showOk', context })
}

// What a key does when pressed. Everything is a Deckboy command line, which is
// the same vocabulary the manual documents and Companion drives -- one
// protocol, so a key does exactly what the same command does anywhere else.
function commandFor(action, settings) {
	switch (action) {
		case 'com.deckboy.streamdeck.take': {
			const cue = (settings.cue || '').trim()
			return cue ? `TAKE ${cue}` : 'TAKE'
		}
		case 'com.deckboy.streamdeck.stop':
			return 'STOP'
		case 'com.deckboy.streamdeck.pause':
			return 'PAUSE'
		case 'com.deckboy.streamdeck.panic':
			return 'PANIC'
		case 'com.deckboy.streamdeck.blackout':
			return 'BLACKOUT TOGGLE'
		case 'com.deckboy.streamdeck.command':
			return (settings.command || '').trim()
		default:
			return ''
	}
}

function refreshTitles() {
	const title = link.connected ? transportTitle(link.status) : 'offline'
	for (const [context, entry] of contexts) {
		// A key with its own words keeps them: someone who labelled a key
		// "HOUSE LIGHTS" did not ask for a transport readout on it.
		if (entry.settings && entry.settings.keepTitle) continue
		if (entry.action === 'com.deckboy.streamdeck.take' ||
			entry.action === 'com.deckboy.streamdeck.stop' ||
			entry.action === 'com.deckboy.streamdeck.pause') {
			setTitle(context, title)
		} else if (!link.connected) {
			setTitle(context, 'offline')
		} else {
			setTitle(context, '')
		}
	}
}

link.on('status', refreshTitles)
link.on('connected', refreshTitles)
link.on('disconnected', refreshTitles)

function connectToStreamDeck() {
	ws = new WebSocket(`ws://127.0.0.1:${args.port}`)

	ws.addEventListener('open', () => {
		ws.send(JSON.stringify({
			event: args.registerEvent,
			uuid: args.pluginUUID,
		}))
	})

	ws.addEventListener('message', (event) => {
		let msg
		try {
			msg = JSON.parse(event.data)
		} catch {
			return
		}
		const { event: name, context, action, payload } = msg
		const settings = (payload && payload.settings) || {}

		switch (name) {
			case 'willAppear':
				contexts.set(context, { action, settings })
				link.configure(settings.host, settings.port)
				refreshTitles()
				break
			case 'willDisappear':
				contexts.delete(context)
				break
			case 'didReceiveSettings': {
				const entry = contexts.get(context)
				if (entry) entry.settings = settings
				link.configure(settings.host, settings.port)
				break
			}
			case 'keyDown': {
				const command = commandFor(action, settings)
				if (!command) {
					showAlert(context)
					break
				}
				// The alert is the honest answer to a key that did nothing:
				// on a show machine, a button that looks like it worked and
				// did not is worse than one that says so.
				if (link.send(command)) showOk(context)
				else showAlert(context)
				break
			}
			default:
				break
		}
	})

	ws.addEventListener('close', () => {
		// Stream Deck closing the socket means Stream Deck is going away, and
		// so should we -- there is nothing left to control.
		link.shutdown()
		process.exit(0)
	})
}

if (args.port && args.registerEvent && args.pluginUUID) {
	connectToStreamDeck()
}
