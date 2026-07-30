/*
 * Deckboy status protocol.
 *
 * Deckboy answers `STATUS` on its Companion port with one line per subject:
 *
 *   DECKBOY_<ver> focus=1 decks=1 outputs=1 master_vol=100 ...
 *   DECK 1 name="Deck 1" status=Playing cue="Act 1 Opener" pos=00:12.4 dur=03:41.0 ...
 *   OUTPUT 1 name="Output 1" enabled=on health=live display=2 output_fps=60.1 ...
 *
 * Values are `key=value`, with double quotes around anything containing spaces.
 * Nothing here talks to Companion — keeping the parser pure makes it testable
 * and keeps main.js about connection lifecycle.
 */

/**
 * Split a `key=value key="quoted value"` payload into a plain object.
 * Unknown//malformed fragments are skipped rather than throwing: a partial
 * status line should still yield the fields it did contain.
 */
export function parseKeyValues(text) {
	const out = {}
	// key = ( "quoted, possibly spaced" | bare-token )
	const re = /([A-Za-z_][A-Za-z0-9_]*)=("([^"]*)"|[^\s]*)/g
	let match
	while ((match = re.exec(text)) !== null) {
		const key = match[1]
		out[key] = match[3] !== undefined ? match[3] : match[2]
	}
	return out
}

/**
 * Parse a full STATUS reply into { global, decks: Map, outputs: Map }.
 * Deck/output keys are 1-based, matching what operators see in the UI and
 * what the DECK/SELECT commands expect.
 */
export function parseStatus(payload) {
	const result = { global: {}, decks: new Map(), outputs: new Map() }
	for (const rawLine of String(payload).split(/\r?\n/)) {
		const line = rawLine.trim()
		if (line.length === 0) continue

		if (line.startsWith('DECKBOY')) {
			const spaceAt = line.indexOf(' ')
			result.global = parseKeyValues(spaceAt >= 0 ? line.slice(spaceAt + 1) : '')
			const versionToken = (spaceAt >= 0 ? line.slice(0, spaceAt) : line).split('_')
			result.global.version = versionToken.length > 1 ? versionToken[1] : ''
			continue
		}

		const subject = /^(DECK|OUTPUT)\s+(\d+)\s*(.*)$/.exec(line)
		if (subject) {
			const index = Number.parseInt(subject[2], 10)
			const fields = parseKeyValues(subject[3])
			if (subject[1] === 'DECK') result.decks.set(index, fields)
			else result.outputs.set(index, fields)
		}
	}
	return result
}

/** `00:12.4`, `01:02:03.4` and `--:--` → seconds (null when not a time). */
export function timecodeToSeconds(value) {
	if (typeof value !== 'string' || value.includes('-')) return null
	const parts = value.split(':')
	if (parts.length === 0 || parts.length > 3) return null
	let seconds = 0
	for (const part of parts) {
		const n = Number.parseFloat(part)
		if (!Number.isFinite(n)) return null
		seconds = seconds * 60 + n
	}
	return seconds
}

/** Seconds → `MM:SS` (or `H:MM:SS` past an hour). Empty string for null. */
export function secondsToClock(seconds) {
	if (seconds === null || !Number.isFinite(seconds)) return ''
	const sign = seconds < 0 ? '-' : ''
	const total = Math.floor(Math.abs(seconds))
	const h = Math.floor(total / 3600)
	const m = Math.floor((total % 3600) / 60)
	const s = total % 60
	const pad = (n) => String(n).padStart(2, '0')
	return h > 0 ? `${sign}${h}:${pad(m)}:${pad(s)}` : `${sign}${pad(m)}:${pad(s)}`
}

/**
 * Time left on a deck, derived rather than reported: Deckboy sends pos and dur
 * but not remaining, and remaining is what an operator actually watches.
 */
export function remainingSeconds(deckFields) {
	const pos = timecodeToSeconds(deckFields?.pos)
	const dur = timecodeToSeconds(deckFields?.dur)
	if (pos === null || dur === null) return null
	return Math.max(0, dur - pos)
}

/** Deckboy reports on/off for booleans; treat anything else as false. */
export function isOn(value) {
	return value === 'on' || value === 'true' || value === '1' || value === 'yes'
}
