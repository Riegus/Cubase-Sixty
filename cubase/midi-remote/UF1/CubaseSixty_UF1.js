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

console.log('[CubaseSixty UF1] script loaded, version 0.18.0-dev (MODE: PLUGIN = Quick Controls, DAW / SENDS = sends)')

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
// FLIP swaps fader and pan knob (the fader then controls pan), so there are four sub-pages: selected
// track / main output, each normal or flipped. The bridge activates them with notes 44 / 45 / 46 / 47.
var stripSelected = stripArea.makeSubPage('Selected track')
var stripMaster = stripArea.makeSubPage('Main output')
var stripSelectedFlip = stripArea.makeSubPage('Selected track, flipped')
var stripMasterFlip = stripArea.makeSubPage('Main output, flipped')

function bindStrip(values, normal, flipped) {
	page.makeValueBinding(fader.mSurfaceValue, values.mVolume).setValueTakeOverModeJump().setSubPage(normal)
	page.makeValueBinding(vpot.mSurfaceValue, values.mPan).setSubPage(normal)
	page.makeValueBinding(fader.mSurfaceValue, values.mPan).setValueTakeOverModeJump().setSubPage(flipped)
	page.makeValueBinding(vpot.mSurfaceValue, values.mVolume).setSubPage(flipped)
	var subs = [normal, flipped]
	for (var k = 0; k < subs.length; ++k) {
		page.makeValueBinding(solo.mSurfaceValue, values.mSolo).setTypeToggle().setSubPage(subs[k])
		page.makeValueBinding(cut.mSurfaceValue, values.mMute).setTypeToggle().setSubPage(subs[k])
	}
}
bindStrip(track, stripSelected, stripSelectedFlip)
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
	bindStrip(mainOut.mValue, stripMaster, stripMasterFlip)
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
activatorButton(4, 46, stripSelectedFlip)
activatorButton(5, 47, stripMasterFlip)

// V-pots 1-4 (CC 17-20, pushes notes 33-36): sends 1-4 of the selected track.
// Turn = send level, push = send on / off. The knob above the fader (CC 16) is pan, bound above.

// Screen feedback for a send V-pot: the value line shows "S1 -6.0dB", the bar under it the level.
// A V-pot's value line: 'S<n> <level>' for a send, '<name> <value>' for a Quick Control (PLUGIN view).
// SENDS view: each send's name, level and position also go to the bridge for the display's V-pot cells
// (SysEx 15: pot index, position 0..100, "name|level"). Sent in every view; the bridge shows them only in SENDS.
var loggedSendTitles = 0
function sendCellInfo(activeDevice, index, cell) {
	if (qcActive) return
	var text = cell.value === '' && cell.name === '' ? '' : cell.name + '|' + cell.value
	var key = cell.pos + '/' + text
	if (key === cell.sent) return
	cell.sent = key
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x15, index, cell.pos], toAscii7(text), [0xF7]))
}
function attachSendFeedback(surfaceValue, number) {
	var title = ''
	var lastText = null
	var lastPos = null
	var cell = { name: '', value: '', pos: 0, sent: null }
	surfaceValue.mOnTitleChange = function (activeDevice, objectTitle, valueTitle) {
		if (loggedSendTitles < 8) { ++loggedSendTitles; console.log('[CubaseSixty UF1] V-pot ' + number + ' title: object [' + objectTitle + '] value [' + valueTitle + ']') }
		title = valueTitle || objectTitle || ''
		if (!qcActive) { cell.name = objectTitle || valueTitle || ''; sendCellInfo(activeDevice, number - 1, cell) }
	}
	surfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
		if (!qcActive) {
			var v = String(value)
			cell.value = v === '' ? '' : v + (units ? units : (/^-?[0-9.]+$/.test(v) ? 'dB' : ''))
			sendCellInfo(activeDevice, number - 1, cell)
		}
		var text
		if (qcActive) {
			var v = String(value) + (units ? units : '')
			var room = Math.max(3, 12 - v.length - 1)
			text = (title ? String(title).substr(0, room) + ' ' : 'QC' + (number + sendOffset) + ' ') + v
		} else {
			text = 'S' + (number + sendOffset) + ' ' + value + (units ? units : (/^-?[0-9.]+$/.test(String(value)) ? 'dB' : ''))
		}
		if (text === lastText) return
		lastText = text
		sendValueLine(activeDevice, text, false)
	}
	surfaceValue.mOnProcessValueChange = function (activeDevice, value) {
		if (!qcActive) { cell.pos = Math.max(0, Math.min(100, Math.round(value * 100))); sendCellInfo(activeDevice, number - 1, cell) }
		if (value === lastPos) return
		lastPos = value
		sendBar(activeDevice, value, 0x02, false)
	}
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

// Pan is the value line's home view (sends show for a few seconds); the fader's level goes to the dB field.
// Under FLIP the two controls swap, so the pan text and bar come from the fader and the level from the knob.
var flipActive = false
var lastPanText = null, lastPanPos = null, lastLevelText = null
function panText(activeDevice, value, units) {
	var text = 'Pan ' + value + (units ? units : '')
	if (text === lastPanText) return
	lastPanText = text
	sendValueLine(activeDevice, text, true)
}
function panPos(activeDevice, value) {
	if (value === lastPanPos) return
	lastPanPos = value
	sendBar(activeDevice, value, 0x01, true)
}
function levelText(activeDevice, value) {
	if (value === lastLevelText) return
	lastLevelText = value
	sendLevel(activeDevice, value)
}
vpot.mSurfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
	if (flipActive) levelText(activeDevice, value); else panText(activeDevice, value, units)
}
vpot.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
	if (!flipActive) panPos(activeDevice, value)
}

// 5-8 switches the V-pots between sends 1-4 and 5-8 (the bridge sends notes 53 / 54).
var sendArea = page.makeSubPageArea('Sends')
var sends14 = sendArea.makeSubPage('Sends 1-4')
var sends58 = sendArea.makeSubPage('Sends 5-8')
// MODE picks the view (the bridge keeps MODE and the soft keys): PLUGIN puts the V-pots on Cubase's Quick
// Controls (of the focused plug-in window, or the track's own), 1-4 or 5-8 (notes 88 / 89).
var qc14 = sendArea.makeSubPage('Quick Controls 1-4')
var qc58 = sendArea.makeSubPage('Quick Controls 5-8')
var sendOffset = 0
var qcActive = false
sends14.mOnActivate = function (activeDevice) { sendOffset = 0; qcActive = false }
sends58.mOnActivate = function (activeDevice) { sendOffset = 4; qcActive = false }
qc14.mOnActivate = function (activeDevice) { sendOffset = 0; qcActive = true }
qc58.mOnActivate = function (activeDevice) { sendOffset = 4; qcActive = true }
activatorButton(2, 53, sends14)
activatorButton(3, 54, sends58)
activatorButton(6, 88, qc14)
activatorButton(7, 89, qc58)
var quickControls = null
try { quickControls = page.mHostAccess.mFocusedQuickControls } catch (e) { console.log('[CubaseSixty UF1] Quick Controls not available: ' + e) }

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
	if (quickControls) {
		try {
			page.makeValueBinding(sendKnob.mSurfaceValue, quickControls.getByIndex(sendIndex)).setSubPage(qc14)
			page.makeValueBinding(sendKnob.mSurfaceValue, quickControls.getByIndex(sendIndex + 4)).setSubPage(qc58)
		} catch (e) {
			console.log('[CubaseSixty UF1] Quick Control ' + (sendIndex + 1) + ' could not be bound: ' + e)
		}
	}
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
	if (flipActive) panText(activeDevice, value, units); else levelText(activeDevice, value)
}
fader.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
	if (flipActive) panPos(activeDevice, value)
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
function stripActivated(activeDevice, master, flip) {
	masterActive = master
	if (flip !== flipActive) { flipActive = flip; lastPanText = lastPanPos = lastLevelText = null }
	showStrip(activeDevice)
}
stripSelected.mOnActivate = function (activeDevice) { stripActivated(activeDevice, false, false) }
stripMaster.mOnActivate = function (activeDevice) { stripActivated(activeDevice, true, false) }
stripSelectedFlip.mOnActivate = function (activeDevice) { stripActivated(activeDevice, false, true) }
stripMasterFlip.mOnActivate = function (activeDevice) { stripActivated(activeDevice, true, true) }

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
// With SCRUB on, the bridge sends the jog as notes 63 / 64: Cubase's own jog (scrubs the audio).
var scrubLeft = pulseButton(10, 63)
var scrubRight = pulseButton(11, 64)
page.makeCommandBinding(scrubLeft.mSurfaceValue, 'Transport', 'Jog Left')
page.makeCommandBinding(scrubRight.mSurfaceValue, 'Transport', 'Jog Right')

// SHIFT + jog = previous / next marker (79 / 80); SHIFT + ARROW = step back / forward one bar (81 / 82);
// SHUTTLE (SHIFT + SCRUB) sends notes 65-78 for Cubase's shuttle speeds, reverse 8x .. 1/8x, then 1/8x .. 8x.
var moreTransport = [
	{ note: 79, command: 'Locate Previous Marker' },
	{ note: 80, command: 'Locate Next Marker' },
	{ note: 81, command: 'Step Back Bar' },
	{ note: 82, command: 'Step Bar' }
]
var shuttleSpeeds = ['Reverse 8x', 'Reverse 4x', 'Reverse 2x', 'Reverse 1x', 'Reverse 1/2x', 'Reverse 1/4x', 'Reverse 1/8x',
	'1/8x', '1/4x', '1/2x', '1x', '2x', '4x', '8x']
for (var sp = 0; sp < shuttleSpeeds.length; ++sp) moreTransport.push({ note: 65 + sp, command: 'Shuttle Play ' + shuttleSpeeds[sp] })
for (var mt = 0; mt < moreTransport.length; ++mt) {
	var mtButton = surface.makeButton(mt, 9, 1, 1)
	mtButton.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, moreTransport[mt].note)
	page.makeCommandBinding(mtButton.mSurfaceValue, 'Transport', moreTransport[mt].command)
}

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
	page.makeValueBinding(meter, selectedValue).setSubPage(stripSelectedFlip)
	if (mainValue) {
		page.makeValueBinding(meter, mainValue).setSubPage(stripMaster)
		page.makeValueBinding(meter, mainValue).setSubPage(stripMasterFlip)
	}
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

// ---------------------------------------------------------------------------
// The key above the display (note 62): bypass all inserts of the selected track, with an LED that is on while
// the track has active (non-bypassed) inserts and off when it has none.
// One insert viewer per used slot. The script reports the state to the bridge as note 62: velocity 0 = no
// inserts (LED off), 64 = inserts active (LED half lit), 127 = all bypassed (LED lit). On a press the bridge
// sends every slot its new state on MIDI channel 2, notes 0..15 (velocity 127 = bypass, 0 = active).
// (Channel 2, because notes 83..98 on channel 1 collided with the transport keys and started playback.)
// ---------------------------------------------------------------------------

;(function () {
	var SLOTS = 16
	var slots = []
	var ledOn = null
	function updateLed(activeDevice) {
		var present = false, active = false
		for (var k = 0; k < slots.length; ++k) if (slots[k].present) { present = true; if (!slots[k].bypassed) active = true }
		var vel = !present ? 0 : active ? 64 : 127
		if (vel === ledOn) return
		ledOn = vel
		midiOutput.sendMidi(activeDevice, [0x90, 62, vel])
	}
	try {
		for (var k = 0; k < SLOTS; ++k) {
			var viewer = selectedChannel.mInsertAndStripEffects.makeInsertEffectViewer('CubaseSixtyBypass' + k)
				.excludeEmptySlots().accessSlotAtIndex(k)
			var state = surface.makeCustomValueVariable('insertBypass' + k)
			page.makeValueBinding(state, viewer.mBypass)
			var slot = { present: false, bypassed: false, state: state }
			slots.push(slot)
			;(function (slot, index) {
				viewer.mOnChangePluginIdentity = function (activeDevice, activeMapping, name) {
					slot.present = !!name
					updateLed(activeDevice)
				}
				state.mOnProcessValueChange = function (activeDevice, value) {
					slot.bypassed = value > 0.5
					updateLed(activeDevice)
				}
			})(slot, k)
			var slotKey = surface.makeButton(k, 10, 1, 1)
			slotKey.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(1, k)
			page.makeValueBinding(slotKey.mSurfaceValue, viewer.mBypass)
		}
	} catch (e) {
		console.log('[CubaseSixty UF1] insert bypass via insert viewers not available: ' + e)
	}
})()

// ---------------------------------------------------------------------------
// Time display: the transport position goes to the bridge as SysEx 0D <text> and shows in the big
// display's 7-segment field. The 360 key (note 55) steps the time format, like clicking the format in
// Cubase's transport bar.
// ---------------------------------------------------------------------------

;(function () {
	var primary = null, locator = null
	try { primary = page.mHostAccess.mTransport.mTimeDisplay.mPrimary; locator = primary.mTransportLocator } catch (e) { }
	if (!locator) { console.log('[CubaseSixty UF1] time display not available in this Cubase version'); return }
	// The callback is mOnChange(activeDevice, activeMapping, time, format) (as in the community X-Touch
	// script); the arguments are checked, so a different order still works.
	var lastTime = null
	var logged = 0
	locator.mOnChange = function () {
		var strings = []
		for (var a = 0; a < arguments.length; ++a) if (typeof arguments[a] === 'string') strings.push(arguments[a])
		if (logged < 2) { ++logged; console.log('[CubaseSixty UF1] time display: ' + JSON.stringify(strings)) }
		var time = strings.length ? strings[0] : null
		if (time === null || time === lastTime) return
		lastTime = time
		midiOutput.sendMidi(arguments[0], SYSEX_HEADER.concat([0x0D], toAscii7(time), [0xF7]))
	}
	var formatKey = surface.makeButton(7, 1, 1, 1)
	formatKey.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 55)
	var holders = [locator.mAction, locator.mActions, primary.mAction, primary.mActions]
	var names = ['mNextTimeFormat', 'mCycleTimeFormat', 'mCycleTimeFormats', 'mNextFormat']
	for (var h = 0; h < holders.length; ++h) {
		if (!holders[h]) continue
		for (var n = 0; n < names.length; ++n) {
			if (holders[h][names[n]]) {
				page.makeActionBinding(formatKey.mSurfaceValue, holders[h][names[n]])
				console.log('[CubaseSixty UF1] 360 key: bound to ' + names[n])
				return
			}
		}
	}
	// No API action: use the command that swaps the primary and secondary time formats (the two clocks in
	// Cubase's transport bar), so 360 switches between, say, bars and timecode.
	page.makeCommandBinding(formatKey.mSurfaceValue, 'Transport', 'Exchange Time Formats')
	console.log('[CubaseSixty UF1] 360 key: Transport / Exchange Time Formats')
})()

// ---------------------------------------------------------------------------
// Track number for the channel-number box: Cubase does not give it, so a hidden bank of all mixer channels
// (the same channel types as the UF8 bank) watches which one is selected. Its position is the number,
// counted in MixConsole order. Sent as SysEx 0E <high 7 bits> <low 7 bits>; 0 = not found.
// ---------------------------------------------------------------------------

;(function () {
	var CHANNELS = 256
	var selectedIndex = -1
	var sentNumber = -1
	function sendNumber(activeDevice) {
		var n = selectedIndex + 1
		if (n === sentNumber) return
		sentNumber = n
		midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0E, (n >> 7) & 0x7F, n & 0x7F, 0xF7]))
	}
	try {
		var zone = page.mHostAccess.mMixConsole.makeMixerBankZone('Track numbers').excludeInputChannels().excludeOutputChannels()
			.setFollowVisibility(false)   // count hidden tracks too, so the numbers match the MixConsole's
		for (var c = 0; c < CHANNELS; ++c) {
			var ch = zone.makeMixerBankChannel()
			var sel = surface.makeCustomValueVariable('trackNumberSel' + c)
			page.makeValueBinding(sel, ch.mValue.mSelected)
			;(function (index) {
				sel.mOnProcessValueChange = function (activeDevice, value) {
					if (value > 0.5) selectedIndex = index
					else if (selectedIndex === index) selectedIndex = -1
					sendNumber(activeDevice)
				}
			})(c)
		}
	} catch (e) {
		console.log('[CubaseSixty UF1] track numbers not available: ' + e)
	}
})()

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
