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
var SCRIPT_VERSION = '0.12.0-dev (ports: to Cubase / from Cubase)'
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
	// SEL in the AUTO selection mode (the bridge sends it on its own note): the track's automation Write.
	strip.autoWrite = surface.makeButton(x, 15, 2, 1)

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
	strip.autoWrite.mSurfaceValue.mMidiBinding
		.setInputPort(midiInput).setOutputPort(midiOutput)
		.bindToNote(0, 112 + i)
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

// What the faders and the V-pots are on: two sub-page areas, switched by the bridge (MIDI channel 2):
//   faders: note 80 = track volume, 81 = track pan (FLIP), 82-89 = send 1-8 of each track (SEND / PLUGIN 1-8,
//           Rea-Sixty's "Send N -> all tracks"; CUT = that send on / off)
//   V-pots: note 90 = track pan, 91 = track volume (FLIP), 92 = sends 1-8 of the selected track (long FLIP,
//           Rea-Sixty's "8 sends of focused track")
// The strip's name shows the send's destination while a send view is on (the faders' one wins).
var faderArea = page.makeSubPageArea('Faders')
var vpotArea = page.makeSubPageArea('VPots')
var faderVol = faderArea.makeSubPage('Volume')
var faderPan = faderArea.makeSubPage('Pan')
var faderSend = []
for (var fs = 0; fs < 8; ++fs) faderSend.push(faderArea.makeSubPage('Send' + (fs + 1)))
var vpotPan = vpotArea.makeSubPage('Pan')
var vpotVol = vpotArea.makeSubPage('Volume')
var vpotSends = vpotArea.makeSubPage('SelectedSends')
var route = { fader: 'vol', send: -1, vpot: 'pan' }   // fader: vol / pan / send (send = 0..7); vpot: pan / vol / sends
var uf8FlipActive = false   // (the fader on pan)
var flipResets = []
var sendTitles = []       // [strip][send]: each banked track's send destinations
var selSendTitles = []    // [send]: the selected track's
for (var st = 0; st < 8; ++st) sendTitles.push(['', '', '', '', '', '', '', ''])
var routeSelected = page.mHostAccess.mTrackSelection.mMixerChannel
function routeChanged(activeDevice) {
	uf8FlipActive = route.fader === 'pan'
	for (var r = 0; r < flipResets.length; ++r) flipResets[r]()
	for (var i = 0; i < STRIPS; ++i) paintStripName(activeDevice, i)
	if (stripModeOn) stripModeUpdateAll(activeDevice)   // FLIP and the send views take the fader back
}
// The name a strip shows: the send's destination in a send view, else the track's.
function paintStripName(activeDevice, index) {
	var text = stripTitles[index] || ''
	// (A track without that send shows a blank name, not an empty one: the bridge takes an empty name as no track
	// on the strip and switches its LEDs off.)
	if (route.fader === 'send') text = text ? (sendTitles[index][route.send] || ' ') : ''
	else if (route.vpot === 'sends') text = selSendTitles[index] || ' '
	sendStripName(activeDevice, index, text)
}
;(function () {
	function key(note, sub, apply) {
		var k = surface.makeButton(30 + (note - 80) % 6, 15 + Math.floor((note - 80) / 6), 1, 1)
		k.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(1, note)
		page.makeActionBinding(k.mSurfaceValue, sub.mAction.mActivate)
		sub.mOnActivate = function (activeDevice) { apply(); routeChanged(activeDevice) }
	}
	key(80, faderVol, function () { route.fader = 'vol' })
	key(81, faderPan, function () { route.fader = 'pan' })
	for (var n = 0; n < 8; ++n) (function (n) { key(82 + n, faderSend[n], function () { route.fader = 'send'; route.send = n }) })(n)
	key(90, vpotPan, function () { route.vpot = 'pan' })
	key(91, vpotVol, function () { route.vpot = 'vol' })
	key(92, vpotSends, function () { route.vpot = 'sends' })
	for (var s = 0; s < 8; ++s) (function (s) {
		var slot = routeSelected.mSends.getByIndex(s)
		slot.mOnTitleChange = function (activeDevice, activeMapping, title) {
			selSendTitles[s] = title || ''
			if (route.vpot === 'sends' && route.fader !== 'send') paintStripName(activeDevice, s)
		}
	})(s)
})()

// SSL soft keys (see the end of the file): one Direct Access per strip, onto its channel.
var skDa = [], skStrip = [], skAm = null, skFocus = null, skRescanAt = 0
var trackDb = [], dbResend = []
var trackPan = [], panResend = [], stripPanShow = []
for (var i = 0; i < STRIPS; ++i) {
	var channel = bankZone.makeMixerBankChannel()
	var strip = strips[i]

	page.makeValueBinding(strip.fader.mSurfaceValue, channel.mValue.mVolume)
		.setValueTakeOverModeJump().setSubPage(faderVol)
	page.makeValueBinding(strip.fader.mSurfaceValue, channel.mValue.mPan)
		.setValueTakeOverModeJump().setSubPage(faderPan)
	page.makeValueBinding(strip.vpot.mSurfaceValue, channel.mValue.mPan).setSubPage(vpotPan)
	page.makeValueBinding(strip.vpot.mSurfaceValue, channel.mValue.mVolume).setSubPage(vpotVol)
	page.makeValueBinding(strip.vpot.mSurfaceValue, routeSelected.mSends.getByIndex(i).mLevel).setSubPage(vpotSends)
	page.makeValueBinding(strip.cut.mSurfaceValue, channel.mValue.mMute).setTypeToggle().setSubPage(faderVol)
	page.makeValueBinding(strip.cut.mSurfaceValue, channel.mValue.mMute).setTypeToggle().setSubPage(faderPan)
	for (var sn = 0; sn < 8; ++sn) {
		var sendSlot = channel.mSends.getByIndex(sn)
		page.makeValueBinding(strip.fader.mSurfaceValue, sendSlot.mLevel).setValueTakeOverModeJump().setSubPage(faderSend[sn])
		page.makeValueBinding(strip.cut.mSurfaceValue, sendSlot.mOn).setTypeToggle().setSubPage(faderSend[sn])
		;(function (index, n) {
			sendSlot.mOnTitleChange = function (activeDevice, activeMapping, title) {
				sendTitles[index][n] = title || ''
				if (route.fader === 'send' && route.send === n) paintStripName(activeDevice, index)
			}
		})(i, sn)
	}
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
	// Top soft key = record-enable for the track (its LED shows the state).
	page.makeValueBinding(strip.softKey.mSurfaceValue, channel.mValue.mRecordEnable)
		.setTypeToggle()
	// SEL in AUTO (note 112 + strip from the bridge): the track's automation Write; its LED feedback goes back
	// on the same note, and the bridge shows it on SEL (red).
	page.makeValueBinding(strip.autoWrite.mSurfaceValue, channel.mValue.mAutomationWrite)
		.setTypeToggle()
	;(function (index, ch) {
		skStrip[index] = { obj: -1, model: null, scanned: false, wanted: null, tag: -1, sent: null }
		try { skDa[index] = page.mHostAccess.makeDirectAccess(ch) } catch (e) { if (index === 0) out('[CubaseSixty] no Direct Access, the SSL soft keys do nothing: ' + e) }
		if (!skDa[index]) return
		skDa[index].mOnParameterChange = function (activeDevice, activeMapping, objectID, tag) {
			skAm = activeMapping
			if (skStrip[index].faderOn && objectID === skStrip[index].obj && tag === skStrip[index].faderTag) stripFaderSend(activeDevice, index)
			if (skStrip[index].panOn && objectID === skStrip[index].obj && tag === skStrip[index].panTag) stripPanSend(activeDevice, index)
			// Only the focused parameter (a plug-in reports every parameter's change, e.g. under automation).
			if (skFocus && objectID === skStrip[index].tagObj && tag === skStrip[index].tag) skReport(activeDevice, index)
		}
		skDa[index].mOnObjectChange = function (activeDevice, activeMapping, objectID) {
			skAm = activeMapping
			skStrip[index].scanned = false
			skRescanAt = Date.now() + 300
		}
	})(i, channel)
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
			stripTitles[index] = title
			paintStripName(activeDevice, index)
			if (skStrip[index]) { skStrip[index].scanned = false; skStrip[index].wanted = null; skAm = activeMapping; skRescanAt = Date.now() + 150 }
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
		// The V-pot on a send of the selected track: "Send" + its level on the value line, a unipolar ring.
		function sendText(activeDevice, value, units) {
			var text = 'S' + String(value) + (units ? units : '')
			if (text === lastPanText) return
			lastPanText = text
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0D, index], toAscii7(String(value) + (units ? ' ' + units : '')), [0xF7]))
		}
		function sendPos(activeDevice, value) {
			var pos = Math.max(0, Math.min(127, Math.round(value * 127)))
			if (pos === lastPanPos + 1000) return
			lastPanPos = pos + 1000
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0E, index, pos, 0xF7]))
		}
		function dbText(activeDevice, value) {
			var text = String(value)
			if (route.fader === 'vol') trackDb[index] = text   // for leaving strip mode
			if (skStrip[index] && skStrip[index].faderOn) return   // strip mode: the dB field is the strip's fader
			if (text === lastDb) return
			lastDb = text
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0C, index], toAscii7(text), [0xF7]))
		}
		dbResend[index] = function (activeDevice) { lastDb = null; if (trackDb[index] !== undefined) dbText(activeDevice, trackDb[index]) }
		// Strip mode: the V-pot on the SSL strip's pan; Cubase's track pan is kept for the way back.
		panResend[index] = function (activeDevice) {
			lastPanText = null; lastPanPos = null
			if (trackPan[index]) { panText(activeDevice, trackPan[index].value, trackPan[index].units); panPos(activeDevice, trackPan[index].pos) }
		}
		stripPanShow[index] = function (activeDevice, text, pos) {
			lastPanText = null; lastPanPos = null
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x07, index], toAscii7(text), [0xF7]))
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x06, index, Math.max(0, Math.min(127, Math.round(pos * 127))), 0xF7]))
		}
		vpot.mSurfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
			if (route.vpot === 'pan') { trackPan[index] = trackPan[index] || { pos: 0.5 }; trackPan[index].value = value; trackPan[index].units = units }
			if (route.vpot === 'pan' && skStrip[index] && skStrip[index].panOn) return
			if (route.vpot === 'sends') sendText(activeDevice, value, units)
			else if (route.vpot === 'vol') dbText(activeDevice, value)
			else panText(activeDevice, value, units)
		}
		vpot.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
			if (route.vpot === 'pan') { trackPan[index] = trackPan[index] || {}; trackPan[index].pos = value }
			if (route.vpot === 'pan' && skStrip[index] && skStrip[index].panOn) return
			if (route.vpot === 'sends') sendPos(activeDevice, value)
			else if (route.vpot === 'pan') panPos(activeDevice, value)
		}
		fader.mSurfaceValue.mOnDisplayValueChange = function (activeDevice, value, units) {
			if (route.fader === 'pan') panText(activeDevice, value, units); else dbText(activeDevice, value)
		}
		fader.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
			if (route.fader === 'pan') panPos(activeDevice, value)
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
// The CHANNEL encoder's modes (picked in the bridge: hold the encoder's push and turn, or the NAV / NUDGE keys):
// each detent is a Cubase command, sent on MIDI channel 2 (notes 0-5). Bank by 1 uses the Page notes (44 / 45).
// ---------------------------------------------------------------------------

var encoderCommands = [
	{ note: 0, category: 'Project', command: 'Select Track: Prev' },
	{ note: 1, category: 'Project', command: 'Select Track: Next' },
	{ note: 2, category: 'Transport', command: 'Nudge Cursor Left' },
	{ note: 3, category: 'Transport', command: 'Nudge Cursor Right' },
	{ note: 4, category: 'Transport', command: 'Locate Previous Marker' },
	{ note: 5, category: 'Transport', command: 'Locate Next Marker' },
	{ note: 6, category: 'Navigate', command: 'Up' },     // Channel Select with track-stepping = project
	{ note: 7, category: 'Navigate', command: 'Down' }
]
for (var ec = 0; ec < encoderCommands.length; ++ec) {
	var encButton = surface.makeButton(ec, 16, 1, 1)
	encButton.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(1, encoderCommands[ec].note)
	page.makeCommandBinding(encButton.mSurfaceValue, encoderCommands[ec].category, encoderCommands[ec].command)
}

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
	if (m[4] >= 0x21 && m[4] <= 0x29) {
		try { skMessage(activeDevice, m) } catch (e) { out('[CubaseSixty] soft key: ' + e) }
		return
	}
	if (m[4] === 0x18 && m.length >= 8) {
		if (++listNamesSeen === 1) out('[CubaseSixty] channel lists from the helper script arriving')
		onAllTitle(activeDevice, (m[5] << 7) | m[6], decodeName(m, 7))
	} else if (m[4] === 0x19 && m.length >= 7) {
		++listNamesSeen
		onRecTitle(activeDevice, m[5], decodeName(m, 6))
	}
}

// ---------------------------------------------------------------------------
// SSL soft keys (Rea-Sixty's default for the top soft keys, as SSL 360 has them). The bridge owns the six pages of
// eight (V-POT, 1-5) and their labels; a top soft key focuses one parameter (SysEx 21 <page> <slot>, 7F 7F = none),
// and the eight V-pots then work it on each strip's SSL channel strip: 22 <strip> <delta + 64> = turned,
// 23 <strip> = pushed. Each strip's value goes back as 20 <strip> <has> <position 0-127> <ring 1 / 2> <text>.
// The parameters by index, per model (as the UC1 script; the indices from the dumps in docs/ssl-native-params).
// kind: s = switch (push / turn switches it), b = gain or trim (centre bar, push = 0 dB), u = anything else.
// ---------------------------------------------------------------------------
function skp(kind, cs2, b, e, g, link) { return { kind: kind, idx: { cs2: cs2, b: b, e: e, g: g, link: link } } }
var SK_NONE = null
var SK_PAGES = [
	[skp('s', 0, 0, 0, 0, 0), skp('b', 2, 2, 2, 6, 4), skp('s', 3, 5, 5, 11, 5), skp('s', null, 3, 3, 9, null),
		skp('u', null, 4, 4, 10, 44), SK_NONE, skp('s', null, null, null, 7, null), skp('u', null, null, null, 8, null)],
	[skp('u', 40, 8, 9, 15, 2), SK_NONE, SK_NONE, SK_NONE, skp('u', 5, 10, 14, 20, 7), skp('u', 4, 11, 15, 21, 6),
		skp('s', 6, 14, 18, 22, 15), skp('s', 7, null, 19, 23, 14)],
	[skp('u', 9, 16, 21, 24, 19), skp('b', 10, 17, 22, 25, 20), skp('s', 8, 18, 23, 26, 21), SK_NONE,
		skp('u', 11, 19, 24, 28, 17), skp('b', 12, 20, 25, 29, 16), skp('u', 13, 21, 26, 30, 18), SK_NONE],
	[skp('u', 14, 22, 27, 32, 12), skp('b', 15, 23, 28, 33, 11), skp('u', 16, 24, 29, 34, 13), SK_NONE, SK_NONE,
		skp('u', 18, 25, 30, 35, 10), skp('b', 19, 26, 31, 36, 9), skp('s', 17, 27, 32, 37, 8)],
	[skp('s', 20, 28, 33, 38, 22), skp('u', 26, 33, 38, 42, 23), skp('u', 23, 30, 35, 39, 26), skp('u', 24, 31, 36, 40, 27),
		skp('u', 25, 32, 37, 41, 28), skp('s', 22, null, 39, 43, 24), skp('s', 21, null, null, null, 25), SK_NONE],
	[skp('u', 32, 36, 44, 48, 31), skp('u', 30, 35, 43, 47, 30), skp('u', 31, 34, 42, 46, 29), skp('u', 29, null, null, null, 32),
		skp('s', 28, null, 45, 49, 34), skp('s', 27, 37, 46, 50, 33), SK_NONE, skp('b', 39, 7, 8, 14, 37)]
]
// The bus comp pages (Q2; Rea-Sixty's kBcBanks): THR, ATTACK, RELEASE, RATIO, S/C HPF, MIX, EXT S/C, BUS COMP, then
// OUTGAIN; the other pages empty. BUS COMP is the plug-in's bypass, shown the other way round ('si': On = compressing). Parameter indices for Bus Compressor 2 / the 360 Link Bus Compressor (as the UC1
// script's map), or a parameter name; null = that model has none.
function bcp(kind, bc2, lbc) { return { kind: kind, idx: { bc2: bc2, lbc: lbc } } }
var SK_PAGES_BC = [
	[bcp('u', 2, 1), bcp('u', 4, 3), bcp('u', 5, 4), bcp('u', 6, 5), bcp('u', 7, 6), bcp('u', 8, 7), bcp('s', 'External S/C', null), bcp('si', 10, 0)],
	[bcp('u', 3, 2), SK_NONE, SK_NONE, SK_NONE, SK_NONE, SK_NONE, SK_NONE, SK_NONE],
	[], [], [], []
]
var BC_MODELS = [['SSL 360 Link Bus', 'lbc'], ['Bus Compressor 2', 'bc2']]
// The SSL channel strips by name; the first match wins (the Link bus compressor is not a channel strip).
var SK_MODELS = [['SSL 360 Link Bus', null], ['Channel Strip 2', 'cs2'], ['4K G', 'g'], ['4K E', 'e'], ['4K B', 'b'], ['SSL 360 Link', 'link']]
var SK_SKIP = { QuickControls: 1, InputFilter: 1, Panner: 1, Modulators: 1, Strips: 1, EQ: 1, Sends: 1, Outputs: 1, Foldbacks: 1, DirectRouting: 1 }
var SK_STEP = 0.02   // per detent, as the UF1's PLUGIN view

function skScan(index) {
	var st = skStrip[index], da = skDa[index]
	st.scanned = true
	st.obj = -1
	st.model = null
	st.bcObj = -1
	st.bcModel = null
	if (!da || !skAm) return
	var base
	try { base = da.getBaseObjectID(skAm) } catch (e) { return }
	var seen = {}, count = 0
	function rec(id, depth, parentType) {
		if ((st.obj >= 0 && st.bcObj >= 0) || count > 300 || seen[id]) return
		seen[id] = true
		++count
		var type = '', title = ''
		try { type = da.getObjectTypeName(skAm, id) } catch (e) {}
		try { title = da.getObjectTitle(skAm, id) } catch (e) {}
		var hit = false
		for (var b = 0; b < BC_MODELS.length && !hit; ++b) if (title && title.indexOf(BC_MODELS[b][0]) >= 0) {
			hit = true
			if (st.bcObj < 0) { st.bcObj = id; st.bcModel = BC_MODELS[b][1] }
		}
		for (var k = 0; k < SK_MODELS.length && !hit; ++k) if (title && title.indexOf(SK_MODELS[k][0]) >= 0) {
			hit = true
			if (SK_MODELS[k][1] && st.obj < 0) { st.obj = id; st.model = SK_MODELS[k][1] }
		}
		if (hit || depth >= 4 || (/Channel$/.test(parentType) && SK_SKIP[type])) return
		var n = 0
		try { n = da.getNumberOfChildObjects(skAm, id) } catch (e) { return }
		for (var i = 0; i < n && i < 256; ++i) rec(da.getChildObjectID(skAm, id, i), depth + 1, type)
	}
	rec(base, 0, '')
}
// The focused parameter on strip index: { da, obj, tag, kind } or null.
function skTarget(index) {
	if (!skFocus || !skAm) return null
	var slot = (skFocus.bc ? SK_PAGES_BC : SK_PAGES)[skFocus.bank][skFocus.slot]
	if (!slot) return null
	var st = skStrip[index]
	if (!st.scanned) skScan(index)
	var obj = skFocus.bc ? st.bcObj : st.obj
	if (obj < 0) return null
	var idx = slot.idx[skFocus.bc ? st.bcModel : st.model]
	if (idx === null || idx === undefined) return null
	var da = skDa[index]
	try {
		var tag = -1
		if (typeof idx === 'number') tag = da.getParameterTagByIndex(skAm, obj, idx)
		else {   // by name
			var n = da.getNumberOfParameters(skAm, obj), want = idx.toLowerCase()
			for (var i = 0; i < n && tag < 0; ++i) {
				var tg = da.getParameterTagByIndex(skAm, obj, i)
				if (String(da.getParameterTitle(skAm, obj, tg, 48)).toLowerCase() === want) tag = tg
			}
			if (tag < 0) return null
		}
		st.tag = tag
		st.tagObj = obj
		return { da: da, obj: obj, tag: tag, kind: slot.kind }
	} catch (e) { return null }
}
function skReport(activeDevice, index) {
	skStrip[index].tag = -1
	var t = skTarget(index)
	var body
	if (!t) body = [0x20, index, 0, 0, 1].concat(toAscii7('-'))
	else {
		var v = t.da.getParameterProcessValue(skAm, t.obj, t.tag)
		var text = String(t.da.getParameterDisplayValue(skAm, t.obj, t.tag))
		var units = ''
		try { units = t.da.getParameterDisplayUnits(skAm, t.obj, t.tag) || '' } catch (e) {}
		if (units && text.indexOf(units) < 0) text += units
		if (t.kind === 'si') { v = v >= 0.5 ? 0 : 1; text = v ? 'On' : 'Off' }
		body = [0x20, index, 1, Math.max(0, Math.min(127, Math.round(v * 127))), t.kind === 'b' ? 2 : 1].concat(toAscii7(text.replace(/\s+/g, ' ').trim()))
	}
	var key = body.join(',')
	if (key === skStrip[index].sent) return   // unchanged
	skStrip[index].sent = key
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat(body, [0xF7]))
}
function skReportAll(activeDevice) { for (var i = 0; i < STRIPS; ++i) skReport(activeDevice, i) }
// ---------------------------------------------------------------------------
// SSL Strip Mode (the PLUGIN key): each strip's fader on its SSL channel strip's own fader / output gain instead of
// the track's volume; a strip whose track has none keeps volume. The bridge says on / off (SysEx 24 <0/1>) and
// sends a hand's position (25 <strip> <msb> <lsb>); the script answers per strip whether its fader / V-pot are on
// the strip (26 <strip> <fader 0/1> <pan 0/1>; the bridge then ignores Cubase's volume there, and sends the V-pot as
// 28 <strip> <64 + delta>, its push as 29 <strip> = centre) and the strip fader's value for the motor
// (27 <strip> <msb> <lsb>), with its value in the dB field; the pan shows as the track's pan does (06 / 07).
// ---------------------------------------------------------------------------
var stripModeOn = false
var STRIP_FADER_NAMES = ['fader level', 'fader', 'output gain', 'out gain', 'output level']
var STRIP_PAN_NAMES = ['pan', 'panning', 'stereo pan', 'pan/bal', 'balance']
// The strip's fader and pan parameters, found by name once per plug-in.
function stripTags(index) {
	var st = skStrip[index], da = skDa[index]
	if (!st.scanned) skScan(index)
	if (st.obj < 0 || !da || !skAm) return false
	if (st.faderObj === st.obj) return true
	st.faderObj = st.obj
	st.faderTag = -1
	st.panTag = -1
	var n = 0
	try { n = da.getNumberOfParameters(skAm, st.obj) } catch (e) { return false }
	var titles = []
	for (var i = 0; i < n && i < 400; ++i) {
		try { var tag = da.getParameterTagByIndex(skAm, st.obj, i); titles.push({ tag: tag, t: String(da.getParameterTitle(skAm, st.obj, tag, 48)).toLowerCase() }) } catch (e) {}
	}
	function find(names) {
		for (var w = 0; w < names.length; ++w) for (var k = 0; k < titles.length; ++k) if (titles[k].t === names[w]) return titles[k].tag
		return -1
	}
	st.faderTag = find(STRIP_FADER_NAMES)
	st.panTag = find(STRIP_PAN_NAMES)
	return true
}
function stripFaderTag(index) { return stripTags(index) ? skStrip[index].faderTag : -1 }
function stripPanTag(index) { return stripTags(index) ? skStrip[index].panTag : -1 }
// 26 <strip> <fader 0/1> <pan 0/1>: whether the strip's fader / V-pot are on the SSL strip's fader / pan.
function stripModeUpdate(activeDevice, index) {
	var st = skStrip[index]
	var on = stripModeOn && route.fader === 'vol' && stripFaderTag(index) >= 0
	var pan = stripModeOn && route.vpot === 'pan' && stripPanTag(index) >= 0
	if (on !== !!st.faderOn || pan !== !!st.panOn) {
		var wasFader = !!st.faderOn, wasPan = !!st.panOn
		st.faderOn = on
		st.panOn = pan
		midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x26, index, on ? 1 : 0, pan ? 1 : 0, 0xF7]))
		if (wasFader && !on) dbResend[index](activeDevice)
		if (wasPan && !pan) panResend[index](activeDevice)
	}
	if (on) stripFaderSend(activeDevice, index)
	if (pan) stripPanSend(activeDevice, index)
}
function stripPanSend(activeDevice, index) {
	var st = skStrip[index], da = skDa[index], text = ''
	var v = da.getParameterProcessValue(skAm, st.obj, st.panTag)
	try { text = String(da.getParameterDisplayValue(skAm, st.obj, st.panTag)).replace(/\s+/g, ' ').trim() } catch (e) {}
	stripPanShow[index](activeDevice, text, v)
}
function stripModeUpdateAll(activeDevice) { for (var i = 0; i < STRIPS; ++i) stripModeUpdate(activeDevice, i) }
function stripFaderText(index) {
	var st = skStrip[index], da = skDa[index], text = ''
	try { text = String(da.getParameterDisplayValue(skAm, st.obj, st.faderTag)) } catch (e) {}
	return text.replace(/\s+/g, ' ').trim()
}
function stripFaderSend(activeDevice, index) {
	var st = skStrip[index], da = skDa[index]
	var v = Math.round(Math.max(0, Math.min(1, da.getParameterProcessValue(skAm, st.obj, st.faderTag))) * 16383)
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x27, index, (v >> 7) & 0x7F, v & 0x7F, 0xF7]))
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0C, index], toAscii7(stripFaderText(index)), [0xF7]))
}

function skMessage(activeDevice, m) {
	if (m[4] === 0x24 && m.length >= 7) {
		stripModeOn = m[5] !== 0
		out('[CubaseSixty] SSL Strip Mode ' + (stripModeOn ? 'on' : 'off'))
		stripModeUpdateAll(activeDevice)
		return
	}
	if (m[4] === 0x28 && m.length >= 8) {   // a V-pot turn on the strip's pan (64 = no change)
		var pi = m[5]
		if (pi >= STRIPS || !skStrip[pi].panOn) return
		var st = skStrip[pi], da = skDa[pi]
		var cur = da.getParameterProcessValue(skAm, st.obj, st.panTag)
		da.setParameterProcessValue(skAm, st.obj, st.panTag, Math.max(0, Math.min(1, cur + (m[6] - 64) / 100)))
		stripPanSend(activeDevice, pi)
		return
	}
	if (m[4] === 0x29 && m.length >= 7) {   // V-pot push on the strip's pan: centre
		var ci = m[5]
		if (ci >= STRIPS || !skStrip[ci].panOn) return
		skDa[ci].setParameterProcessValue(skAm, skStrip[ci].obj, skStrip[ci].panTag, 0.5)
		stripPanSend(activeDevice, ci)
		return
	}
	if (m[4] === 0x25 && m.length >= 9) {
		var si = m[5]
		if (si >= STRIPS || !skStrip[si].faderOn) return
		skDa[si].setParameterProcessValue(skAm, skStrip[si].obj, skStrip[si].faderTag, ((m[6] << 7) | m[7]) / 16383)
		midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0C, si], toAscii7(stripFaderText(si)), [0xF7]))
		return
	}
	if (m[4] === 0x21 && m.length >= 8) {
		skFocus = m[5] === 0x7F ? null : { bank: m[5] & 7, slot: m[6], bc: m[5] >= 8 }   // bank + 8 = the bus comp (Q2)
		for (var i = 0; i < STRIPS; ++i) { skStrip[i].wanted = null; skStrip[i].sent = null }
		if (skFocus) skReportAll(activeDevice)
		return
	}
	if (!skFocus || m.length < 7 || m[5] >= STRIPS) return
	var index = m[5], t = skTarget(index)
	if (!t) return
	var st = skStrip[index], v
	if (m[4] === 0x23) {   // push
		if (t.kind === 's' || t.kind === 'si') v = t.da.getParameterProcessValue(skAm, t.obj, t.tag) >= 0.5 ? 0 : 1
		else if (t.kind === 'b') v = 0.5
		else return
	} else {               // turn
		var delta = m[6] - 64
		if (t.kind === 's') v = delta > 0 ? 1 : 0
		else if (t.kind === 'si') v = delta > 0 ? 0 : 1   // turned right = compressing
		else {
			// A stepped parameter snaps a small change back, so a turn is added up while it goes on (< 1 s).
			var now = Date.now(), w = st.wanted
			var from = w && w.tag === t.tag && now - w.at < 1000 ? w.value : t.da.getParameterProcessValue(skAm, t.obj, t.tag)
			v = Math.max(0, Math.min(1, from + delta * SK_STEP))
			st.wanted = { tag: t.tag, value: v, at: now }
		}
	}
	t.da.setParameterProcessValue(skAm, t.obj, t.tag, v)
	skReport(activeDevice, index)
}
page.mOnActivate = function (activeDevice, activeMapping) { skAm = activeMapping }
page.mOnIdle = function (activeDevice, activeMapping) {
	skAm = activeMapping
	if (skRescanAt && Date.now() >= skRescanAt) {
		skRescanAt = 0
		if (skFocus) { try { skReportAll(activeDevice) } catch (e) { out('[CubaseSixty] soft keys: ' + e) } }
		if (stripModeOn) { try { stripModeUpdateAll(activeDevice) } catch (e) { out('[CubaseSixty] strip mode: ' + e) } }
	}
}
