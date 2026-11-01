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

// SysEx constants live at the top: the name/colour callbacks fire while the
// mapping below is still being built, before later `var` lines have run.
var SYSEX_HEADER = [0xF0, 0x7D, 0x52, 0x36]
var SYSEX_NAME = 0x01
var SYSEX_COLOR = 0x02
var MAX_NAME_BYTES = 32

// Debug logging to the MIDI Remote Script Console. Set to true to log every
// fader, solo, mute, name and colour change (very chatty while a fader moves).
var DEBUG = false
var SCRIPT_VERSION = '0.5.2-dev (ports: to Cubase / from Cubase)'
function dbg(text) {
	if (DEBUG) console.log('[CubaseSixty] ' + text)
}
console.log('[CubaseSixty] script loaded, version ' + SCRIPT_VERSION)

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

for (var i = 0; i < STRIPS; ++i) {
	var channel = bankZone.makeMixerBankChannel()
	var strip = strips[i]

	page.makeValueBinding(strip.fader.mSurfaceValue, channel.mValue.mVolume)
		.setValueTakeOverModeJump()
	page.makeValueBinding(strip.vpot.mSurfaceValue, channel.mValue.mPan)
	page.makeValueBinding(strip.select.mSurfaceValue, channel.mValue.mSelected)
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
				var v = Math.max(0, Math.min(127, Math.round(value * 127)))
				if (v === last) return
				last = v
				midiOutput.sendMidi(activeDevice, [0xB0, cc, v])
			}
		}
		try {
			meterSide('meterL', ch.mValue.getVUChannelMeterByChannelIndex(0), 80 + index)
			meterSide('meterR', ch.mValue.getVUChannelMeterByChannelIndex(1), 88 + index)
		} catch (e) {
			if (index === 0) console.log('[CubaseSixty] stereo meters not available (' + e + '), using one meter per strip')
			try { meterSide('meter', ch.mValue.mVUMeter, 80 + index) } catch (e2) { if (index === 0) console.log('[CubaseSixty] meters not available: ' + e2) }
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
		}
		// Known Cubase limit: this only fires when a track is selected or banked in, not the
		// moment its colour is changed (tested on Cubase 15 with several other colour hooks).
		channel.mOnColorChange = function (activeDevice, activeMapping, r, g, b, a, isActive) {
			dbg('color strip ' + (index + 1) + ': r=' + r + ' g=' + g + ' b=' + b + ' a=' + a + ' active=' + isActive)
			sendStripColor(activeDevice, index, r, g, b)
		}
	})(i)

	// Pan on the strip's display: value line ("Pan" + Cubase's text) and the readout bar under it.
	;(function (index, vpot) {
		var lastText = null
		var lastPos = null
		vpot.mSurfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
			var text = String(value) + (units ? units : '')
			if (text === lastText) return
			lastText = text
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x07, index], toAscii7(text), [0xF7]))
		}
		vpot.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
			var pos = Math.max(0, Math.min(127, Math.round(value * 127)))
			if (pos === lastPos) return
			lastPos = pos
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x06, index, pos, 0xF7]))
		}
	})(i, strip.vpot)

	;(function (index, fader) {
		// The fader's level as text ("-4.94") for the dB readout on the strip (SysEx 0C <strip> <text>).
		var lastDb = null
		fader.mSurfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
			var text = String(value)
			if (text === lastDb) return
			lastDb = text
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0C, index], toAscii7(text), [0xF7]))
		}
		fader.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value, diff) {
			dbg('fader ' + (index + 1) + ' value ' + Number(value).toFixed(4))
		}
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
var recTitles = []
var armableSent = []

var recZone = page.mHostAccess.mMixConsole.makeMixerBankZone('Recordable')
var recFilters = ['excludeInputChannels', 'excludeOutputChannels', 'excludeGroupChannels', 'excludeFXChannels', 'excludeVCAChannels']
for (var f = 0; f < recFilters.length; ++f) {
	try { recZone[recFilters[f]]() } catch (e) { console.log('[CubaseSixty] recordable bank: ' + recFilters[f] + ' not available: ' + e) }
}

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

var recTitleCount = 0
for (var k = 0; k < REC_CHANNELS; ++k) {
	var recChannel = recZone.makeMixerBankChannel()
	var recProbe = surface.makeCustomValueVariable('recProbe' + k)
	page.makeValueBinding(recProbe, recChannel.mValue.mRecordEnable)
	;(function (slot) {
		recChannel.mOnTitleChange = function (activeDevice, activeMapping, title) {
			if (recTitles[slot] === undefined && title) {
				recTitleCount++
				if (recTitleCount === 1 || recTitleCount % 16 === 0) console.log('[CubaseSixty] recordable tracks seen: ' + recTitleCount + ' (latest: ' + title + ')')
			}
			recTitles[slot] = title
			for (var i = 0; i < STRIPS; ++i) updateArmable(activeDevice, i)
		}
	})(k)
}

// ---------------------------------------------------------------------------
// Start banked at the selected track: when the script starts, step bank by bank (a hidden control bound
// to "next bank") until the selected track's name shows on one of the eight strips. Only at start-up;
// after that the bank stays where you put it.
// ---------------------------------------------------------------------------

var SEEK_MAX_STEPS = 32
var seek = { active: true, steps: 0, titlesSinceStep: 0, selectedTitle: '', ownPresses: 0 }


function selectedIsVisible() {
	if (!seek.selectedTitle) return false
	for (var i = 0; i < STRIPS; ++i) if (stripTitles[i] === seek.selectedTitle) return true
	return false
}

function seekCheck(activeDevice) {
	if (!seek.active || !seek.selectedTitle) return
	if (selectedIsVisible()) { seek.active = false; console.log('[CubaseSixty] banked to the selected track (' + seek.selectedTitle + ')'); return }
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
	if (seek.steps >= SEEK_MAX_STEPS) { seek.active = false; console.log('[CubaseSixty] selected track not found within ' + SEEK_MAX_STEPS + ' banks'); return }
	seek.steps++
	seek.titlesSinceStep = 0
	seek.snapshot = stripTitles.slice()
	console.log('[CubaseSixty] seeking the selected track (' + seek.selectedTitle + '): next bank, step ' + seek.steps)
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
}

// Any bank key pressed by hand ends the search for good.
function stopSeekOnPress(button) {
	button.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
		if (value > 0 && seek.ownPresses > 0) { seek.ownPresses--; return }   // our own step, sent by the bridge
		if (seek.active && value > 0) { seek.active = false; console.log('[CubaseSixty] bank key pressed: start-up search stopped') }
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
