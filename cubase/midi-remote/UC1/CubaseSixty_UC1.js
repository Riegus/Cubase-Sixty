// Cubase-Sixty – MIDI Remote script for the SSL UC1 (via the Cubase-Sixty Bridge)
//
// The UC1 controls the SSL plug-ins on the SELECTED track, like SSL 360 does:
//   - the Channel Strip knobs and buttons drive the track's channel strip plug-in (Channel Strip 2, 4K E / G / B,
//     SSL 360 Link),
//   - the seven Bus Comp knobs and the Bus Comp IN key drive a Bus Compressor 2 (or SSL 360 Link Bus Compressor)
//     anywhere in the project: the Bus Comp encoder steps through them (the large name row shows their tracks);
//     until one is picked, the one on an output channel, else the first,
//   - CHANNEL encoder: previous / next track; SOLO / CUT / polarity: the selected track's,
//   - the 7-segment display: the selected channel's number in the MixConsole.
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

// Log lines also go to the bridge (SysEx 7E <source>), which writes them to cubase\logs\scripts.log, so they can
// be read without Cubase's script console. They are queued and sent from the idle callback (sending needs the
// active device).
var LOG_SOURCE = 3
var logQueue = []
function out(text) {
	console.log(text)
	if (logQueue.length < 1000) logQueue.push(String(text))
}
function flushLog(activeDevice) {
	while (logQueue.length) {
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

var SCRIPT_VERSION = '0.4.4-dev'
out('[CubaseSixty UC1] script loaded, version ' + SCRIPT_VERSION)
function log(text) { out('[CubaseSixty UC1] ' + text) }

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

function pmap(match, label, knobs, buttons, bypass, inverted, header) {
	return { match: match, label: label, knobs: knobs, buttons: buttons, bypass: bypass, inverted: inverted || [], header: header || '' }
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
		{}, 0, [], '360 LINK BC'),
	pmap('Bus Compressor 2', 'BC 2',
		kv([K.BcThreshold, 2, K.BcMakeup, 3, K.BcAttack, 4, K.BcRelease, 5, K.BcRatio, 6, K.BcScHpf, 7, K.BcMix, 8]),
		{}, 10, [], 'BUS COMP 2')
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
// A knob's LED ring: value 0..1, or null for off.
var ringShown = {}
function setRing(activeDevice, knob, value, dim) {
	var v = value === null ? 0x3FFF : Math.max(0, Math.min(16382, Math.round(value * 16382)))
	var key = v * 2 + (dim ? 1 : 0)
	if (ringShown[knob] === key) return
	ringShown[knob] = key
	sysex(activeDevice, [0x36, knob, (v >> 7) & 0x7F, v & 0x7F, dim ? 1 : 0])
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

// The whole MixConsole, for the Bus Comp section (which stays on its own track, as with SSL 360's Bus Comp
// encoder). Created in a try: if Cubase refuses, the Bus Comp knobs fall back to the selected track's bus comp.
var daMix = null
try { daMix = page.mHostAccess.makeDirectAccess(page.mHostAccess.mMixConsole) } catch (e) { log('no Direct Access to the MixConsole: ' + e) }

// The mapping Direct Access calls need. Kept from the page's callbacks (the SysEx handler gets none).
var am = null
// A target is what one section of the UC1 drives: { da (the Direct Access it was found through), label, header,
// knobs: knob id -> ref, buttons: button id -> ref, bypass: ref (the IN key; lit while the ref reads "on"),
// objs: the object ids involved, knobByKey: "obj:tag" -> knob id }. A ref is one parameter:
// { obj, tag, invert (true: flipped, for knobs that run the other way / LEDs lit at 0), enable (a ref switched on
// when the knob is turned, e.g. an EQ band's on switch) }. SSL plug-ins: built from the maps above; Cubase's own
// channel strip: built from the parameters' names (cubaseStrip below).
function ref(acc, obj, tag, invert) { return { obj: obj, tag: tag, invert: !!invert } }
function makeTarget(acc, obj, map, name) {
	var t = { da: acc, obj: obj, map: map, name: name, label: map.label, header: map.header, knobs: {}, buttons: {}, bypass: null, objs: {} }
	t.objs[obj] = true
	for (var k in map.knobs) t.knobs[k] = ref(acc, obj, acc.getParameterTagByIndex(am, obj, map.knobs[k]), map.inverted.indexOf(Number(k)) >= 0)
	for (var b in map.buttons) t.buttons[b] = ref(acc, obj, acc.getParameterTagByIndex(am, obj, map.buttons[b]), false)
	if (map.bypass >= 0) t.bypass = ref(acc, obj, acc.getParameterTagByIndex(am, obj, map.bypass), true)
	indexKnobs(t)
	return t
}
function indexKnobs(t) {
	t.knobByKey = {}
	for (var k in t.knobs) t.knobByKey[t.knobs[k].obj + ':' + t.knobs[k].tag] = Number(k)
}
var state = {
	cs: null,          // the channel strip plug-in on the selected track
	bcSelected: null,  // the bus comp on the selected track (used when the MixConsole cannot be searched)
	bcList: [],        // every bus comp in the project, in MixConsole order
	bcAnchor: null,    // object id of the bus comp the Bus Comp section is on (null = not chosen yet)
	rescanAt: 0,       // when to look at the selected track's plug-ins again (0 = not pending)
	mixScanAt: 0,      // when to search the MixConsole for bus comps again (0 = not pending)
	fine: false,
	attack: { comp: false, gate: false },   // Cubase strip: the Release knob is on attack (F.ATT)
	dumped: {}         // plug-in names whose parameters were already listed in the log
}

// Every object under base, depth first: { id, depth, parent index }. A channel's branches of the kinds in skip are
// not entered (sends, panners, EQ ... cannot hold plug-ins); elsewhere the same kinds can be groups of channels
// (the MixConsole's output channels may sit under an "Outputs" object). Each object is listed once.
var SKIP_TYPES = { QuickControls: 1, InputFilter: 1, Panner: 1, Modulators: 1, Strips: 1, EQ: 1, Sends: 1,
	Outputs: 1, Foldbacks: 1, DirectRouting: 1 }
// Inside the selected track the channel strip and input filter are wanted, so only these are skipped there.
var SKIP_TYPES_TRACK = { QuickControls: 1, Panner: 1, Modulators: 1, Sends: 1, Outputs: 1, Foldbacks: 1, DirectRouting: 1 }
function walk(acc, base, maxDepth, limit, skip) {
	skip = skip || SKIP_TYPES
	var out = []
	var seen = {}
	function rec(id, depth, parent) {
		if (out.length >= limit || seen[id]) return
		seen[id] = true
		var type = ''
		try { type = acc.getObjectTypeName(am, id) } catch (e) {}
		out.push({ id: id, depth: depth, parent: parent, type: type })
		var me = out.length - 1
		var inChannel = parent >= 0 && /Channel$/.test(out[parent].type)
		if (depth >= maxDepth || (inChannel && skip[type])) return
		var n = 0
		try { n = acc.getNumberOfChildObjects(am, id) } catch (e) { return }
		for (var i = 0; i < n && i < 512; ++i) rec(acc.getChildObjectID(am, id, i), depth + 1, me)
	}
	rec(base, 0, -1)
	return out
}
function findMap(list, name) {
	for (var i = 0; i < list.length; ++i) if (name.indexOf(list[i].match) >= 0) return list[i]
	return null
}
function info(acc, id) {
	var r = { title: '', params: 0 }
	try { r.title = acc.getObjectTitle(am, id) } catch (e) {}
	try { r.params = acc.getNumberOfParameters(am, id) } catch (e) {}
	return r
}

var scans = 0
function rescan(activeDevice) {
	if (!am) return
	state.cs = null
	state.bcSelected = null
	var base
	try { base = da.getBaseObjectID(am) } catch (e) { log('getBaseObjectID failed: ' + e); return }
	var objects = walk(da, base, 4, 400, SKIP_TYPES_TRACK)
	++scans
	for (var i = 0; i < objects.length; ++i) {
		var id = objects[i].id
		var o = info(da, id)
		if (o.params < 4 || !o.title) continue
		var bc = findMap(BUS_COMPS, o.title)
		if (bc && !state.bcSelected) { state.bcSelected = makeTarget(da, id, bc, o.title); dumpParams(da, id, o.title); continue }
		var cs = findMap(CHANNEL_STRIPS, o.title)
		if (cs && cs.label && !state.cs) { state.cs = makeTarget(da, id, cs, o.title); dumpParams(da, id, o.title) }
	}
	// No SSL channel strip: Cubase's own (input filter, EQ, the strip's gate and compressor).
	if (!state.cs) state.cs = cubaseStrip(objects)
	log('selected track: channel strip ' + (state.cs ? '"' + state.cs.name + '"' : 'none')
		+ ', bus comp ' + (state.bcSelected ? '"' + state.bcSelected.name + '" #' + state.bcSelected.obj : 'none'))
	showNumber(activeDevice, base)
	paintAll(activeDevice)
}

// ---------------------------------------------------------------------------
// Cubase's own channel strip, for tracks without an SSL channel strip: the input filter (gain, low / high cut), the
// channel EQ (LF = band 1, LMF = 2, HMF = 3, HF = 4) and the strip's gate and compressor modules (when loaded).
// Their parameters are found by name; the first time each is seen its parameter list goes to the log.
// ---------------------------------------------------------------------------
function paramList(obj) {
	var out = []
	var n = 0
	try { n = da.getNumberOfParameters(am, obj) } catch (e) {}
	for (var i = 0; i < n && i < 200; ++i) {
		var tag = da.getParameterTagByIndex(am, obj, i)
		out.push({ tag: tag, title: da.getParameterTitle(am, obj, tag, 40) })
	}
	return out
}
// The first parameter whose name matches every pattern (and none of the excluded ones).
function findParam(list, patterns, exclude) {
	for (var i = 0; i < list.length; ++i) {
		var ok = true
		for (var p = 0; p < patterns.length && ok; ++p) if (!patterns[p].test(list[i].title)) ok = false
		for (var x = 0; exclude && x < exclude.length && ok; ++x) if (exclude[x].test(list[i].title)) ok = false
		if (ok) return list[i]
	}
	return null
}
function band(n) { return new RegExp('(^|\\D)' + n + '(\\D|$)') }
var RX = {
	gain: /gain/i, freq: /freq/i, q: /(^|\W)q(\W|$)|quality/i, on: /(^|\W)on(\W|$)|enable|active/i,
	bypass: /bypass/i, lowCut: /low\s*cut|lo\s*cut|(^|\W)lc(\W|$)|high\s*pass|(^|\W)hp/i,
	highCut: /high\s*cut|hi\s*cut|(^|\W)hc(\W|$)|low\s*pass|(^|\W)lp/i,
	thresh: /thresh/i, ratio: /ratio/i, release: /release/i, range: /range/i, hold: /(^|\W)hold/i, attack: /attack/i, digit: /\d/, auto: /auto|mode/i
}
var stripLogs = 0
function cubaseStrip(objects) {
	var inputFilter = null, eq = null, gate = null, comp = null
	for (var i = 0; i < objects.length; ++i) {
		var o = objects[i]
		if (o.type === 'InputFilter') inputFilter = o.id
		else if (o.type === 'EQ') eq = o.id
		else if (o.type !== 'Slot' && o.parent >= 0 && objects[o.parent].type === 'Slot') {
			// A strip module sits in a slot under the track's "Strip"; insert plug-ins sit in slots under "Inserts".
			var folder = objects[o.parent].parent
			if (folder < 0 || objects[folder].type !== 'Strips') continue
			var title = info(da, o.id).title
			if (/gate/i.test(title) && !gate) gate = o.id
			else if (/comp/i.test(title) && !comp) comp = o.id
		}
	}
	if (!inputFilter && !eq && !gate && !comp) return null
	var t = { da: da, label: 'CUBASE', name: 'Cubase channel strip', header: '', knobs: {}, buttons: {}, bypass: null, objs: {} }
	var found = [], missing = []
	function knob(id, name, obj, list, patterns, exclude, enable, offAt) {
		var p = obj ? findParam(list, patterns, exclude) : null
		if (!p) { missing.push(name); return }
		t.knobs[id] = { obj: obj, tag: p.tag, invert: false, title: p.title.substr(0, 16), enable: enable, offAt: offAt }
		t.objs[obj] = true
		found.push(name + '="' + p.title + '"')
	}
	function sw(obj, list, patterns, exclude) {
		var p = obj ? findParam(list, patterns, exclude) : null
		return p ? { obj: obj, tag: p.tag, invert: false, title: p.title } : null
	}
	var inList = inputFilter ? paramList(inputFilter) : []
	var eqList = eq ? paramList(eq) : []
	var gateList = gate ? paramList(gate) : []
	var compList = comp ? paramList(comp) : []
	if (inputFilter) dumpParams(da, inputFilter, 'Cubase input filter')
	if (eq) dumpParams(da, eq, 'Cubase channel EQ')
	if (gate) dumpParams(da, gate, 'Cubase strip gate: ' + info(da, gate).title)
	if (comp) dumpParams(da, comp, 'Cubase strip compressor: ' + info(da, comp).title)

	knob(K.InputTrim, 'trim', inputFilter, inList, [RX.gain])
	var lcOn = sw(inputFilter, inList, [RX.lowCut, RX.on]), hcOn = sw(inputFilter, inList, [RX.highCut, RX.on])
	// The cut filters switch off at the end of their range, as on an SSL strip (low cut fully left, high cut fully right).
	knob(K.HighPass, 'low cut', inputFilter, inList, [RX.lowCut, RX.freq], null, lcOn, 0)
	knob(K.LowPass, 'high cut', inputFilter, inList, [RX.highCut, RX.freq], null, hcOn, 1)
	var bands = [[1, K.LfGain, K.LfFreq, null], [2, K.LmfGain, K.LmfFreq, K.LmfQ], [3, K.HmfGain, K.HmfFreq, K.HmfQ], [4, K.HfGain, K.HfFreq, null]]
	for (var b = 0; b < bands.length; ++b) {
		var n = bands[b][0], bx = band(n)
		var on = sw(eq, eqList, [bx, RX.on])
		knob(bands[b][1], 'band ' + n + ' gain', eq, eqList, [bx, RX.gain], null, on)
		knob(bands[b][2], 'band ' + n + ' freq', eq, eqList, [bx, RX.freq], null, on)
		if (bands[b][3] !== null) knob(bands[b][3], 'band ' + n + ' Q', eq, eqList, [bx, RX.q], null, on)
	}
	knob(K.CompThreshold, 'comp threshold', comp, compList, [RX.thresh])
	knob(K.CompRatio, 'comp ratio', comp, compList, [RX.ratio])
	// "Auto Release" (a switch) must not be taken for the release time.
	knob(K.CompRelease, 'comp release', comp, compList, [RX.release], [RX.auto])
	knob(K.GateThreshold, 'gate threshold', gate, gateList, [RX.thresh])
	knob(K.GateRange, 'gate range', gate, gateList, [RX.range])
	knob(K.GateRelease, 'gate release', gate, gateList, [RX.release], [RX.auto])
	knob(K.GateHold, 'gate hold', gate, gateList, [RX.hold], [RX.thresh])
	// The UC1 has no attack knobs: the F.ATT keys switch their section's Release knob between release and attack
	// (lit = attack). The choice stays when you change tracks.
	t.flip = {}
	function attackFlip(key, mode, releaseKnob, obj, list) {
		var p = obj ? findParam(list, [RX.attack], [RX.auto]) : null
		if (!p || !t.knobs[releaseKnob]) return
		t.flip[key] = { mode: mode, knob: releaseKnob, release: t.knobs[releaseKnob],
			attack: { obj: obj, tag: p.tag, invert: false, title: p.title.substr(0, 16) } }
		found.push(mode + ' attack="' + p.title + '"')
		if (state.attack[mode]) t.knobs[releaseKnob] = t.flip[key].attack
	}
	attackFlip(B.FastAttComp, 'comp', K.CompRelease, comp, compList)
	attackFlip(B.FastAttGate, 'gate', K.GateRelease, gate, gateList)
	// EQ IN: lit while the EQ is not bypassed. DYN IN: the compressor's bypass, if it has one.
	var eqBypass = sw(eq, eqList, [RX.bypass], [RX.digit])
	if (eqBypass) { eqBypass.invert = true; t.buttons[B.EqIn] = eqBypass; t.objs[eq] = true }
	var compBypass = sw(comp, compList, [RX.bypass])
	if (compBypass) { compBypass.invert = true; t.buttons[B.DynIn] = compBypass; t.objs[comp] = true }
	if (++stripLogs <= 5) {
		log('Cubase strip:' + (missing.length ? ' not found: ' + missing.join(', ') : ' everything found')
			+ '; EQ IN ' + (eqBypass ? '"' + eqBypass.title + '"' : 'none') + ', DYN IN ' + (compBypass ? '"' + compBypass.title + '"' : 'none'))
		for (var f = 0; f < found.length; ++f) log('Cubase strip:   ' + found[f])
	}
	indexKnobs(t)
	return t
}

// The 7-segment number: the selected channel's number in the MixConsole. Direct Access getMixerChannelIndex gives it
// as is (tested: index 50 for the channel the MixConsole numbers 50).
// Hiding or showing a track renumbers the others without telling the script, so the idle callback asks again
// once a second.
var numberLogs = 0, numberShown = -2, numberBase = -1, numberCheckAt = 0
function showNumber(activeDevice, base) {
	numberBase = base
	var index = -1, zone = -1
	try { index = da.getMixerChannelIndex(am, base) } catch (e) {}
	var v = index > 0 ? index : 0x3FFF
	if (v === numberShown) return
	if (++numberLogs <= 10) {
		try { zone = da.getMixerChannelZone(am, base) } catch (e) {}
		log('mixer channel index ' + index + ', zone ' + zone)
	}
	numberShown = v
	sysex(activeDevice, [0x37, (v >> 7) & 0x7F, v & 0x7F])
}

// Every bus comp in the project. Each is listed with the track (channel) it sits on.
var mixScans = 0, lastMixScan = 0
function scanMixer(activeDevice) {
	if (!daMix || !am) return
	lastMixScan = Date.now()
	var base
	try { base = daMix.getBaseObjectID(am) } catch (e) { log('MixConsole getBaseObjectID failed: ' + e); daMix = null; return }
	var objects = walk(daMix, base, 6, 20000)
	var verbose = ++mixScans <= 1
	var list = []
	for (var i = 0; i < objects.length; ++i) {
		var o = objects[i]
		if (verbose && o.depth <= 3 && !/^(Slot|Panner)$/.test(o.type)) log('mix ' + new Array(o.depth + 1).join('  ') + '#' + o.id + ' [' + o.type + '] "' + info(daMix, o.id).title + '"')
		if (o.depth < 2) continue
		var inf = info(daMix, o.id)
		if (inf.params < 4 || !inf.title) continue
		var map = findMap(BUS_COMPS, inf.title)
		if (!map) continue
		// The track: the nearest enclosing object whose kind is a channel.
		var p = o.parent, track = '', output = false
		while (p >= 0) {
			if (/Channel$/.test(objects[p].type)) {
				track = info(daMix, objects[p].id).title
				output = /Output/.test(objects[p].type)
				break
			}
			p = objects[p].parent
		}
		var dup = false
		for (var d = 0; d < list.length; ++d) if (list[d].obj === o.id) dup = true
		if (!dup) {
			var t = makeTarget(daMix, o.id, map, inf.title)
			t.track = track
			t.output = output
			list.push(t)
		}
	}
	if (verbose || list.length !== state.bcList.length)
		log('MixConsole: ' + objects.length + ' objects, ' + list.length + ' bus comps'
			+ (list.length ? ': ' + list.map(function (b) { return '"' + b.track + '" #' + b.obj }).join(', ') : ''))
	state.bcList = list
	showBcNames(activeDevice)
	paintAll(activeDevice)
}

// The bus comp the Bus Comp section controls: the one picked with the Bus Comp encoder, else one on an output
// channel, else the first in the project (Rea-Sixty: the master's, else the first track's), else the selected track's.
function anchorIndex() {
	var list = state.bcList
	for (var i = 0; i < list.length; ++i) if (list[i].obj === state.bcAnchor) return i
	for (var j = 0; j < list.length; ++j) if (list[j].output) return j
	return list.length ? 0 : -1
}
function busComp() {
	var i = anchorIndex()
	return i >= 0 ? state.bcList[i] : state.bcSelected
}
function showBcNames(activeDevice) {
	var i = anchorIndex()
	var list = state.bcList
	var names = ['', '', '']
	if (i >= 0) {
		names[1] = list[i].track
		if (i > 0) names[0] = list[i - 1].track
		if (i + 1 < list.length) names[2] = list[i + 1].track
	}
	var sep = [0x7F, 0x7F, 0x7F]
	sysex(activeDevice, [0x38].concat(encodeName(names[0]), sep, encodeName(names[1]), sep, encodeName(names[2])))
}
// The Bus Comp encoder: step through the project's bus comps.
// The MixConsole does not always report a plug-in added to another track (one added to the Stereo Out was missed),
// so the list is read again when the encoder is turned after a pause (a search takes ~70 ms in a big project).
function onBcEncoder(activeDevice, steps) {
	if (!state.bcList.length || Date.now() - lastMixScan > 2000) scanMixer(activeDevice)
	var list = state.bcList
	if (!list.length) return
	var i = anchorIndex()
	var next = Math.max(0, Math.min(list.length - 1, i + steps))
	state.bcAnchor = list[next].obj
	showBcNames(activeDevice)
	sysex(activeDevice, [0x39].concat(encodeName(list[next].header)))
	sysex(activeDevice, [0x35, ZONE_BC])
	paintAll(activeDevice)
}

function paintAll(activeDevice) {
	var bc = busComp()
	setLabel(activeDevice, state.cs ? state.cs.label : bc ? bc.label : '')
	if (!state.cs) sysex(activeDevice, [0x35, ZONE_CS])
	if (!bc) sysex(activeDevice, [0x35, ZONE_BC])
	paintLeds(activeDevice)
	paintRings(activeDevice)
}

function targetFor(knob) { return isBusCompKnob(knob) ? busComp() : state.cs }
// The ring shows where the knob points: flipped for the knobs whose parameter runs the other way.
function paintRing(activeDevice, knob) {
	var target = targetFor(knob)
	var rf = target ? target.knobs[knob] : undefined
	if (!rf) { setRing(activeDevice, knob, null, false); return }
	var v = paramValue(target, rf)
	if (rf.invert) v = 1 - v
	setRing(activeDevice, knob, v, false)
}
function paintRings(activeDevice) {
	for (var knob = 0x00; knob <= 0x1D; ++knob) {
		if (knob === 0x0D || knob === 0x15) continue   // the CHANNEL and Bus Comp encoders have no ring
		try { paintRing(activeDevice, knob) } catch (e) { log('ring 0x' + knob.toString(16) + ': ' + e) }
	}
}

// The first time a plug-in is found, list its parameters (index, tag, name, value), to check the maps.
function dumpParams(acc, obj, name) {
	if (state.dumped[name]) return
	state.dumped[name] = true
	var n = acc.getNumberOfParameters(am, obj)
	log('parameters of "' + name + '" (' + n + '):')
	for (var i = 0; i < n && i < 80; ++i) {
		var tag = acc.getParameterTagByIndex(am, obj, i)
		log('  ' + i + ' tag ' + tag + ' "' + acc.getParameterTitle(am, obj, tag, 32) + '" = '
			+ acc.getParameterDisplayValue(am, obj, tag) + ' ' + acc.getParameterDisplayUnits(am, obj, tag)
			+ ' (' + acc.getParameterProcessValue(am, obj, tag).toFixed(3) + ')')
	}
}

function paramValue(target, rf) { return target.da.getParameterProcessValue(am, rf.obj, rf.tag) }
function toggleParam(target, rf) {
	if (!rf) return
	var v = paramValue(target, rf)
	target.da.setParameterProcessValue(am, rf.obj, rf.tag, v >= 0.5 ? 0 : 1)
}
// A switch's LED: lit when the parameter is on (or, for refs marked invert, off: a bypass lights IN when 0).
function lit(target, rf) { return (paramValue(target, rf) >= 0.5) !== rf.invert }

// Button LEDs from the plug-ins' parameters. IN keys: lit while the plug-in is not bypassed.
var CS_BUTTONS = [B.HfBell, B.EqType, B.EqIn, B.LfBell, B.FastAttComp, B.Peak, B.DynIn, B.Expand, B.FastAttGate, B.ScListen]
function paintLeds(activeDevice) {
	for (var i = 0; i < CS_BUTTONS.length; ++i) {
		var b = CS_BUTTONS[i]
		var on = false
		if (state.cs && state.cs.flip && state.cs.flip[b]) on = state.attack[state.cs.flip[b].mode]
		else if (state.cs && state.cs.buttons[b]) {
			try { on = lit(state.cs, state.cs.buttons[b]) } catch (e) {}
		}
		setLed(activeDevice, b, on ? 1 : 0)
	}
	var csIn = false, bcIn = false
	var bc = busComp()
	try { if (state.cs && state.cs.bypass) csIn = lit(state.cs, state.cs.bypass) } catch (e) {}
	try { if (bc && bc.bypass) bcIn = lit(bc, bc.bypass) } catch (e) {}
	setLed(activeDevice, B.ChannelIn, csIn ? 1 : 0)
	setLed(activeDevice, B.BusCompIn, bcIn ? 1 : 0)
	setLed(activeDevice, B.Fine, state.fine ? 1 : 0)
}

function onKnob(activeDevice, id, clicks) {
	if (id === 0x15) { onBcEncoder(activeDevice, clicks); return }
	var target = targetFor(id)
	if (!target) return
	var rf = target.knobs[id]
	if (!rf) return
	var acc = target.da
	var step = clicks / 64 * (state.fine ? 0.25 : 1)
	if (rf.invert) step = -step
	var v = acc.getParameterProcessValue(am, rf.obj, rf.tag) + step
	v = Math.max(0, Math.min(1, v))
	acc.setParameterProcessValue(am, rf.obj, rf.tag, v)
	// Turning a knob whose section is switched off switches it on (a Cubase EQ band, a cut filter); a cut filter
	// turned to the end of its range switches off.
	if (rf.enable) {
		var want = rf.offAt === undefined || Math.abs(v - rf.offAt) > 0.0001 ? 1 : 0
		var isOn = acc.getParameterProcessValue(am, rf.enable.obj, rf.enable.tag) >= 0.5 ? 1 : 0
		if (want !== isOn) acc.setParameterProcessValue(am, rf.enable.obj, rf.enable.tag, want)
	}
	setRing(activeDevice, id, rf.invert ? 1 - v : v, false)
	var value = acc.getParameterDisplayValue(am, rf.obj, rf.tag)
	var units = acc.getParameterDisplayUnits(am, rf.obj, rf.tag)
	if (rf.enable && rf.offAt !== undefined && Math.abs(v - rf.offAt) <= 0.0001) { value = 'OFF'; units = '' }
	readout(activeDevice, isBusCompKnob(id) ? ZONE_BC : ZONE_CS, rf.title || acc.getParameterTitle(am, rf.obj, rf.tag, 16),
		value + (units ? units : ''))
}

function onButton(activeDevice, id) {
	if (id === B.Fine) { state.fine = !state.fine; setLed(activeDevice, B.Fine, state.fine ? 1 : 0); return }
	if (id === B.ChannelIn) { if (state.cs) toggleParam(state.cs, state.cs.bypass); paintLeds(activeDevice); return }
	if (id === B.BusCompIn) { var bc = busComp(); if (bc) toggleParam(bc, bc.bypass); paintLeds(activeDevice); return }
	if (state.cs && state.cs.flip && state.cs.flip[id]) {
		var f = state.cs.flip[id]
		state.attack[f.mode] = !state.attack[f.mode]
		var rf = state.attack[f.mode] ? f.attack : f.release
		state.cs.knobs[f.knob] = rf
		indexKnobs(state.cs)
		paintLeds(activeDevice)
		paintRing(activeDevice, f.knob)
		var units = state.cs.da.getParameterDisplayUnits(am, rf.obj, rf.tag)
		readout(activeDevice, ZONE_CS, rf.title, state.cs.da.getParameterDisplayValue(am, rf.obj, rf.tag) + (units || ''))
		return
	}
	if (state.cs && state.cs.buttons[id]) {
		toggleParam(state.cs, state.cs.buttons[id])
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

// Parameter changes from anywhere (the plug-in's window, automation): keep rings and LEDs right.
function onParameterChange(activeDevice, objectID, tag) {
	var bc = busComp()
	var target = state.cs && state.cs.objs[objectID] ? state.cs : bc && bc.objs[objectID] ? bc : null
	if (!target) return
	try {
		var knob = target.knobByKey[objectID + ':' + tag]
		if (knob !== undefined) paintRing(activeDevice, knob)
		else paintLeds(activeDevice)
	} catch (e) {}
}
da.mOnParameterChange = function (activeDevice, activeMapping, objectID, tag) {
	am = activeMapping
	onParameterChange(activeDevice, objectID, tag)
}
// Objects added or removed (a plug-in inserted, removed or replaced): look again shortly.
da.mOnObjectChange = function (activeDevice, activeMapping, objectID) {
	am = activeMapping
	state.rescanAt = Date.now() + 300
	state.mixScanAt = Date.now() + 500   // a bus comp added to the selected track belongs in the list too
}
if (daMix) {
	daMix.mOnParameterChange = function (activeDevice, activeMapping, objectID, tag) {
		am = activeMapping
		if (!state.cs || !state.cs.objs[objectID]) onParameterChange(activeDevice, objectID, tag)
	}
	var mixChanges = 0
	daMix.mOnObjectChange = function (activeDevice, activeMapping, objectID) {
		am = activeMapping
		if (++mixChanges <= 5) log('MixConsole object change #' + objectID)
		state.mixScanAt = Date.now() + 500
	}
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
	ringShown = {}
	labelShown = null
	log('page active')
	state.rescanAt = Date.now() + 100
	state.mixScanAt = Date.now() + 200
	numberShown = -2
}
page.mOnIdle = function (activeDevice, activeMapping) {
	am = activeMapping
	flushLog(activeDevice)
	var now = Date.now()
	if (numberBase >= 0 && now >= numberCheckAt) {
		numberCheckAt = now + 1000
		try { showNumber(activeDevice, numberBase) } catch (e) {}
	}
	if (state.rescanAt && now >= state.rescanAt) {
		state.rescanAt = 0
		try { rescan(activeDevice) } catch (e) { log('scan failed: ' + e) }
	}
	if (state.mixScanAt && now >= state.mixScanAt) {
		state.mixScanAt = 0
		var t0 = Date.now()
		try { scanMixer(activeDevice) } catch (e) { log('MixConsole scan failed: ' + e) }
		if (mixScans <= 3) log('MixConsole scan took ' + (Date.now() - t0) + ' ms')
	}
}
