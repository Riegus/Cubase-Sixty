// Cubase-Sixty – MIDI Remote script for the SSL UF8 (via the Cubase-Sixty Bridge)
//
// Eight faders with touch, SEL / SOLO / CUT, record-enable on the top soft keys,
// channel editor on the V-pot push, banking, transport, automation Read / Write,
// and track names and colours for the scribble strips.
// The MIDI it speaks is defined in cubase/docs/bridge-midi-protocol.md.
//
// Install in
//   Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\UF8\
// (tools\install-scripts.ps1 does this for you.)

var midiremote_api = require('midiremote_api_v1')

// Log lines also go to the bridge (SysEx 7E <source>), which writes them to cubase\logs\scripts.log, so they can
// be read without Cubase's script console. They are queued and sent from the idle callback (sending needs the
// active device).
var LOG_SOURCE = 1
var logQueue = []
function out(text) {
	console.log(text)
	if (logQueue.length < 1000) logQueue.push(String(text))
}
// At most 20 lines per idle tick (~10 a second), so a long parameter list does not arrive as one big burst.
function flushLog(activeDevice) {
	for (var sent = 0; logQueue.length && sent < 20; ++sent) {
		var t = logQueue.shift()
		var body = [0xF0, 0x7D, 0x52, 0x36, 0x7E, LOG_SOURCE]
		for (var n = 0; n < t.length && n < 400; ++n) {
			var c = t.charCodeAt(n)
			body.push((c >> 14) & 0x7F, (c >> 7) & 0x7F, c & 0x7F)
		}
		body.push(0xF7)
		midiOutput.sendMidi(activeDevice, body)
	}
}

// SysEx constants live at the top: the name/colour callbacks fire while the
// mapping below is still being built, before later `var` lines have run.
var SYSEX_HEADER = [0xF0, 0x7D, 0x52, 0x36]
var SYSEX_NAME = 0x01
var SYSEX_COLOR = 0x02
var MAX_NAME_BYTES = 32

// Debug logging to the MIDI Remote Script Console. Set to true to log every
// fader, solo, mute, name and colour change (very chatty while a fader moves).
var DEBUG = false
var SCRIPT_VERSION = '0.11.1-dev (ports: to Cubase / from Cubase)'
function dbg(text) {
	if (DEBUG) out('[CubaseSixty] ' + text)
}
out('[CubaseSixty] script loaded, version ' + SCRIPT_VERSION)

var deviceDriver = midiremote_api.makeDeviceDriver('CubaseSixty', 'UF8', 'Riegus')

var midiInput = deviceDriver.mPorts.makeMidiInput()
var midiOutput = deviceDriver.mPorts.makeMidiOutput()

deviceDriver
	.makeDetectionUnit()
	.detectPortPair(midiInput, midiOutput)
	.expectInputNameEquals('Cubase-Sixty UF8 to Cubase')
	.expectOutputNameEquals('Cubase-Sixty UF8 from Cubase')

var surface = deviceDriver.mSurface

// ---------------------------------------------------------------------------
// Surface layout (a simple on-screen picture of the UF8)
// ---------------------------------------------------------------------------

var STRIPS = 8
var strips = []

for (var i = 0; i < STRIPS; ++i) {
	var x = i * 3
	var strip = {}

	strip.select = surface.makeButton(x, 0, 2, 1)
	strip.solo = surface.makeButton(x, 1, 1, 1)
	strip.cut = surface.makeButton(x + 1, 1, 1, 1)
	strip.vpot = surface.makeKnob(x, 2, 2, 2)
	strip.fader = surface.makeFader(x, 4, 2, 8)
	strip.faderTouch = surface.makeCustomValueVariable('faderTouch' + (i + 1))
	strip.vpush = surface.makeButton(x, 12, 2, 1)
	strip.softKey = surface.makeButton(x, 13, 2, 1)

	strip.select.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToNote(0, 24 + i)
	strip.solo.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToNote(0, 8 + i)
	strip.cut.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToNote(0, 16 + i)
	strip.softKey.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToNote(0, 48 + i)
	strip.vpush.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToNote(0, 32 + i)
	strip.vpot.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput)
		.bindToControlChange(0, 16 + i)
		.setTypeRelativeSignedBit()
	strip.fader.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToPitchBend(i)
	strip.faderTouch.mMidiBinding
		.setInputPort(midiInput)
		.bindToNote(0, 104 + i)

	strips.push(strip)
}

var bankLeft = surface.makeButton(0, 14, 2, 1)
var bankRight = surface.makeButton(2, 14, 2, 1)
var stopButton = surface.makeButton(6, 14, 2, 1)
var playButton = surface.makeButton(8, 14, 2, 1)
var recordButton = surface.makeButton(10, 14, 2, 1)

bankLeft.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 46)
bankRight.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 47)
stopButton.mSurfaceValue.mMidiBinding
	.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 93)
playButton.mSurfaceValue.mMidiBinding
	.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 94)
recordButton.mSurfaceValue.mMidiBinding
	.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 95)

// ---------------------------------------------------------------------------
// Mapping: mixer page
// ---------------------------------------------------------------------------

var page = deviceDriver.mMapping.makePage('Mixer')

var bankZone = page.mHostAccess.mMixConsole
	.makeMixerBankZone()
	.excludeInputChannels()
	.excludeOutputChannels()

// FLIP swaps faders and V-pots on all strips (faders on pan, V-pots on volume). Two sub-pages, activated by
// the bridge with notes 77 (normal) / 78 (flipped).
var flipArea = page.makeSubPageArea('Flip')
var flipNormal = flipArea.makeSubPage('Normal')
var flipFlipped = flipArea.makeSubPage('Flipped')
var uf8FlipActive = false
var flipResets = []
;(function () {
	var normalKey = surface.makeButton(34, 14, 1, 1)
	var flippedKey = surface.makeButton(35, 14, 1, 1)
	normalKey.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 77)
	flippedKey.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 78)
	page.makeActionBinding(normalKey.mSurfaceValue, flipNormal.mAction.mActivate)
	page.makeActionBinding(flippedKey.mSurfaceValue, flipFlipped.mAction.mActivate)
	function setFlip(on) {
		if (on === uf8FlipActive) return
		uf8FlipActive = on
		for (var r = 0; r < flipResets.length; ++r) flipResets[r]()
	}
	flipNormal.mOnActivate = function (activeDevice) { setFlip(false) }
	flipFlipped.mOnActivate = function (activeDevice) { setFlip(true) }
})()

for (var i = 0; i < STRIPS; ++i) {
	var channel = bankZone.makeMixerBankChannel()
	var strip = strips[i]

	page.makeValueBinding(strip.fader.mSurfaceValue, channel.mValue.mVolume)
		.setValueTakeOverModeJump().setSubPage(flipNormal)
	page.makeValueBinding(strip.vpot.mSurfaceValue, channel.mValue.mPan).setSubPage(flipNormal)
	page.makeValueBinding(strip.fader.mSurfaceValue, channel.mValue.mPan)
		.setValueTakeOverModeJump().setSubPage(flipFlipped)
	page.makeValueBinding(strip.vpot.mSurfaceValue, channel.mValue.mVolume).setSubPage(flipFlipped)
	page.makeValueBinding(strip.select.mSurfaceValue, channel.mValue.mSelected)
	;(function (index) {
		// Which strips are selected: tells two tracks with the same name apart (follow selection, below).
		strip.select.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
			stripSelected[index] = value > 0.5
			followStripSelect(activeDevice)   // another track of the same name has no new name to report
		}
	})(i)
	page.makeValueBinding(strip.solo.mSurfaceValue, channel.mValue.mSolo)
		.setTypeToggle()
	page.makeValueBinding(strip.cut.mSurfaceValue, channel.mValue.mMute)
		.setTypeToggle()
	// Top soft key = record-enable for the track (its LED shows the state).
	page.makeValueBinding(strip.softKey.mSurfaceValue, channel.mValue.mRecordEnable)
		.setTypeToggle()
	// Meters: left and right VU level to the bridge as CC 80 + strip (left) and CC 88 + strip (right),
	// 0..127. A mono channel only reports the left side; the bridge then shows it on both.
	;(function (index, ch) {
		function meterSide(name, hostValue, cc) {
			var meter = surface.makeCustomValueVariable(name + index)
			page.makeValueBinding(meter, hostValue)
			var last = -1
			meter.mOnProcessValueChange = function (activeDevice, value) {
				var v = meterToMidi(value)
				if (v === last) return
				last = v
				midiOutput.sendMidi(activeDevice, [0xB0, cc, v])
			}
		}
		try {
			meterSide('meterL', ch.mValue.getVUChannelMeterByChannelIndex(0), 80 + index)
			meterSide('meterR', ch.mValue.getVUChannelMeterByChannelIndex(1), 88 + index)
		} catch (e) {
			if (index === 0) out('[CubaseSixty] stereo meters not available (' + e + '), using one meter per strip')
			try { meterSide('meter', ch.mValue.mVUMeter, 80 + index) } catch (e2) { if (index === 0) out('[CubaseSixty] meters not available: ' + e2) }
		}
	})(i, channel)
	// V-pot push = open the channel editor for the track.
	page.makeValueBinding(strip.vpush.mSurfaceValue, channel.mValue.mEditorOpen)
		.setTypeToggle()

	// Scribble strip: send the track's name and colour to the bridge as SysEx
	// (see "Scribble strips and track colours" in the protocol doc).
	// These callbacks belong to the mixer bank channel itself (host side), not to
	// the fader's surface value: the surface-value versions only fired when a
	// track was renamed and never for colours.
	;(function (index) {
		channel.mOnTitleChange = function (activeDevice, activeMapping, title) {
			dbg('title strip ' + (index + 1) + ': [' + title + ']')
			sendStripName(activeDevice, index, title)
			stripTitles[index] = title
			updateArmable(activeDevice, index)
			seekOnTitle(activeDevice)
			followOnStripTitle(activeDevice)
		}
		// Known Cubase limit: this only fires when a track is selected or banked in, not the
		// moment its colour is changed (tested on Cubase 15 with several other colour hooks).
		channel.mOnColorChange = function (activeDevice, activeMapping, r, g, b, a, isActive) {
			dbg('color strip ' + (index + 1) + ': r=' + r + ' g=' + g + ' b=' + b + ' a=' + a + ' active=' + isActive)
			sendStripColor(activeDevice, index, r, g, b)
		}
	})(i)

	// Pan on the strip's display (value line "Pan" + Cubase's text, readout bar) and the level in the dB
	// readout. Normally pan comes from the V-pot and the level from the fader; under FLIP the other way round.
	;(function (index, vpot, fader) {
		var lastPanText = null, lastPanPos = null, lastDb = null
		flipResets.push(function () { lastPanText = lastPanPos = lastDb = null })
		function panText(activeDevice, value, units) {
			var text = String(value) + (units ? units : '')
			if (text === lastPanText) return
			lastPanText = text
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x07, index], toAscii7(text), [0xF7]))
		}
		function panPos(activeDevice, value) {
			var pos = Math.max(0, Math.min(127, Math.round(value * 127)))
			if (pos === lastPanPos) return
			lastPanPos = pos
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x06, index, pos, 0xF7]))
		}
		function dbText(activeDevice, value) {
			var text = String(value)
			if (text === lastDb) return
			lastDb = text
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0C, index], toAscii7(text), [0xF7]))
		}
		vpot.mSurfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
			if (uf8FlipActive) dbText(activeDevice, value); else panText(activeDevice, value, units)
		}
		vpot.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
			if (!uf8FlipActive) panPos(activeDevice, value)
		}
		fader.mSurfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
			if (uf8FlipActive) panText(activeDevice, value, units); else dbText(activeDevice, value)
		}
		fader.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
			if (uf8FlipActive) panPos(activeDevice, value)
		}
	})(i, strip.vpot, strip.fader)

	;(function (index, fader) {
		strips[index].solo.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
			dbg('strip ' + (index + 1) + ' SOLO ' + Number(value).toFixed(2))
		}
		strips[index].cut.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
			dbg('strip ' + (index + 1) + ' MUTE ' + Number(value).toFixed(2))
		}
		if (fader.mSurfaceValue.mTouchState) {
			fader.mSurfaceValue.mTouchState.mOnProcessValueChange = function (activeDevice, value) {
				dbg('fader ' + (index + 1) + ' touch ' + value)
			}
		}
	})(i, strip.fader)

	// Fader touch: tell Cubase the fader is held, so automation in Touch mode
	// behaves like a touch-sensitive fader. Needs MIDI Remote API 1.1 (Cubase 13+);
	// the guard keeps the script loading on older versions.
	if (strip.fader.mSurfaceValue.mTouchState) {
		strip.fader.mSurfaceValue.mTouchState.bindTo(strip.faderTouch)
	}
}

// Automation Read / Write act on the selected track (the UF8 buttons are global).
var autoRead = surface.makeButton(12, 14, 2, 1)
var autoWrite = surface.makeButton(14, 14, 2, 1)
autoRead.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 56)
autoWrite.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 57)
var selectedTrack = page.mHostAccess.mTrackSelection.mMixerChannel
page.makeValueBinding(autoRead.mSurfaceValue, selectedTrack.mValue.mAutomationRead).setTypeToggle()
page.makeValueBinding(autoWrite.mSurfaceValue, selectedTrack.mValue.mAutomationWrite).setTypeToggle()

page.makeActionBinding(bankLeft.mSurfaceValue, bankZone.mAction.mPrevBank)
page.makeActionBinding(bankRight.mSurfaceValue, bankZone.mAction.mNextBank)

var transport = page.mHostAccess.mTransport.mValue
page.makeValueBinding(stopButton.mSurfaceValue, transport.mStop).setTypeToggle()
page.makeValueBinding(playButton.mSurfaceValue, transport.mStart).setTypeToggle()
page.makeValueBinding(recordButton.mSurfaceValue, transport.mRecord).setTypeToggle()

// Foot switches 1 and 2: play and record.
var foot1 = surface.makeButton(16, 14, 2, 1)
var foot2 = surface.makeButton(18, 14, 2, 1)
foot1.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 62)
foot2.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 63)
page.makeValueBinding(foot1.mSurfaceValue, transport.mStart).setTypeToggle()
page.makeValueBinding(foot2.mSurfaceValue, transport.mRecord).setTypeToggle()

// ---------------------------------------------------------------------------
// SysEx helpers: scribble-strip name and colour
// ---------------------------------------------------------------------------


function toAscii7(text) {
	var out = []
	var str = text || ''
	for (var n = 0; n < str.length && out.length < MAX_NAME_BYTES; ++n) {
		var code = str.charCodeAt(n)
		out.push(code < 0x80 ? code : 0x3F)
	}
	return out
}

function to7bit(value) {
	// The API reports colour components as 0..1.
	return Math.max(0, Math.min(127, Math.round(value * 127)))
}

function sendStripName(activeDevice, index, text) {
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([SYSEX_NAME, index], toAscii7(text), [0xF7]))
}

function sendStripColor(activeDevice, index, r, g, b) {
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([SYSEX_COLOR, index, to7bit(r), to7bit(g), to7bit(b), 0xF7]))
}

// ---------------------------------------------------------------------------
// Which strips can be record-armed? Cubase does not say, so a second, hidden bank holds only the channel
// types that can record (audio, instrument, sampler, MIDI) and collects their names. A UF8 strip whose
// name is not among them (group, FX, VCA, bus, empty) gets a dark top soft key: note 64 + strip, velocity
// 127 = cannot arm, 0 = can arm. The bridge darkens the key and ignores presses.
// ---------------------------------------------------------------------------

var REC_CHANNELS = 128
var stripTitles = []
var stripSelected = []
// TEST SWITCH: true = the UF8 does not follow the selection itself; the script only logs what Cubase does
// (selection and bank moves), to see how Cubase's own banking behaves.
var FOLLOW_OBSERVE_ONLY = false
var followOnStripTitle = function () {}   // set by the follow-selection block
var followCurrentTitle = function () {}   // set by the follow-selection block
var followStripSelect = function () {}    // set by the follow-selection block
var onAllTitle = function () {}           // set by the follow-selection block
var recTitles = []
var armableSent = []


function isRecordable(title) {
	if (!title) return false
	for (var k = 0; k < REC_CHANNELS; ++k) if (recTitles[k] === title) return true
	return false
}

function updateArmable(activeDevice, index) {
	var armable = isRecordable(stripTitles[index])
	if (armableSent[index] === armable) return
	armableSent[index] = armable
	midiOutput.sendMidi(activeDevice, [0x90, 64 + index, armable ? 0 : 127])
}

// The recordable tracks' names come from the helper script (SysEx 19 via the bridge, see the end of this
// script): as a list in this script they made every bank move about 0.25 s slower.
var recTitleCount = 0
function onRecTitle(activeDevice, slot, title) {
	if (recTitles[slot] === title) return
	if (!recTitles[slot] && title) {
		recTitleCount++
		if (recTitleCount === 1 || recTitleCount % 16 === 0) out('[CubaseSixty] recordable tracks seen: ' + recTitleCount + ' (latest: ' + title + ')')
	}
	recTitles[slot] = title
	for (var i = 0; i < STRIPS; ++i) updateArmable(activeDevice, i)
}

// ---------------------------------------------------------------------------
// Start banked at the selected track: when the script starts, step bank by bank (a hidden control bound
// to "next bank") until the selected track's name shows on one of the eight strips. Only at start-up;
// after that the bank stays where you put it.
// ---------------------------------------------------------------------------

var SEEK_MAX_STEPS = 32
// The old start-up search (one bank at a time) is off: "follow the selection" below now does the start-up
// jump too. Kept inactive so its hooks stay harmless.
var seek = { active: false, steps: 0, titlesSinceStep: 0, selectedTitle: '', ownPresses: 0 }


function selectedIsVisible() {
	if (!seek.selectedTitle) return false
	for (var i = 0; i < STRIPS; ++i) if (stripTitles[i] === seek.selectedTitle) return true
	return false
}

function seekCheck(activeDevice) {
	if (!seek.active || !seek.selectedTitle) return
	if (selectedIsVisible()) { seek.active = false; out('[CubaseSixty] banked to the selected track (' + seek.selectedTitle + ')'); return }
	// Wait until the current bank has reported all its names before stepping again, or the bank that
	// holds the selected track could be skipped.
	// Cubase answers a bank key first by re-sending the SAME eight names, and only then with the new
	// bank. So a bank counts as arrived only when every strip shows a different name than before the
	// step (counting re-sent names stepped too early, and the queued steps then overshot).
	if (seek.steps === 0) {
		if (seek.titlesSinceStep < STRIPS) return
	} else {
		for (var c = 0; c < STRIPS; ++c) if (stripTitles[c] === seek.snapshot[c]) return
	}
	if (seek.steps >= SEEK_MAX_STEPS) { seek.active = false; out('[CubaseSixty] selected track not found within ' + SEEK_MAX_STEPS + ' banks'); return }
	seek.steps++
	seek.titlesSinceStep = 0
	seek.snapshot = stripTitles.slice()
	out('[CubaseSixty] seeking the selected track (' + seek.selectedTitle + '): next bank, step ' + seek.steps)
	// Setting a hidden control from the script did not move the bank, so ask the bridge to send a
	// bank-right key press (SysEx 0B), exactly as if the key had been pressed.
	seek.ownPresses++
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0B, 0xF7]))
}

function seekOnTitle(activeDevice) {
	if (!seek.active) return
	seek.titlesSinceStep++
	seekCheck(activeDevice)
}

page.mHostAccess.mTrackSelection.mMixerChannel.mOnTitleChange = function (activeDevice, activeMapping, title) {
	seek.selectedTitle = title
	seekCheck(activeDevice)
	followCurrentTitle(activeDevice, title)
}

// Any bank key pressed by hand ends the search for good.
function stopSeekOnPress(button) {
	button.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
		if (value > 0 && seek.ownPresses > 0) { seek.ownPresses--; return }   // our own step, sent by the bridge
		if (seek.active && value > 0) { seek.active = false; out('[CubaseSixty] bank key pressed: start-up search stopped') }
	}
}
stopSeekOnPress(bankLeft)
stopSeekOnPress(bankRight)

// ---------------------------------------------------------------------------
// Zoom pad (notes 72-76: up, left, centre, right, down), same as the UF1's NAV cross: left / right = zoom
// out / in, up / down = vertical zoom in / out, centre opens / closes the editor.
// ---------------------------------------------------------------------------

var zoomKeys = [
	{ note: 72, category: 'Zoom', command: 'Zoom In Vertically' },
	{ note: 73, category: 'Zoom', command: 'Zoom Out' },
	{ note: 74, category: 'Editors', command: 'Open/Close Editor' },
	{ note: 75, category: 'Zoom', command: 'Zoom In' },
	{ note: 76, category: 'Zoom', command: 'Zoom Out Vertically' }
]
for (var z = 0; z < zoomKeys.length; ++z) {
	var zoomButton = surface.makeButton(20 + z, 14, 1, 1)
	zoomButton.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, zoomKeys[z].note)
	page.makeCommandBinding(zoomButton.mSurfaceValue, zoomKeys[z].category, zoomKeys[z].command)
}

// ---------------------------------------------------------------------------
// Page left / right (notes 44 / 45): move the bank by one channel instead of eight.
// ---------------------------------------------------------------------------

var pageLeft = surface.makeButton(26, 14, 1, 1)
var pageRight = surface.makeButton(27, 14, 1, 1)
pageLeft.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 44)
pageRight.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 45)
page.makeActionBinding(pageLeft.mSurfaceValue, bankZone.mAction.mShiftLeft)
page.makeActionBinding(pageRight.mSurfaceValue, bankZone.mAction.mShiftRight)
stopSeekOnPress(pageLeft)
stopSeekOnPress(pageRight)

// ---------------------------------------------------------------------------
// Automation mode keys (notes 58 / 59 / 60 / 61 = Off / Trim / Latch / Touch): Cubase's global automation
// mode. Cubase has no Off mode, so the Off key selects Cross-Over.
// ---------------------------------------------------------------------------

var autoModes = [
	{ note: 58, command: 'Automation Mode - Cross-Over' },
	{ note: 59, command: 'Automation Mode - Trim' },
	{ note: 60, command: 'Automation Mode - Auto-Latch' },
	{ note: 61, command: 'Automation Mode - Touch' }
]
for (var a = 0; a < autoModes.length; ++a) {
	var modeButton = surface.makeButton(28 + a, 15, 1, 1)
	modeButton.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, autoModes[a].note)
	page.makeCommandBinding(modeButton.mSurfaceValue, 'Automation', autoModes[a].command)
}

// Meter scale: Cubase reports the meter as linear amplitude (0..1), which read far too low when sent
// straight to the LEDs. Convert to dB and map -55..0 dBFS onto 0..127 (the bridge then uses the UF8's own
// -55..0 dB scale, as in Rea-Sixty). METER_FLOOR_DB sets the bottom of the scale.
var METER_FLOOR_DB = -55
function meterToMidi(value) {
	if (!(value > 0)) return 0
	var db = 20 * Math.log(value) / Math.LN10
	if (db >= 0) return 127
	if (db <= METER_FLOOR_DB) return 0
	return Math.round((db - METER_FLOOR_DB) / -METER_FLOOR_DB * 127)
}

// ---------------------------------------------------------------------------
// Follow the selection: when a track is selected that is not on the eight strips (e.g. in the MixConsole),
// bank to it. A hidden bank of all channels (same types as the UF8 bank) gives each channel's position,
// name and selected state; the strips' names give the bank's current position. The bridge sends the bank
// presses (SysEx 10 jump = reset + n bank-rights, 0B / 0F single steps). The timing comes from Cubase's
// idle callback (about ten calls a second, measured), because notes from the bridge cannot run script code.
//   - After a selection change, wait 300 ms: Cubase moves the bank itself first, in steps of eight.
//   - If the track is still not on a strip: jump (back on the eight-grid), then check and correct with
//     single steps. A try that did not move the bank (presses ignored) jumps again.
//   - At start-up Cubase does not bank to the selection and ignores bank presses for a while: check every
//     second for 30 s from the first idle call, until the selected track has been seen on a strip.
// ---------------------------------------------------------------------------

;(function () {
	var CHANNELS = 256
	var allTitles = []
	var selectedIndex = -1
	var followTries = 0
	var lastTryOffset = -1   // the bank position when the last try was sent
	var dueMs = 0            // when the next check is due (0 = none)
	var startupUntil = 0     // start-up checks until this time (0 = not started yet, -1 = done)
	var nextStartupCheck = 0
	// At start-up the UF8 is held frozen while the tries run (Cubase ignores presses at first, so it can take
	// several), and shows the bank once the selected track is on a strip (SysEx 13 01 / 13 00).
	var startupFrozen = false
	function startupFreeze(activeDevice, on) {
		if (startupFrozen === on) return
		startupFrozen = on
		midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x13, on ? 1 : 0, 0xF7]))
	}
	function nowMs() {
		try { var t = Date.now(); if (typeof t === 'number' && t > 0) return t } catch (e) {}
		return 0
	}
	// Several tracks can be selected; follow Cubase's current track (the last one clicked, the one the UF1
	// shows): the selected channel whose name is the current track's name.
	var currentTitle = ''
	// The selected track is found by Cubase's current track name. With several tracks of that name: the one
	// whose UF8 strip is selected, if it is on the strips (exact); otherwise the one nearest to the previous
	// selection (or to the bank, if there was none), since selection mostly moves step by step.
	function pickCurrent(activeDevice) {
		var pick = -1
		if (currentTitle) {
			var candidates = []
			for (var j = 0; j < allTitles.length; ++j) if (allTitles[j] === currentTitle) candidates.push(j)
			if (candidates.length === 1) pick = candidates[0]
			else if (candidates.length > 1) {
				var offset = currentOffset()
				if (offset >= 0) {
					for (var s = 0; s < STRIPS; ++s) {
						if (stripSelected[s] && stripTitles[s] === currentTitle && candidates.indexOf(offset + s) >= 0) { pick = offset + s; break }
					}
				}
				if (pick < 0) {
					var near = selectedIndex >= 0 ? selectedIndex : Math.max(offset, 0)
					for (var c = 0; c < candidates.length; ++c) {
						if (pick < 0 || Math.abs(candidates[c] - near) < Math.abs(pick - near)) pick = candidates[c]
					}
				}
			}
		}
		sendSameNameRank(activeDevice, pick)
		// Only a real change may bank: Cubase re-sends the same values after every bank step.
		if (pick === selectedIndex) return
		selectedIndex = pick
		if (FOLLOW_OBSERVE_ONLY) { out('[CubaseSixty] observe: selected #' + (pick + 1) + ' [' + allTitles[pick] + '], bank starts at #' + (currentOffset() + 1)); return }
		followTries = 0
		if (pick < 0) { dueMs = 0; return }
		dueMs = nowMs() + 300   // let Cubase move the bank first
	}
	followCurrentTitle = function (activeDevice, title) {
		currentTitle = title
		pickCurrent(activeDevice)
	}
	followStripSelect = function (activeDevice) { pickCurrent(activeDevice) }
	// Tell the UF1 script (via the bridge) which of several same-named tracks is selected: its rank among
	// them, top to bottom (SysEx 1C <rank> <name, 3 x 7 bits per character>). The UF1 is not told otherwise:
	// Cubase reports only the name, which does not change between them.
	var sentRank = -1, sentRankTitle = null
	function sendSameNameRank(activeDevice, pick) {
		if (pick < 0 || !currentTitle) return
		var rank = 0, count = 0
		for (var j = 0; j < allTitles.length; ++j) {
			if (allTitles[j] !== currentTitle) continue
			if (j < pick) ++rank
			++count
		}
		if (count < 2 || (rank === sentRank && currentTitle === sentRankTitle)) return
		sentRank = rank
		sentRankTitle = currentTitle
		var name = []
		for (var n = 0; n < currentTitle.length && n < 48; ++n) {
			var c = currentTitle.charCodeAt(n)
			name.push((c >> 14) & 0x7F, (c >> 7) & 0x7F, c & 0x7F)
		}
		midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x1C, rank & 0x7F], name, [0xF7]))
	}
	function currentOffset() {
		// The bank position: where the strips' names appear in the full list.
		for (var o = 0; o < CHANNELS; ++o) {
			var match = true, any = false
			for (var i = 0; i < STRIPS; ++i) {
				var t = stripTitles[i]
				if (!t) continue
				any = true
				if (allTitles[o + i] !== t) { match = false; break }
			}
			if (any && match) return o
		}
		return -1
	}
	function follow(activeDevice) {
		if (FOLLOW_OBSERVE_ONLY || seek.active || selectedIndex < 0) { followTries = 0; return }
		var offset = currentOffset()
		if (offset < 0) { followTries = 0; return }
		var delta = selectedIndex - offset
		if (delta >= 0 && delta < STRIPS) {   // on a strip: done
			if (followTries > 0) out('[CubaseSixty] follow: selected #' + (selectedIndex + 1) + ' is on strip ' + (delta + 1))
			if (startupUntil > 0) { startupUntil = -1; startupFreeze(activeDevice, false); out('[CubaseSixty] follow: start-up done') }
			followTries = 0
			return
		}
		if (followTries >= 12) { out('[CubaseSixty] follow: gave up'); followTries = 0; return }
		var now = nowMs()
		if (startupUntil > 0) startupFreeze(activeDevice, true)
		var jump = followTries === 0 || offset === lastTryOffset
		++followTries
		lastTryOffset = offset
		out('[CubaseSixty] follow: selected #' + (selectedIndex + 1) + ' [' + allTitles[selectedIndex] + '], bank starts at #' + (offset + 1) + ', try ' + followTries)
		if (jump) {
			// Reset to the first bank (back on Cubase's eight-grid) and send the bank-rights in one burst.
			var banks = Math.min(Math.floor(selectedIndex / STRIPS), 64)
			out('[CubaseSixty] follow: jump = reset + ' + banks + ' bank steps')
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x10, banks, 0xF7]))
			dueMs = now + banks * 100 + 1000   // the bridge spaces the presses 100 ms apart and shows the end 400 ms later
			return
		}
		// Correct with all the remaining bank steps at once (the bridge shows only the end). Seen at start-up:
		// a jump right after loading lost most of its presses.
		var steps = delta < 0 ? Math.ceil(-delta / STRIPS) : Math.floor(delta / STRIPS)
		steps = Math.max(1, Math.min(steps, 64))
		out('[CubaseSixty] follow: ' + steps + ' bank step(s) ' + (delta < 0 ? 'left' : 'right'))
		midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([delta < 0 ? 0x0F : 0x0B, steps, 0xF7]))
		dueMs = now + steps * 100 + 800
	}
	deviceDriver.mOnIdle = function (activeDevice) {
		flushLog(activeDevice)
		var now = nowMs()
		if (now <= 0) return
		requestListsIfNeeded(activeDevice, now)
		// Start-up: Cubase ignores most bank presses right after loading (seen: a jump sent at once moved one
		// bank), so the first check waits 2.5 s.
		if (startupUntil === 0) { startupUntil = now + 30000; nextStartupCheck = now + 2500 }
		if (dueMs > 0 && now >= dueMs) { dueMs = 0; follow(activeDevice); return }
		if (startupUntil > 0) {
			if (now > startupUntil) { startupUntil = -1; startupFreeze(activeDevice, false); out('[CubaseSixty] follow: start-up checks ended'); return }
			if (now >= nextStartupCheck && dueMs === 0) {
				nextStartupCheck = now + 1000
				followTries = 0
				follow(activeDevice)
			}
		}
	}
	// Tell the bridge how many tracks the bank has and where it starts (SysEx 12: total, offset, each 2 x 7
	// bit; 7F 7F = not known), so its bank-right stops at the bank where the last track is on strip 8.
	var sentTotal = -2, sentOffset = -2
	function reportBank(activeDevice) {
		var total = 0
		while (total < CHANNELS && allTitles[total]) ++total
		var offset = currentOffset()
		if (total === sentTotal && offset === sentOffset) return
		sentTotal = total
		sentOffset = offset
		function two(v) { return v < 0 ? [0x7F, 0x7F] : [(v >> 7) & 0x7F, v & 0x7F] }
		midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x12]).concat(two(total)).concat(two(offset)).concat([0xF7]))
	}
	followOnStripTitle = function (activeDevice) { reportBank(activeDevice) }
	var resetKey = surface.makeButton(36, 14, 1, 1)
	resetKey.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 79)
	page.makeActionBinding(resetKey.mSurfaceValue, bankZone.mAction.mResetBank)
	// All channels' names come from the helper script (SysEx 18 via the bridge): as a list in this script
	// they made every bank move about 0.25 s slower.
	onAllTitle = function (activeDevice, index, title) {
		if (allTitles[index] === title) return
		allTitles[index] = title
		reportBank(activeDevice)
		if (title && title === currentTitle) pickCurrent(activeDevice)
	}
})()

// ---------------------------------------------------------------------------
// Names from the helper script (cubase/midi-remote/Helper), passed on by the bridge as SysEx:
//   18 <index hi> <index lo> <name>   all channels (follow selection)
//   19 <slot> <name>                  recordable channels (record-enable keys)
// Names are 3 x 7 bits per character. Until the first names arrive (Cubase drops what is sent while a
// project loads), the script asks for them every 2 s (SysEx 1B, passed on to the helper).
// ---------------------------------------------------------------------------
var listNamesSeen = 0
var listRequestAt = 0
function requestListsIfNeeded(activeDevice, now) {
	if (listNamesSeen > 0 || now < listRequestAt) return
	listRequestAt = now + 2000
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x1B, 0xF7]))
}
function decodeName(m, from) {
	var s = ''
	for (var i = from; i + 2 < m.length - 1; i += 3) s += String.fromCharCode((m[i] << 14) | (m[i + 1] << 7) | m[i + 2])
	return s
}
midiInput.mOnSysex = function (activeDevice, message) {
	var m = []
	for (var i = 0; i < message.length; ++i) m.push(message[i])
	if (m.length < 6 || m[0] !== 0xF0 || m[1] !== 0x7D || m[2] !== 0x52 || m[3] !== 0x36) return
	if (m[4] === 0x18 && m.length >= 8) {
		if (++listNamesSeen === 1) out('[CubaseSixty] channel lists from the helper script arriving')
		onAllTitle(activeDevice, (m[5] << 7) | m[6], decodeName(m, 7))
	} else if (m[4] === 0x19 && m.length >= 7) {
		++listNamesSeen
		onRecTitle(activeDevice, m[5], decodeName(m, 6))
	}
}
