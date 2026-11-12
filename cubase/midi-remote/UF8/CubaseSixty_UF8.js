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
var SCRIPT_VERSION = '0.15.7-dev (ports: to Cubase / from Cubase)'
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
// Surface layout: the on-screen picture of the UF8 in Cubase's MIDI Remote tab. Positions are those of Rea-Sixty's
// drawing of the UF8 (1000 x 490, its settings window, from SSL's user guide), divided by 20. Keys the bridge
// handles itself are plain panels (they have no element here). Elements that are not a key of their own (commands
// and modes the bridge sends: encoder modes, routes, SEL in AUTO, ...) sit small in rows below the unit.
// ---------------------------------------------------------------------------

function px(v) { return v / 20 }
var cmdCount = 0
function cmdButton() {
	var n = cmdCount++
	return surface.makeButton(px(10 + (n % 40) * 24.5), px(500 + Math.floor(n / 40) * 24.5), px(22), px(22))
}
function panel(x, y, w, h) { surface.makeBlindPanel(px(x), px(y), px(w), px(h)) }
// Left panel: LAYER 1-3, Q1-Q3, 360, the soft-key pages, PLUGIN / CHANNEL, FLIP.
for (var lq = 0; lq < 3; ++lq) { panel(15, 22 + lq * 26, 36, 22); panel(57, 22 + lq * 26, 36, 22) }
panel(15, 108, 78, 24)
for (var sp = 0; sp < 8; ++sp) panel(13 + (sp % 2) * 42, 152 + Math.floor(sp / 2) * 26, 38, 22)
panel(13, 260, 50, 22); panel(67, 260, 50, 22)
panel(13, 326, 104, 22)
// Right panel: soft-key banks, PAN / FINE, NORM / REC / AUTO, the CHANNEL encoder, NAV / NUDGE / FOCUS.
for (var sb = 0; sb < 6; ++sb) panel(852 + (sb % 3) * 46, 22 + Math.floor(sb / 3) * 24, 41, 20)
panel(852, 80, 64, 22); panel(921, 80, 64, 22)
panel(852, 128, 43, 20); panel(899, 128, 43, 20); panel(946, 128, 39, 20)
surface.makeBlindPanel(px(886), px(168), px(64), px(64))
panel(852, 244, 44, 22); panel(898, 244, 44, 22); panel(944, 244, 41, 22)

var STRIPS = 8
var strips = []

for (var i = 0; i < STRIPS; ++i) {
	var sx = 138 + i * 87   // the strip's left edge
	var strip = {}

	strip.softKey = surface.makeButton(px(sx + 6), px(12), px(68), px(22))
	panel(sx + 4, 40, 72, 58)   // the colour display
	strip.vpot = surface.makeKnob(px(sx + 22), px(106), px(36), px(36))
	strip.solo = surface.makeButton(px(sx + 8), px(152), px(64), px(16))
	strip.cut = surface.makeButton(px(sx + 8), px(172), px(64), px(16))
	strip.select = surface.makeButton(px(sx + 8), px(192), px(64), px(16))
	strip.fader = surface.makeFader(px(sx + 28), px(220), px(24), px(220))
	strip.faderTouch = surface.makeCustomValueVariable('faderTouch' + (i + 1))
	strip.vpush = cmdButton()   // (the V-pot's push)
	// SEL in the AUTO selection mode (the bridge sends it on its own note): the track's automation Write.
	strip.autoWrite = cmdButton()

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

var bankLeft = surface.makeButton(px(870), px(314), px(42), px(22))
var bankRight = surface.makeButton(px(922), px(314), px(42), px(22))
// (The UF8 has no transport keys: these come from the bridge.)
var stopButton = cmdButton()
var playButton = cmdButton()
var recordButton = cmdButton()

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
	for (var r = 0; r < flipResets.length; ++r) flipResets[r](activeDevice)
	for (var i = 0; i < STRIPS; ++i) paintStripName(activeDevice, i)
	stripModeUpdateAll(activeDevice)   // FLIP takes the fader back from strip mode; the send views put it on a send
}
// The name a strip shows: the send's destination in a send view, else the track's.
function paintStripName(activeDevice, index) {
	var text = stripTitles[index] || ''
	// (A track without that send shows a blank name, not an empty one: the bridge takes an empty name as no track
	// on the strip and switches its LEDs off.)
	// The fader send view keeps the track names (each strip is a track; the destination is the same on all of them);
	// the V-pot send view shows the selected track's send names.
	if (route.vpot === 'sends' && route.fader !== 'send') text = emptySend(selSendTitles[index]) ? ' ' : selSendTitles[index]
	sendStripName(activeDevice, index, text)
}
;(function () {
	function key(note, sub, apply) {
		var k = cmdButton()
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
			if (route.vpot === 'sends' && route.fader !== 'send') { paintStripName(activeDevice, s); skRescanAt = Date.now() + 150 }   // (another track: its levels)
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
	// (Send levels go through Direct Access, see "Send views" below: the send's on switch is only a placeholder, a
	// control needs a binding in every sub page.)
	page.makeValueBinding(strip.vpot.mSurfaceValue, routeSelected.mSends.getByIndex(i).mOn).setSubPage(vpotSends)
	page.makeValueBinding(strip.cut.mSurfaceValue, channel.mValue.mMute).setTypeToggle().setSubPage(faderVol)
	page.makeValueBinding(strip.cut.mSurfaceValue, channel.mValue.mMute).setTypeToggle().setSubPage(faderPan)
	for (var sn = 0; sn < 8; ++sn) {
		var sendSlot = channel.mSends.getByIndex(sn)
		page.makeValueBinding(strip.fader.mSurfaceValue, sendSlot.mOn).setSubPage(faderSend[sn])   // (placeholder: see "Send views")
		page.makeValueBinding(strip.cut.mSurfaceValue, sendSlot.mOn).setTypeToggle().setSubPage(faderSend[sn])
		;(function (index, n) {
			sendSlot.mOnTitleChange = function (activeDevice, activeMapping, title) {
				sendTitles[index][n] = title || ''
				if (route.fader === 'send' && route.send === n) skRescanAt = Date.now() + 150   // a send added / removed: the fader's target
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
		skStrip[index] = { obj: -1, model: null, scanned: false, wanted: null, tag: -1, sent: null, faderOn: false, panOn: false, fTag: -1, vTag: -1, fObj: -1, vObj: -1, fKind: null, vKind: null, vDa: null, vOn: -1 }
		try { skDa[index] = page.mHostAccess.makeDirectAccess(ch) } catch (e) { if (index === 0) out('[CubaseSixty] no Direct Access, the SSL soft keys do nothing: ' + e) }
		if (!skDa[index]) return
		skDa[index].mOnParameterChange = function (activeDevice, activeMapping, objectID, tag) {
			skAm = activeMapping
			var st = skStrip[index]
			if (st.faderOn && objectID === st.fObj && tag === st.fTag) stripFaderSend(activeDevice, index)
			if (st.panOn && st.vKind === 'plugin' && objectID === st.vObj && tag === st.vTag) stripPanSend(activeDevice, index)
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
		// The controls' last values, whatever they were on: Cubase sends the new bindings' values before the sub-page
		// change is announced, so they are drawn again once it is (FLIP showed the pan in the dB field otherwise).
		var vpotShown = { value: null, units: '', pos: null }, faderShown = { value: null, units: '', pos: null }
		flipResets.push(function (activeDevice) {
			lastPanText = lastPanPos = lastDb = null
			if (!activeDevice) return
			// (The handlers by name: Cubase does not give back a callback read from the surface value.)
			if (faderShown.value !== null) faderDisplay(activeDevice, faderShown.value, faderShown.units)
			if (faderShown.pos !== null) faderProcess(activeDevice, faderShown.pos)
			if (vpotShown.value !== null) vpotDisplay(activeDevice, vpotShown.value, vpotShown.units)
			if (vpotShown.pos !== null) vpotProcess(activeDevice, vpotShown.pos)
		})
		function panText(activeDevice, value, units) {
			if (stripOwnsPan(index)) return   // strip mode: the value line shows the plug-in's pan
			var text = String(value) + (units ? units : '')
			if (text === lastPanText) return
			lastPanText = text
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x07, index], toAscii7(text), [0xF7]))
		}
		function panPos(activeDevice, value) {
			if (stripOwnsPan(index)) return
			var pos = Math.max(0, Math.min(127, Math.round(value * 127)))
			if (pos === lastPanPos) return
			lastPanPos = pos
			midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x06, index, pos, 0xF7]))
		}
		function dbText(activeDevice, value) {
			var text = String(value)
			if (route.fader === 'vol') trackDb[index] = text   // for leaving strip mode
			if (stripOwnsDb(index)) return   // strip mode: the dB field shows the plug-in's control
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
		function vpotDisplay(activeDevice, value, units) {
			vpotShown.value = value; vpotShown.units = units
			if (route.vpot === 'pan') { trackPan[index] = trackPan[index] || { pos: 0.5 }; trackPan[index].value = value; trackPan[index].units = units }
			if (route.vpot === 'sends') return   // (the send levels are shown from Direct Access)
			else if (route.vpot === 'vol') dbText(activeDevice, value)
			else panText(activeDevice, value, units)
		}
		function vpotProcess(activeDevice, value) {
			vpotShown.pos = value
			if (route.vpot === 'pan') { trackPan[index] = trackPan[index] || {}; trackPan[index].pos = value }
			if (route.vpot === 'sends') return
			else if (route.vpot === 'pan') panPos(activeDevice, value)
		}
		function faderDisplay(activeDevice, value, units) {
			faderShown.value = value; faderShown.units = units
			if (route.fader === 'pan') panText(activeDevice, value, units); else dbText(activeDevice, value)
		}
		function faderProcess(activeDevice, value) {
			faderShown.pos = value
			if (route.fader === 'pan') panPos(activeDevice, value)
		}
		vpot.mSurfaceValue.mOnDisplayValueChange = vpotDisplay
		vpot.mSurfaceValue.mOnProcessValueChange = vpotProcess
		fader.mSurfaceValue.mOnDisplayValueChange = faderDisplay
		fader.mSurfaceValue.mOnProcessValueChange = faderProcess
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
var autoRead = surface.makeButton(px(48), px(372), px(33), px(22))
var autoWrite = surface.makeButton(px(83), px(372), px(33), px(22))
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
var foot1 = surface.makeButton(px(13), px(442), px(50), px(22))
var foot2 = surface.makeButton(px(67), px(442), px(50), px(22))
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
// Nav Mode (NAV held, the top soft keys): channel 2 notes 10-17 = To Marker 1-8, 18-25 = Recall Cycle Marker 1-8.
for (var nm = 1; nm <= 8; ++nm) {
	encoderCommands.push({ note: 9 + nm, category: 'Transport', command: 'To Marker ' + nm })
	encoderCommands.push({ note: 17 + nm, category: 'Transport', command: 'Recall Cycle Marker ' + nm })
}
// After a cycle marker is recalled (which only sets the locators), the cursor goes to its start.
encoderCommands.push({ note: 26, category: 'Transport', command: 'To Left Locator' })
for (var ec = 0; ec < encoderCommands.length; ++ec) {
	var encButton = cmdButton()
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
	// The cursor pad: up, left, centre, right, down.
	var zoomAt = [[901, 374], [864, 406], [901, 406], [938, 406], [901, 438]][z]
	var zoomButton = surface.makeButton(px(zoomAt[0]), px(zoomAt[1]), px(34), px(26))
	zoomButton.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, zoomKeys[z].note)
	page.makeCommandBinding(zoomButton.mSurfaceValue, zoomKeys[z].category, zoomKeys[z].command)
}

// ---------------------------------------------------------------------------
// Page left / right (notes 44 / 45): move the bank by one channel instead of eight.
// ---------------------------------------------------------------------------

var pageLeft = surface.makeButton(px(13), px(300), px(50), px(22))
var pageRight = surface.makeButton(px(67), px(300), px(50), px(22))
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
	// OFF, TRIM, LATCH, TOUCH (READ / WRITE are above)
	var modeAt = [[13, 372], [13, 396], [48, 396], [83, 396]][a]
	var modeButton = surface.makeButton(px(modeAt[0]), px(modeAt[1]), px(33), px(22))
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

var followRestart = function () {}, followStop = function () {}, driverActivations = 0
// The UF1's BANK without a UF8 moves these strips and selects on them (SysEx 5D from the bridge): follow stays out
// of it meanwhile, as the selection Cubase reports then is late and on the old bank (it banked back).
var followQuietUntil = 0
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
	// A project closed and another opened (the driver deactivated and activated again): the start-up follow again.
	followRestart = function (activeDevice) {
		startupUntil = 0
		followTries = 0
		dueMs = 0
		selectedIndex = -1
		lastTryOffset = -1
		allTitles = []
	}
	followStop = function (activeDevice) { startupUntil = -1; dueMs = 0; startupFreeze(activeDevice, false) }
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
		if (FOLLOW_OBSERVE_ONLY || seek.active || selectedIndex < 0 || nowMs() < followQuietUntil) { followTries = 0; return }
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
	var resetKey = cmdButton()
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
// Closing a project (Cubase shows the Hub) deactivates the driver, opening one activates it again: the bridge blanks
// the UF8 meanwhile (SysEx 7C 00 / 01), and the follow and the track lists start over.
deviceDriver.mOnDeactivate = function (activeDevice) {
	out('[CubaseSixty] inactive (no project)')
	try { followStop(activeDevice) } catch (e) {}
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x7C, 0x00, 0xF7]))
	flushLog(activeDevice)
}
deviceDriver.mOnActivate = function (activeDevice) {
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x7C, 0x01, 0xF7]))
	if (!driverActivations++) return   // the first activation is Cubase's start: everything starts anyway
	out('[CubaseSixty] active again (a project is open)')
	listNamesSeen = 0
	listRequestAt = 0
	try { followRestart(activeDevice) } catch (e) {}
}
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
	if (m[4] >= 0x21 && m[4] <= 0x2A) {   // (2A was added without this range: the levels never arrived, 12.11.2026)
		try { skMessage(activeDevice, m) } catch (e) { out('[CubaseSixty] soft key: ' + e) }
		return
	}
	if (m[4] === 0x5D && m.length >= 7) {   // the UF1 banks these strips: no follow for <n> x 100 ms
		followQuietUntil = Date.now() + m[5] * 100   // (nowMs is inside the follow block)
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
// Send views (10.11.2026): a MIDI Remote binding on a send level made Cubase pause ~0.8 s at a time while the level
// kept changing (found on the UF1). In the send views the faders (send N of every track) and the V-pots (the selected
// track's sends) go through the same path as strip mode: the bridge sends the hand (25) / turn (28), the script
// writes the send slot's level through Direct Access (the strip's channel / the selected channel) and reports it
// back (27 for the motor, the dB field; 0D / 0E for the V-pot's value line and ring). Their bindings are on the
// send's on switch, as placeholders. The V-pot push switches the selected track's send on / off.
// An empty send slot: Cubase keeps eight per track, an empty one has no destination (its title is "" or only its
// number) but still a level (0 dB).
function emptySend(title) { return !title || /^\s*\d*\s*-?\s*$/.test(String(title)) }
var selDa = null
try { selDa = page.mHostAccess.makeDirectAccess(routeSelected) } catch (e) { out('[CubaseSixty] no Direct Access on the selected track: the V-pot send view does nothing: ' + e) }
// The level parameter of send slot n (0-7) on the channel da is on: { obj, tag, on } or null.
function sendSlotLevel(da, n) {
	if (!da || !skAm) return null
	var base = da.getBaseObjectID(skAm)
	if (base < 0) return null
	var c = da.getNumberOfChildObjects(skAm, base)
	for (var i = 0; i < c; ++i) {
		var id = da.getChildObjectID(skAm, base, i)
		if (da.getObjectTypeName(skAm, id) !== 'Sends') continue
		if (n >= da.getNumberOfChildObjects(skAm, id)) return null
		var slot = da.getChildObjectID(skAm, id, n)
		var np = da.getNumberOfParameters(skAm, slot), level = -1, on = -1
		for (var k = 0; k < np; ++k) {
			var tag = da.getParameterTagByIndex(skAm, slot, k)
			var title = String(da.getParameterTitle(skAm, slot, tag, 32))
			if (level < 0 && /level/i.test(title)) level = tag
			if (on < 0 && /^on$/i.test(title)) on = tag
		}
		return level >= 0 ? { obj: slot, tag: level, on: on } : null
	}
	return null
}
// What each control is on: strip mode puts the fader on the plug-in's fader and the V-pot on its pan (FLIP: the other
// way round); the fader send view puts each fader on its track's send N; the V-pot send view each V-pot on the
// selected track's send. st.fObj / fTag, st.vObj / vTag: what the fader / V-pot moves; fKind / vKind 'plugin' or
// 'send' (null = the track's own, through the bindings).
// 26 <strip> <fader 0/1> <V-pot 0/1>: whether the strip's fader / V-pot are the script's (the bridge then sends 25 /
// 28 and ignores Cubase's own values there).
function stripModeUpdate(activeDevice, index) {
	var st = skStrip[index]
	if (!st) return
	var normal = route.fader === 'vol' && route.vpot === 'pan', flipped = route.fader === 'pan' && route.vpot === 'vol'
	var fTag = -1, vTag = -1, fObj = -1, vObj = -1, fKind = null, vKind = null, vDa = null, vOn = -1
	if (route.fader === 'send') {
		fKind = 'send'
		var fs = null
		if (!emptySend(sendTitles[index][route.send])) { try { fs = sendSlotLevel(skDa[index], route.send) } catch (e) {} }
		if (fs) { fObj = fs.obj; fTag = fs.tag }
	} else if (stripModeOn && (normal || flipped) && stripTags(index)) {
		fTag = normal ? st.faderTag : st.panTag
		vTag = normal ? st.panTag : st.faderTag
		if (fTag >= 0) { fKind = 'plugin'; fObj = st.obj }
		if (vTag >= 0) { vKind = 'plugin'; vObj = st.obj; vDa = skDa[index] }
	}
	if (route.vpot === 'sends' && route.fader !== 'send') {
		vKind = 'send'; vDa = selDa; vObj = -1; vTag = -1
		var vs = null
		if (!emptySend(selSendTitles[index])) { try { vs = sendSlotLevel(selDa, index) } catch (e) {} }
		if (vs) { vObj = vs.obj; vTag = vs.tag; vOn = vs.on }
	}
	// A send view takes the control even where the track has no such send (it then shows an empty send).
	var on = fKind === 'send' || fTag >= 0, pan = vKind === 'send' || vTag >= 0
	var changed = on !== !!st.faderOn || pan !== !!st.panOn || fTag !== st.fTag || vTag !== st.vTag || fObj !== st.fObj || vObj !== st.vObj
	st.faderOn = on
	st.panOn = pan
	st.fTag = fTag
	st.vTag = vTag
	st.fObj = fObj
	st.vObj = vObj
	st.fKind = fKind
	st.vKind = vKind
	st.vDa = vDa
	st.vOn = vOn
	if (changed) { st.fPolled = st.vPolled = st.vOnPolled = null }
	if (changed) {
		midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x26, index, on ? 1 : 0, pan ? 1 : 0, 0xF7]))
		flipResets[index](activeDevice)   // the track's own values where the script no longer has them
	}
	if (on) stripFaderSend(activeDevice, index)
	if (pan) stripPanSend(activeDevice, index)
}
// Who shows in the dB field / on the value line: the script where its fader / V-pot is on a plug-in control or send.
function stripOwnsDb(index) {
	var st = skStrip[index]
	return !!st && ((st.faderOn && (st.fKind === 'send' || st.fTag === st.faderTag)) || (st.panOn && st.vKind === 'plugin' && st.vTag === st.faderTag))
}
function stripOwnsPan(index) {
	var st = skStrip[index]
	return !!st && ((st.faderOn && st.fKind === 'plugin' && st.fTag === st.panTag) || (st.panOn && (st.vKind === 'send' || st.vTag === st.panTag)))
}
function daText(da, obj, tag) {
	var text = ''
	try { text = String(da.getParameterDisplayValue(skAm, obj, tag)) } catch (e) {}
	return text.replace(/\s+/g, ' ').trim()
}
function daUnits(da, obj, tag) {
	try { return String(da.getParameterDisplayUnits(skAm, obj, tag) || '') } catch (e) { return '' }
}
// The fader's target shown: a plug-in's fader level or a send level in the dB field, a plug-in's pan on the value line.
function stripFaderShow(activeDevice, index) {
	var st = skStrip[index]
	if (st.fObj < 0) { midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0C, index], toAscii7(''), [0xF7])); return }
	var text = daText(skDa[index], st.fObj, st.fTag)
	if (st.fKind === 'plugin' && st.fTag === st.panTag) stripPanShow[index](activeDevice, text, skDa[index].getParameterProcessValue(skAm, st.fObj, st.fTag))
	else midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0C, index], toAscii7(text), [0xF7]))
}
// The V-pot's target shown: a send on the value line ("Send" + level) and a unipolar ring; a plug-in's pan on the
// value line, its fader level in the dB field.
function stripPanSend(activeDevice, index) {
	var st = skStrip[index]
	if (st.vKind === 'send') {
		var text = '', pos = 0, exact = null
		if (st.vObj >= 0) {
			text = daText(st.vDa, st.vObj, st.vTag)
			var u = daUnits(st.vDa, st.vObj, st.vTag)
			if (text && u) text += ' ' + u
			// (While turning: the value just written, Cubase's own comes a moment later.)
			var ww = st.vWanted
			var pv = ww && ww.obj === st.vObj && ww.tag === st.vTag && Date.now() - ww.at < 400 ? ww.value : st.vDa.getParameterProcessValue(skAm, st.vObj, st.vTag)
			pos = Math.max(0, Math.min(127, Math.round(pv * 127)))
			// Cubase's own value: with its exact position, from which the bridge learns the level curve (it draws the
			// value text itself while turning).
			if (!(ww && pv === ww.value && Date.now() - ww.at < 400)) { var p14 = Math.round(Math.max(0, Math.min(1, pv)) * 16383); exact = [(p14 >> 7) & 0x7F, p14 & 0x7F] }
		}
		midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0D, index], toAscii7(text), [0xF7]))
		midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0E, index, pos], exact || [], [0xF7]))
		return
	}
	var text2 = daText(st.vDa, st.vObj, st.vTag)
	if (st.vTag === st.panTag) stripPanShow[index](activeDevice, text2, st.vDa.getParameterProcessValue(skAm, st.vObj, st.vTag))
	else midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x0C, index], toAscii7(text2), [0xF7]))
}
function stripModeUpdateAll(activeDevice) { for (var i = 0; i < STRIPS; ++i) stripModeUpdate(activeDevice, i) }
function stripFaderSend(activeDevice, index) {
	var st = skStrip[index]
	var v = st.fObj < 0 ? 0 : Math.round(Math.max(0, Math.min(1, skDa[index].getParameterProcessValue(skAm, st.fObj, st.fTag))) * 16383)
	midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat([0x27, index, (v >> 7) & 0x7F, v & 0x7F, 0xF7]))
	stripFaderShow(activeDevice, index)
}
// The selected track's sends changed elsewhere (the mouse, automation, our own writes): the V-pots show them.
if (selDa) {
	selDa.mOnParameterChange = function (activeDevice, activeMapping, objectID, tag) {
		skAm = activeMapping
		if (route.vpot !== 'sends') return
		for (var i = 0; i < STRIPS; ++i) {
			var st = skStrip[i]
			if (st && st.panOn && st.vKind === 'send' && st.vObj === objectID && (st.vTag === tag || st.vOn === tag)) stripPanSend(activeDevice, i)
		}
	}
	selDa.mOnObjectChange = function (activeDevice, activeMapping, objectID) { skAm = activeMapping; skRescanAt = Date.now() + 300 }
}

function skMessage(activeDevice, m) {
	if (m[4] === 0x24 && m.length >= 7) {
		stripModeOn = m[5] !== 0
		out('[CubaseSixty] SSL Strip Mode ' + (stripModeOn ? 'on' : 'off'))
		stripModeUpdateAll(activeDevice)
		return
	}
	if (m[4] === 0x2A && m.length >= 9) {   // a V-pot on a send (the V-pot send view): the level itself, from the bridge
		var ai = m[5]
		if (ai >= STRIPS || !skStrip[ai].panOn || skStrip[ai].vKind !== 'send' || skStrip[ai].vObj < 0) {
			// (Logged, at most once a second: a level the script could not write.)
			if (ai < STRIPS && Date.now() - (skStrip[ai].refusedAt || 0) > 1000) {
				skStrip[ai].refusedAt = Date.now()
				out('[CubaseSixty] send V-pot ' + (ai + 1) + ': level not written (V-pot on ' + skStrip[ai].vKind + ', send object ' + skStrip[ai].vObj + ', route ' + route.fader + ' / ' + route.vpot + ')')
			}
			return
		}
		var as = skStrip[ai], av = ((m[6] << 7) | m[7]) / 16383, at = Date.now()
		as.vWanted = { obj: as.vObj, tag: as.vTag, value: av, at: at }
		as.vSettleAt = at + 600   // (the bridge draws ring and value itself for 0.5 s after a turn: then Cubase's value once)
		as.vDa.setParameterProcessValue(skAm, as.vObj, as.vTag, av)
		return
	}
	if (m[4] === 0x28 && m.length >= 8) {   // a V-pot turn on the script's target (64 = no change)
		var pi = m[5]
		if (pi >= STRIPS || !skStrip[pi].panOn || skStrip[pi].vObj < 0) return
		var st = skStrip[pi], da = st.vDa
		// Quick turns count from the value last written (for 1 s): read back at once, Cubase still gives the old value
		// and steps were lost (the V-pot send view lagged, 12.11.2026). The ring is drawn from it at once too.
		var nowT = Date.now()
		var w = st.vWanted
		var cur = w && w.obj === st.vObj && w.tag === st.vTag && nowT - w.at < 1000 ? w.value : da.getParameterProcessValue(skAm, st.vObj, st.vTag)
		var next = Math.max(0, Math.min(1, cur + (m[6] - 64) / 100))
		st.vWanted = { obj: st.vObj, tag: st.vTag, value: next, at: nowT }
		st.vSettleAt = nowT + 600   // (the bridge draws the ring itself for 0.5 s after a turn: then Cubase's value once)
		da.setParameterProcessValue(skAm, st.vObj, st.vTag, next)
		stripPanSend(activeDevice, pi)
		return
	}
	if (m[4] === 0x29 && m.length >= 7) {   // V-pot push: centre the plug-in's pan; on a send: the send on / off
		var ci = m[5]
		if (ci >= STRIPS || !skStrip[ci].panOn || skStrip[ci].vObj < 0) return
		var cs = skStrip[ci]
		if (cs.vKind === 'send') {
			if (cs.vOn >= 0) cs.vDa.setParameterProcessValue(skAm, cs.vObj, cs.vOn, cs.vDa.getParameterProcessValue(skAm, cs.vObj, cs.vOn) >= 0.5 ? 0 : 1)
		} else if (cs.vTag === cs.panTag) {
			cs.vDa.setParameterProcessValue(skAm, cs.vObj, cs.vTag, 0.5)
		}
		stripPanSend(activeDevice, ci)
		return
	}
	if (m[4] === 0x25 && m.length >= 9) {   // the hand on a fader that is the script's (a plug-in control or a send)
		var si = m[5]
		if (si >= STRIPS || !skStrip[si].faderOn || skStrip[si].fObj < 0) return
		skDa[si].setParameterProcessValue(skAm, skStrip[si].fObj, skStrip[si].fTag, ((m[6] << 7) | m[7]) / 16383)
		return   // (shown by the poll, sendViewPoll: read back at once, the text was not updated yet)
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
// The send views' levels read about nine times a second (each idle tick) and sent when changed: Cubase reports a
// Direct Access change only when an edit ends (a mouse drag showed only at the release), and right after a write the
// text was not updated yet (10.11.2026).
function sendViewPoll(activeDevice) {
	for (var i = 0; i < STRIPS; ++i) {
		var st = skStrip[i]
		if (!st) continue
		if (st.faderOn && st.fKind === 'send' && st.fObj >= 0) {
			var v = skDa[i].getParameterProcessValue(skAm, st.fObj, st.fTag)
			if (v !== st.fPolled) { st.fPolled = v; stripFaderSend(activeDevice, i) }
		}
		if (st.panOn && st.vKind === 'send' && st.vObj >= 0) {
			var w = st.vDa.getParameterProcessValue(skAm, st.vObj, st.vTag)
			var on = st.vOn >= 0 ? st.vDa.getParameterProcessValue(skAm, st.vObj, st.vOn) : 0
			var txt = daText(st.vDa, st.vObj, st.vTag)
			var settle = st.vSettleAt && Date.now() >= st.vSettleAt
			if (settle) st.vSettleAt = 0
			if (settle || w !== st.vPolled || on !== st.vOnPolled || txt !== st.vTextPolled) { st.vPolled = w; st.vOnPolled = on; st.vTextPolled = txt; stripPanSend(activeDevice, i) }
		}
	}
}
page.mOnIdle = function (activeDevice, activeMapping) {
	skAm = activeMapping
	if (route.fader === 'send' || route.vpot === 'sends') { try { sendViewPoll(activeDevice) } catch (e) {} }
	if (skRescanAt && Date.now() >= skRescanAt) {
		skRescanAt = 0
		if (skFocus) { try { skReportAll(activeDevice) } catch (e) { out('[CubaseSixty] soft keys: ' + e) } }
		if (stripModeOn || route.fader === 'send' || route.vpot === 'sends') { try { stripModeUpdateAll(activeDevice) } catch (e) { out('[CubaseSixty] strip mode: ' + e) } }
	}
}
