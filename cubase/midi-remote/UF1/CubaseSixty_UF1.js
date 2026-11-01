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

console.log('[CubaseSixty UF1] script loaded, version 0.9.2-dev (sends 1-8, MASTER, SHIFT automation, meter)')

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

// The strip (fader, pan, SOLO, CUT) follows the selected track, or the main output while MASTER is on.
// Two sub-pages; the bridge activates them with notes 44 (selected track) / 45 (main output).
var stripArea = page.makeSubPageArea('Strip')
var stripSelected = stripArea.makeSubPage('Selected track')
var stripMaster = stripArea.makeSubPage('Main output')

page.makeValueBinding(fader.mSurfaceValue, track.mVolume).setValueTakeOverModeJump().setSubPage(stripSelected)
page.makeValueBinding(solo.mSurfaceValue, track.mSolo).setTypeToggle().setSubPage(stripSelected)
page.makeValueBinding(cut.mSurfaceValue, track.mMute).setTypeToggle().setSubPage(stripSelected)
page.makeValueBinding(vpot.mSurfaceValue, track.mPan).setSubPage(stripSelected)
page.makeValueBinding(sel.mSurfaceValue, track.mEditorOpen).setTypeToggle()

// The main output: a one-channel bank with only output channels.
var mainOut = null
try {
	var outZone = page.mHostAccess.mMixConsole.makeMixerBankZone('Main output')
	var outFilters = ['excludeInputChannels', 'excludeAudioChannels', 'excludeInstrumentChannels', 'excludeSamplerChannels',
		'excludeMIDIChannels', 'excludeFXChannels', 'excludeGroupChannels', 'excludeVCAChannels']
	for (var of = 0; of < outFilters.length; ++of) {
		try { outZone[outFilters[of]]() } catch (e) { console.log('[CubaseSixty UF1] main output bank: ' + outFilters[of] + ' not available') }
	}
	mainOut = outZone.makeMixerBankChannel()
	page.makeValueBinding(fader.mSurfaceValue, mainOut.mValue.mVolume).setValueTakeOverModeJump().setSubPage(stripMaster)
	page.makeValueBinding(solo.mSurfaceValue, mainOut.mValue.mSolo).setTypeToggle().setSubPage(stripMaster)
	page.makeValueBinding(cut.mSurfaceValue, mainOut.mValue.mMute).setTypeToggle().setSubPage(stripMaster)
	page.makeValueBinding(vpot.mSurfaceValue, mainOut.mValue.mPan).setSubPage(stripMaster)
} catch (e) {
	console.log('[CubaseSixty UF1] main output could not be bound: ' + e)
}

function activatorButton(x, note, subPage) {
	var button = surface.makeButton(x, 7, 1, 1)
	button.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, note)
	page.makeActionBinding(button.mSurfaceValue, subPage.mAction.mActivate)
}
activatorButton(0, 44, stripSelected)
activatorButton(1, 45, stripMaster)

// V-pots 1-4 (CC 17-20, pushes notes 33-36): sends 1-4 of the selected track.
// Turn = send level, push = send on / off. The knob above the fader (CC 16) is pan, bound above.

// Screen feedback for a send V-pot: the value line shows "S1 -6.0dB", the bar under it the level.
function attachSendFeedback(surfaceValue, number) {
	attachFeedback(surfaceValue, function () { return 'S' + (number + sendOffset) + ' ' }, 0x02, false)
}

// Value line + readout bar for one control. Cubase re-sends every bound value whenever one changes, so
// only a value that really changed may take over the display (otherwise the line jumps to the last one).
// barStyle: 0x01 pointer (pan), 0x02 fill from the left (levels).
function attachFeedback(surfaceValue, prefix, barStyle, home) {
	var lastText = null
	var lastPos = null
	surfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
		var text = (typeof prefix === 'function' ? prefix() : prefix) + value + (units ? units : (barStyle === 0x02 && /^-?[0-9.]+$/.test(String(value)) ? 'dB' : ''))
		if (text === lastText) return
		lastText = text
		sendValueLine(activeDevice, text, home)
	}
	surfaceValue.mOnProcessValueChange = function (activeDevice, value) {
		if (value === lastPos) return
		lastPos = value
		sendBar(activeDevice, value, barStyle, home)
	}
}

attachFeedback(vpot.mSurfaceValue, 'Pan ', 0x01, true)   // pan is the home view; sends show for a few seconds

// 5-8 switches the V-pots between sends 1-4 and 5-8 (the bridge sends notes 53 / 54).
var sendArea = page.makeSubPageArea('Sends')
var sends14 = sendArea.makeSubPage('Sends 1-4')
var sends58 = sendArea.makeSubPage('Sends 5-8')
var sendOffset = 0
sends14.mOnActivate = function (activeDevice) { sendOffset = 0 }
sends58.mOnActivate = function (activeDevice) { sendOffset = 4 }
activatorButton(2, 53, sends14)
activatorButton(3, 54, sends58)

var selectedChannel = page.mHostAccess.mTrackSelection.mMixerChannel
for (var sendIndex = 0; sendIndex < 4; ++sendIndex) {
	var sendKnob = surface.makeKnob(6 + sendIndex * 2, 2, 2, 2)
	sendKnob.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput)
		.bindToControlChange(0, 17 + sendIndex)
		.setTypeRelativeSignedBit()
	var sendPush = surface.makeButton(6 + sendIndex * 2, 4, 2, 1)
	sendPush.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 33 + sendIndex)
	attachSendFeedback(sendKnob.mSurfaceValue, sendIndex + 1)
	var banks = [{ subPage: sends14, index: sendIndex }, { subPage: sends58, index: sendIndex + 4 }]
	for (var bk = 0; bk < banks.length; ++bk) {
		try {
			var send = selectedChannel.mSends.getByIndex(banks[bk].index)
			page.makeValueBinding(sendKnob.mSurfaceValue, send.mLevel).setSubPage(banks[bk].subPage)
			page.makeValueBinding(sendPush.mSurfaceValue, send.mOn).setTypeToggle().setSubPage(banks[bk].subPage)
		} catch (e) {
			console.log('[CubaseSixty UF1] send ' + (banks[bk].index + 1) + ' could not be bound: ' + e)
		}
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

// home = true: pan (the line's normal content, 08/09); false: a send, shown for a few seconds (04/05).
function sendValueLine(activeDevice, text, home) {
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([home ? 0x08 : 0x04], toAscii7(text), [0xF7]))
}
function sendBar(activeDevice, position01, style, home) {
	var pos = Math.max(0, Math.min(100, Math.round(position01 * 100)))
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([home ? 0x09 : 0x05, pos, style || 0x02, 0xF7]))
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

// Name and colour: the selected track's, or the main output's while MASTER is on.
var masterActive = false
var selectedInfo = { title: '', r: 0, g: 0, b: 0 }
var masterInfo = { title: 'Main Out', r: 0.5, g: 0.5, b: 0.5 }

function showStrip(activeDevice) {
	var info = masterActive ? masterInfo : selectedInfo
	sendName(activeDevice, info.title)
	sendColor(activeDevice, info.r, info.g, info.b)
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0A, 0xF7]))   // new strip: show its pan
}

var selected = page.mHostAccess.mTrackSelection.mMixerChannel
selected.mOnColorChange = function (activeDevice, activeMapping, r, g, b, a, isActive) {
	dbg('selected color: ' + r + ' ' + g + ' ' + b + ' a=' + a)
	selectedInfo.r = r; selectedInfo.g = g; selectedInfo.b = b
	if (!masterActive) sendColor(activeDevice, r, g, b)
}
selected.mOnTitleChange = function (activeDevice, activeMapping, title) {
	dbg('selected title: [' + title + ']')
	selectedInfo.title = title
	if (masterActive) return
	sendName(activeDevice, title)
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0A, 0xF7]))   // new track: show its pan
}
if (mainOut) {
	mainOut.mOnTitleChange = function (activeDevice, activeMapping, title) {
		if (title) masterInfo.title = title
		if (masterActive) sendName(activeDevice, masterInfo.title)
	}
	mainOut.mOnColorChange = function (activeDevice, activeMapping, r, g, b, a, isActive) {
		if (r || g || b) { masterInfo.r = r; masterInfo.g = g; masterInfo.b = b }
		if (masterActive) sendColor(activeDevice, masterInfo.r, masterInfo.g, masterInfo.b)
	}
}
stripMaster.mOnActivate = function (activeDevice) { masterActive = true; showStrip(activeDevice) }
stripSelected.mOnActivate = function (activeDevice) { masterActive = false; showStrip(activeDevice) }

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

// ---------------------------------------------------------------------------
// NAV cross (notes 48-52): left / right = zoom out / in, up / down = vertical zoom in / out, centre opens /
// closes the editor (like Enter).
// The bridge handles ARROW (one track) and BANK (eight tracks) itself, as channel-encoder pulses.
// ---------------------------------------------------------------------------

var navKeys = [
	{ note: 48, category: 'Zoom', command: 'Zoom In Vertically' },
	{ note: 49, category: 'Zoom', command: 'Zoom Out' },
	{ note: 50, category: 'Editors', command: 'Open/Close Editor' },
	{ note: 51, category: 'Zoom', command: 'Zoom In' },
	{ note: 52, category: 'Zoom', command: 'Zoom Out Vertically' }
]
for (var n = 0; n < navKeys.length; ++n) {
	var navButton = surface.makeButton(4 + n, 6, 1, 1)
	navButton.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, navKeys[n].note)
	page.makeCommandBinding(navButton.mSurfaceValue, navKeys[n].category, navKeys[n].command)
}

// ---------------------------------------------------------------------------
// SHIFT + transport soft keys (the bridge sends notes 56-61): the automation keys printed on them.
// < READ / > WRITE act on the selected track; CYCLE = Cross-Over, CLICK = Trim, 1 = Latch, 2 = Touch set
// Cubase's global automation mode (the same commands as the UF8's automation keys).
// ---------------------------------------------------------------------------

function noteButton(x, note) {
	var button = surface.makeButton(x, 8, 1, 1)
	button.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, note)
	return button
}
page.makeValueBinding(noteButton(0, 56).mSurfaceValue, track.mAutomationRead).setTypeToggle()
page.makeValueBinding(noteButton(1, 57).mSurfaceValue, track.mAutomationWrite).setTypeToggle()
var uf1AutoModes = [
	{ note: 58, command: 'Automation Mode - Cross-Over' },
	{ note: 59, command: 'Automation Mode - Trim' },
	{ note: 60, command: 'Automation Mode - Auto-Latch' },
	{ note: 61, command: 'Automation Mode - Touch' }
]
for (var am = 0; am < uf1AutoModes.length; ++am) {
	page.makeCommandBinding(noteButton(2 + am, uf1AutoModes[am].note).mSurfaceValue, 'Automation', uf1AutoModes[am].command)
}

// ---------------------------------------------------------------------------
// Meter: the strip's VU level (selected track, or the main output while MASTER is on) as CC 80 (0..127).
// ---------------------------------------------------------------------------

function uf1MeterSide(name, cc, selectedValue, mainValue) {
	var meter = surface.makeCustomValueVariable(name)
	page.makeValueBinding(meter, selectedValue).setSubPage(stripSelected)
	if (mainValue) page.makeValueBinding(meter, mainValue).setSubPage(stripMaster)
	var last = -1
	meter.mOnProcessValueChange = function (activeDevice, value) {
		var v = meterToMidi(value)
		if (v === last) return
		last = v
		midiOutput.sendMidi(activeDevice, [0xB0, cc, v])
	}
}
// Left = CC 80, right = CC 81 (a mono channel only reports the left side; the bridge then mirrors it).
try {
	var selectedValues = selectedChannel.mValue
	uf1MeterSide('meterL', 80, selectedValues.getVUChannelMeterByChannelIndex(0), mainOut ? mainOut.mValue.getVUChannelMeterByChannelIndex(0) : null)
	uf1MeterSide('meterR', 81, selectedValues.getVUChannelMeterByChannelIndex(1), mainOut ? mainOut.mValue.getVUChannelMeterByChannelIndex(1) : null)
} catch (e) {
	console.log('[CubaseSixty UF1] stereo meter not available (' + e + '), using one meter')
	try { uf1MeterSide('meter', 80, track.mVUMeter, mainOut ? mainOut.mValue.mVUMeter : null) } catch (e2) { console.log('[CubaseSixty UF1] meter not available: ' + e2) }
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
