// Cubase-Sixty – MIDI Remote script for the SSL UC1 (via the Cubase-Sixty Bridge)
//
// The UC1 controls the SSL plug-ins on the SELECTED track, like SSL 360 does:
//   - the Channel Strip knobs and buttons drive the track's channel strip plug-in (Channel Strip 2, 4K E / G / B,
//     SSL 360 Link),
//   - the seven Bus Comp knobs and the Bus Comp IN key drive its Bus Compressor 2 (or SSL 360 Link Bus Compressor),
//   - CHANNEL encoder: previous / next track; SOLO / CUT / polarity: the selected track's.
//
// The plug-ins are found and driven through Cubase's Direct Access API (page.mHostAccess.makeDirectAccess): the
// selected track's objects are searched for a plug-in whose name contains one of the names below, and its
// parameters are addressed by their index (the VST3 parameter order, the same numbers Rea-Sixty uses). The bridge
// sends the UC1's knobs and buttons as SysEx (20 / 21) and paints what the script sends back (track name, colour,
// plug-in label, value readouts, LEDs; SysEx 30-35). See cubase/docs/bridge-midi-protocol.md.
//
// Install in
//   Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty\UC1\
// (tools\install-scripts.ps1 does this for you.)

var midiremote_api = require('midiremote_api_v1')

var SCRIPT_VERSION = '0.1.1-dev'
console.log('[CubaseSixty UC1] script loaded, version ' + SCRIPT_VERSION)
function log(text) { console.log('[CubaseSixty UC1] ' + text) }

var SYSEX_HEADER = [0xF0, 0x7D, 0x52, 0x36]

var deviceDriver = midiremote_api.makeDeviceDriver('CubaseSixty', 'UC1', 'Riegus')
var midiInput = deviceDriver.mPorts.makeMidiInput()
var midiOutput = deviceDriver.mPorts.makeMidiOutput()
deviceDriver
	.makeDetectionUnit()
	.detectPortPair(midiInput, midiOutput)
	.expectInputNameEquals('Cubase-Sixty UC1 to Cubase')
	.expectOutputNameEquals('Cubase-Sixty UC1 from Cubase')

var surface = deviceDriver.mSurface

// ---------------------------------------------------------------------------
// UC1 control ids (Rea-Sixty extension/src/UC1Protocol.h)
// ---------------------------------------------------------------------------

var K = {
	LowPass: 0x00, HighPass: 0x01, HfGain: 0x02, HfFreq: 0x03, HmfGain: 0x04, HmfFreq: 0x05, HmfQ: 0x06,
	LmfGain: 0x07, LmfFreq: 0x08, LmfQ: 0x09, LfFreq: 0x0A, LfGain: 0x0B, InputTrim: 0x0C, FaderLevel: 0x16,
	BcRatio: 0x0E, BcScHpf: 0x0F, BcAttack: 0x10, BcRelease: 0x11, BcThreshold: 0x12, BcMakeup: 0x13, BcMix: 0x14,
	GateRelease: 0x17, GateHold: 0x18, GateThreshold: 0x19, GateRange: 0x1A,
	CompRelease: 0x1B, CompThreshold: 0x1C, CompRatio: 0x1D
}
var B = {
	HfBell: 0x08, EqType: 0x09, EqIn: 0x0A, LfBell: 0x0B, BusCompIn: 0x0C,
	Back: 0x0E, Confirm: 0x0F, Routing: 0x10, Presets: 0x11, Knob360: 0x12, Magnifier: 0x13, SecEncPush: 0x0D,
	FastAttComp: 0x14, Peak: 0x15, DynIn: 0x16, Expand: 0x17, FastAttGate: 0x18,
	Polarity: 0x19, ScListen: 0x1A, SoloClear: 0x1B, Solo: 0x1C, Cut: 0x1D, ChannelIn: 0x1E, Fine: 0x1F
}
var ZONE_CS = 0x03, ZONE_BC = 0x05

// ---------------------------------------------------------------------------
// Plug-in maps: UC1 control -> parameter index (Rea-Sixty extension/src/UC1PluginMap.cpp)
// ---------------------------------------------------------------------------

function pmap(match, label, knobs, buttons, bypass, inverted) {
	return { match: match, label: label, knobs: knobs, buttons: buttons, bypass: bypass, inverted: inverted || [] }
}
function kv(pairs) { var o = {}; for (var i = 0; i < pairs.length; i += 2) o[pairs[i]] = pairs[i + 1]; return o }
// Knobs whose parameter runs the other way (clockwise = lower value). Rea-Sixty also turns Gate Threshold
// round; in Cubase that one already runs the right way (tested on Channel Strip 2).
var CS_INVERSIONS = [K.LowPass, K.HmfQ, K.LmfQ, K.CompThreshold]

// Order matters: the first name contained in the plug-in's name wins.
var CHANNEL_STRIPS = [
	pmap('SSL 360 Link Bus', null, {}, {}, -1),   // the Bus Comp variant; listed so it is not taken for a channel strip
	pmap('Channel Strip 2', 'CS 2',
		kv([K.LowPass, 4, K.HighPass, 5, K.HfGain, 19, K.HfFreq, 18, K.HmfGain, 15, K.HmfFreq, 14, K.HmfQ, 16,
			K.LmfGain, 12, K.LmfFreq, 11, K.LmfQ, 13, K.LfFreq, 9, K.LfGain, 10, K.InputTrim, 2, K.FaderLevel, 38,
			K.CompThreshold, 24, K.CompRatio, 23, K.CompRelease, 25,
			K.GateThreshold, 30, K.GateRange, 31, K.GateRelease, 32, K.GateHold, 29]),
		kv([B.HfBell, 17, B.EqType, 7, B.EqIn, 6, B.LfBell, 8, B.FastAttComp, 22, B.Peak, 21, B.DynIn, 20,
			B.Expand, 27, B.FastAttGate, 28, B.ScListen, 37]),
		0, CS_INVERSIONS),
	pmap('4K G', '4K G',
		kv([K.LowPass, 21, K.HighPass, 20, K.HfGain, 36, K.HfFreq, 35, K.HmfGain, 33, K.HmfFreq, 32, K.HmfQ, 34,
			K.LmfGain, 29, K.LmfFreq, 28, K.LmfQ, 30, K.LfFreq, 24, K.LfGain, 25, K.InputTrim, 6, K.FaderLevel, 12,
			K.CompThreshold, 40, K.CompRatio, 39, K.CompRelease, 41,
			K.GateThreshold, 47, K.GateRange, 46, K.GateRelease, 48]),
		kv([B.HfBell, 37, B.EqType, 23, B.EqIn, 22, B.LfBell, 26, B.FastAttComp, 43, B.DynIn, 38,
			B.Expand, 50, B.FastAttGate, 49, B.ScListen, 51]),
		0),
	pmap('4K E', '4K E',
		kv([K.LowPass, 15, K.HighPass, 14, K.HfGain, 31, K.HfFreq, 30, K.HmfGain, 28, K.HmfFreq, 27, K.HmfQ, 29,
			K.LmfGain, 25, K.LmfFreq, 24, K.LmfQ, 26, K.LfFreq, 21, K.LfGain, 22, K.InputTrim, 2, K.FaderLevel, 6,
			K.CompThreshold, 36, K.CompRatio, 35, K.CompRelease, 37,
			K.GateThreshold, 43, K.GateRange, 42, K.GateRelease, 44]),
		kv([B.HfBell, 32, B.EqType, 19, B.EqIn, 18, B.LfBell, 23, B.FastAttComp, 39, B.DynIn, 33,
			B.Expand, 46, B.FastAttGate, 45, B.ScListen, 47]),
		0),
	pmap('4K B', '4K B',
		kv([K.LowPass, 11, K.HighPass, 10, K.HfGain, 26, K.HfFreq, 25, K.HmfGain, 23, K.HmfFreq, 22, K.HmfQ, 24,
			K.LmfGain, 20, K.LmfFreq, 19, K.LmfQ, 21, K.LfFreq, 16, K.LfGain, 17, K.InputTrim, 2, K.FaderLevel, 6,
			K.CompThreshold, 31, K.CompRatio, 30, K.CompRelease, 32,
			K.GateThreshold, 35, K.GateRange, 34, K.GateRelease, 36]),
		kv([B.HfBell, 27, B.EqIn, 14, B.LfBell, 18, B.DynIn, 28, B.Expand, 37, B.ScListen, 41]),
		0),
	pmap('SSL 360 Link', 'Link',
		kv([K.LowPass, 6, K.HighPass, 7, K.HfGain, 9, K.HfFreq, 10, K.HmfGain, 11, K.HmfFreq, 12, K.HmfQ, 13,
			K.LmfGain, 16, K.LmfFreq, 17, K.LmfQ, 18, K.LfFreq, 19, K.LfGain, 20, K.InputTrim, 4, K.FaderLevel, 1,
			K.CompThreshold, 27, K.CompRatio, 26, K.CompRelease, 28,
			K.GateThreshold, 30, K.GateRange, 29, K.GateRelease, 31, K.GateHold, 32]),
		kv([B.HfBell, 8, B.EqType, 14, B.EqIn, 15, B.LfBell, 21, B.FastAttComp, 24, B.Peak, 25, B.DynIn, 22,
			B.Expand, 33, B.FastAttGate, 34, B.ScListen, 36]),
		0)
]
var BUS_COMPS = [
	pmap('SSL 360 Link Bus Compressor', 'L-BC',
		kv([K.BcThreshold, 1, K.BcMakeup, 2, K.BcAttack, 3, K.BcRelease, 4, K.BcRatio, 5, K.BcScHpf, 6, K.BcMix, 7]),
		{}, 0),
	pmap('Bus Compressor 2', 'BC 2',
		kv([K.BcThreshold, 2, K.BcMakeup, 3, K.BcAttack, 4, K.BcRelease, 5, K.BcRatio, 6, K.BcScHpf, 7, K.BcMix, 8]),
		{}, 10)
]
function isBusCompKnob(id) { return id >= K.BcRatio && id <= K.BcMix }

// ---------------------------------------------------------------------------
// Messages to the bridge
// ---------------------------------------------------------------------------

function encodeName(text) {
	var out = []
	var str = text || ''
	for (var n = 0; n < str.length && n < 48; ++n) {
		var c = str.charCodeAt(n)
		out.push((c >> 14) & 0x7F, (c >> 7) & 0x7F, c & 0x7F)
	}
	return out
}
function sysex(activeDevice, body) { midiOutput.sendMidi(activeDevice, SYSEX_HEADER.concat(body, [0xF7])) }
var ledShown = {}
function setLed(activeDevice, button, state) {
	if (ledShown[button] === state) return
	ledShown[button] = state
	sysex(activeDevice, [0x34, button, state])
}
var labelShown = null
function setLabel(activeDevice, label) {
	if (labelShown === label) return
	labelShown = label
	sysex(activeDevice, [0x32].concat(encodeName(label)))
}
// "<label padded to 16><value right-aligned in 7>", Rea-Sixty's readout format.
function readout(activeDevice, zone, label, value) {
	var l = (label || '').substr(0, 16)
	while (l.length < 16) l += ' '
	var v = value || ''
	while (v.length < 7) v = ' ' + v
	sysex(activeDevice, [0x33, zone].concat(encodeName(l + v)))
}

// ---------------------------------------------------------------------------
// Mapping
// ---------------------------------------------------------------------------

var page = deviceDriver.mMapping.makePage('UC1')
var selected = page.mHostAccess.mTrackSelection.mMixerChannel
var track = selected.mValue
var da = page.mHostAccess.makeDirectAccess(selected)

// Note-bound keys (the bridge sends these as notes, with the LED coming back the same way).
function noteButton(x, note) {
	var button = surface.makeButton(x, 0, 1, 1)
	button.mSurfaceValue.mMidiBinding.setInputPort(midiInput).setOutputPort(midiOutput).bindToNote(0, note)
	return button
}
page.makeValueBinding(noteButton(0, B.Solo).mSurfaceValue, track.mSolo).setTypeToggle()
page.makeValueBinding(noteButton(1, B.Cut).mSurfaceValue, track.mMute).setTypeToggle()
try {
	page.makeValueBinding(noteButton(2, B.Polarity).mSurfaceValue, selected.mPreFilter.mPhaseSwitch).setTypeToggle()
} catch (e) { log('polarity could not be bound: ' + e) }
function commandButton(x, note, category, command) {
	var button = surface.makeButton(x, 1, 1, 1)
	button.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToNote(0, note)
	page.makeCommandBinding(button.mSurfaceValue, category, command)
}
commandButton(0, 100, 'Project', 'Select Track: Next')
commandButton(1, 101, 'Project', 'Select Track: Prev')
commandButton(2, B.SoloClear, 'Edit', 'Deactivate All Solo')

// The mapping Direct Access calls need. Kept from the page's callbacks (the SysEx handler gets none).
var am = null
var state = {
	cs: null,        // { obj, map, name } the channel strip plug-in on the selected track
	bc: null,        // the bus compressor
	rescanAt: 0,     // when to look at the selected track's plug-ins again (0 = not pending)
	fine: false,
	dumped: {}       // plug-in names whose parameters were already listed in the log
}

// Every object under the selected track, depth first: { id, depth }.
function walk(base) {
	var out = []
	function rec(id, depth) {
		out.push({ id: id, depth: depth })
		if (depth >= 4) return
		var n = 0
		try { n = da.getNumberOfChildObjects(am, id) } catch (e) { return }
		for (var i = 0; i < n && i < 64; ++i) rec(da.getChildObjectID(am, id, i), depth + 1)
	}
	rec(base, 0)
	return out
}
function findMap(list, name) {
	for (var i = 0; i < list.length; ++i) if (name.indexOf(list[i].match) >= 0) return list[i]
	return null
}

var scans = 0
function rescan(activeDevice) {
	if (!am) return
	state.cs = null
	state.bc = null
	var base
	try { base = da.getBaseObjectID(am) } catch (e) { log('getBaseObjectID failed: ' + e); return }
	var objects = walk(base)
	var verbose = ++scans <= 3
	if (verbose) log('scan ' + scans + ': ' + objects.length + ' objects under the selected track (base ' + base + ')')
	for (var i = 0; i < objects.length; ++i) {
		var id = objects[i].id
		var title = '', type = '', params = 0
		try { title = da.getObjectTitle(am, id) } catch (e) {}
		try { type = da.getObjectTypeName(am, id) } catch (e) {}
		try { params = da.getNumberOfParameters(am, id) } catch (e) {}
		if (verbose) log('  ' + new Array(objects[i].depth + 1).join('  ') + '#' + id + ' [' + type + '] "' + title + '" params ' + params)
		if (params < 4 || !title) continue
		var bc = findMap(BUS_COMPS, title)
		if (bc && !state.bc) { state.bc = { obj: id, map: bc, name: title }; dumpParams(id, title); continue }
		var cs = findMap(CHANNEL_STRIPS, title)
		if (cs && cs.label && !state.cs) { state.cs = { obj: id, map: cs, name: title }; dumpParams(id, title) }
	}
	log('selected track: channel strip ' + (state.cs ? '"' + state.cs.name + '"' : 'none')
		+ ', bus comp ' + (state.bc ? '"' + state.bc.name + '"' : 'none'))
	setLabel(activeDevice, state.cs ? state.cs.map.label : state.bc ? state.bc.map.label : '')
	if (!state.cs) sysex(activeDevice, [0x35, ZONE_CS])
	if (!state.bc) sysex(activeDevice, [0x35, ZONE_BC])
	paintLeds(activeDevice)
}

// The first time a plug-in is found, list its parameters (index, tag, name, value), to check the maps.
function dumpParams(obj, name) {
	if (state.dumped[name]) return
	state.dumped[name] = true
	var n = da.getNumberOfParameters(am, obj)
	log('parameters of "' + name + '" (' + n + '):')
	for (var i = 0; i < n && i < 80; ++i) {
		var tag = da.getParameterTagByIndex(am, obj, i)
		log('  ' + i + ' tag ' + tag + ' "' + da.getParameterTitle(am, obj, tag, 32) + '" = '
			+ da.getParameterDisplayValue(am, obj, tag) + ' ' + da.getParameterDisplayUnits(am, obj, tag)
			+ ' (' + da.getParameterProcessValue(am, obj, tag).toFixed(3) + ')')
	}
}

function paramValue(target, index) {
	var tag = da.getParameterTagByIndex(am, target.obj, index)
	return da.getParameterProcessValue(am, target.obj, tag)
}
function toggleParam(target, index) {
	var tag = da.getParameterTagByIndex(am, target.obj, index)
	var v = da.getParameterProcessValue(am, target.obj, tag)
	da.setParameterProcessValue(am, target.obj, tag, v >= 0.5 ? 0 : 1)
}

// Button LEDs from the plug-ins' parameters. IN keys: lit while the plug-in is not bypassed.
var CS_BUTTONS = [B.HfBell, B.EqType, B.EqIn, B.LfBell, B.FastAttComp, B.Peak, B.DynIn, B.Expand, B.FastAttGate, B.ScListen]
function paintLeds(activeDevice) {
	for (var i = 0; i < CS_BUTTONS.length; ++i) {
		var b = CS_BUTTONS[i]
		var on = false
		if (state.cs && state.cs.map.buttons[b] !== undefined) {
			try { on = paramValue(state.cs, state.cs.map.buttons[b]) >= 0.5 } catch (e) {}
		}
		setLed(activeDevice, b, on ? 1 : 0)
	}
	var csIn = false, bcIn = false
	try { if (state.cs) csIn = paramValue(state.cs, state.cs.map.bypass) < 0.5 } catch (e) {}
	try { if (state.bc) bcIn = paramValue(state.bc, state.bc.map.bypass) < 0.5 } catch (e) {}
	setLed(activeDevice, B.ChannelIn, csIn ? 1 : 0)
	setLed(activeDevice, B.BusCompIn, bcIn ? 1 : 0)
	setLed(activeDevice, B.Fine, state.fine ? 1 : 0)
}

function onKnob(activeDevice, id, clicks) {
	var target = isBusCompKnob(id) ? state.bc : state.cs
	if (!target) return
	var index = target.map.knobs[id]
	if (index === undefined) return
	var tag = da.getParameterTagByIndex(am, target.obj, index)
	var step = clicks / 64 * (state.fine ? 0.25 : 1)
	if (target.map.inverted.indexOf(id) >= 0) step = -step
	var v = da.getParameterProcessValue(am, target.obj, tag) + step
	v = Math.max(0, Math.min(1, v))
	da.setParameterProcessValue(am, target.obj, tag, v)
	var value = da.getParameterDisplayValue(am, target.obj, tag)
	var units = da.getParameterDisplayUnits(am, target.obj, tag)
	readout(activeDevice, target === state.bc ? ZONE_BC : ZONE_CS, da.getParameterTitle(am, target.obj, tag, 16),
		value + (units ? units : ''))
}

function onButton(activeDevice, id) {
	if (id === B.Fine) { state.fine = !state.fine; setLed(activeDevice, B.Fine, state.fine ? 1 : 0); return }
	if (id === B.ChannelIn) { if (state.cs) toggleParam(state.cs, state.cs.map.bypass); paintLeds(activeDevice); return }
	if (id === B.BusCompIn) { if (state.bc) toggleParam(state.bc, state.bc.map.bypass); paintLeds(activeDevice); return }
	if (state.cs && state.cs.map.buttons[id] !== undefined) {
		toggleParam(state.cs, state.cs.map.buttons[id])
		paintLeds(activeDevice)
		return
	}
	log('button 0x' + id.toString(16) + ' has no function yet')
}

var sysexErrors = 0
midiInput.mOnSysex = function (activeDevice, message) {
	if (message.length < 7 || message[4] === undefined) return
	var cmd = message[4]
	if (cmd !== 0x20 && cmd !== 0x21) return
	if (!am) { if (++sysexErrors <= 3) log('UC1 event before the page was active; ignored'); return }
	try {
		if (cmd === 0x20) onKnob(activeDevice, message[5], message[6] - 64)
		else if (message[6] === 1) onButton(activeDevice, message[5])
	} catch (e) {
		if (++sysexErrors <= 10) log('event 0x' + cmd.toString(16) + ' id 0x' + message[5].toString(16) + ' failed: ' + e)
	}
}

// Parameter changes from anywhere (the plug-in's window, automation): keep the LEDs right.
da.mOnParameterChange = function (activeDevice, activeMapping, objectID, tag) {
	am = activeMapping
	if ((state.cs && objectID === state.cs.obj) || (state.bc && objectID === state.bc.obj)) paintLeds(activeDevice)
}
// Objects added or removed (a plug-in inserted, removed or replaced): look again shortly.
da.mOnObjectChange = function (activeDevice, activeMapping, objectID) {
	am = activeMapping
	state.rescanAt = Date.now() + 300
}

selected.mOnTitleChange = function (activeDevice, activeMapping, title) {
	am = activeMapping
	sysex(activeDevice, [0x30].concat(encodeName(title)))
	state.rescanAt = Date.now() + 100
}
selected.mOnColorChange = function (activeDevice, activeMapping, r, g, b, a, isActive) {
	sysex(activeDevice, [0x31, Math.round(r * 127), Math.round(g * 127), Math.round(b * 127)])
}

page.mOnActivate = function (activeDevice, activeMapping) {
	am = activeMapping
	ledShown = {}
	labelShown = null
	log('page active')
	state.rescanAt = Date.now() + 100
}
page.mOnIdle = function (activeDevice, activeMapping) {
	am = activeMapping
	if (state.rescanAt && Date.now() >= state.rescanAt) {
		state.rescanAt = 0
		try { rescan(activeDevice) } catch (e) { log('scan failed: ' + e) }
	}
}
