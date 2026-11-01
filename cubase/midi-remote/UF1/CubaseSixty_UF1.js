// Cubase-Sixty – MIDI Remote script for the SSL UF1 (via the Cubase-Sixty Bridge)
//
// The UF1 is one channel strip, so it controls the SELECTED track:
// fader (with touch), SOLO / CUT, transport keys, SEL and the V-pot pushes.
// The MIDI it speaks is the UF8's strip-1 numbering (see cubase/docs/bridge-midi-protocol.md).
//
// Install in
//   Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\UF1\
// (tools\install-scripts.ps1 does this for you.)

var midiremote_api = require('midiremote_api_v1')

console.log('[CubaseSixty UF1] script loaded, version 0.5.2-dev (display, sends with screen feedback, channel encoder, jog)')

var deviceDriver = midiremote_api.makeDeviceDriver('CubaseSixty', 'UF1', 'Riegus')

var midiInput = deviceDriver.mPorts.makeMidiInput()
var midiOutput = deviceDriver.mPorts.makeMidiOutput()

deviceDriver
	.makeDetectionUnit()
	.detectPortPair(midiInput, midiOutput)
	.expectInputNameEquals('Cubase-Sixty UF1 to Cubase')
	.expectOutputNameEquals('Cubase-Sixty UF1 from Cubase')

var surface = deviceDriver.mSurface

// ---------------------------------------------------------------------------
// Surface layout
// ---------------------------------------------------------------------------

var fader = surface.makeFader(0, 4, 2, 8)
var faderTouch = surface.makeCustomValueVariable('faderTouch')
var solo = surface.makeButton(3, 1, 1, 1)
var cut = surface.makeButton(4, 1, 1, 1)
var sel = surface.makeButton(5, 1, 1, 1)
var vpot = surface.makeKnob(0, 2, 2, 2)

fader.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToPitchBend(0)
faderTouch.mMidiBinding
	.setInputPort(midiInput)
	.bindToNote(0, 104)
solo.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 8)
cut.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 16)
sel.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, 24)
vpot.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToControlChange(0, 16).setTypeRelativeSignedBit()

// Transport keys (notes 86/87/91..95), with LED feedback.
function transportButton(x, note) {
	var button = surface.makeButton(x, 13, 1, 1)
	button.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, note)
	return button
}
var rwd = transportButton(0, 91)
var ffw = transportButton(1, 92)
var stop = transportButton(2, 93)
var play = transportButton(3, 94)
var rec = transportButton(4, 95)
var cycle = transportButton(5, 86)
var click = transportButton(6, 87)

// ---------------------------------------------------------------------------
// Mapping: everything acts on the selected track
// ---------------------------------------------------------------------------

var page = deviceDriver.mMapping.makePage('Selected track')

var track = page.mHostAccess.mTrackSelection.mMixerChannel.mValue
var transport = page.mHostAccess.mTransport.mValue

page.makeValueBinding(fader.mSurfaceValue, track.mVolume).setValueTakeOverModeJump()
page.makeValueBinding(solo.mSurfaceValue, track.mSolo).setTypeToggle()
page.makeValueBinding(cut.mSurfaceValue, track.mMute).setTypeToggle()
page.makeValueBinding(sel.mSurfaceValue, track.mEditorOpen).setTypeToggle()
page.makeValueBinding(vpot.mSurfaceValue, track.mPan)

// V-pots 1-4 (CC 17-20, pushes notes 33-36): sends 1-4 of the selected track.
// Turn = send level, push = send on / off. The knob above the fader (CC 16) is pan, bound above.

// Screen feedback for a send V-pot: the value line shows "S1 -6.0dB", the bar under it the level.
function attachSendFeedback(surfaceValue, number) {
	// Cubase re-sends all four sends' values whenever one changes. Only a send whose value really changed
	// may take over the display, otherwise the line jumps back to the last send in the refresh (S4).
	var lastText = null
	var lastPos = null
	surfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
		var text = 'S' + number + ' ' + value + (units ? units : (/^-?[0-9.]+$/.test(String(value)) ? 'dB' : ''))
		if (text === lastText) return
		lastText = text
		sendValueLine(activeDevice, text)
	}
	surfaceValue.mOnProcessValueChange = function (activeDevice, value) {
		if (value === lastPos) return
		lastPos = value
		sendBar(activeDevice, value)
	}
}

var selectedChannel = page.mHostAccess.mTrackSelection.mMixerChannel
for (var sendIndex = 0; sendIndex < 4; ++sendIndex) {
	var sendKnob = surface.makeKnob(6 + sendIndex * 2, 2, 2, 2)
	sendKnob.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput)
		.bindToControlChange(0, 17 + sendIndex)
		.setTypeRelativeSignedBit()
	var sendPush = surface.makeButton(6 + sendIndex * 2, 4, 2, 1)
	sendPush.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 33 + sendIndex)
	try {
		var send = selectedChannel.mSends.getByIndex(sendIndex)
		page.makeValueBinding(sendKnob.mSurfaceValue, send.mLevel)
		attachSendFeedback(sendKnob.mSurfaceValue, sendIndex + 1)
		page.makeValueBinding(sendPush.mSurfaceValue, send.mOn).setTypeToggle()
	} catch (e) {
		console.log('[CubaseSixty UF1] send ' + (sendIndex + 1) + ' could not be bound: ' + e)
	}
}

// Fader touch: lets Cubase treat the fader as touch-sensitive for automation (API 1.1+).
if (fader.mSurfaceValue.mTouchState) {
	fader.mSurfaceValue.mTouchState.bindTo(faderTouch)
}

page.makeValueBinding(rwd.mSurfaceValue, transport.mRewind)
page.makeValueBinding(ffw.mSurfaceValue, transport.mForward)
page.makeValueBinding(stop.mSurfaceValue, transport.mStop).setTypeToggle()
page.makeValueBinding(play.mSurfaceValue, transport.mStart).setTypeToggle()
page.makeValueBinding(rec.mSurfaceValue, transport.mRecord).setTypeToggle()
page.makeValueBinding(cycle.mSurfaceValue, transport.mCycleActive).setTypeToggle()
page.makeValueBinding(click.mSurfaceValue, transport.mMetronomeActive).setTypeToggle()


// ---------------------------------------------------------------------------
// Display: the selected track's name, level and colour go to the bridge as SysEx
// (same header and name / colour messages as the UF8; strip 0). Command 03 = level text.
// ---------------------------------------------------------------------------

var SYSEX_HEADER = [0xF0, 0x7D, 0x52, 0x36]
var DEBUG = false

function dbg(text) {
	if (DEBUG) console.log('[CubaseSixty UF1] ' + text)
}

function toAscii7(text) {
	var out = []
	var str = String(text || '')
	for (var n = 0; n < str.length && out.length < 32; ++n) {
		var code = str.charCodeAt(n)
		out.push(code >= 0x20 && code < 0x80 ? code : 0x3F)
	}
	return out
}

function to7bit(value) {
	return Math.max(0, Math.min(127, Math.round(value * 127)))
}

function sendName(activeDevice, text) {
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x01, 0], toAscii7(text), [0xF7]))
}
function sendColor(activeDevice, r, g, b) {
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x02, 0, to7bit(r), to7bit(g), to7bit(b), 0xF7]))
}
function sendLevel(activeDevice, text) {
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x03], toAscii7(text), [0xF7]))
}

function sendValueLine(activeDevice, text) {
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x04], toAscii7(text), [0xF7]))
}
function sendBar(activeDevice, position01) {
	var pos = Math.max(0, Math.min(100, Math.round(position01 * 100)))
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x05, pos, 0xF7]))
}

// The fader is bound to the selected track's volume, so its surface value reports that
// track's level as text.
fader.mSurfaceValue.mOnTitleChange = function (activeDevice, objectTitle, valueTitle) {
	dbg('title: object=[' + objectTitle + '] value=[' + valueTitle + ']')
}
fader.mSurfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
	dbg('display value: [' + value + '] units=[' + units + ']')
	sendLevel(activeDevice, value)
}

// Name and colour of the selected track.
var selected = page.mHostAccess.mTrackSelection.mMixerChannel
selected.mOnColorChange = function (activeDevice, activeMapping, r, g, b, a, isActive) {
	dbg('selected color: ' + r + ' ' + g + ' ' + b + ' a=' + a)
	sendColor(activeDevice, r, g, b)
}
selected.mOnTitleChange = function (activeDevice, activeMapping, title) {
	dbg('selected title: [' + title + ']')
	sendName(activeDevice, title)
}

// ---------------------------------------------------------------------------
// Channel encoder (notes 40 / 41 = left / right) and jog wheel (42 / 43): Cubase commands.
// makeCommandBinding(surfaceValue, category, name) takes the command's internal category and name.
// ---------------------------------------------------------------------------

function pulseButton(x, note) {
	var button = surface.makeButton(x, 6, 1, 1)
	button.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, note)
	return button
}
var trackPrev = pulseButton(0, 40)
var trackNext = pulseButton(1, 41)
var jogLeft = pulseButton(2, 42)
var jogRight = pulseButton(3, 43)

page.makeCommandBinding(trackPrev.mSurfaceValue, 'Project', 'Select Track: Prev')
page.makeCommandBinding(trackNext.mSurfaceValue, 'Project', 'Select Track: Next')
page.makeCommandBinding(jogLeft.mSurfaceValue, 'Transport', 'Nudge Cursor Left')
page.makeCommandBinding(jogRight.mSurfaceValue, 'Transport', 'Nudge Cursor Right')
