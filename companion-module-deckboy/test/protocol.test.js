/*
 * Parser tests run against a verbatim STATUS reply captured from Deckboy
 * v0.80.2 over the Companion port — not a hand-written approximation, so a
 * change to the app's status format shows up here as a failing test.
 */

import assert from 'node:assert/strict'
import { test } from 'node:test'

import { parseStatus, parseKeyValues, remainingSeconds, secondsToClock, timecodeToSeconds } from '../src/protocol.js'

const SAMPLE = [
	'DECKBOY_0.01 focus=1 decks=1 focused_output=1 outputs=1 panic_profile=outputs_off panic_fade_s=0.9 panic_restore=off find="" find_matches=0 find_cursor=0 find_deck=0 video_mode=native video_hz=auto video_depth=auto canvas=off integrations="atem[off,ok],ndi-trigger[off,ok]" master_dimmer=100 blackout=off master_vol=0',
	'DECK 1 name="Deck 1" status=Playing selected=2 active=2 selected_num="2" selected_id="cue-d28f172495035dfd" display=2 route=1 layer=0 raster=1280x720 depth=8-bit audio="system default" overlay=off view=0,0 warp=off transition=crossfade transition_s=0 tc=00:00:00:00 cue="counter" cue_id="cue-d28f172495035dfd" in=00:00.0 out=01:00.0 pos=00:05.1 dur=01:00.0 vol=100 decode_fps=--.-',
	'OUTPUT 1 name="Output 1" id="out-87735c2a3f680a83" enabled=on health=live type=window host=1 display=2 layers=1 alpha=100 delay_ms=0 output_fps=60.1 stream_fps=--.- backend=window[ok]',
].join('\n')

test('parses quoted values containing spaces and commas', () => {
	const kv = parseKeyValues('name="Deck 1" audio="system default" raster=1280x720')
	assert.equal(kv.name, 'Deck 1')
	assert.equal(kv.audio, 'system default')
	assert.equal(kv.raster, '1280x720')
})

test('parses an empty quoted value', () => {
	assert.equal(parseKeyValues('find="" find_matches=0').find, '')
})

test('parses a full status report', () => {
	const state = parseStatus(SAMPLE)
	assert.equal(state.global.version, '0.01')
	assert.equal(state.global.focus, '1')
	assert.equal(state.global.master_vol, '0')
	assert.equal(state.global.blackout, 'off')

	const deck = state.decks.get(1)
	assert.equal(deck.status, 'Playing')
	assert.equal(deck.cue, 'counter')
	assert.equal(deck.active, '2')
	assert.equal(deck.audio, 'system default')

	const output = state.outputs.get(1)
	assert.equal(output.enabled, 'on')
	assert.equal(output.health, 'live')
	assert.equal(output.display, '2')
})

test('ignores unrelated lines without throwing', () => {
	const state = parseStatus('some toast text\n\nDECK 2 status=Stopped')
	assert.equal(state.decks.get(2).status, 'Stopped')
})

test('converts timecodes', () => {
	assert.equal(timecodeToSeconds('00:05.1'), 5.1)
	assert.equal(timecodeToSeconds('01:00.0'), 60)
	assert.equal(timecodeToSeconds('01:02:03.0'), 3723)
	assert.equal(timecodeToSeconds('--:--'), null)
})

test('derives remaining time', () => {
	const deck = parseStatus(SAMPLE).decks.get(1)
	assert.equal(Math.round(remainingSeconds(deck) * 10) / 10, 54.9)
	assert.equal(secondsToClock(remainingSeconds(deck)), '00:54')
})

test('remaining is null when the deck has no duration', () => {
	assert.equal(remainingSeconds({ pos: '--:--', dur: '--:--' }), null)
	assert.equal(secondsToClock(null), '')
})
