// Cubase-Sixty – MIDI Remote script for SSL UF8 (via the Cubase-Sixty Bridge)
//
// Draft v0: eight faders with touch, SEL/SOLO/CUT, banking and transport.
// The MIDI it speaks is defined in cubase/docs/bridge-midi-protocol.md.
//
// Cubase looks for this file in
//   Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\SSL\
// See the repo README for linking that folder to this one.

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
var SCRIPT_VERSION = '0.1.0-dev (ports: to Cubase / from Cubase)'
function dbg(text) {
	if (DEBUG) console.log('[CubaseSixty] ' + text)
}
console.log('[CubaseSixty] script loaded, version ' + SCRIPT_VERSION)

var deviceDriver = midiremote_api.makeDeviceDriver('CubaseSixty', 'SSL', 'Riegus')

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
		}
		// Known Cubase limit: this only fires when a track is selected or banked in, not the
		// moment its colour is changed (tested on Cubase 15 with several other colour hooks).
		channel.mOnColorChange = function (activeDevice, activeMapping, r, g, b, a, isActive) {
			dbg('color strip ' + (index + 1) + ': r=' + r + ' g=' + g + ' b=' + b + ' a=' + a + ' active=' + isActive)
			sendStripColor(activeDevice, index, r, g, b)
		}
	})(i)

	;(function (index, fader) {
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

