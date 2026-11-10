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

// Log lines also go to the bridge (SysEx 7E <source>), which writes them to cubase\logs\scripts.log, so they can
// be read without Cubase's script console. They are queued and sent from the idle callback (sending needs the
// active device).
var LOG_SOURCE = 2
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

out('[CubaseSixty UF1] script loaded, version 0.24.1-dev (MODE: PLUGIN = SSL plug-in pages or Quick Controls, DAW / SENDS = sends)')

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
// Surface layout: the on-screen picture of the UF1 in Cubase's MIDI Remote tab. Positions are those of Rea-Sixty's
// drawing of the UF1 (600 x 680, its settings window), divided by 20. Keys the bridge handles itself are plain
// panels. Elements that are not a key of their own (commands and modes the bridge sends: jog modes, shuttle speeds,
// SHIFT functions, ...) sit small in rows below the unit.
// ---------------------------------------------------------------------------

function px(v) { return v / 20 }
var cmdCount = 0
function cmdButton() {
	var n = cmdCount++
	return surface.makeButton(px(10 + (n % 24) * 24.5), px(690 + Math.floor(n / 24) * 24.5), px(22), px(22))
}
function panel(x, y, w, h) { surface.makeBlindPanel(px(x), px(y), px(w), px(h)) }
panel(34, 22, 54, 20)        // SOFT
panel(26, 48, 124, 106)      // the small screen
for (var ds = 0; ds < 4; ++ds) panel(242.5 + ds * 95 - 38, 22, 76, 20)   // the display soft keys
panel(195, 50, 380, 150)     // the screen
panel(228, 296, 80, 80)      // CHANNEL encoder
panel(360, 266, 52, 24); panel(416, 266, 52, 24); panel(490, 266, 38, 24); panel(530, 266, 38, 24)   // MODE, 5-8, ARROW
panel(360, 300, 38, 24); panel(400, 300, 38, 24); panel(530, 300, 40, 24)   // BANK, SCRUB
panel(422, 392, 120, 120)    // jog wheel
panel(204, 572, 48, 28); panel(257, 572, 48, 28); panel(363, 572, 48, 28); panel(469, 572, 48, 28)   // < > CLICK 2
panel(523, 572, 46, 28)      // SHIFT
panel(16, 592, 52, 24); panel(72, 592, 52, 24)   // FOOT 1 / 2

var fader = surface.makeFader(px(108), px(214), px(24), px(262))
var faderTouch = surface.makeCustomValueVariable('faderTouch')
var solo = surface.makeButton(px(26), px(214), px(56), px(24))
var cut = surface.makeButton(px(26), px(242), px(56), px(24))
var sel = surface.makeButton(px(26), px(270), px(56), px(24))
var vpot = surface.makeKnob(px(72), px(164), px(32), px(32))

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
	// RWD FFW STOP PLAY REC along the bottom; CYCLE is the third key of the row above.
	var button = x < 5 ? surface.makeButton(px(204 + x * 82.75), px(608), px(34), px(34)) : surface.makeButton(px(310), px(572), px(48), px(28))
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
		try { outZone[outFilters[of]]() } catch (e) { out('[CubaseSixty UF1] main output bank: ' + outFilters[of] + ' not available') }
	}
	mainOut = outZone.makeMixerBankChannel()
	bindStrip(mainOut.mValue, stripMaster, stripMasterFlip)
} catch (e) {
	out('[CubaseSixty UF1] main output could not be bound: ' + e)
}

function activatorButton(x, note, subPage) {
	// MASTER and FLIP have their keys; the other two combinations come from the bridge.
	var button = x === 1 ? surface.makeButton(px(26), px(522), px(56), px(26)) : x === 4 ? surface.makeButton(px(26), px(492), px(56), px(26)) : cmdButton()
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
// The send's name is its destination, reported by the send slot itself (the knob's title is only the slot
// number and "Level"). sendNames holds sends 1-8; the cells show the four on the V-pots' page.
// The V-pots 1-4 are absolute: the bridge keeps each one's position, adds the turns itself and sends the
// new position (as Rea-Sixty owns the value in REAPER). So it needs Cubase's position: SysEx 17 <pot>
// <high 7 bits> <low 7 bits> on every change, and again every second (Cubase drops what is sent while a
// project loads).
var vpotPositions = [-1, -1, -1, -1]
function reportVpotPosition(activeDevice, index, value) {
	var v = Math.max(0, Math.min(16383, Math.round(value * 16383)))
	vpotPositions[index] = v
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x17, index, (v >> 7) & 0x7F, v & 0x7F, 0xF7]))
}
var vpotIdleLast = 0
deviceDriver.mOnIdle = function (activeDevice) {
	flushLog(activeDevice)
	var now = 0
	try { now = Date.now() } catch (e) { return }
	if (qcResendAt && now >= qcResendAt) {
		qcResendAt = 0
		if (qcActive) for (var q = 0; q < 4; ++q) sendQcCell(activeDevice, q)
	}
	if (now - vpotIdleLast < 1000) return
	vpotIdleLast = now
	for (var i = 0; i < 4; ++i) {
		var v = vpotPositions[i]
		if (v >= 0) midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x17, i, (v >> 7) & 0x7F, v & 0x7F, 0xF7]))
	}
}
var sendNames = ['', '', '', '', '', '', '', '']
var sendCells = []
// Cubase names a send "1- Reverb Vocal": drop the number, and fit the name into the cell's 9-character label
// zone the way Rea-Sixty does: first drop vowels that do not start a word (from the end), then cut.
function cellName(name) {
	var n = String(name || '').replace(/^\s*\d+\s*-\s*/, '')
	var max = 9
	if (n.length <= max) return n
	var chars = n.split('')
	for (var i = chars.length - 1; i > 0 && chars.join('').length > max; --i) {
		if (/[aeiouAEIOU]/.test(chars[i]) && chars[i - 1] !== ' ') chars.splice(i, 1)
	}
	n = chars.join('')
	if (n.length > max) n = n.replace(/ /g, '')
	return n.substr(0, max)
}
function refreshCellNames(activeDevice) {
	for (var i = 0; i < sendCells.length; ++i) {
		if (!sendCells[i]) continue
		sendCells[i].name = cellName(sendNames[sendOffset + i])
		sendCellInfo(activeDevice, i, sendCells[i])
	}
}
function sendCellInfo(activeDevice, index, cell) {
	if (qcActive) return
	var text = cell.value === '' && cell.name === '' ? '' : cell.name + '|' + cell.value
	var key = cell.pos + '/' + text
	if (key === cell.sent) return
	cell.sent = key
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x15, index, cell.pos], toAscii7(text), [0xF7]))
}
// A Quick Control's cell from what its callbacks last reported (name, value, position). Built again on each of
// them, because Cubase reports a new name and a new value separately (and not at all when it did not change).
// The header's CHANNEL field in PLUGIN without an SSL plug-in: what the Quick Controls belong to (SysEx 56), so it is
// clear which plug-in the V-pots work.
var qcSourceSent = null
function sendQcSource(activeDevice) {
	var src = ''
	for (var i = 0; i < 4 && !src; ++i) if (sendCells[i] && sendCells[i].qcObject) src = sendCells[i].qcObject
	if (src === qcSourceSent) return
	qcSourceSent = src
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x56], toAscii7(src || '-'), [0xF7]))
}
function sendQcCell(activeDevice, index) {
	var c = sendCells[index]
	if (!c) return
	if (qcActive) sendQcSource(activeDevice)
	var name = c.qcTitle || '', v = c.qcValue || ''
	qcCells[index] = [0x51, index, c.qcPos || 0, name || v ? 2 : 0].concat(toAscii7(name || v ? cellName(name || 'QC' + (index + 1 + sendOffset)) + '|' + v : ''))
	if (plug.obj < 0) midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat(qcCells[index], [0xF7]))
}
function attachSendFeedback(surfaceValue, number) {
	var title = ''
	var lastText = null
	var lastPos = null
	var cell = { name: '', value: '', pos: 0, sent: null }
	sendCells[number - 1] = cell
	surfaceValue.mOnTitleChange = function (activeDevice, objectTitle, valueTitle) {
		title = valueTitle || objectTitle || ''
		cell.qcTitle = title
		cell.qcObject = valueTitle ? (objectTitle || '') : ''   // what the Quick Control belongs to (a plug-in's name)
		if (qcActive) sendQcCell(activeDevice, number - 1)   // the name can change without the value changing
	}
	surfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
		if (!qcActive) {
			var v = String(value)
			cell.value = v === '' ? '' : v + (units ? units : (/^-?[0-9.]+$/.test(v) ? 'dB' : ''))
			sendCellInfo(activeDevice, number - 1, cell)
		}
		var text
		if (qcActive) {
			// PLUGIN view without an SSL plug-in: the Quick Control's name and value in the V-pot cell (kept, so
			// they can be sent again when the SSL plug-in goes away).
			cell.qcValue = String(value) + (units ? units : '')
			sendQcCell(activeDevice, number - 1)
		}
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
		reportVpotPosition(activeDevice, number - 1, value)
		var qp = Math.max(0, Math.min(100, Math.round(value * 100)))
		if (qcActive && qp !== cell.qcPos) { cell.qcPos = qp; sendQcCell(activeDevice, number - 1) } else cell.qcPos = qp
		if (!qcActive) { cell.pos = Math.max(0, Math.min(100, Math.round(value * 100))); sendCellInfo(activeDevice, number - 1, cell) }
		if (value === lastPos) return
		lastPos = value
		sendBar(activeDevice, value, 0x02, false)
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
// The pan knob's and the fader's last values (Cubase's, whatever they are on), for drawing them again when strip
// mode or FLIP changes who shows where; in strip mode the plug-in's fader / pan take the dB field / value line.
var knobShown = { value: null, units: '', pos: null }, faderShown = { value: null, units: '', pos: null }
function knobDisplay(activeDevice, value, units) {
	knobShown.value = value; knobShown.units = units
	if (flipActive) { if (!stripOwnsDb()) levelText(activeDevice, value) }
	else if (!stripOwnsPan()) panText(activeDevice, value, units)
}
function knobProcess(activeDevice, value) {
	knobShown.pos = value
	if (!flipActive && !stripOwnsPan()) panPos(activeDevice, value)
}
vpot.mSurfaceValue.mOnDisplayValueChange = knobDisplay
vpot.mSurfaceValue.mOnProcessValueChange = knobProcess

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
sends14.mOnActivate = function (activeDevice) { sendOffset = 0; qcActive = false; refreshCellNames(activeDevice) }
sends58.mOnActivate = function (activeDevice) { sendOffset = 4; qcActive = false; refreshCellNames(activeDevice) }
// After a switch between 1-4 and 5-8 the cells are sent once more a moment later, when Cubase has reported the
// new Quick Controls (the order of its callbacks and the activation is not fixed).
var qcResendAt = 0
qc14.mOnActivate = function (activeDevice) { sendOffset = 0; qcActive = true; qcResendAt = Date.now() + 150 }
qc58.mOnActivate = function (activeDevice) { sendOffset = 4; qcActive = true; qcResendAt = Date.now() + 150 }
activatorButton(2, 53, sends14)
activatorButton(3, 54, sends58)
activatorButton(6, 88, qc14)
activatorButton(7, 89, qc58)
var quickControls = null
try { quickControls = page.mHostAccess.mFocusedQuickControls } catch (e) { out('[CubaseSixty UF1] Quick Controls not available: ' + e) }

var selectedChannel = page.mHostAccess.mTrackSelection.mMixerChannel
for (var sendIndex = 0; sendIndex < 4; ++sendIndex) {
	var sendKnob = surface.makeKnob(px(222.5 + sendIndex * 95), px(208), px(40), px(40))
	sendKnob.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput)
		.bindToControlChange14Bit(0, 17 + sendIndex)   // absolute, 14 bit (CC 17-20 + 49-52): the bridge owns the turn
		.setTypeAbsolute()
	var sendPush = cmdButton()   // (the V-pot's push)
	sendPush.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 33 + sendIndex)
	attachSendFeedback(sendKnob.mSurfaceValue, sendIndex + 1)
	var banks = [{ subPage: sends14, index: sendIndex }, { subPage: sends58, index: sendIndex + 4 }]
	if (quickControls) {
		try {
			page.makeValueBinding(sendKnob.mSurfaceValue, quickControls.getByIndex(sendIndex)).setSubPage(qc14)
			page.makeValueBinding(sendKnob.mSurfaceValue, quickControls.getByIndex(sendIndex + 4)).setSubPage(qc58)
		} catch (e) {
			out('[CubaseSixty UF1] Quick Control ' + (sendIndex + 1) + ' could not be bound: ' + e)
		}
	}
	for (var bk = 0; bk < banks.length; ++bk) {
		try {
			var send = selectedChannel.mSends.getByIndex(banks[bk].index)
			;(function (slot) {
				send.mOnTitleChange = function (activeDevice, activeMapping, name) {
					sendNames[slot] = name || ''
					if (!qcActive && slot >= sendOffset && slot < sendOffset + 4) refreshCellNames(activeDevice)
				}
			})(banks[bk].index)
			page.makeValueBinding(sendKnob.mSurfaceValue, send.mLevel).setSubPage(banks[bk].subPage)
			page.makeValueBinding(sendPush.mSurfaceValue, send.mOn).setTypeToggle().setSubPage(banks[bk].subPage)
		} catch (e) {
			out('[CubaseSixty UF1] send ' + (banks[bk].index + 1) + ' could not be bound: ' + e)
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
	if (DEBUG) out('[CubaseSixty UF1] ' + text)
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
function faderDisplay(activeDevice, value, units) {
	faderShown.value = value; faderShown.units = units
	if (flipActive) { if (!stripOwnsPan()) panText(activeDevice, value, units) }
	else if (!stripOwnsDb()) levelText(activeDevice, value)
}
function faderProcess(activeDevice, value) {
	faderShown.pos = value
	if (flipActive && !stripOwnsPan()) panPos(activeDevice, value)
}
fader.mSurfaceValue.mOnDisplayValueChange = faderDisplay
fader.mSurfaceValue.mOnProcessValueChange = faderProcess
function redrawTrackValues(activeDevice) {
	lastPanText = lastPanPos = lastLevelText = null
	if (faderShown.value !== null) faderDisplay(activeDevice, faderShown.value, faderShown.units)
	if (faderShown.pos !== null) faderProcess(activeDevice, faderShown.pos)
	if (knobShown.value !== null) knobDisplay(activeDevice, knobShown.value, knobShown.units)
	if (knobShown.pos !== null) knobProcess(activeDevice, knobShown.pos)
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

var trackNumberForTitle = function () {}   // set by the track-number block
var sameNameHint = function () {}          // set by the track-number block
// From the UF8 script, via the bridge: which of several same-named tracks is selected (SysEx 1C).
midiInput.mOnSysex = function (activeDevice, message) {
	var m = []
	for (var i = 0; i < message.length; ++i) m.push(message[i])
	if (m.length >= 9 && m[0] === 0xF0 && m[4] === 0x53) {
		try { plugEvent(activeDevice, m[5], m[6], m[7], m.length >= 10 && m[8] === 1) } catch (e) { out('[CubaseSixty UF1] PLUGIN event failed: ' + e) }
		return
	}
	if (m[4] === 0x5E && m.length >= 7) {   // TEST: EQ graph polling on / off (Settings: uf1-eq-poll)
		eqPollOn = m[5] !== 0
		out('[CubaseSixty UF1] turn test: EQ graph polling ' + (eqPollOn ? 'on' : 'off'))
		return
	}
	if (m[4] === 0x5C && m.length >= 7) {   // a track step past hidden channels
		try { stepTrack(activeDevice, m[5] ? 1 : -1) } catch (e) { out('[CubaseSixty UF1] track step: ' + e) }
		return
	}
	if (m[4] === 0x57 && m.length >= 8) {
		// From the UC1 script, via the bridge: the strip its magnifier key picked.
		var choice = { kind: m[5], ord: m[6] }
		// A real change (the magnifier key) takes over from SHIFT + the CHANNEL encoder's pick; the UC1 repeating its
		// choice (it does after every repaint, and after a track title change cleared it here) does not.
		if (!uc1Choice || uc1Choice.kind !== choice.kind || uc1Choice.ord !== choice.ord) {
			if (uc1Choice) plug.pick = null
			uc1Choice = choice
			plug.rescanAt = Date.now() + 50
		}
		return
	}
	if ((m[4] === 0x3C || m[4] === 0x3D) && m.length >= 7) {
		try { onPresetMessage(activeDevice, m) } catch (e) { out('[CubaseSixty UF1] preset: ' + e) }
		return
	}
	if (m.length < 7 || m[0] !== 0xF0 || m[4] !== 0x1C) return
	var name = ''
	for (var j = 6; j + 2 < m.length - 1; j += 3) name += String.fromCharCode((m[j] << 14) | (m[j + 1] << 7) | m[j + 2])
	sameNameHint(activeDevice, name, m[5])
}
var selected = page.mHostAccess.mTrackSelection.mMixerChannel
selected.mOnColorChange = function (activeDevice, activeMapping, r, g, b, a, isActive) {
	dbg('selected color: ' + r + ' ' + g + ' ' + b + ' a=' + a)
	selectedInfo.r = r; selectedInfo.g = g; selectedInfo.b = b
	if (!masterActive) sendColor(activeDevice, r, g, b)
}
// Track stepping past the tracks hidden in the MixConsole (track-stepping = mixconsole, cubase\settings.txt): the
// bridge asks for a step (SysEx 5C <1 forward / 0 back>). The script reads the MixConsole's channels (Direct Access
// on the whole MixConsole: order and visibility), counts how many tracks lie between the selected one and the next
// visible one, and triggers the track selection's next / previous action that many times at once. (Stepping and
// then looking at the new track was slow: Cubase reports the new selection up to a second later.) See
// cubase\docs\banking.md.
var stepMix = null
try { stepMix = page.mHostAccess.makeDirectAccess(page.mHostAccess.mMixConsole) } catch (e) { out('[CubaseSixty UF1] no Direct Access to the MixConsole, track steps include hidden tracks: ' + e) }
var stepLast = { id: -1, at: 0 }
// The last channel the selection was on (or a step went to): from a track without a channel (a folder, the
// Input/Output folder, the ruler) a press needs to know which way the tracks are.
var stepChan = -1
// Folder tracks (and the other tracks without a channel: ruler, marker, the Input/Output folder) are not among the
// MixConsole's channels, but the next / previous track action stops on them. When a step lands on one, the script
// steps once more the same way (back, if there is no track beyond the target) and remembers it per track and
// direction, so the next step there goes past at once. The memory is dropped when the MixConsole's channels change.
var stepPending = null   // the last step, until it has landed: { from, to, dir, at, fixes, settleAt }
var stepExtra = {}       // "<from id>:<dir>" -> extra steps
var stepExtraKey = ''    // the channel list the memory belongs to
var stepListed = false   // (the channel list is logged once, when the selected track is not in it)
// Every track's channel in track order: the VST Mixer's children, except the inputs and outputs, sorted by
// getMixerChannelIndex (the track order, even where the MixConsole shows group / FX channels elsewhere).
// (Instrument tracks are "Synth", not "...Channel"; folders do not nest channels in this tree.)
function stepChannels() {
	var list = []
	var base = stepMix.getBaseObjectID(plugAm)
	var n = stepMix.getNumberOfChildObjects(plugAm, base)
	for (var i = 0; i < n && i < 1024; ++i) {
		var id = stepMix.getChildObjectID(plugAm, base, i)
		var type = ''
		try { type = stepMix.getObjectTypeName(plugAm, id) } catch (e) {}
		if (/Input|Output|ControlRoom|Monitor/.test(type)) continue
		var vis = true, idx = i
		try { vis = stepMix.isMixerChannelVisible(plugAm, id) } catch (e) {}
		try { idx = stepMix.getMixerChannelIndex(plugAm, id) } catch (e) {}
		list.push({ id: id, vis: vis, idx: idx })
	}
	list.sort(function (a, b) { return a.idx - b.idx })
	return list
}
function trackAction(forward) {
	return forward ? page.mHostAccess.mTrackSelection.mAction.mNextTrack : page.mHostAccess.mTrackSelection.mAction.mPrevTrack
}
function stepTrack(activeDevice, dir) {
	if (!plugAm) return
	var count = 1
	if (stepMix) {
		try {
			var list = stepChannels()
			var now = Date.now()
			// Cubase reports the new selection up to ~0.8 s later: until then (or while a step has not landed) a
			// press counts from where the last one went, not from the old selection (that overshot).
			var cur = (stepPending || now - stepLast.at < 1500) && stepLast.id >= 0 ? stepLast.id : plugDa.getBaseObjectID(plugAm)
			var at = -1
			for (var i = 0; i < list.length; ++i) if (list[i].id === cur) at = i
			if (at >= 0) stepChan = cur
			// On a track that is not in the list (a folder, the ruler, the Input/Output folder and the Stereo Out in
			// it), with no visible track that way from the last channel (above the first track or below the last): a
			// step that way would only go further from the tracks (10.11.2026: stuck on Input/Output / Stereo Out).
			// Step back towards the last channel instead.
			if (at < 0 && stepChan >= 0) {
				var atChan = -1
				for (var ci = 0; ci < list.length; ++ci) if (list[ci].id === stepChan) atChan = ci
				var ahead = false
				for (var cj = atChan + dir; atChan >= 0 && cj >= 0 && cj < list.length; cj += dir) if (list[cj].vis) { ahead = true; break }
				if (atChan >= 0 && !ahead) {
					out('[CubaseSixty UF1] track step: on "' + selectedInfo.title + '" (not a track in the list), no track that way; back towards the last one')
					stepLast = { id: stepChan, at: now }
					stepPending = { from: cur, to: stepChan, dir: -dir, at: now, fixes: 0 }
					trackAction(dir < 0).trigger(plugAm)
					return
				}
			}
			var key = list.map(function (c) { return c.id }).join(',')
			if (key !== stepExtraKey) { stepExtra = {}; stepExtraKey = key }
			if (at >= 0) {
				var k = at + dir
				while (k >= 0 && k < list.length && !list[k].vis) k += dir
				if (k < 0 || k >= list.length) return   // no visible track that way
				var extra = stepExtra[cur + ':' + dir] || 0   // a known folder on the way
				count = Math.abs(k - at) + extra
				stepLast = { id: list[k].id, at: now }
				stepChan = list[k].id
				// Checked where it lands, unless it already goes past a known folder.
				stepPending = extra ? null : { from: cur, to: list[k].id, dir: dir, at: now, fixes: 0 }
			}
			if (at < 0 && cur >= 0 && !stepListed) {
				out('[CubaseSixty UF1] track step: "' + selectedInfo.title + '" (object ' + cur + ') is not among the MixConsole\'s channels; hidden tracks are not skipped from it. The channels:')
				stepListed = true
				var parts = list.map(function (c) { var t = ''; try { t = stepMix.getObjectTitle(plugAm, c.id) } catch (e) {} return c.idx + ' "' + t + '" #' + c.id + (c.vis ? '' : ' (hidden)') })
				for (var q = 0; q < parts.length; q += 8) out('[CubaseSixty UF1]   ' + parts.slice(q, q + 8).join(', '))
			}
		} catch (e) { out('[CubaseSixty UF1] track step: ' + e) }
	}
	var action = trackAction(dir > 0)
	for (var c = 0; c < count; ++c) action.trigger(plugAm)
}
// Where did the step land? Looked at once Cubase's reports have stopped for a moment (page.mOnIdle): it also
// reports the tracks a step passes, late. On a track: left as it is. On a track without a channel (-1): one more
// step (back, if there is no visible track beyond the target), at most three times.
function stepCheckLanding() {
	var p = stepPending
	if (!p || !stepMix || !plugAm || Date.now() - p.at > 4000) { stepPending = null; return }
	var cur = -1
	try { cur = plugDa.getBaseObjectID(plugAm) } catch (e) {}
	if (cur === p.to) { stepChan = cur; stepPending = null; return }   // arrived
	var list = stepChannels()
	var atTo = -1, atCur = -1
	for (var i = 0; i < list.length; ++i) { if (list[i].id === p.to) atTo = i; if (list[i].id === cur) atCur = i }
	// On a track in the list: left as it is. Not in it (no channel: -1; or the Stereo Out in the Input/Output
	// folder): one more step.
	if (atCur >= 0) { stepChan = cur; stepPending = null; return }
	if (atTo < 0 || p.fixes >= 3) { stepPending = null; return }
	var beyond = false
	for (var b = atTo + p.dir; b >= 0 && b < list.length; b += p.dir) if (list[b].vis) { beyond = true; break }
	++p.fixes
	p.at = Date.now()
	if (beyond) stepExtra[p.from + ':' + p.dir] = (stepExtra[p.from + ':' + p.dir] || 0) + 1
	out('[CubaseSixty UF1] track step: on a track without a channel ("' + selectedInfo.title + '"); one ' + (beyond ? 'more' : 'back'))
	trackAction(beyond === (p.dir > 0)).trigger(plugAm)
}
selected.mOnTitleChange = function (activeDevice, activeMapping, title) {
	dbg('selected title: [' + title + ']')
	plugAm = activeMapping
	plug.rescanAt = Date.now() + 100   // the PLUGIN view: look for the new track's SSL plug-in
	uc1Choice = null                   // the UC1 says again which strip it is on
	selectedInfo.title = title
	// Cubase reports the tracks a step passes too (folders among them), late: the landing is looked at once the
	// reports have stopped for a moment (page.mOnIdle).
	if (stepPending) stepPending.settleAt = Date.now() + 450
	if (masterActive) return
	sendName(activeDevice, title)
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0A, 0xF7]))   // new track: show its pan
	trackNumberForTitle(activeDevice, title)
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
	stripModeSend(activeDevice)
	if (flip !== flipActive) { flipActive = flip; lastPanText = lastPanPos = lastLevelText = null; stripModeSend(activeDevice) }
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
	var button = cmdButton()
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
// Quick key 1 (without SHIFT) = clear every solo, as in Rea-Sixty: MIDI channel 2, note 20 (channel 1's notes
// near the transport keys started playback, see the key above the display).
var soloClear = surface.makeButton(px(416), px(572), px(48), px(28))   // quick key 1
soloClear.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(1, 20)
page.makeCommandBinding(soloClear.mSurfaceValue, 'Edit', 'Deactivate All Solo')
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
	var mtButton = cmdButton()
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
	// The NAV cross: up, left, centre, right, down.
	var navAt = [[252, 408, 32, 30], [212, 442, 36, 32], [252, 442, 32, 32], [288, 442, 36, 32], [252, 476, 32, 30]][n]
	var navButton = surface.makeButton(px(navAt[0]), px(navAt[1]), px(navAt[2]), px(navAt[3]))
	navButton.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, navKeys[n].note)
	page.makeCommandBinding(navButton.mSurfaceValue, navKeys[n].category, navKeys[n].command)
}

// ---------------------------------------------------------------------------
// Jog modes (Rea-Sixty's jog objects; the bridge keeps the mode, aims the nav cross and sends one of these per
// detent or key press): MIDI channel 2, notes 30-55. Items: Nudge Left / Right moves the selected events, the
// cross selects (Navigate). Envelope: Nudge Left / Right moves the selected automation points in time (Nudge Up / Down
// would move them to another lane; there is no command for their value). Range: the edge the cross aims at (Nudge Start / End / Top / Bottom), or the whole range (Nudge
// Left / Right). Fades: the fade-in / fade-out length of the selected events.
// ---------------------------------------------------------------------------

var jogCommands = [
	[30, 'Nudge', 'Left'], [31, 'Nudge', 'Right'], [32, 'Nudge', 'Up'], [33, 'Nudge', 'Down'],
	[34, 'Nudge', 'Start Left'], [35, 'Nudge', 'Start Right'], [36, 'Nudge', 'End Left'], [37, 'Nudge', 'End Right'],
	[38, 'Nudge', 'Top Up'], [39, 'Nudge', 'Top Down'], [40, 'Nudge', 'Bottom Up'], [41, 'Nudge', 'Bottom Down'],
	[42, 'Audio', 'Decrement Fade In Length'], [43, 'Audio', 'Increment Fade In Length'],
	[44, 'Audio', 'Decrement Fade Out Length'], [45, 'Audio', 'Increment Fade Out Length'],
	[46, 'Navigate', 'Left'], [47, 'Navigate', 'Right'], [48, 'Navigate', 'Up'], [49, 'Navigate', 'Down'],
	[50, 'Navigate', 'Add Left'], [51, 'Navigate', 'Add Right'], [52, 'Navigate', 'Add Up'], [53, 'Navigate', 'Add Down'],
	[54, 'Zoom', 'Zoom to Selection'], [55, 'Zoom', 'Undo Zoom']
]
for (var jc = 0; jc < jogCommands.length; ++jc) {
	var jogKey = cmdButton()
	jogKey.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(1, jogCommands[jc][0])
	page.makeCommandBinding(jogKey.mSurfaceValue, jogCommands[jc][1], jogCommands[jc][2])
}

// ---------------------------------------------------------------------------
// SHIFT + transport soft keys (the bridge sends notes 56-61): the automation keys printed on them.
// < READ / > WRITE act on the selected track; CYCLE = Cross-Over, CLICK = Trim, 1 = Latch, 2 = Touch set
// Cubase's global automation mode (the same commands as the UF8's automation keys).
// ---------------------------------------------------------------------------

function noteButton(x, note) {
	var button = cmdButton()
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
	out('[CubaseSixty UF1] stereo meter not available (' + e + '), using one meter')
	try { uf1MeterSide('meter', 80, track.mVUMeter, mainOut ? mainOut.mValue.mVUMeter : null) } catch (e2) { out('[CubaseSixty UF1] meter not available: ' + e2) }
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
			var slotKey = cmdButton()
			slotKey.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(1, k)
			page.makeValueBinding(slotKey.mSurfaceValue, viewer.mBypass)
		}
	} catch (e) {
		out('[CubaseSixty UF1] insert bypass via insert viewers not available: ' + e)
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
	if (!locator) { out('[CubaseSixty UF1] time display not available in this Cubase version'); return }
	// The callback is mOnChange(activeDevice, activeMapping, time, format) (as in the community X-Touch
	// script); the arguments are checked, so a different order still works.
	var lastTime = null
	var logged = 0
	locator.mOnChange = function () {
		var strings = []
		for (var a = 0; a < arguments.length; ++a) if (typeof arguments[a] === 'string') strings.push(arguments[a])
		if (logged < 2) { ++logged; out('[CubaseSixty UF1] time display: ' + JSON.stringify(strings)) }
		var time = strings.length ? strings[0] : null
		if (time === null || time === lastTime) return
		lastTime = time
		midiOutput.sendMidi(arguments[0], SYSEX_HEADER.concat([0x0D], toAscii7(time), [0xF7]))
	}
	var formatKey = surface.makeButton(px(490), px(300), px(38), px(24))   // the 360 key
	formatKey.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, 55)
	var holders = [locator.mAction, locator.mActions, primary.mAction, primary.mActions]
	var names = ['mNextTimeFormat', 'mCycleTimeFormat', 'mCycleTimeFormats', 'mNextFormat']
	for (var h = 0; h < holders.length; ++h) {
		if (!holders[h]) continue
		for (var n = 0; n < names.length; ++n) {
			if (holders[h][names[n]]) {
				page.makeActionBinding(formatKey.mSurfaceValue, holders[h][names[n]])
				out('[CubaseSixty UF1] 360 key: bound to ' + names[n])
				return
			}
		}
	}
	// No API action: use the command that swaps the primary and secondary time formats (the two clocks in
	// Cubase's transport bar), so 360 switches between, say, bars and timecode.
	page.makeCommandBinding(formatKey.mSurfaceValue, 'Transport', 'Exchange Time Formats')
	out('[CubaseSixty UF1] 360 key: Transport / Exchange Time Formats')
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
	// Found by the selected track's name in the hidden bank's names: a value binding per channel (its
	// selected state) made Cubase pause for up to 0.7 s at a time while a send was turned (traced).
	var numberTitles = []
	var currentTitle = ''
	// With several tracks of the same name: the UF8 script tells which one is selected (its rank among them,
	// SysEx 1C via the bridge; it sees the selection on its strips). Without that (no UF8, or no word yet):
	// the one nearest to the previous selection, since selection mostly moves step by step.
	var hintTitle = null, hintRank = -1
	function findNumber(activeDevice) {
		var found = -1
		if (currentTitle) {
			var candidates = []
			for (var i = 0; i < CHANNELS; ++i) if (numberTitles[i] === currentTitle) candidates.push(i)
			if (candidates.length > 1 && hintTitle === currentTitle && hintRank >= 0 && hintRank < candidates.length) found = candidates[hintRank]
			else {
				for (var c = 0; c < candidates.length; ++c) {
					var k = candidates[c]
					if (found < 0 || (selectedIndex >= 0 && Math.abs(k - selectedIndex) < Math.abs(found - selectedIndex))) found = k
				}
			}
		}
		selectedIndex = found
		sendNumber(activeDevice)
	}
	sameNameHint = function (activeDevice, title, rank) {
		hintTitle = title
		hintRank = rank
		if (title === currentTitle) findNumber(activeDevice)
	}
	trackNumberForTitle = function (activeDevice, title) { currentTitle = title; findNumber(activeDevice) }
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
			;(function (index, ch) {
				ch.mOnTitleChange = function (activeDevice, activeMapping, title) {
					if (numberTitles[index] === title) return
					numberTitles[index] = title
					if (title && title === currentTitle) findNumber(activeDevice)
				}
			})(c, ch)
		}
	} catch (e) {
		out('[CubaseSixty UF1] track numbers not available: ' + e)
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

// ---------------------------------------------------------------------------
// PLUGIN view: the SSL plug-in on the selected track, as Rea-Sixty (and SSL 360) show it. The V-pots, the four
// display soft keys and ◄ ► work the plug-in's controls in pages (SSL's UF1 User Guide p188 tables, as measured
// for Rea-Sixty on SSL 360 2.1.12). The plug-in is found and driven through Direct Access, its parameters by name.
// Without an SSL plug-in on the track the V-pots stay on Cubase's Quick Controls.
//
// To the bridge:
//   50 <present 0/1> <page> <pages> <eq in 0/1> <ordinal> <model…>    the plug-in shown (model "CS 2", "4K E" …)
//   51 <pot> <position 0-100> <flags: 1 bipolar, 2 used> <label|value> one V-pot cell
//   52 <key> <state: 0 off, 1 on, 2 no switch> <label…>               one soft key (0x7F = the Ø glyph)
// From the bridge (PLUGIN view, with a plug-in present):
//   53 <kind> <a> <b>   kind 0 = V-pot a turned by b - 64 detents, 1 = V-pot a pushed, 2 = soft key a pressed,
//                       3 = page (a = 0 back, 1 forward)
// ---------------------------------------------------------------------------

var plugAm = null
var uc1Choice = null   // the UC1's strip on the selected track (SysEx 57): { kind 0 none / 1 SSL / 2 Cubase's, ord }
var qcCells = [null, null, null, null]   // the Quick Controls' cells (SysEx 51), for the PLUGIN view without an SSL plug-in
var plugDa = null
try { plugDa = page.mHostAccess.makeDirectAccess(selectedChannel) } catch (e) { out('[CubaseSixty UF1] no Direct Access, PLUGIN view stays on Quick Controls: ' + e) }

// Rea-Sixty's strip types: 0 Channel Strip 2, 1 4K B, 2 4K E, 3 360 Link, 4 Bus Compressor 2, 5 4K G, 6 360 Link Bus
// Compressor. The first match in this order wins (the Link bus compressor's name also contains "SSL 360 Link").
var PLUG_TYPES = [
	{ match: 'SSL 360 Link Bus', type: 6, model: 'BC 2', strip: false },
	{ match: 'Bus Compressor 2', type: 4, model: 'BC 2', strip: false },
	{ match: 'Channel Strip 2', type: 0, model: 'CS 2', strip: true },
	{ match: '4K G', type: 5, model: '4K G', strip: true },
	{ match: '4K E', type: 2, model: '4K E', strip: true },
	{ match: '4K B', type: 1, model: '4K B', strip: true },
	{ match: 'SSL 360 Link', type: 3, model: 'Link', strip: true }
]
var PLUG_PAGES = [8, 10, 10, 8, 2, 10, 2]
// V-pot pages: [parameter name, label, bipolar]. A name matches the parameter of that name, else the first one
// containing it (case ignored).
function vp(param, label, bipolar) { return { param: param, label: label, bipolar: !!bipolar } }
var NONE = null
var EMPTY4 = [NONE, NONE, NONE, NONE]
var STRIP_EQ_PAGES = [
	[vp('LF Gain', 'LF Gain', true), vp('LF Freq', 'LF Freq'), NONE, NONE],
	[vp('LMF Gain', 'LMF Gain', true), vp('LMF Freq', 'LMF Freq'), vp('LMF Q', 'LMF Q'), NONE],
	[vp('HMF Gain', 'HMF Gain', true), vp('HMF Freq', 'HMF Freq'), vp('HMF Q', 'HMF Q'), NONE],
	[vp('HF Gain', 'HF Gain', true), vp('HF Freq', 'HF Freq'), NONE, NONE]
]
var VPOT_PAGES = {
	0: [[vp('Width', 'Width'), NONE, vp('Output Trim', 'Out Trim', true), vp('Compressor Mix', 'Comp Mix')],
		[vp('Input Trim', 'In Trim', true), NONE, vp('High Pass', 'HighPass'), vp('Low Pass', 'Low Pass')]].concat(STRIP_EQ_PAGES, [
		[vp('Compressor Ratio', 'Ratio'), vp('Compressor Threshold', 'Thresh'), vp('Compressor Release', 'Release'), NONE],
		[vp('Gate Range', 'Range'), vp('Gate Threshold', 'Thresh'), vp('Gate Release', 'Release'), vp('Gate Hold', 'Hold')]]),
	1: [[vp('Width', 'Width'), vp('Mic', 'Mic'), vp('Output Trim', 'Out Trim', true), vp('Compressor Mix', 'Comp Mix')],
		[vp('Input Trim', 'In Trim', true), NONE, vp('High Pass', 'HighPass'), vp('Low Pass', 'Low Pass')]].concat(STRIP_EQ_PAGES, [
		[vp('Compressor Ratio', 'Ratio'), vp('Compressor Threshold', 'Thresh'), NONE, vp('Compressor Release', 'Release')],
		[vp('Gate Range', 'Range'), vp('Gate Threshold', 'Thresh'), vp('Gate Release', 'Release'), NONE], EMPTY4, EMPTY4]),
	2: [[vp('Width', 'Width'), vp('Mic', 'Mic'), vp('Output Trim', 'Out Trim', true), vp('Compressor Mix', 'Mix')],
		[vp('Input Trim', 'In Trim', true), NONE, vp('High Pass', 'HighPass'), vp('Low Pass', 'Low Pass')]].concat(STRIP_EQ_PAGES, [
		[vp('Compressor Ratio', 'Ratio'), vp('Compressor Threshold', 'Thresh'), NONE, vp('Compressor Release', 'Release')],
		[vp('Gate Range', 'Range'), vp('Gate Threshold', 'Thresh'), vp('Gate Release', 'Release'), NONE], EMPTY4, EMPTY4]),
	3: [[vp('Width', 'Width'), vp('Saturation Amount', 'Sat Amt'), vp('Output Trim', 'Out Trim', true), vp('Comp Mix', 'Comp Mix')],
		[vp('Input Trim', 'In Trim', true), NONE, vp('High Pass', 'HighPass'), vp('Low Pass', 'Low Pass')]].concat(STRIP_EQ_PAGES, [
		[vp('Comp Ratio', 'Ratio'), vp('Comp Threshold', 'Thresh'), vp('Comp Release', 'Release'), NONE],
		[vp('Gate Range', 'Range'), vp('Gate Threshold', 'Thresh'), vp('Gate Release', 'Release'), vp('Gate Hold', 'Hold')]]),
	4: [[vp('Threshold', 'Thresh'), vp('Ratio', 'Ratio'), vp('Attack', 'Attack'), vp('Release', 'Release')],
		[vp('Makeup', 'Makeup'), vp('Dry/Wet', 'Mix'), vp('Sidechain HPF', 'S/C HPF'), NONE]],
	5: [[vp('Width', 'Width'), vp('Mic', 'Mic'), vp('Output Trim', 'Out Trim', true), vp('Compressor Mix', 'Mix')],
		[vp('Input Trim', 'In Trim', true), vp('Impedance', 'Impedance'), vp('High Pass', 'HighPass'), vp('Low Pass', 'Low Pass')]].concat(STRIP_EQ_PAGES, [
		[vp('Compressor Ratio', 'Ratio'), vp('Compressor Threshold', 'Thresh'), NONE, vp('Compressor Release', 'Release')],
		[vp('Gate Range', 'Range'), vp('Gate Threshold', 'Thresh'), vp('Gate Release', 'Release'), NONE], EMPTY4, EMPTY4]),
	6: [[vp('Threshold', 'Thresh'), vp('Ratio', 'Ratio'), vp('Attack', 'Attack'), vp('Release', 'Release')],
		[vp('Makeup', 'Makeup'), vp('Mix', 'Mix'), vp('S/C HPF', 'S/C HPF'), NONE]]
}
// Soft-key pages: [parameter name or null (label only, no switch here), label]. 'Ø' = SSL's phase glyph.
function sk(param, label) { return { param: param, label: label } }
var SK0 = sk(null, '')
var SK_EMPTY = [SK0, SK0, SK0, SK0]
function eqKeys(type, lf, hf) {
	// The four EQ pages: LF BELL / HF BELL on key 1 of the LF / HF pages, the EQ type or colour and EQ IN on 3 / 4.
	var k3 = type === 1 ? SK0 : type === 3 ? sk('EQ Type', 'EQ TYPE') : type === 0 ? sk('EQ Type', 'E') : sk('EQ Colour', 'EQ COLOUR')
	var k4 = type === 3 ? sk('EQ In', 'EQ IN') : sk('EQ In', 'EQ')
	return [[lf, SK0, k3, k4], [SK0, SK0, k3, k4], [SK0, SK0, k3, k4], [hf, SK0, k3, k4]]
}
var SOFT_KEYS = {
	0: [[sk('Polarity', 'Ø'), SK0, sk(null, 'SOLO SAFE'), sk(null, 'PLUG-IN')],
		[sk(null, 'S/C MODE'), SK0, sk(null, 'HQ MODE'), sk(null, 'A/B')]].concat(eqKeys(0, sk('LF Type', 'LF BELL'), sk('HF Type', 'HF BELL')), [
		[sk('Compressor Fast Attack', 'FAST ATTACK'), sk('Compressor Peak', 'PEAK'), sk('S/C Listen', 'S/C LISTEN'), sk('Dynamics In', 'DYNAMICS')],
		[sk('Gate Expander', 'EXPAND'), sk('Gate Attack', 'FAST ATTACK'), SK0, sk('Dynamics In', 'DYNAMICS')]]),
	1: [[sk('Polarity', 'Ø'), sk('Pre IN', 'PRE'), sk(null, 'SOLO SAFE'), sk(null, 'PLUG-IN')],
		[sk('Filters In', 'FILTERS'), SK0, sk(null, 'HQ MODE'), sk(null, 'A/B')]].concat(eqKeys(1, sk('LF Type', 'LF BELL'), sk('HF Type', 'HF BELL')), [
		[SK0, SK0, SK0, sk('Dynamics In', 'DYN')],
		[SK0, sk('Gate Expander', 'EXPANDER'), sk('S/C Listen', 'S/C LISTEN'), sk('Dynamics In', 'DYN')],
		[sk('Compressor Auto Make-up', 'AUTO MAKEUP'), SK0, SK0, SK0],
		[sk('Gate Expander', 'EXPANDER'), SK0, SK0, SK0]]),
	2: [[sk('Polarity', 'Ø'), sk('Pre', 'PRE'), sk(null, 'SOLO SAFE'), sk(null, 'PLUG-IN')],
		[sk('Filters In', 'FILTERS'), SK0, sk(null, 'HQ MODE'), sk(null, 'A/B')]].concat(eqKeys(2, sk('LF Type', 'LF BELL'), sk('HF Type', 'HF BELL')), [
		[sk('Compressor Fast Attack', 'FAST ATTACK'), SK0, SK0, sk('Dynamics In', 'DYN')],
		[sk('Gate Fast Attack', 'FAST ATTACK'), sk('Gate Expander', 'EXPANDER'), sk('S/C Listen', 'S/C LISTEN'), sk('Dynamics In', 'DYN')],
		[sk('Compressor Auto Make-up', 'AUTO MAKEUP'), SK0, SK0, SK0],
		[sk('Gate Expander', 'EXPANDER'), SK0, SK0, SK0]]),
	3: [[sk('Phase', 'Ø'), sk('Saturation In', 'SATURATION IN'), sk(null, 'SOLO SAFE'), sk(null, 'PLUG-IN')],
		[sk('Listen', 'LISTEN'), SK0, SK0, SK0]].concat(eqKeys(3, sk('LF Type', 'LF TYPE'), sk('HF Type', 'HF TYPE')), [
		[sk('Comp Fast Attack', 'CMP FST ATTK'), sk('Comp Peak', 'COMP PEAK'), sk('Listen', 'LISTEN'), sk('Dynamics In', 'DYNAMICS IN')],
		[sk('Gate Expander', 'GTE EXPANDR'), sk('Gate Attack', 'GATE ATTACK'), SK0, sk('Dynamics In', 'DYNAMICS IN')]]),
	4: [[sk('External S/C', 'EXT S/C'), sk('Oversampling', 'OVERSAMP'), SK0, sk(null, 'A/B')], SK_EMPTY],
	5: [[sk('Polarity', 'Ø'), sk('Pre', 'PRE'), sk(null, 'SOLO SAFE'), sk(null, 'PLUG-IN')],
		[sk('Filters In', 'FILTERS'), sk('Impedance In', 'IMP IN'), sk(null, 'HQ MODE'), sk(null, 'A/B')]].concat(eqKeys(5, sk('LF Type', 'LF BELL'), sk('HF Type', 'HF BELL')), [
		[sk('Compressor Fast Attack', 'FAST ATTACK'), SK0, SK0, sk('Dynamics In', 'DYN')],
		[sk('Gate Fast Attack', 'FAST ATTACK'), sk('Gate Expander', 'EXPANDER'), sk('S/C Listen', 'S/C LISTEN'), sk('Dynamics In', 'DYN')],
		[sk('Compressor Auto Make-up', 'AUTO MAKEUP'), SK0, SK0, SK0],
		[sk('Gate Expander', 'EXPANDER'), SK0, SK0, SK0]]),
	6: [SK_EMPTY, SK_EMPTY]
}
var PLUG_STEP = 0.02   // per detent (Rea-Sixty's kUf1CsVPotStep: about 50 detents end to end)

var plug = { obj: -1, kind: null, title: '', ordinal: 0, page: 0, params: [], byName: {}, rescanAt: 0,
	sentInfo: null, sentCells: [null, null, null, null], sentKeys: [null, null, null, null], wanted: {}, missing: {} }

function plugParam(name) {
	if (!name || plug.obj < 0) return null
	if (plug.byName[name] !== undefined) return plug.byName[name]
	var want = name.toLowerCase(), hit = null
	for (var i = 0; i < plug.params.length && !hit; ++i) if (plug.params[i].lower === want) hit = plug.params[i]
	for (var j = 0; j < plug.params.length && !hit; ++j) if (plug.params[j].lower.indexOf(want) >= 0) hit = plug.params[j]
	plug.byName[name] = hit
	if (!hit && !plug.missing[name]) { plug.missing[name] = true; out('[CubaseSixty UF1] PLUGIN: "' + plug.title + '" has no parameter "' + name + '"') }
	return hit
}
function plugValue(p) { return plugDa.getParameterProcessValue(plugAm, plug.obj, p.tag) }
function plugPages() { return plug.kind ? PLUG_PAGES[plug.kind.type] : 1 }
function plugVpots() { return plug.kind ? (VPOT_PAGES[plug.kind.type][plug.page] || EMPTY4) : EMPTY4 }
function plugKeys() {
	if (!plug.kind) return SK_EMPTY
	var keys = (SOFT_KEYS[plug.kind.type][plug.page] || SK_EMPTY).slice()
	// 4K G: key 1 of the LF and HF pages follows the EQ colour (Pink = the G EQ, whose ÷3 / ×3 switches it is).
	if (plug.kind.type === 5 && (plug.page === 2 || plug.page === 5)) {
		var colour = plugParam('EQ Colour')
		if (colour && plugValue(colour) < 0.5) keys[0] = plug.page === 2 ? sk('LMF div3', 'LMF DIV3') : sk('HMF x3', 'HMF X3')
	}
	return keys
}

// Every object under the selected channel (as the UC1 script walks it), for the SSL plug-ins in its inserts.
// Inside the channel the input filter, the EQ and the strip are walked too (Cubase's own EQ for the graph), as the
// UC1 script does; the rest cannot hold what is looked for.
var PLUG_SKIP = { QuickControls: 1, Panner: 1, Modulators: 1, Sends: 1, Outputs: 1, Foldbacks: 1, DirectRouting: 1 }
function plugWalk(base) {
	var list = [], seen = {}
	function rec(id, depth, parentType, parent) {
		if (list.length >= 300 || seen[id]) return
		seen[id] = true
		var type = ''
		try { type = plugDa.getObjectTypeName(plugAm, id) } catch (e) {}
		list.push({ id: id, type: type, parent: parent })
		var me = list.length - 1
		if (depth >= 4 || (/Channel$/.test(parentType) && PLUG_SKIP[type])) return
		var n = 0
		try { n = plugDa.getNumberOfChildObjects(plugAm, id) } catch (e) { return }
		for (var i = 0; i < n && i < 256; ++i) rec(plugDa.getChildObjectID(plugAm, id, i), depth + 1, type, me)
	}
	rec(base, 0, '', -1)
	return list
}
function plugRescan(activeDevice) {
	if (!plugDa || !plugAm) return
	var base
	try { base = plugDa.getBaseObjectID(plugAm) } catch (e) { return }
	var objects = plugWalk(base)
	var strip = null, busComp = null, stripOrd = 0, eqObj = -1, filterObj = -1, sslOrd = 0, strips = [], all = []
	for (var i = 0; i < objects.length; ++i) {
		if (objects[i].type === 'EQ' && eqObj < 0) eqObj = objects[i].id
		if (objects[i].type === 'InputFilter' && filterObj < 0) filterObj = objects[i].id
		var title = '', n = 0
		try { title = plugDa.getObjectTitle(plugAm, objects[i].id) } catch (e) {}
		try { n = plugDa.getNumberOfParameters(plugAm, objects[i].id) } catch (e) {}
		if (!title || n < 4) continue
		var kind = null
		for (var k = 0; k < PLUG_TYPES.length && !kind; ++k) if (title.indexOf(PLUG_TYPES[k].match) >= 0) kind = PLUG_TYPES[k]
		if (!kind) continue
		// The insert slot it sits in (slots titled "1".."16", under the track's inserts): for opening its window.
		var slotIndex = -1, pa = objects[i].parent
		if (pa >= 0 && objects[pa].type === 'Slot') {   // (as the UC1 script finds it for its 360 key)
			try { slotIndex = parseInt(plugDa.getObjectTitle(plugAm, objects[pa].id), 10) - 1 } catch (e) {}
			if (!(slotIndex >= 0 && slotIndex < 16)) slotIndex = -1
		}
		var entry = { id: objects[i].id, kind: kind, title: title, ordinal: 0, sslOrd: sslOrd, slot: slotIndex }
		all.push(entry)
		// The channel strips' position among the track's channel strips: how the bridge tells their EQ curves apart.
		if (kind.strip) { entry.ordinal = stripOrd; strips.push(entry); if (!strip) strip = entry; ++stripOrd }
		else if (!busComp) busComp = entry
		++sslOrd   // the position among the track's SSL plug-ins, as the UC1 script counts it
	}
	plug.all = all
	plug.base = base
	// The strip the UC1 is on (magnifier key), when it has said so for this track: the same SSL strip, or Cubase's
	// own (then no SSL strip here: Quick Controls and Cubase's EQ).
	if (uc1Choice && uc1Choice.kind === 2) { strip = null; busComp = null }
	else if (uc1Choice && uc1Choice.kind === 1) for (var si = 0; si < strips.length; ++si) if (strips[si].sslOrd === uc1Choice.ord) strip = strips[si]
	// SHIFT + the CHANNEL encoder picked one (newer than the UC1's choice): that one, or none (Quick Controls).
	if (plug.pick && plug.pick.base === base) {
		var picked = null
		for (var pk = 0; pk < all.length; ++pk) if (all[pk].id === plug.pick.id) picked = all[pk]
		if (plug.pick.id === 'cubase') { strip = null; busComp = null }
		else if (picked) { strip = picked.kind.strip ? picked : null; busComp = picked.kind.strip ? null : picked }
	}
	var found = strip || busComp
	plugReportSlot(activeDevice, found ? found.slot : -1)
	var id = found ? found.id : -1
	// Cubase's own channel EQ and input filter, for the EQ graph on tracks without an SSL channel strip.
	cubaseEq.eq = eqObj
	cubaseEq.filter = filterObj
	cubaseEq.tags = {}
	cubaseEq.sent = null
	if (strip) cubaseEq.eq = cubaseEq.filter = -1   // the SSL strip's own curve is drawn instead
	else {
		var lists = [[eqObj, 'eq'], [filterObj, 'in']]
		for (var li = 0; li < lists.length; ++li) {
			var o = lists[li][0]
			if (o < 0) continue
			var cnt = 0
			try { cnt = plugDa.getNumberOfParameters(plugAm, o) } catch (e) {}
			for (var pi = 0; pi < cnt && pi < 64; ++pi) {
				var tg = plugDa.getParameterTagByIndex(plugAm, o, pi)
				var tt = ''
				try { tt = plugDa.getParameterTitle(plugAm, o, tg, 32) } catch (e) {}
				if (tt) cubaseEq.tags[lists[li][1] + ':' + tt] = tg
			}
		}
	}
	var eqWhere = (eqObj >= 0 ? 'EQ' : 'no EQ') + ', ' + (filterObj >= 0 ? 'input filter' : 'no input filter')
	if (!strip && eqWhere !== cubaseEq.logged) { cubaseEq.logged = eqWhere; out('[CubaseSixty UF1] EQ graph: Cubase channel (' + eqWhere + ', ' + Object.keys(cubaseEq.tags).length + ' parameters)') }
	cubaseEqSend(activeDevice)
	if (id !== plug.obj) {
		plug.obj = id
		plug.kind = found ? found.kind : null
		plug.title = found ? found.title : ''
		plug.ordinal = found ? found.ordinal : 0
		plug.page = 0
		plug.byName = {}
		plug.wanted = {}
		plug.params = []
		if (found) {
			var count = plugDa.getNumberOfParameters(plugAm, id)
			for (var p = 0; p < count && p < 256; ++p) {
				var tag = plugDa.getParameterTagByIndex(plugAm, id, p)
				var t = ''
				try { t = plugDa.getParameterTitle(plugAm, id, tag, 48) } catch (e) {}
				plug.params.push({ tag: tag, title: t, lower: String(t).toLowerCase() })
			}
		}
		out('[CubaseSixty UF1] PLUGIN: ' + (found ? '"' + found.title + '" (' + found.kind.model + ', ' + plug.params.length + ' parameters, insert slot ' + (found.slot >= 0 ? found.slot + 1 : 'not found') + ')' : 'no SSL plug-in, V-pots on Quick Controls'))
	}
	plugPaint(activeDevice, true)
	stripModeSend(activeDevice)
}

function plugSend(activeDevice, body) { midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat(body, [0xF7])) }
// The insert slot of the plug-in the PLUGIN view is on (SysEx 58 <slot 0-15, 7F = none>): the CHANNEL encoder's push
// opens / closes its window (PLUGIN event kind 5: the script moves an insert viewer there), or the track's channel
// editor without one.
var slotReported = null
function plugReportSlot(activeDevice, slot) {
	var v = slot >= 0 ? slot : 0x7F
	if (v === slotReported) return
	slotReported = v
	plugSend(activeDevice, [0x58, v])
}
// SHIFT + CHANNEL encoder: the next / previous SSL plug-in on the track, then none (Quick Controls), wrapping.
function plugStep(activeDevice, dir) {
	var list = (plug.all || []).map(function (e) { return e.id }).concat(['cubase'])
	var cur = list.indexOf(plug.obj >= 0 ? plug.obj : 'cubase')
	if (cur < 0) cur = list.length - 1
	var next = ((cur + dir) % list.length + list.length) % list.length
	plug.pick = { base: plug.base, id: list[next] }
	plugRescan(activeDevice)
	// The UC1 follows to a channel strip (or Cubase's own, for "none"; a bus comp leaves it where it is): SysEx 59
	// <kind> <ordinal>, on to the UC1 script as 45.
	var e = null
	for (var i = 0; i < (plug.all || []).length; ++i) if (plug.all[i].id === list[next]) e = plug.all[i]
	if (list[next] === 'cubase') plugSend(activeDevice, [0x59, 2, 0])
	else if (e && e.kind.strip) plugSend(activeDevice, [0x59, 1, e.sslOrd & 0x7F])
}
function plugPaint(activeDevice, force) {
	if (force) { plug.sentInfo = null; plug.sentCells = [null, null, null, null]; plug.sentKeys = [null, null, null, null] }
	var present = plug.obj >= 0 ? 1 : 0
	var eqIn = 0
	// The parameters on show (EQ In, the cells, the keys): only their changes repaint (mOnParameterChange).
	var watch = {}
	if (present && plug.kind.strip) { var eq = plugParam('EQ In'); eqIn = eq && plugValue(eq) >= 0.5 ? 1 : 0; if (eq) watch[eq.tag] = true }
	var info = [0x50, present, plug.page, plugPages(), eqIn, plug.ordinal & 0x7F].concat(toAscii7(present ? plug.kind.model : ''), [0x00], toAscii7(present ? plug.title : ''))
	var infoKey = info.join(',')
	if (infoKey !== plug.sentInfo) {
		plug.sentInfo = infoKey
		plugSend(activeDevice, info)
		// No SSL plug-in (any more): the cells go back to the Quick Controls.
		if (!present) for (var q = 0; q < 4; ++q) if (qcCells[q]) plugSend(activeDevice, qcCells[q])
	}
	if (!present) return
	var pots = plugVpots()
	for (var i = 0; i < 4; ++i) { plugCell(activeDevice, i, pots[i]); var cp = pots[i] ? plugParam(pots[i].param) : null; if (cp) watch[cp.tag] = true }
	var keys = plugKeys()
	plug.watch = watch
	for (var k = 0; k < 4; ++k) {
		var key = keys[k]
		var p = key.param ? plugParam(key.param) : null
		if (p) watch[p.tag] = true
		var st = !p ? 2 : plugValue(p) >= 0.5 ? 1 : 0
		if (key.label === 'PLUG-IN' && stripFaderParam()) st = stripMode.on ? 1 : 0   // strip mode (below)
		var label = []
		for (var c = 0; c < key.label.length; ++c) { var code = key.label.charCodeAt(c); label.push(code === 0xD8 ? 0x7F : code < 0x80 ? code : 0x3F) }
		var body = [0x52, k, st].concat(label)
		var keyKey = body.join(',')
		if (keyKey === plug.sentKeys[k]) continue
		plug.sentKeys[k] = keyKey
		plugSend(activeDevice, body)
	}
}
function plugCell(activeDevice, i, slot) {
	var p = slot ? plugParam(slot.param) : null
	var body
	if (!p) body = [0x51, i, 0, 0]
	else {
		var v = plugValue(p)
		var value = String(plugDa.getParameterDisplayValue(plugAm, plug.obj, p.tag))
		var units = ''
		try { units = plugDa.getParameterDisplayUnits(plugAm, plug.obj, p.tag) || '' } catch (e) {}
		if (units && value.indexOf(units) < 0) value += units
		body = [0x51, i, Math.max(0, Math.min(100, Math.round(v * 100))), (slot.bipolar ? 1 : 0) | 2].concat(toAscii7(slot.label + '|' + value))
	}
	var key = body.join(',')
	if (key === plug.sentCells[i]) return
	plug.sentCells[i] = key
	plugSend(activeDevice, body)
}

function plugEvent(activeDevice, kind, a, b, fine) {
	if (kind === 4 && plugAm) { plugStep(activeDevice, a ? 1 : -1); return }
	if (kind === 5) { windowToggle(activeDevice); return }
	if (plug.obj < 0 || !plugAm) return
	if (kind === 3) {
		var n = plugPages()
		plug.page = (plug.page + (a ? 1 : n - 1)) % n
		plugPaint(activeDevice, false)
		return
	}
	if (kind === 0 || kind === 1) {
		var slot = plugVpots()[a]
		var p = slot ? plugParam(slot.param) : null
		if (!p) return
		var v
		if (kind === 1) {
			if (!slot.bipolar) return
			v = 0.5   // push: a gain / trim back to 0 dB
		} else {
			// A parameter with a few steps (a ratio) snaps a small change back, so while the V-pot turns (steps less
			// than 1 s apart) the turn is added up here instead of read back from the plug-in.
			var now = Date.now(), w = plug.wanted[p.tag]
			var from = w && now - w.at < 1000 ? w.value : plugValue(p)
			v = Math.max(0, Math.min(1, from + (b - 64) * PLUG_STEP * (fine ? 0.25 : 1)))
			plug.wanted[p.tag] = { value: v, at: now }
		}
		plugDa.setParameterProcessValue(plugAm, plug.obj, p.tag, v)
		plugCell(activeDevice, a, slot)
		return
	}
	if (kind === 6) { stripFaderMove(activeDevice, (a << 7) | b); return }
	if (kind === 7) { stripPanTurn(activeDevice, b - 64); return }   // strip mode: the pan knob
	if (kind === 2) {
		var key = plugKeys()[a]
		if (key && key.label === 'PLUG-IN' && stripFaderParam()) { stripMode.on = !stripMode.on; stripModeSend(activeDevice); plugPaint(activeDevice, false); return }
		var q = key && key.param ? plugParam(key.param) : null
		if (!q) return
		plugDa.setParameterProcessValue(plugAm, plug.obj, q.tag, plugValue(q) >= 0.5 ? 0 : 1)
		plugPaint(activeDevice, false)
	}
}

// Strip mode (Rea-Sixty's SSL Strip Mode on the UF1, the PLUG-IN key on the PLUGIN view's first page): the fader is
// on the SSL channel strip's own fader / output gain instead of the track's volume. The bridge sends the hand's
// position as PLUGIN event kind 6 (14 bit in a / b) and ignores Cubase's volume while it is on (SysEx 5A <0/1>); the
// strip's fader comes back as SysEx 5B <msb> <lsb> for the motor, its value in the dB field. On a track without
// an SSL channel strip (or one without such a parameter) the fader stays on volume; the mode stays on for the next.
var stripMode = { on: false, sent: false, param: null, pan: null, panSent: false, fp: null, pp: null }
var STRIP_PAN_NAMES = ['pan', 'panning', 'stereo pan', 'pan/bal', 'balance']
var STRIP_FADER_NAMES = ['Fader Level', 'Fader', 'Output Gain', 'Out Gain', 'Output Level']
function stripFaderParam() {
	if (plug.obj < 0 || !plug.kind || !plug.kind.strip) return null
	for (var i = 0; i < STRIP_FADER_NAMES.length; ++i) {
		var want = STRIP_FADER_NAMES[i].toLowerCase()
		for (var j = 0; j < plug.params.length; ++j) if (plug.params[j].lower === want) return plug.params[j]
	}
	return null
}
function stripPanParam() {
	if (plug.obj < 0 || !plug.kind || !plug.kind.strip) return null
	for (var i = 0; i < STRIP_PAN_NAMES.length; ++i)
		for (var j = 0; j < plug.params.length; ++j) if (plug.params[j].lower === STRIP_PAN_NAMES[i]) return plug.params[j]
	return null
}
// Without FLIP the fader is on the plug-in's fader and the pan knob on its pan; with FLIP the other way round (the
// plug-in stays in control). stripMode.param = what the fader moves, stripMode.pan = what the pan knob moves.
function stripModeSend(activeDevice) {
	var live = stripMode.on && !masterActive
	var fp = live ? stripFaderParam() : null, pp = live ? stripPanParam() : null
	var p = flipActive ? pp : fp, k = flipActive ? fp : pp
	var on = !!p, knob = !!k
	var changed = on !== stripMode.sent || knob !== stripMode.panSent || p !== stripMode.param || k !== stripMode.pan
	stripMode.param = p
	stripMode.pan = k
	stripMode.fp = fp
	stripMode.pp = pp
	stripMode.sent = on
	stripMode.panSent = knob
	if (changed) {
		plugSend(activeDevice, [0x5A, on ? 1 : 0, knob ? 1 : 0])
		out('[CubaseSixty UF1] strip mode: fader on ' + (on ? '"' + p.title + '"' : 'the track') + ', pan knob on ' + (knob ? '"' + k.title + '"' : 'the track'))
		redrawTrackValues(activeDevice)
	}
	if (on) stripFaderSend(activeDevice)
	if (knob) stripPanSend(activeDevice)
}
function stripOwnsDb() { return !!stripMode.fp && (stripMode.param === stripMode.fp || stripMode.pan === stripMode.fp) }
function stripOwnsPan() { return !!stripMode.pp && (stripMode.param === stripMode.pp || stripMode.pan === stripMode.pp) }
// A plug-in parameter shown where its kind belongs: its fader in the dB field, its pan on the value line.
function stripShow(activeDevice, p) {
	var text = ''
	try { text = String(plugDa.getParameterDisplayValue(plugAm, plug.obj, p.tag)).replace(/\s+/g, ' ').trim() } catch (e) {}
	if (p === stripMode.pp) {
		lastPanText = null; lastPanPos = null
		sendValueLine(activeDevice, 'Pan ' + text, true)
		sendBar(activeDevice, plugValue(p), 0x01, true)
	} else sendLevel(activeDevice, text)
}
function stripFaderSend(activeDevice) {
	var p = stripMode.param
	if (!p || !plugAm) return
	var v = Math.round(Math.max(0, Math.min(1, plugValue(p))) * 16383)
	plugSend(activeDevice, [0x5B, (v >> 7) & 0x7F, v & 0x7F])
	stripShow(activeDevice, p)
}
function stripPanSend(activeDevice) {
	if (stripMode.pan && plugAm) stripShow(activeDevice, stripMode.pan)
}
function stripPanTurn(activeDevice, delta) {
	var p = stripMode.pan
	if (!p || !plugAm) return
	plugDa.setParameterProcessValue(plugAm, plug.obj, p.tag, Math.max(0, Math.min(1, plugValue(p) + delta / 100)))
	stripShow(activeDevice, p)
}
function stripFaderMove(activeDevice, v14) {
	var p = stripMode.param
	if (!p || !plugAm) return
	plugDa.setParameterProcessValue(plugAm, plug.obj, p.tag, v14 / 16383)
	stripShow(activeDevice, p)
}

if (plugDa) {
	plugDa.mOnParameterChange = function (activeDevice, activeMapping, objectID, tag) {
		plugAm = activeMapping
		// Only a parameter on show repaints (a plug-in can report many changes, e.g. under automation).
		if (objectID === plug.obj && (!plug.watch || plug.watch[tag])) plugPaint(activeDevice, false)
		if (objectID === plug.obj && stripMode.sent && stripMode.param && tag === stripMode.param.tag) stripFaderSend(activeDevice)
		if (objectID === plug.obj && stripMode.panSent && stripMode.pan && tag === stripMode.pan.tag) stripPanSend(activeDevice)
		if (objectID === cubaseEq.eq || objectID === cubaseEq.filter) cubaseEqSend(activeDevice)
	}
	plugDa.mOnObjectChange = function (activeDevice, activeMapping, objectID) {
		plugAm = activeMapping
		plug.rescanAt = Date.now() + 300
	}
}
page.mOnActivate = function (activeDevice, activeMapping) {
	plugAm = activeMapping
	plugSend(activeDevice, [0x54])   // ask the bridge for the V-pot page of the view it shows
	plug.obj = -2   // look again and send everything
	slotReported = null
	plug.rescanAt = Date.now() + 200
}
var eqPollAt = 0
// TEST (10.11.2026, V-pots / encoders not smooth since 6.11): the EQ polling can be switched off from the Settings
// page (SysEx 5E), and every 10 s the script logs how long the EQ reads took and the longest wait between two idle
// calls (a long wait = Cubase busy, and the V-pots' values wait too).
var eqPollOn = true
var idleStat = { since: 0, last: 0, calls: 0, maxGap: 0, eqReads: 0, eqMs: 0, eqMax: 0 }
function idleStats() {
	var now = Date.now()
	var st = idleStat
	if (!st.since) { st.since = now; st.last = now; return }
	st.maxGap = Math.max(st.maxGap, now - st.last)
	st.last = now
	++st.calls
	if (now - st.since < 10000) return
	out('[CubaseSixty UF1] turn test: ' + st.calls + ' idle calls in ' + (now - st.since) + ' ms, longest wait ' + st.maxGap +
		' ms; EQ polling ' + (eqPollOn ? 'on: ' + st.eqReads + ' reads, ' + st.eqMs + ' ms in all, longest ' + st.eqMax + ' ms' : 'off'))
	idleStat = { since: now, last: now, calls: 0, maxGap: 0, eqReads: 0, eqMs: 0, eqMax: 0 }
}
page.mOnIdle = function (activeDevice, activeMapping) {
	idleStats()
	if (stepPending && stepPending.settleAt && Date.now() >= stepPending.settleAt) {
		stepPending.settleAt = 0
		try { stepCheckLanding() } catch (e) { out('[CubaseSixty UF1] track step: ' + e) }
	}
	plugAm = activeMapping
	// Cubase's EQ is also read here, about seven times a second: its changes do not reliably arrive as parameter
	// changes.
	// Only a changed band list is sent.
	var t = Date.now()
	if (eqPollOn && t >= eqPollAt && (cubaseEq.eq >= 0 || cubaseEq.filter >= 0)) {
		eqPollAt = t + 150
		try { cubaseEqSend(activeDevice) } catch (e) { if (!cubaseEq.failed) { cubaseEq.failed = true; out('[CubaseSixty UF1] EQ graph: ' + e) } }
		var took = Date.now() - t
		++idleStat.eqReads
		idleStat.eqMs += took
		idleStat.eqMax = Math.max(idleStat.eqMax, took)
	}
	windowIdle(activeDevice)
	if (plug.rescanAt && Date.now() >= plug.rescanAt) {
		plug.rescanAt = 0
		try { plugRescan(activeDevice) } catch (e) { out('[CubaseSixty UF1] PLUGIN scan failed: ' + e) }
	}
}

// The plug-in window: one insert viewer (16 fixed-slot viewers made every track change lag), moved by the script
// itself (its actions can be triggered directly). On the CHANNEL encoder's push (PLUGIN event kind 5) it goes to the
// first slot, then on by the PLUGIN view's slot number, and the window is toggled once the viewer reports the
// plug-in it is on and that is the one shown (else one slot on per idle tick, then back; Cubase moves the viewer
// with a delay, so steps sent as MIDI notes landed on the wrong slot).
var editWin = { viewer: null, state: null, open: 0, name: '', job: null }
try {
	editWin.viewer = selected.mInsertAndStripEffects.makeInsertEffectViewer('CubaseSixtyEdit')
	var editState = surface.makeCustomValueVariable('pluginWindow')
	page.makeValueBinding(editState, editWin.viewer.mEdit)
	editWin.state = editState
	editState.mOnProcessValueChange = function (activeDevice, value) { editWin.open = value }
	editWin.viewer.mOnChangePluginIdentity = function (activeDevice, activeMapping, name) { editWin.name = name || '' }
} catch (e) { out('[CubaseSixty UF1] plug-in window switch could not be set up: ' + e) }
function windowToggle(activeDevice) {
	if (!editWin.viewer || !plugAm || plug.obj < 0) { out('[CubaseSixty UF1] plug-in window: no plug-in shown'); return }
	var slot = -1
	for (var i = 0; i < (plug.all || []).length; ++i) if (plug.all[i].id === plug.obj) slot = plug.all[i].slot
	if (slot < 0) { out('[CubaseSixty UF1] plug-in window: the insert slot of "' + plug.title + '" is not known'); return }
	try {
		editWin.viewer.mAction.mReset.trigger(plugAm)
		for (var k = 0; k < slot; ++k) editWin.viewer.mAction.mNext.trigger(plugAm)
	} catch (e) { out('[CubaseSixty UF1] plug-in window: ' + e); return }
	editWin.job = { title: plug.title, at: Date.now() + 60, tries: 0 }
}
function windowIdle(activeDevice) {
	var job = editWin.job
	if (!job || Date.now() < job.at) return
	// Not there yet: on by one slot (16 tries), then back by one (16 more), in case the start was not slot 1.
	if (editWin.name && job.title.indexOf(editWin.name) < 0 && editWin.name.indexOf(job.title) < 0 && job.tries < 32) {
		++job.tries
		job.at = Date.now() + 60
		try { (job.tries <= 16 ? editWin.viewer.mAction.mNext : editWin.viewer.mAction.mPrev).trigger(plugAm) } catch (e) { editWin.job = null }
		return
	}
	editWin.job = null
	out('[CubaseSixty UF1] plug-in window: "' + job.title + '", viewer on "' + editWin.name + '" after ' + job.tries + ' extra steps, window ' + (editWin.open >= 0.5 ? 'open: closing' : 'closed: opening'))
	if (job.tries >= 32) { out('[CubaseSixty UF1] plug-in window: "' + job.title + '" not found in the inserts'); return }
	// Through the value variable bound to the viewer's edit switch (increment / decrement on the host value did
	// nothing: a small step on a switch).
	try { editWin.state.setProcessValue(activeDevice, editWin.open >= 0.5 ? 0 : 1) } catch (e) { out('[CubaseSixty UF1] plug-in window: ' + e) }
}

// ---------------------------------------------------------------------------
// Presets: the bridge's preset browser (SHIFT + 5-8) reads the SSL preset file and sends each value (SysEx 3C <section>
// <id> 7F 7F 7F <value>, then 3D <section> <name>); they go into the plug-in the PLUGIN view shows. The same way as the
// UC1 script loads them: an SSL preset names its parameters by their JUCE ids, whose Direct Access tag is the id's
// Java-style string hash (31 * h + c, sign bit cleared) plus 4201; a value is a choice's index or a plain number in the
// parameter's unit (set as its display value and checked, else found on the display).
// ---------------------------------------------------------------------------
function juceTag(id) {
	var h = 0
	for (var i = 0; i < id.length; ++i) h = (Math.imul(31, h) + id.charCodeAt(i)) | 0
	return (h & 0x7FFFFFFF) + 4201
}
function leadingNumber(text) {
	var r = /^\s*\+?(-?\d*\.?\d+(e[-+]?\d+)?)/i.exec(text || '')
	return r ? parseFloat(r[1]) : NaN
}
var presetLoad = { applied: 0, skipped: 0, unknown: 0, failed: [] }
function applyPresetValue(id, text) {
	if (plug.obj < 0 || !plugAm) { ++presetLoad.skipped; return }
	var acc = plugDa, am = plugAm, obj = plug.obj, tag = juceTag(id)
	var title = ''
	try { title = acc.getParameterTitle(am, obj, tag, 40) } catch (e) {}
	if (!title || title === '-') { ++presetLoad.unknown; return }   // view state / a placeholder, not a parameter
	var v = parseFloat(text)
	if (isNaN(v)) { ++presetLoad.skipped; presetLoad.failed.push(title + '=' + text); return }
	var plainMax = acc.convertParameterProcessValueToPlain(am, obj, tag, 1)
	if (plainMax > 1.0001) {   // a choice: the value is its index
		acc.setParameterProcessValue(am, obj, tag, acc.convertParameterPlainToProcessValue(am, obj, tag, Math.round(v)))
		++presetLoad.applied
		return
	}
	if ((v === 0 || v === 1) && Math.abs(plainMax - 1) < 1e-6) {
		// A switch, or a continuous parameter at an end: try it as a switch first.
		var before = acc.getParameterProcessValue(am, obj, tag)
		acc.setParameterDisplayValue(am, obj, tag, String(v))
		var shown = leadingNumber(acc.getParameterDisplayValue(am, obj, tag))
		if (!isNaN(shown) && Math.abs(shown - v) < 1e-6) { ++presetLoad.applied; return }
		acc.setParameterProcessValue(am, obj, tag, v)
		var disp = acc.getParameterDisplayValue(am, obj, tag)
		if (isNaN(leadingNumber(disp)) || Math.abs(leadingNumber(disp) - v) < 1e-6) { ++presetLoad.applied; return }
		acc.setParameterProcessValue(am, obj, tag, before)
	}
	var at = function (n) { acc.setParameterProcessValue(am, obj, tag, n); return acc.getParameterDisplayValue(am, obj, tag) }
	var probe = function () {
		var pts = []
		for (var k = 0; k <= 64; ++k) { var d = at(k / 64); pts.push({ n: k / 64, text: d, num: leadingNumber(d) }) }
		return pts
	}
	// A stepped parameter whose file value is its index: a short list of distinct readings, and the index picks one.
	var pts = null
	if (v === Math.round(v) && v >= 0) {
		pts = probe()
		var steps = []
		for (var q = 0; q < pts.length; ++q) if (!steps.length || steps[steps.length - 1].text !== pts[q].text) steps.push(pts[q])
		if (steps.length <= 24 && v < steps.length) {
			var byValue = null
			for (var w = 0; w < steps.length; ++w) if (!isNaN(steps[w].num) && Math.abs(steps[w].num - v) < 1e-6) byValue = steps[w]
			acc.setParameterProcessValue(am, obj, tag, (byValue || steps[v]).n)
			++presetLoad.applied
			return
		}
	}
	// A number in the parameter's unit: let the plug-in parse it, check, else bisect on the display.
	acc.setParameterDisplayValue(am, obj, tag, String(v))
	var got = leadingNumber(acc.getParameterDisplayValue(am, obj, tag))
	if (!isNaN(got) && Math.abs(got - v) <= Math.max(Math.abs(v) * 0.02, 0.05)) { ++presetLoad.applied; return }
	if (!pts) pts = probe()
	var lo = null, hi = null
	for (var z = 0; z + 1 < pts.length; ++z) {
		var p0 = pts[z], p1 = pts[z + 1]
		if (isNaN(p0.num) || isNaN(p1.num)) continue
		if ((p0.num - v) * (p1.num - v) <= 0) { lo = p0; hi = p1; break }
	}
	if (!lo) {
		// Outside the numbered part: an end that reads "OFF" (a filter at its end) takes values beyond the range.
		var first = null, last = null
		for (var y = 0; y < pts.length; ++y) if (!isNaN(pts[y].num)) { if (!first) first = pts[y]; last = pts[y] }
		var offEnd = null
		if (first && isNaN(pts[0].num) && (first.num > last.num ? v > first.num : v < first.num)) offEnd = pts[0]
		if (last && isNaN(pts[pts.length - 1].num) && (last.num > first.num ? v > last.num : v < last.num)) offEnd = pts[pts.length - 1]
		if (offEnd) { acc.setParameterProcessValue(am, obj, tag, offEnd.n); ++presetLoad.applied; return }
		++presetLoad.skipped; presetLoad.failed.push(title + '=' + text); return
	}
	var a = lo.n, b = hi.n, asc = hi.num > lo.num, bestN = lo.n, bestErr = Math.abs(lo.num - v)
	for (var it = 0; it < 16; ++it) {
		var n = (a + b) / 2, num = leadingNumber(at(n))
		if (isNaN(num)) break
		if (Math.abs(num - v) < bestErr) { bestErr = Math.abs(num - v); bestN = n }
		if ((num < v) === asc) a = n; else b = n
	}
	acc.setParameterProcessValue(am, obj, tag, bestN)
	if (bestErr <= Math.max(Math.abs(hi.num - lo.num), Math.abs(v) * 0.02, 1e-6)) ++presetLoad.applied
	else { ++presetLoad.skipped; presetLoad.failed.push(title + '=' + text) }
}
function decodeParts(message, from) {
	var text = '', parts = [], i = from
	for (; i + 2 < message.length && message[i] !== 0xF7; i += 3) {
		if (message[i] === 0x7F && message[i + 1] === 0x7F && message[i + 2] === 0x7F) { parts.push(text); text = ''; continue }
		text += String.fromCharCode((message[i] << 14) | (message[i + 1] << 7) | message[i + 2])
	}
	parts.push(text)
	return parts
}
function onPresetMessage(activeDevice, message) {
	if (message[4] === 0x3C) {
		var parts = decodeParts(message, 6)
		try { applyPresetValue(parts[0], parts[1] || '') } catch (e) { ++presetLoad.skipped; presetLoad.failed.push(parts[0] + ': ' + e) }
		return
	}
	var name = decodeParts(message, 6)[0]
	out('[CubaseSixty UF1] preset "' + name + '" into ' + (plug.obj >= 0 ? '"' + plug.title + '"' : 'nothing (no SSL plug-in)') + ': '
		+ presetLoad.applied + ' set, ' + presetLoad.skipped + ' not set, ' + presetLoad.unknown + ' not parameters'
		+ (presetLoad.failed.length ? ' (not set: ' + presetLoad.failed.slice(0, 12).join(', ') + ')' : ''))
	presetLoad = { applied: 0, skipped: 0, unknown: 0, failed: [] }
	if (plug.obj >= 0) plugPaint(activeDevice, false)
}

// ---------------------------------------------------------------------------
// The EQ graph for Cubase's own channel EQ (tracks without an SSL channel strip). Cubase does not send a curve the
// way SSL's plug-ins do, so the script sends the EQ's settings and the bridge draws the curve from them (Rea-Sixty's
// renderer, Uf1EqCurve). SysEx 55 <text>: "<kind> <Hz> <dB> <Q> <order>" per band, ';' between bands; kinds B bell,
// L / H low / high shelf, P / C high pass (low cut) / low pass (high cut). Empty = nothing switched on (flat).
// The four bands count when on (an inverted band, or Invert All, turns its gain round) and the EQ not bypassed; the
// input filter's low / high cut when on (order from the slope, 6 dB per octave each).
// ---------------------------------------------------------------------------
var cubaseEq = { eq: -1, filter: -1, tags: {}, sent: null }
function cubaseEqParam(key) {
	var tag = cubaseEq.tags[key]
	if (tag === undefined) return null
	var obj = key.charAt(0) === 'e' ? cubaseEq.eq : cubaseEq.filter
	return { process: plugDa.getParameterProcessValue(plugAm, obj, tag), text: String(plugDa.getParameterDisplayValue(plugAm, obj, tag)) }
}
function eqNumber(text) {
	var r = /(-?\d*\.?\d+)\s*(k?)/i.exec(text || '')
	if (!r) return NaN
	return parseFloat(r[1]) * (r[2] ? 1000 : 1)
}
function cubaseEqSend(activeDevice) {
	if (!plugDa || !plugAm) return
	var bands = []
	if (cubaseEq.eq >= 0) {
		var bypass = cubaseEqParam('eq:Bypass')
		var invertAll = cubaseEqParam('eq:Invert All')
		if (!bypass || bypass.process < 0.5) {
			for (var b = 1; b <= 4; ++b) {
				var on = cubaseEqParam('eq:EQ ' + b + ' On')
				if (!on || on.process < 0.5) continue
				var type = cubaseEqParam('eq:EQ ' + b + ' Type'), gain = cubaseEqParam('eq:EQ ' + b + ' Gain')
				var freq = cubaseEqParam('eq:EQ ' + b + ' Freq'), q = cubaseEqParam('eq:EQ ' + b + ' Q')
				var inv = cubaseEqParam('eq:Invert ' + b)
				var t = type ? type.text : 'Parametric'
				var kind = /low\s*shelf/i.test(t) ? 'L' : /high\s*shelf/i.test(t) ? 'H' : /high\s*pass|low\s*cut/i.test(t) ? 'P'
					: /low\s*pass|high\s*cut/i.test(t) ? 'C' : 'B'
				var g = gain ? eqNumber(gain.text) : 0
				if (((inv && inv.process >= 0.5) ? 1 : 0) ^ ((invertAll && invertAll.process >= 0.5) ? 1 : 0)) g = -g
				var f = freq ? eqNumber(freq.text) : 1000
				var qv = q ? eqNumber(q.text) : 1
				var order = /\bII\b/.test(t) ? 4 : 2   // the cut types: I = 12, II = 24 dB per octave (approximate)
				if (!isNaN(f)) bands.push(kind + ' ' + f + ' ' + (isNaN(g) ? 0 : g) + ' ' + (isNaN(qv) || qv <= 0 ? 1 : qv) + ' ' + order)
			}
		}
	}
	if (cubaseEq.filter >= 0) {
		var fb = cubaseEqParam('in:Bypass')
		if (!fb || fb.process < 0.5) {
			var cuts = [['LC', 'P'], ['HC', 'C']]
			for (var c = 0; c < cuts.length; ++c) {
				var con = cubaseEqParam('in:' + cuts[c][0] + '-On')
				if (!con || con.process < 0.5) continue
				var cf = cubaseEqParam('in:' + cuts[c][0] + '-Freq'), cs = cubaseEqParam('in:' + cuts[c][0] + '-Slope')
				var hz = cf ? eqNumber(cf.text) : NaN
				var slope = cs ? eqNumber(cs.text) : 12
				if (!isNaN(hz)) bands.push(cuts[c][1] + ' ' + hz + ' 0 0.7 ' + Math.max(1, Math.min(8, Math.round((isNaN(slope) ? 12 : slope) / 6))))
			}
		}
	}
	var text = bands.join(';')
	if (text === cubaseEq.sent) return
	cubaseEq.sent = text
	if (cubaseEq.bandCount !== bands.length) { cubaseEq.bandCount = bands.length; out('[CubaseSixty UF1] EQ graph: ' + bands.length + ' band(s) on' + (bands.length ? ': ' + text : '')) }
	// "-" for none: a SysEx with nothing after the command is too short for the bridge to take.
	plugSend(activeDevice, [0x55].concat(asciiAll(text || '-')))
}
// toAscii7 stops at 32 characters; the band list is longer.
function asciiAll(text) {
	var r = []
	for (var i = 0; i < text.length; ++i) { var c = text.charCodeAt(i); r.push(c >= 0x20 && c < 0x80 ? c : 0x3F) }
	return r
}

// Closing a project (Cubase shows the Hub) deactivates the driver, opening one activates it again: the bridge shows
// "No Project" meanwhile (SysEx 7C 00 / 01); what the scripts send on the next project paints the surface again.
deviceDriver.mOnDeactivate = function (activeDevice) {
	out('[CubaseSixty UF1] inactive (no project)')
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x7C, 0x00, 0xF7]))
	try { flushLog(activeDevice) } catch (e) {}
}
deviceDriver.mOnActivate = function (activeDevice) { midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x7C, 0x01, 0xF7])) }
