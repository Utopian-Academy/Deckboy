// The property inspector's side of the conversation.
//
// Stream Deck opens the inspector in a browser view and hands it the same
// WebSocket arrangement the plugin gets. This is the small amount of glue that
// turns that into "read the settings, write the settings", so each inspector
// page is a form and nothing else.

let websocket = null
let uuid = null
let settings = {}

// Stream Deck calls this global by name when it opens the inspector.
window.connectElgatoStreamDeckSocket = function (port, inUUID, registerEvent, info, actionInfo) {
	uuid = inUUID
	try {
		settings = JSON.parse(actionInfo).payload.settings || {}
	} catch {
		settings = {}
	}
	websocket = new WebSocket('ws://127.0.0.1:' + port)
	websocket.onopen = () => {
		websocket.send(JSON.stringify({ event: registerEvent, uuid: inUUID }))
		fill()
	}
}

function save() {
	if (!websocket || websocket.readyState !== 1) return
	websocket.send(JSON.stringify({
		event: 'setSettings',
		context: uuid,
		payload: settings,
	}))
}

// Every field carries the settings key it edits, so adding a field to a page
// needs no matching change here.
function fill() {
	for (const field of document.querySelectorAll('[data-setting]')) {
		const key = field.dataset.setting
		if (settings[key] !== undefined) field.value = settings[key]
		field.addEventListener('input', () => {
			settings[key] = field.value
			save()
		})
	}
}
