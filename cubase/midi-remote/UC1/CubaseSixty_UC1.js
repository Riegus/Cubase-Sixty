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

var SCRIPT_VERSION = '0.12.0-dev'
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
// The routing switches (ROUTING screen), by parameter index: filters to input, filters to S/C, EQ to S/C, dynamics
// pre EQ, external S/C (Rea-Sixty PluginMap.cpp). The 360 Link has none.
var ROUTING = { 'CS 2': [33, 35, 36, 34, 1], '4K E': [16, 17, 20, 34, 1], '4K B': [12, 13, 15, 29, 1], '4K G': [2, 3, 4, 5, 1] }
for (var ri = 0; ri < CHANNEL_STRIPS.length; ++ri) if (ROUTING[CHANNEL_STRIPS[ri].label]) CHANNEL_STRIPS[ri].routing = ROUTING[CHANNEL_STRIPS[ri].label]
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
commandButton(3, 102, 'Navigate', 'Down')   // track-stepping = project (cubase\settings.txt)
commandButton(4, 103, 'Navigate', 'Up')

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
	if (map.routing) t.routing = map.routing.map(function (i) { return ref(acc, obj, acc.getParameterTagByIndex(am, obj, i), false) })
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
	csList: [],        // the selected track's channel strips (SSL ones in insert order, then Cubase's own)
	csChoice: {},      // per track (base object id): the key of the strip picked with the magnifier key
	csBase: -1,
	trackName: '',
	restoreBcAt: 0,    // when to put the Bus Comp name row back after the strip carousel
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
	var list = []
	var sslCount = 0, bcCount = 0
	var base
	try { base = da.getBaseObjectID(am) } catch (e) { log('getBaseObjectID failed: ' + e); return }
	var objects = walk(da, base, 4, 400, SKIP_TYPES_TRACK)
	++scans
	// For the favourites: the insert slots (empty ones, and the last one in use) and a favourite the UC1 has no map
	// for (it is still stepped from).
	state.insertSlots = []   // in order: { id, used }
	state.favOn = { cs: null, bc: null }
	for (var s = 0; s < objects.length; ++s) {
		var so = objects[s]
		if (so.type !== 'Slot' || so.parent < 0 || !/Insert/.test(objects[so.parent].type)) continue
		state.insertSlots.push({ id: so.id, used: s + 1 < objects.length && objects[s + 1].parent === s })
	}
	for (var i = 0; i < objects.length; ++i) {
		var id = objects[i].id
		var o = info(da, id)
		if (o.params < 4 || !o.title) continue
		var slotAbove = objects[i].parent >= 0 && objects[objects[i].parent].type === 'Slot' ? objects[objects[i].parent].id : -1
		for (var fd in fav.lists) if (!state.favOn[fd] && slotAbove >= 0 && favIndex(fd, o.title) >= 0)
			state.favOn[fd] = { acc: da, obj: id, slotObj: slotAbove, name: o.title }
		var bc = findMap(BUS_COMPS, o.title)
		// Each SSL strip / bus comp's position among them on the track (insert order): the order they connect to
		// SSL 360 Core in, which is how the bridge finds their gain-reduction streams.
		var csMatch = bc ? null : findMap(CHANNEL_STRIPS, o.title)
		var sslOrd = bc || (csMatch && csMatch.label) ? sslCount++ : -1
		if (bc && !state.bcSelected) {
			state.bcSelected = makeTarget(da, id, bc, o.title)
			state.bcSelected.sslOrd = bcCount
			state.bcSelected.slotObj = slotAbove
			var bcSlotAt = objects[i].parent   // its insert slot, for its window (the secondary encoder's push)
			if (bcSlotAt >= 0 && objects[bcSlotAt].type === 'Slot') state.bcSelected.insertIndex = parseInt(info(da, objects[bcSlotAt].id).title, 10) - 1
			dumpParams(da, id, o.title)
		}
		if (bc) { ++bcCount; continue }
		var cs = csMatch
		if (cs && cs.label) {
			var t = makeTarget(da, id, cs, o.title)
			t.sslOrd = sslOrd
			t.slotObj = slotAbove
			// The insert slot it sits in (slots are titled "1".."16"), for opening its window (360 key).
			var slotAt = objects[i].parent
			if (slotAt >= 0 && objects[slotAt].type === 'Slot') t.insertIndex = parseInt(info(da, objects[slotAt].id).title, 10) - 1
			t.key = 'obj' + id
			t.short = cs.label + (t.insertIndex >= 0 ? ' (' + (t.insertIndex + 1) + ')' : '')
			list.push(t)
			dumpParams(da, id, o.title)
		}
	}
	// Cubase's own strip (input filter, EQ, the strip's gate and compressor) is always there: the only one on a track
	// without an SSL channel strip, else the last choice for the magnifier key.
	var cubase = cubaseStrip(objects)
	if (cubase) { cubase.key = 'cubase'; cubase.short = 'Cubase'; list.push(cubase) }
	state.csList = list
	state.csBase = base
	var choice = state.csChoice[base]
	state.cs = list.length ? list[0] : null
	for (var c = 0; c < list.length; ++c) if (list[c].key === choice) state.cs = list[c]
	favApplyPending(activeDevice, list.concat(state.bcSelected ? [state.bcSelected] : []))
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
	var inputFilter = null, eq = null, gate = null, comp = null, gateSlot = null, compSlot = null, compTitle = ''
	var emptySlots = []   // strip slots with no module in them (a gate or compressor can be loaded there)
	for (var i = 0; i < objects.length; ++i) {
		var o = objects[i]
		if (o.type === 'Slot' && o.parent >= 0 && objects[o.parent].type === 'Strips'
			&& (i + 1 >= objects.length || objects[i + 1].parent !== i)) emptySlots.push(o.id)
		if (o.type === 'InputFilter') inputFilter = o.id
		else if (o.type === 'EQ') eq = o.id
		else if (o.type !== 'Slot' && o.parent >= 0 && objects[o.parent].type === 'Slot') {
			// A strip module sits in a slot under the track's "Strip"; insert plug-ins sit in slots under "Inserts".
			var folder = objects[o.parent].parent
			if (folder < 0 || objects[folder].type !== 'Strips') continue
			var title = info(da, o.id).title
			var kind = /gate/i.test(title) ? 'gate' : /comp/i.test(title) ? 'comp' : null
			if (kind === 'gate' && !gate) { gate = o.id; gateSlot = objects[o.parent].id }
			else if (kind === 'comp' && !comp) { comp = o.id; compSlot = objects[o.parent].id; compTitle = title }
			if (kind) rememberModule(kind, o.id, title, info(da, objects[o.parent].id).title)
		}
	}
	if (!inputFilter && !eq && !gate && !comp) return null
	var t = { da: da, label: 'CUBASE', name: 'Cubase channel strip', header: '', emptySlots: emptySlots,
		compSlot: compSlot, compTitle: compTitle, gateSlot: gateSlot,
		noGate: !gate, noComp: !comp, knobs: {}, buttons: {}, bypass: null, objs: {} }
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
	// LF BELL / HF BELL: bands 1 and 4 between their shelf (as they come: "Low / High Shelf II") and a bell
	// ("Parametric II", the top of the type list). Lit while a bell. Switching also switches the band on.
	var bells = [[B.LfBell, 1], [B.HfBell, 4]]
	for (var bb = 0; bb < bells.length; ++bb) {
		var typ = sw(eq, eqList, [band(bells[bb][1]), /type/i])
		if (!typ) continue
		typ.eqType = true
		typ.enable = sw(eq, eqList, [band(bells[bb][1]), RX.on])
		t.buttons[bells[bb][0]] = typ
		t.objs[eq] = true
		found.push((bells[bb][1] === 1 ? 'LF' : 'HF') + ' bell="' + typ.title + '"')
	}
	// Each compressor type by the controls it shows in the strip (exact parameter names, from the log). The Tube and
	// Vintage Compressors have no threshold and no ratio on their strip panel: Input Gain (drives them into
	// compression) goes on the Ratio knob and Output Gain on the Threshold knob, as the Ratio knob sits left of the
	// Threshold knob on the UC1 (input left, output right, as on the panel).
	var layout = comp ? COMP_LAYOUTS[info(da, comp).title] : null
	function exact(name) { return new RegExp('^' + name + '$', 'i') }
	if (layout) {
		knob(K.CompThreshold, 'comp threshold', comp, compList, [exact(layout.thresh)])
		knob(K.CompRatio, 'comp ratio', comp, compList, [exact(layout.ratio)])
		knob(K.CompRelease, 'comp release', comp, compList, [exact(layout.release)])
	} else {
		knob(K.CompThreshold, 'comp threshold', comp, compList, [RX.thresh])
		knob(K.CompRatio, 'comp ratio', comp, compList, [RX.ratio], [/high/i])
		// "Auto Release" (a switch) must not be taken for the release time.
		knob(K.CompRelease, 'comp release', comp, compList, [RX.release], [RX.auto])
	}
	knob(K.GateThreshold, 'gate threshold', gate, gateList, [RX.thresh])
	knob(K.GateRange, 'gate range', gate, gateList, [RX.range])
	knob(K.GateRelease, 'gate release', gate, gateList, [RX.release], [RX.auto])
	knob(K.GateHold, 'gate hold', gate, gateList, [RX.hold], [RX.thresh])
	// The UC1 has no attack knobs: the F.ATT keys switch their section's Release knob between release and attack
	// (lit = attack). The choice stays when you change tracks.
	t.flip = {}
	function attackFlip(key, mode, releaseKnob, obj, list, attackName) {
		var p = obj ? findParam(list, attackName ? [exact(attackName)] : [RX.attack], [RX.auto]) : null
		if (!p || !t.knobs[releaseKnob]) return
		t.flip[key] = { mode: mode, knob: releaseKnob, release: t.knobs[releaseKnob],
			attack: { obj: obj, tag: p.tag, invert: false, title: p.title.substr(0, 16) } }
		found.push(mode + ' attack="' + p.title + '"')
		if (state.attack[mode]) t.knobs[releaseKnob] = t.flip[key].attack
	}
	attackFlip(B.FastAttComp, 'comp', K.CompRelease, comp, compList, layout ? layout.attack : null)
	attackFlip(B.FastAttGate, 'gate', K.GateRelease, gate, gateList)
	// EQ IN: lit while the EQ is not bypassed. DYN IN: the compressor's bypass, if it has one.
	var eqBypass = sw(eq, eqList, [RX.bypass], [RX.digit])
	if (eqBypass) { eqBypass.invert = true; t.buttons[B.EqIn] = eqBypass; t.objs[eq] = true }
	// DYN IN: the compressor's bypass; EXP (Cubase's Noise Gate has no expander mode): the gate's bypass. Each lit
	// while its module is in. SC LISTEN: the gate's side-chain monitor.
	var compBypass = sw(comp, compList, [RX.bypass]), gateBypass = sw(gate, gateList, [RX.bypass])
	t.dynBypass = []
	if (compBypass) { compBypass.invert = true; t.dynBypass.push(compBypass); t.objs[comp] = true }
	if (gateBypass) { gateBypass.invert = true; t.buttons[B.Expand] = gateBypass; t.objs[gate] = true }
	var scMonitor = sw(gate, gateList, [/^sc\s*monitor$/i])
	if (scMonitor) { t.buttons[B.ScListen] = scMonitor; t.objs[gate] = true }
	if (++stripLogs <= 5) {
		log('Cubase strip:' + (missing.length ? ' not found: ' + missing.join(', ') : ' everything found')
			+ '; EQ IN ' + (eqBypass ? '"' + eqBypass.title + '"' : 'none') + ', DYN IN ' + (compBypass ? '"' + compBypass.title + '"' : 'none') + ', EXP ' + (gateBypass ? '"' + gateBypass.title + '"' : 'none') + ', SC LISTEN ' + (scMonitor ? '"' + scMonitor.title + '"' : 'none'))
		for (var f = 0; f < found.length; ++f) log('Cubase strip:   ' + found[f])
	}
	indexKnobs(t)
	return t
}

// Cubase's strip modules are switched on, not loaded: a strip slot without a module has nothing in it until its
// module is switched on (the Direct Access plug-in manager lists no plug-ins for strip slots). The script cannot
// set a host value by itself, so the on switches of the selected track's Gate and Compressor are bound to CC 40 /
// 41 (channel 1), and the script asks the bridge to send that CC (SysEx 3A <cc> 127). Turning a Gate or Dyn knob
// whose module is missing or off switches it on; the knobs work once the track has been looked at again.
var COMP_LAYOUTS = {
	'Standard Compressor': { thresh: 'Threshold', ratio: 'Ratio', release: 'Release', attack: 'Attack' },
	'Tube Compressor': { thresh: 'Output Gain', ratio: 'Input Gain', release: 'Release Time', attack: 'Attack Time' },
	'VintageCompressor': { thresh: 'Output Gain', ratio: 'Input Gain', release: 'Release Time', attack: 'Attack Time' }
}
var GATE_KNOBS = [K.GateThreshold, K.GateRange, K.GateRelease, K.GateHold]
var COMP_KNOBS = [K.CompThreshold, K.CompRatio, K.CompRelease]
var CC_GATE_ON = 40, CC_COMP_ON = 41
var stripOn = { gate: -1, comp: -1 }   // the modules' on switches as Cubase reports them (-1 = not known)
try {
	var stripFx = selected.mInsertAndStripEffects.mStripEffects
	var bindOn = function (x, cc, kind, hostValue) {
		var k = surface.makeKnob(x, 3, 1, 1)
		k.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToControlChange(0, cc)
		page.makeValueBinding(k.mSurfaceValue, hostValue)
		k.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) {
			if (stripOn[kind] !== value) log('strip ' + kind + ' on switch: ' + value)
			stripOn[kind] = value
		}
	}
	bindOn(0, CC_GATE_ON, 'gate', stripFx.mGate.mOn)
	bindOn(1, CC_COMP_ON, 'comp', stripFx.mCompressor.mOn)
} catch (e) { log('strip module switches could not be bound: ' + e) }
// Strip modules by kind: name and plug-in class id (the unique name without its "-<instance>" ending), which is what
// an empty strip slot is loaded with (Direct Access trySetSlotPlugin). Known ones are listed; any other module seen
// in a track's strip is added (and logged with its id, to be listed here).
var STRIP_MODULES = {
	gate: [{ name: 'Noise Gate', uid: 'C3B0615A2A444991B423673DEE2379A7', slot: '1' }],
	comp: [
		{ name: 'Standard Compressor', short: 'Standard Comp', uid: 'E022B5972163463CBA2036708D5AF5A5', slot: '2' },
		{ name: 'Tube Compressor', short: 'Tube Comp', uid: '7186B3CC877647BAADD5EAC0785001DF', slot: '2' },
		{ name: 'VintageCompressor', short: 'Vintage Comp', uid: '2CA7A4D872A14FDD99B4932F2FD98854', slot: '2' }
	]
}
function rememberModule(kind, obj, title, slotTitle) {
	var list = STRIP_MODULES[kind]
	for (var i = 0; i < list.length; ++i) if (list[i].name === title) return
	var uname = ''
	try { uname = da.getObjectUniqueName(am, obj) } catch (e) {}
	if (!uname) return
	list.push({ name: title, uid: uname.replace(/-\d+$/, ''), slot: slotTitle })
	log('strip ' + kind + ' seen: "' + title + '" in slot "' + slotTitle + '", class id ' + uname.replace(/-\d+$/, ''))
}
function loadModule(activeDevice, target, kind, module, intoSlot) {
	// Replacing: the slot given. Else the slot it sits in by default if that one is free, else the first free one.
	var slot = intoSlot || target.emptySlots[0]
	if (!intoSlot) for (var e = 0; e < target.emptySlots.length; ++e) if (info(da, target.emptySlots[e]).title === module.slot) slot = target.emptySlots[e]
	if (!slot) { log('no free strip slot for "' + module.name + '"'); return }
	var ok = false
	try { ok = da.mPluginManager.trySetSlotPlugin(am, slot, module.uid, true) } catch (err) { log('trySetSlotPlugin failed: ' + err) }
	log('loading "' + module.name + '" into strip slot "' + info(da, slot).title + '": ' + (ok ? 'done' : 'refused'))
	sysex(activeDevice, [0x3A, kind === 'gate' ? CC_GATE_ON : CC_COMP_ON, 127])
	readout(activeDevice, ZONE_CS, module.short || module.name, ok ? 'ON' : 'FAILED')
	state.rescanAt = Date.now() + 300
}

// The compressor picker: a Dyn knob turned on a track without a compressor (and more than one compressor type
// known) shows the types in the readout; turning any Dyn knob steps through them, CONFIRM or DYN IN loads the one
// shown, BACK (or 8 s without a turn, or another track) cancels.
var picker = null   // { kind, index, until, target }
function showPicker(activeDevice) {
	var list = STRIP_MODULES[picker.kind]
	readout(activeDevice, ZONE_CS, list[picker.index].short || list[picker.index].name, (picker.index + 1) + '/' + list.length)
}
function closePicker(activeDevice, why) {
	if (!picker) return
	picker = null
	if (why) readout(activeDevice, ZONE_CS, why, '')
}

var dynPressedAt = 0
function openCompPicker(activeDevice, target) {
	var list = STRIP_MODULES.comp
	var index = 0
	for (var i = 0; i < list.length; ++i) if (list[i].name === target.compTitle) index = i
	picker = { kind: 'comp', index: index, acc: 0, until: Date.now() + 8000, target: target,
		replaceSlot: target.noComp ? null : target.compSlot }
	showPicker(activeDevice)
}
var switchTried = {}
function switchOnModuleFor(activeDevice, target, knob, clicks) {
	var kind = GATE_KNOBS.indexOf(knob) >= 0 ? 'gate' : COMP_KNOBS.indexOf(knob) >= 0 ? 'comp' : null
	if (!kind) return false
	var missing = kind === 'gate' ? target.noGate : target.noComp
	if (picker && picker.kind === kind) {   // (the track may have been looked at again meanwhile: a new target)
		// One type per 4 clicks (a click per type went by too fast to stop on one).
		picker.until = Date.now() + 8000
		if ((clicks > 0) !== (picker.acc > 0)) picker.acc = 0
		picker.acc += clicks
		if (Math.abs(picker.acc) < 4) return true
		var n = STRIP_MODULES[kind].length
		picker.index = ((picker.index + (picker.acc > 0 ? 1 : -1)) % n + n) % n
		picker.acc = 0
		showPicker(activeDevice)
		return true
	}
	if (!missing && stripOn[kind] !== 0) return false
	if (missing && target.emptySlots.length && STRIP_MODULES[kind].length > 1) {
		picker = { kind: kind, index: 0, acc: 0, until: Date.now() + 8000, target: target }
		showPicker(activeDevice)
		return true
	}
	var key = numberBase + ':' + kind
	if (switchTried[key] && Date.now() - switchTried[key] < 2000) return true
	switchTried[key] = Date.now()
	log('switching on the strip ' + (kind === 'gate' ? 'gate' : 'compressor') + ' (was ' + (missing ? 'not there' : 'off') + ')')
	if (missing && target.emptySlots.length) { loadModule(activeDevice, target, kind, STRIP_MODULES[kind][0]); return true }
	sysex(activeDevice, [0x3A, kind === 'gate' ? CC_GATE_ON : CC_COMP_ON, 127])
	readout(activeDevice, ZONE_CS, kind === 'gate' ? 'Gate' : 'Compressor', 'ON')
	state.rescanAt = Date.now() + 300
	return true
}

// The magnifier key: the next channel strip on the track (wrapping). The large name row shows the previous / current /
// next strip under the track's name for a moment (SSL 360's instance carousel), then the Bus Comp names again.
function nextStrip(activeDevice, dir) {
	var list = state.csList
	if (list.length < 2) { readout(activeDevice, ZONE_CS, 'One strip only', ''); return }
	var i = list.indexOf(state.cs)
	var next = ((i + (dir || 1)) % list.length + list.length) % list.length
	state.cs = list[next]
	state.csChoice[state.csBase] = state.cs.key
	// The neighbours as they sit in the list (no wrapping in the display: with two strips, wrapping showed the
	// other one above and below).
	var sep = [0x7F, 0x7F, 0x7F]
	var prevName = next > 0 ? list[next - 1].short : ''
	var nextName = next + 1 < list.length ? list[next + 1].short : ''
	sysex(activeDevice, [0x38].concat(encodeName(prevName), sep, encodeName(list[next].short), sep, encodeName(nextName)))
	sysex(activeDevice, [0x39].concat(encodeName(state.trackName)))
	state.restoreBcAt = Date.now() + 1600
	sysex(activeDevice, [0x35, ZONE_CS])
	paintAll(activeDevice)
}

// ---------------------------------------------------------------------------
// The secondary encoder (right of the screen; Rea-Sixty's Encoder 2). Its rotation has four jobs: 'bus-comp' (the
// Bus Comp section through the project's bus comps, the default), 'strips' (the track's channel strips, as the
// magnifier key, both ways), 'cs-fav' / 'bc-fav' (the favourites, as the UF8's CS Fav / BC Fav). The default comes
// from cubase\settings.txt (uc1-encoder2; the bridge sends it as SysEx 4C <0-3>); hold the push and turn to pick
// another (shown in the readout). A short push opens / closes the window of the plug-in the encoder is on.
// ---------------------------------------------------------------------------
var ENC2_MODES = [
	{ key: 'bus-comp', name: 'Bus Comp' }, { key: 'strips', name: 'Strips' },
	{ key: 'cs-fav', name: 'CS Fav' }, { key: 'bc-fav', name: 'BC Fav' }
]
var enc2 = { mode: 0, held: false, turned: false, started: false }
function enc2Show(activeDevice) {
	readout(activeDevice, ZONE_BC, 'Encoder: ' + ENC2_MODES[enc2.mode].name, (enc2.mode + 1) + '/' + ENC2_MODES.length)
}
function enc2Turn(activeDevice, clicks) {
	// Lists: one entry per turn message, and none for 80 ms after a step (a quick turn skipped entries).
	var now = Date.now()
	if (now - (enc2.stepAt || 0) < 80) return
	enc2.stepAt = now
	clicks = clicks > 0 ? 1 : -1
	if (enc2.held) {   // picking a job
		enc2.turned = true
		enc2.mode = Math.max(0, Math.min(ENC2_MODES.length - 1, enc2.mode + (clicks > 0 ? 1 : -1)))
		enc2Show(activeDevice)
		return
	}
	var dir = clicks > 0 ? 1 : -1
	switch (ENC2_MODES[enc2.mode].key) {
	case 'strips': for (var s = 0; s < Math.abs(clicks); ++s) nextStrip(activeDevice, dir); break
	case 'cs-fav': for (var c = 0; c < Math.abs(clicks); ++c) favStep(activeDevice, 'cs', dir); break
	case 'bc-fav': for (var b = 0; b < Math.abs(clicks); ++b) favStep(activeDevice, 'bc', dir); break
	default: onBcEncoder(activeDevice, clicks)
	}
}
function enc2Push(activeDevice, pressed) {
	if (pressed) { enc2.held = true; enc2.turned = false; return }
	enc2.held = false
	if (enc2.turned) { log('secondary encoder: ' + ENC2_MODES[enc2.mode].name); return }
	var key = ENC2_MODES[enc2.mode].key
	if (key === 'strips' || key === 'cs-fav') { toggleWindow(activeDevice); return }
	// The bus comp's window: only for one on the selected track (the window switches are the selected track's).
	var bc = busComp(), mine = state.bcSelected
	if (bc && mine && bc.obj === mine.obj && mine.insertIndex >= 0) {
		sysex(activeDevice, [0x3A, CC_INSERT_EDIT + mine.insertIndex, insertEditOpen[mine.insertIndex] >= 0.5 ? 0 : 127])
		return
	}
	readout(activeDevice, ZONE_BC, 'Window', 'not this track')
}

// ---------------------------------------------------------------------------
// EXT FUNCS (Rea-Sixty's, SSL's own lists): BACK on the main screen opens a list of the channel strip's parameters
// without a knob of their own; the secondary encoder scrolls it, its push switches between scrolling and adjusting
// the one shown, BACK (or CONFIRM) closes it. The bridge owns the screen: the script asks for it (SysEx 4B <1 open>),
// the bridge sends the encoder and keys while it is open (4D <op: 1 turn / 2 push / 3 closed> <64 + steps>), the
// script draws through it (4A <prev> 00 <current> 00 <next> 00 <long name> 00 <value> 00 <pos 0-127> <active>).
// The lists are SSL's minus PLUG-IN, A/B and HQ (no plug-in parameter in Cubase) and SOLO SAFE (as Rea-Sixty).
// Indices: the plug-ins' parameter order (the same in Cubase as in Rea-Sixty's tables).
// ---------------------------------------------------------------------------
function toAscii7(text) {
	var out = []
	text = String(text)
	for (var i = 0; i < text.length; ++i) { var c = text.charCodeAt(i); out.push(c >= 32 && c < 127 ? c : 63) }
	return out
}
function xf(short, long, idx, sw) { return { short: short, long: long, idx: idx, sw: !!sw } }
var EXT_FUNCS = {
	'CS 2': [xf('COMP MIX', 'Mix', 26), xf('FILT IN', 'Filters', 46, 1), xf('PAN', 'Pan', 41), xf('WIDTH', 'Width', 40),
		xf('OUT TRIM', 'Out Trim', 39), xf('WIDTH MD', 'Width Mode', 47, 1), xf('WIDTH FQ', 'Width Freq', 48),
		xf('AUTO MKP', 'Auto Makeup', 49, 1), xf('MKP OFST', 'Auto Makeup Offset', 50)],
	'4K B': [xf('COMP MIX', 'Mix', 33), xf('PRE', 'Pre', 3, 1), xf('MIC', 'Mic', 4), xf('FILT IN', 'Filters', 44, 1),
		xf('PAN', 'Pan', 9), xf('WIDTH', 'Width', 8), xf('OUT TRIM', 'Out Trim', 7), xf('ANLG VCA', 'Analogue', 43, 1),
		xf('WIDTH MD', 'Width Mode', 45, 1), xf('WIDTH FQ', 'Width Freq', 46), xf('AUTO MKP', 'Auto Makeup', 47, 1),
		xf('MKP OFST', 'Auto Makeup Offset', 48)],
	'4K E': [xf('COMP MIX', 'Mix', 38), xf('PRE', 'Pre', 3, 1), xf('MIC', 'Mic', 4), xf('FILT IN', 'Filters', 13, 1),
		xf('PAN', 'Pan', 12), xf('WIDTH', 'Width', 9), xf('OUT TRIM', 'Out Trim', 8), xf('ANLG VCA', 'Analogue', 7, 1),
		xf('WIDTH MD', 'Width Mode', 10, 1), xf('WIDTH FQ', 'Width Freq', 11), xf('AUTO MKP', 'Auto Makeup', 40, 1),
		xf('MKP OFST', 'Auto Makeup Offset', 41)],
	'4K G': [xf('COMP MIX', 'Mix', 42), xf('PRE', 'Pre', 9, 1), xf('MIC', 'Mic', 10), xf('IMPED IN', 'Impedance Circuit', 7, 1),
		xf('IMPEDANCE', 'Impedance', 8), xf('FILT IN', 'Filters', 19, 1), xf('PAN', 'Pan', 18), xf('WIDTH', 'Width', 15),
		xf('OUT TRIM', 'Out Trim', 14), xf('ANLG VCA', 'Analogue', 13, 1), xf('WIDTH MD', 'Width Mode', 16, 1),
		xf('WIDTH FQ', 'Width Freq', 17), xf('AUTO MKP', 'Auto Makeup', 44, 1), xf('MKP OFST', 'Auto Makeup Offset', 45)]
}
var ext = { open: false, idx: 0, active: false }
function extList() {
	var t = state.cs
	return t && t.map && EXT_FUNCS[t.map.label] ? EXT_FUNCS[t.map.label] : []
}
function extTag(item) {
	var t = state.cs
	try { return t.da.getParameterTagByIndex(am, t.obj, item.idx) } catch (e) { return -1 }
}
function extDraw(activeDevice) {
	var list = extList(), n = list.length
	var z = [0x00]
	if (!n) { sysex(activeDevice, [0x4A].concat(toAscii7(''), z, toAscii7('(none)'), z, toAscii7(''), z, toAscii7('EXT FUNCS'), z, toAscii7(''), z, [0, 0])); return }
	ext.idx = ((ext.idx % n) + n) % n
	var cur = list[ext.idx], prev = list[(ext.idx - 1 + n) % n], next = list[(ext.idx + 1) % n]
	var value = '', pos = 0, tag = extTag(cur)
	if (tag >= 0) {
		try {
			var v = state.cs.da.getParameterProcessValue(am, state.cs.obj, tag)
			pos = Math.round(Math.max(0, Math.min(1, v)) * 127)
			value = String(state.cs.da.getParameterDisplayValue(am, state.cs.obj, tag)).replace(/^\s+/, '')
			var units = state.cs.da.getParameterDisplayUnits(am, state.cs.obj, tag)
			if (units && value.indexOf(units) < 0 && value.length + units.length < 9) value += units
		} catch (e) {}
	}
	sysex(activeDevice, [0x4A].concat(toAscii7(prev.short), z, toAscii7(cur.short), z, toAscii7(next.short), z,
		toAscii7(cur.long), z, toAscii7(value.substring(0, 8)), z, [pos, ext.active ? 1 : 0]))
}
function extOpen(activeDevice) {
	ext.open = true
	ext.idx = 0
	ext.active = false
	sysex(activeDevice, [0x4B, 1])
	extDraw(activeDevice)
}
function extEvent(activeDevice, op, steps) {
	if (op === 3) { ext.open = false; ext.active = false; return }
	if (!ext.open) return
	var list = extList()
	if (op === 2) { ext.active = !ext.active; extDraw(activeDevice); return }
	if (op !== 1 || !list.length) return
	if (!ext.active) { ext.idx += steps; extDraw(activeDevice); return }
	var item = list[((ext.idx % list.length) + list.length) % list.length], tag = extTag(item)
	if (tag < 0) return
	var acc = state.cs.da, cur = acc.getParameterProcessValue(am, state.cs.obj, tag)
	var v = item.sw ? (steps > 0 ? 1 : 0) : Math.max(0, Math.min(1, cur + steps / 64 * (state.fine ? 0.25 : 1)))
	acc.setParameterProcessValue(am, state.cs.obj, tag, v)
	extDraw(activeDevice)
}

// The UF1's SHIFT + CHANNEL encoder picked a strip for its PLUGIN view (SysEx 45 <kind> <ordinal>, via the bridge;
// kind 1 = an SSL strip by its position among the track's SSL plug-ins, 2 = Cubase's own): the UC1 goes to the same
// one, as the UF1 follows the magnifier key.
function followUf1(activeDevice, kind, ord) {
	var list = state.csList, want = null
	for (var i = 0; i < list.length; ++i) {
		if (kind === 2 && list[i].key === 'cubase') want = list[i]
		if (kind === 1 && list[i].key !== 'cubase' && (list[i].sslOrd || 0) === ord) want = list[i]
	}
	if (!want || want === state.cs) return
	state.cs = want
	state.csChoice[state.csBase] = want.key
	log('channel strip from the UF1: "' + want.name + '"')
	sysex(activeDevice, [0x35, ZONE_CS])
	paintAll(activeDevice)
}

// The 360 key: open or close the window of the strip the UC1 controls: an SSL plug-in's own window (the insert
// slot's edit switch), or for Cubase's own strip the track's Channel Settings. The script cannot set these itself,
// so they are bound to CCs the bridge sends back (SysEx 3A): CC 50 + slot for the inserts, CC 70 for Channel
// Settings.
var CC_INSERT_EDIT = 50, CC_CHANNEL_EDIT = 70
var insertEditOpen = [], channelEditOpen = 0
try {
	var bindCc = function (x, y, cc, hostValue, onChange) {
		var k = surface.makeKnob(x, y, 1, 1)
		k.mSurfaceValue.mMidiBinding.setInputPort(midiInput).bindToControlChange(0, cc)
		page.makeValueBinding(k.mSurfaceValue, hostValue)
		k.mSurfaceValue.mOnProcessValueChange = function (activeDevice, value) { onChange(value) }
	}
	for (var slot = 0; slot < 16; ++slot) {
		;(function (slot) {
			var viewer = selected.mInsertAndStripEffects.makeInsertEffectViewer('UC1 insert ' + (slot + 1)).accessSlotAtIndex(slot)
			bindCc(slot, 4, CC_INSERT_EDIT + slot, viewer.mEdit, function (v) { insertEditOpen[slot] = v })
		})(slot)
	}
	bindCc(0, 5, CC_CHANNEL_EDIT, track.mEditorOpen, function (v) { channelEditOpen = v })
} catch (e) { log('window switches could not be bound: ' + e) }
function toggleWindow(activeDevice) {
	var t = state.cs
	if (!t) return
	if (t.insertIndex >= 0) {
		var open = insertEditOpen[t.insertIndex] >= 0.5
		sysex(activeDevice, [0x3A, CC_INSERT_EDIT + t.insertIndex, open ? 0 : 127])
	} else {
		sysex(activeDevice, [0x3A, CC_CHANNEL_EDIT, channelEditOpen >= 0.5 ? 0 : 127])
	}
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
	reportGr(activeDevice)
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
	var sslOnChannel = {}
	for (var i = 0; i < objects.length; ++i) {
		var o = objects[i]
		if (verbose && o.depth <= 3 && !/^(Slot|Panner)$/.test(o.type)) log('mix ' + new Array(o.depth + 1).join('  ') + '#' + o.id + ' [' + o.type + '] "' + info(daMix, o.id).title + '"')
		if (o.depth < 2) continue
		var inf = info(daMix, o.id)
		if (inf.params < 4 || !inf.title) continue
		var map = findMap(BUS_COMPS, inf.title)
		var csMap = map ? null : findMap(CHANNEL_STRIPS, inf.title)
		if (!map && !(csMap && csMap.label)) continue
		// The track: the nearest enclosing object whose kind is a channel.
		var p = o.parent, track = '', output = false, channelObj = -1
		while (p >= 0) {
			if (/Channel$/.test(objects[p].type)) {
				channelObj = objects[p].id
				track = info(daMix, channelObj).title
				output = /Output/.test(objects[p].type)
				break
			}
			p = objects[p].parent
		}
		// Its position among the bus comps on that channel (insert order): how the bridge finds its gain reduction.
		if (!map) continue
		sslOnChannel[channelObj] = (sslOnChannel[channelObj] || 0) + 1
		var dup = false
		for (var d = 0; d < list.length; ++d) if (list[d].obj === o.id) dup = true
		if (!dup) {
			var t = makeTarget(daMix, o.id, map, inf.title)
			t.track = track
			t.output = output
			t.channelObj = channelObj
			t.sslOrd = sslOnChannel[channelObj] - 1
			t.slotObj = o.parent >= 0 && objects[o.parent].type === 'Slot' ? objects[o.parent].id : -1
			list.push(t)
		}
	}
	if (verbose || list.length !== state.bcList.length)
		log('MixConsole: ' + objects.length + ' objects, ' + list.length + ' bus comps'
			+ (list.length ? ': ' + list.map(function (b) { return '"' + b.track + '" #' + b.obj }).join(', ') : ''))
	state.bcList = list
	favApplyPending(activeDevice, list)
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

// ---------------------------------------------------------------------------
// Favourites (Rea-Sixty's CS / BC favourites): up to eight channel strips and eight bus comps, listed in
// cubase\favourites.txt; the bridge reads the file and sends the two lists (SysEx 43 <0 = CS, 1 = BC> <names,
// 7F 7F 7F between>) when asked (SysEx 42, on activation and when stepping after a pause). The UF8's CHANNEL
// encoder in CS Fav / BC Fav mode steps through them (SysEx 44 <domain> <1 = forward, 0 = back>): the UC1's name
// rows show the previous / current / next favourite at once, and 0.8 s after the last step the plug-in in
// the slot is replaced by the one shown (one load per pause, not one per notch). The controls both plug-ins have
// on the UC1 are carried across (by value, in their units), so the sound stays. On a track with none, the
// favourite goes into the first free insert slot after the last plug-in.
// The channel strip domain is the strip the UC1 is on (state.cs), the bus comp domain the Bus Comp section's.
// ---------------------------------------------------------------------------
var fav = {
	lists: { cs: [], bc: [] },   // names, as in the file
	askedAt: 0,
	uids: {},                    // plug-in name -> class id (from the plug-in manager's collections)
	pick: null,                  // while stepping: { domain, index, slotObj, until }
	commitAt: 0,
	pending: null,               // after a swap: { slotObj, name, values, until }
	loaded: null                 // the last swap: { slotObj, name, at } (Cubase shows it to the script seconds later)
}
function favIndex(domain, name) {
	var list = fav.lists[domain] || []
	for (var i = 0; i < list.length; ++i) if (list[i].toLowerCase() === String(name).toLowerCase()) return i
	return -1
}
function favShort(name) {
	var m = findMap(CHANNEL_STRIPS, name) || findMap(BUS_COMPS, name)
	return m && m.label ? m.label : name.replace(/^SSL (Native )?/, '')
}
function favAsk(activeDevice) {
	fav.askedAt = Date.now()
	sysex(activeDevice, [0x42])
}
function onFavList(message) {
	var domain = message[5] === 0 ? 'cs' : 'bc'
	var names = decodeName(message, 6).filter(function (n) { return n.length > 0 }).slice(0, 8)
	if (names.join('|') !== fav.lists[domain].join('|')) log('favourites (' + domain + '): ' + (names.length ? names.join(', ') : 'none'))
	fav.lists[domain] = names
}
// What a step acts on: the plug-in in the domain and the slot it sits in, or a free insert slot to put one in.
function favContext(domain) {
	var t = domain === 'cs' ? (state.cs && state.cs.key !== 'cubase' ? state.cs : null) : busComp()
	var on = state.favOn ? state.favOn[domain] : null
	if (t && t.slotObj >= 0) {
		// Just swapped, and Cubase does not show the new plug-in yet: count from it, not from the old one.
		var ld = fav.loaded
		if (ld && ld.slotObj === t.slotObj && Date.now() - ld.at < 8000 && ld.name !== t.name)
			return { acc: t.da, slotObj: t.slotObj, name: ld.name, target: null, track: domain === 'bc' && t.track ? t.track : state.trackName }
		return { acc: t.da, slotObj: t.slotObj, name: t.name, target: t, track: domain === 'bc' && t.track ? t.track : state.trackName }
	}
	if (on) return { acc: on.acc, slotObj: on.slotObj, name: on.name, target: null, track: state.trackName }
	// None on the track: the first free insert slot after the last one in use, else the first free one.
	var slots = state.insertSlots || [], free = null
	for (var i = 0; i < slots.length; ++i) if (!slots[i].used && !free) free = slots[i]; else if (slots[i].used) free = null
	if (!free) for (var j = 0; j < slots.length; ++j) if (!slots[j].used) { free = slots[j]; break }
	return free ? { acc: da, slotObj: free.id, name: '', target: null, track: state.trackName, insert: true } : null
}
function favShow(activeDevice, domain, index, track) {
	var list = fav.lists[domain]
	var sep = [0x7F, 0x7F, 0x7F]
	sysex(activeDevice, [0x38].concat(encodeName(index > 0 ? favShort(list[index - 1]) : ''), sep,
		encodeName(favShort(list[index])), sep, encodeName(index + 1 < list.length ? favShort(list[index + 1]) : '')))
	sysex(activeDevice, [0x39].concat(encodeName(track || '')))
	sysex(activeDevice, [0x35, domain === 'cs' ? ZONE_CS : ZONE_BC])
	state.restoreBcAt = Date.now() + 1600
}
function favStep(activeDevice, domain, dir) {
	if (Date.now() - fav.askedAt > 5000) favAsk(activeDevice)   // the file may have changed; the next step has it
	var list = fav.lists[domain]
	var zone = domain === 'cs' ? ZONE_CS : ZONE_BC
	if (!list.length) { readout(activeDevice, zone, 'No favourites', 'favourites.txt'); return }
	var ctx = favContext(domain)
	if (!ctx) { readout(activeDevice, zone, 'No free slot', ''); return }
	var cur = fav.pick && fav.pick.domain === domain && fav.pick.slotObj === ctx.slotObj ? fav.pick.index : favIndex(domain, ctx.name)
	var next
	if (cur < 0) next = dir > 0 ? 0 : list.length - 1
	else next = ((cur + dir) % list.length + list.length) % list.length
	fav.pick = { domain: domain, index: next, slotObj: ctx.slotObj }
	fav.commitAt = Date.now() + 800
	favShow(activeDevice, domain, next, ctx.track)
}
function favUid(acc, slotObj, name) {
	if (fav.uids[name]) return fav.uids[name]
	var pm = acc.mPluginManager
	var n = 0
	try { n = pm.getNumberOfPluginCollections(am, slotObj) } catch (e) { log('plug-in collections: ' + e) }
	for (var c = 0; c < n; ++c) {
		var col = null
		try { col = pm.getPluginCollectionByIndex(am, slotObj, c) } catch (e) {}
		var entries = col && col.mEntries ? col.mEntries : []
		for (var e = 0; e < entries.length; ++e) if (entries[e].mPluginName) fav.uids[entries[e].mPluginName] = entries[e].mPluginUID
		if (fav.uids[name]) return fav.uids[name]
	}
	for (var k in fav.uids) if (k.toLowerCase() === name.toLowerCase()) return fav.uids[k]
	return null
}
// Numbers in a parameter's own unit, made comparable: Hz, seconds, the rest as shown.
function plainOf(display, units) {
	var n = leadingNumber(display)
	if (isNaN(n)) return NaN
	var u = ((units || '') + ' ' + String(display).replace(/^\s*[-+]?\d*\.?\d+(e[-+]?\d+)?/i, '')).toLowerCase()
	if (/khz|\dk\b|^\s*k\b/.test(u)) return n * 1000
	if (/\bms\b|^\s*ms/.test(u)) return n / 1000
	return n
}
function readPlain(acc, obj, tag) {
	var d = '', u = ''
	try { d = acc.getParameterDisplayValue(am, obj, tag) } catch (e) {}
	try { u = acc.getParameterDisplayUnits(am, obj, tag) || '' } catch (e) {}
	return plainOf(d, u)
}
// Sets a parameter to the value v (Hz, seconds or as shown): the display across the range, then a bisection
// between the two readings around v (or the nearest end).
function setPlain(acc, obj, tag, v) {
	var at = function (n) { acc.setParameterProcessValue(am, obj, tag, n); return readPlain(acc, obj, tag) }
	var pts = []
	for (var k = 0; k <= 32; ++k) pts.push({ n: k / 32, num: at(k / 32) })
	var best = null
	for (var q = 0; q < pts.length; ++q) if (!isNaN(pts[q].num) && (!best || Math.abs(pts[q].num - v) < Math.abs(best.num - v))) best = pts[q]
	if (!best) return false
	for (var z = 0; z + 1 < pts.length; ++z) {
		var p0 = pts[z], p1 = pts[z + 1]
		if (isNaN(p0.num) || isNaN(p1.num) || (p0.num - v) * (p1.num - v) > 0) continue
		var a = p0.n, b = p1.n, asc = p1.num > p0.num
		for (var it = 0; it < 14; ++it) {
			var n = (a + b) / 2, num = at(n)
			if (isNaN(num)) break
			if (Math.abs(num - v) < Math.abs(best.num - v)) best = { n: n, num: num }
			if ((num < v) === asc) a = n; else b = n
		}
		break
	}
	acc.setParameterProcessValue(am, obj, tag, best.n)
	return true
}
// The controls of a mapped plug-in, by UC1 control: knobs as values, keys (and IN) as on / off.
function favSnapshot(t) {
	var out = { knobs: {}, buttons: {}, bypass: null }
	try {
		for (var k in t.knobs) {
			var rf = t.knobs[k]
			var proc = t.da.getParameterProcessValue(am, rf.obj, rf.tag)
			out.knobs[k] = { num: readPlain(t.da, rf.obj, rf.tag), proc: rf.invert ? 1 - proc : proc }
		}
		for (var b in t.buttons) out.buttons[b] = t.da.getParameterProcessValue(am, t.buttons[b].obj, t.buttons[b].tag) >= 0.5
		if (t.bypass) out.bypass = t.da.getParameterProcessValue(am, t.bypass.obj, t.bypass.tag)
	} catch (e) { log('favourite: reading the settings failed: ' + e) }
	return out
}
function favCarry(t, values) {
	var set = 0, missed = []
	for (var k in values.knobs) {
		var rf = t.knobs[k]
		if (!rf) continue
		var v = values.knobs[k]
		try {
			if (!isNaN(v.num) && setPlain(t.da, rf.obj, rf.tag, v.num)) ++set
			else { t.da.setParameterProcessValue(am, rf.obj, rf.tag, rf.invert ? 1 - v.proc : v.proc); ++set }
		} catch (e) { missed.push(k) }
	}
	for (var b in values.buttons) {
		if (!t.buttons[b]) continue
		try { t.da.setParameterProcessValue(am, t.buttons[b].obj, t.buttons[b].tag, values.buttons[b] ? 1 : 0); ++set } catch (e) { missed.push('key ' + b) }
	}
	if (values.bypass !== null && t.bypass) try { t.da.setParameterProcessValue(am, t.bypass.obj, t.bypass.tag, values.bypass) } catch (e) {}
	log('favourite "' + t.name + '": ' + set + ' controls carried' + (missed.length ? ', not: ' + missed.join(', ') : ''))
}
function favCommit(activeDevice) {
	var p = fav.pick
	fav.commitAt = 0
	if (!p) return
	var domain = p.domain
	var zone = domain === 'cs' ? ZONE_CS : ZONE_BC
	var ctx = favContext(domain)
	fav.pick = null
	if (!ctx || ctx.slotObj !== p.slotObj) { log('favourite: the track changed before the swap; not done'); return }
	var name = fav.lists[domain][p.index]
	if (!name || favIndex(domain, ctx.name) === p.index) return
	var uid = favUid(ctx.acc, ctx.slotObj, name)
	if (!uid) { log('favourite "' + name + '": not in Cubase\'s plug-in list (check the name in favourites.txt)'); readout(activeDevice, zone, favShort(name), 'NOT FOUND'); return }
	// The settings to carry: the plug-in's own, or, while Cubase has not shown the last swap yet, the ones that swap
	// was carrying.
	var values = ctx.target ? favSnapshot(ctx.target) : fav.pending && fav.pending.slotObj === ctx.slotObj ? fav.pending.values : null
	var ok = false
	try { ok = ctx.acc.mPluginManager.trySetSlotPlugin(am, ctx.slotObj, uid, true) } catch (e) { log('trySetSlotPlugin failed: ' + e) }
	log('favourite: "' + (ctx.name || '(empty slot)') + '" -> "' + name + '": ' + (ok ? 'done' : 'refused'))
	if (!ok) { readout(activeDevice, zone, favShort(name), 'FAILED'); return }
	fav.pending = values ? { slotObj: ctx.slotObj, name: name, values: values, until: Date.now() + 8000 } : null
	fav.loaded = { slotObj: ctx.slotObj, name: name, at: Date.now() }
	state.rescanAt = Date.now() + 300
	state.mixScanAt = Date.now() + 400
}
// After a swap: once the new plug-in shows up in its slot, the old one's settings go onto it.
function favApplyPending(activeDevice, targets) {
	var p = fav.pending
	if (!p) return
	if (Date.now() > p.until) { fav.pending = null; log('favourite "' + p.name + '": not seen after the swap; settings not carried'); return }
	for (var i = 0; i < targets.length; ++i) {
		var t = targets[i]
		if (!t || t.slotObj !== p.slotObj || t.name.toLowerCase() !== p.name.toLowerCase()) continue
		fav.pending = null
		favCarry(t, p.values)
		return
	}
}

// The channel strip's routing switches for the bridge's ROUTING screen (SysEx 3F; 7F = no routing).
var routingShown = null
function reportRouting(activeDevice) {
	var vals = [0x7F, 0x7F, 0x7F, 0x7F, 0x7F]
	try {
		if (state.cs && state.cs.routing) vals = state.cs.routing.map(function (rf) { return paramValue(state.cs, rf) >= 0.5 ? 1 : 0 })
	} catch (e) {}
	if (routingShown === vals.join(',')) return
	routingShown = vals.join(',')
	if (vals[0] !== 0x7F) log('routing switches (filters to input, filters to S/C, EQ to S/C, dyn pre EQ, ext S/C): ' + vals.join(' '))
	sysex(activeDevice, [0x3F].concat(vals))
}
function setRouting(activeDevice, message) {
	if (!state.cs || !state.cs.routing) return
	for (var k = 0; k < 5; ++k) state.cs.da.setParameterProcessValue(am, state.cs.routing[k].obj, state.cs.routing[k].tag, message[5 + k] ? 1 : 0)
	reportRouting(activeDevice)
}
// Where the gain-reduction meters read (SysEx 40): the channel strip's track (its MixConsole number) and position
// among the SSL plug-ins there, and the same for the bus comp the Bus Comp section is on.
var grShown = null
function reportGr(activeDevice) {
	var csT = 0, csO = 0, model = '', bcT = 0, bcO = 0
	if (state.cs && state.cs.key !== 'cubase' && numberShown > 0 && numberShown !== 0x3FFF) {
		csT = numberShown
		csO = state.cs.sslOrd || 0
		model = state.cs.label || ''
	}
	var bc = busComp()
	if (bc) {
		try {
			if (bc.channelObj >= 0) bcT = daMix.getMixerChannelIndex(am, bc.channelObj)
			else if (bc === state.bcSelected && numberShown !== 0x3FFF) bcT = numberShown
		} catch (e) {}
		bcO = bc.sslOrd || 0
	}
	if (bcT < 0) bcT = 0
	var key = [csT, csO, model, bcT, bcO].join(',')
	if (grShown === key) return
	grShown = key
	log('gain reduction from: channel strip track ' + csT + ' #' + csO + ' "' + model + '", bus comp track ' + bcT + ' #' + bcO)
	sysex(activeDevice, [0x40, (csT >> 7) & 0x7F, csT & 0x7F, csO & 0x7F, (bcT >> 7) & 0x7F, bcT & 0x7F, bcO & 0x7F].concat(encodeName(model)))
}
var pluginsShown = null
// The channel strip the UC1 is on, for the UF1's PLUGIN view (SysEx 41 <kind> <ordinal>, passed on by the bridge):
// kind 0 = none, 1 = an SSL strip (ordinal = its position among the track's SSL plug-ins, bus comps included),
// 2 = Cubase's own strip. Sent with every repaint (after a track change, after the magnifier key).
function reportStripChoice(activeDevice) {
	var kind = !state.cs ? 0 : state.cs.key === 'cubase' ? 2 : 1
	sysex(activeDevice, [0x41, kind, kind === 1 ? (state.cs.sslOrd || 0) & 0x7F : 0])
}
function paintAll(activeDevice) {
	if (ext.open) { try { extDraw(activeDevice) } catch (e) {} }   // another strip / track: its EXT FUNCS
	reportRouting(activeDevice)
	reportStripChoice(activeDevice)
	reportGr(activeDevice)
	var bc = busComp()
	// The SSL plug-ins the two sections are on, for the bridge's PRESETS screen.
	var csName = state.cs && state.cs.key !== 'cubase' ? state.cs.name || '' : ''
	var bcName = bc ? bc.name || '' : ''
	if (pluginsShown !== csName + '|' + bcName) {
		pluginsShown = csName + '|' + bcName
		sysex(activeDevice, [0x3B].concat(encodeName(csName), [0x7F, 0x7F, 0x7F], encodeName(bcName)))
	}
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
// The shelf type each bell key goes back to (by EQ object and tag), learnt when it switches to a bell.
var shelfBefore = {}
// The value of a band's bell type. The outer bands' type lists differ from the mid bands' (the top of band 4's
// list is "High Shelf IV"), so the list is read once: each value is set for a moment and its name read back, and
// the type goes back to what it was (all within one callback, so nothing is heard). Prefers "Parametric II".
var bellValues = {}
function bellValue(target, rf) {
	var key = rf.title
	if (bellValues[key] !== undefined) return bellValues[key]
	var acc = target.da, before = paramValue(target, rf)
	var names = [], seen = {}, found = null, foundAny = null
	for (var i = 0; i <= 32; ++i) {
		var v = i / 32
		acc.setParameterProcessValue(am, rf.obj, rf.tag, v)
		var name = acc.getParameterDisplayValue(am, rf.obj, rf.tag)
		if (seen[name]) continue
		seen[name] = true
		var exact = acc.getParameterProcessValue(am, rf.obj, rf.tag)
		names.push(name + ' (' + exact.toFixed(3) + ')')
		if (/parametric\s*ii$/i.test(name) && found === null) found = exact
		if (/param|bell|peak/i.test(name) && foundAny === null) foundAny = exact
	}
	acc.setParameterProcessValue(am, rf.obj, rf.tag, before)
	log(rf.title + ' types: ' + names.join(', '))
	bellValues[key] = found !== null ? found : foundAny
	return bellValues[key]
}
function isBell(target, rf) { return /param|bell|peak/i.test(target.da.getParameterDisplayValue(am, rf.obj, rf.tag)) }
function toggleParam(target, rf) {
	if (!rf) return
	if (rf.eqType) {
		var key = rf.obj + ':' + rf.tag
		if (isBell(target, rf)) target.da.setParameterProcessValue(am, rf.obj, rf.tag, shelfBefore[key] !== undefined ? shelfBefore[key] : 5 / 7)
		else {
			shelfBefore[key] = paramValue(target, rf)
			var bell = bellValue(target, rf)
			if (bell === null) { log('no bell type found for ' + rf.title); return }
			target.da.setParameterProcessValue(am, rf.obj, rf.tag, bell)
		}
		if (rf.enable && paramValue(target, rf.enable) < 0.5) target.da.setParameterProcessValue(am, rf.enable.obj, rf.enable.tag, 1)
		log('EQ type now "' + target.da.getParameterDisplayValue(am, rf.obj, rf.tag) + '"')
		return
	}
	var v = paramValue(target, rf)
	target.da.setParameterProcessValue(am, rf.obj, rf.tag, v >= 0.5 ? 0 : 1)
}
// A switch's LED: lit when the parameter is on (or, for refs marked invert, off: a bypass lights IN when 0).
function lit(target, rf) {
	if (rf.eqType) return isBell(target, rf)
	return (paramValue(target, rf) >= 0.5) !== rf.invert
}

// Button LEDs from the plug-ins' parameters. IN keys: lit while the plug-in is not bypassed.
var CS_BUTTONS = [B.HfBell, B.EqType, B.EqIn, B.LfBell, B.FastAttComp, B.Peak, B.DynIn, B.Expand, B.FastAttGate, B.ScListen]
function paintLeds(activeDevice) {
	for (var i = 0; i < CS_BUTTONS.length; ++i) {
		var b = CS_BUTTONS[i]
		if (b === B.DynIn && state.cs && state.cs.dynBypass) continue   // painted below
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
	if (state.cs && state.cs.dynBypass) {
		var dynIn = false
		try { for (var d = 0; d < state.cs.dynBypass.length; ++d) if (lit(state.cs, state.cs.dynBypass[d])) dynIn = true } catch (e) {}
		setLed(activeDevice, B.DynIn, dynIn ? 1 : 0)
	}
	setLed(activeDevice, B.BusCompIn, bcIn ? 1 : 0)
	setLed(activeDevice, B.Fine, state.fine ? 1 : 0)
}

function onKnob(activeDevice, id, clicks) {
	if (id === 0x15) { enc2Turn(activeDevice, clicks); return }
	var target = targetFor(id)
	if (!target) return
	var rf = target.knobs[id]
	if (target.emptySlots && switchOnModuleFor(activeDevice, target, id, clicks)) return
	if (!rf) return
	var acc = target.da
	var step = clicks / 64 * (state.fine ? 0.25 : 1)
	if (rf.invert) step = -step
	// A parameter with a few steps (a ratio) snaps each small change back to where it was, so while the knob is
	// being turned (clicks less than 1 s apart) the turn is added up here, not read back from the parameter.
	var now = Date.now()
	var base = rf.wanted !== undefined && now - rf.turnedAt < 1000 ? rf.wanted : acc.getParameterProcessValue(am, rf.obj, rf.tag)
	var v = Math.max(0, Math.min(1, base + step))
	acc.setParameterProcessValue(am, rf.obj, rf.tag, v)
	rf.wanted = v
	rf.turnedAt = now
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
	if (picker) {
		if (id === B.Confirm || id === B.DynIn) {
			var p = picker
			picker = null
			var chosen = STRIP_MODULES[p.kind][p.index]
			var tgt = state.cs && state.cs.emptySlots ? state.cs : p.target
			if (p.replaceSlot && chosen.name === tgt.compTitle) { readout(activeDevice, ZONE_CS, chosen.short || chosen.name, 'KEPT'); return }
			loadModule(activeDevice, tgt, p.kind, chosen, p.replaceSlot)
			return
		}
		if (id === B.Back) { closePicker(activeDevice, 'Cancelled'); return }
	}
	if (id === B.Back) { extOpen(activeDevice); return }   // EXT FUNCS
	// DYN IN on Cubase's strip without a compressor: load the Standard Compressor straight away.
	if (id === B.DynIn && state.cs && state.cs.emptySlots && state.cs.noComp && state.cs.emptySlots.length) {
		loadModule(activeDevice, state.cs, 'comp', STRIP_MODULES.comp[0])
		return
	}
	// EXP on Cubase's strip without a gate: load the Noise Gate (as DYN IN loads a compressor).
	if (id === B.Expand && state.cs && state.cs.emptySlots && state.cs.noGate && state.cs.emptySlots.length) {
		loadModule(activeDevice, state.cs, 'gate', STRIP_MODULES.gate[0])
		return
	}
	if (id === B.DynIn && state.cs && state.cs.dynBypass && state.cs.dynBypass.length) {
		var anyIn = false
		for (var d = 0; d < state.cs.dynBypass.length; ++d) if (lit(state.cs, state.cs.dynBypass[d])) anyIn = true
		for (var d2 = 0; d2 < state.cs.dynBypass.length; ++d2)
			state.cs.da.setParameterProcessValue(am, state.cs.dynBypass[d2].obj, state.cs.dynBypass[d2].tag, anyIn ? 1 : 0)
		paintLeds(activeDevice)
		return
	}
	if (id === B.Magnifier) { nextStrip(activeDevice); return }
	if (id === B.Knob360) { toggleWindow(activeDevice); return }
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

// ---------------------------------------------------------------------------
// Loading an SSL preset (the bridge's PRESETS screen reads the preset file and sends each value: SysEx 3C <section>
// <id> 7F 7F 7F <value>, then 3D <section> <name>). An SSL preset names its parameters by their JUCE ids; the
// Direct Access tag of a JUCE plug-in's parameter is the id's Java-style string hash (31 * h + c, 32 bits, sign bit
// cleared) plus 4201 (checked against all Bus Compressor 2 tags). The value is the plug-in's own: a choice's index
// (set through convertParameterPlainToProcessValue), or a plain number in the parameter's unit (set as its display
// value, which the plug-in parses; checked by reading it back, else found by bisection on the display).
// ---------------------------------------------------------------------------
function juceTag(id) {
	var h = 0
	for (var i = 0; i < id.length; ++i) h = (Math.imul(31, h) + id.charCodeAt(i)) | 0
	return (h & 0x7FFFFFFF) + 4201
}
function leadingNumber(text) {
	var m = /^\s*\+?(-?\d*\.?\d+(e[-+]?\d+)?)/i.exec(text || '')
	return m ? parseFloat(m[1]) : NaN
}
var presetLoad = { applied: 0, skipped: 0, unknown: 0, failed: [] }
function presetTarget(section) {
	if (section === 0) return state.cs && state.cs.key !== 'cubase' ? state.cs : null
	return busComp()
}
function applyPresetValue(section, id, text) {
	var t = presetTarget(section)
	if (!t) { ++presetLoad.skipped; return }
	var acc = t.da, obj = t.obj, tag = juceTag(id)
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
	// The display across the range (65 points), for the two cases below.
	var at = function (n) { acc.setParameterProcessValue(am, obj, tag, n); return acc.getParameterDisplayValue(am, obj, tag) }
	var probe = function () {
		var pts = []
		for (var k = 0; k <= 64; ++k) { var d = at(k / 64); pts.push({ n: k / 64, text: d, num: leadingNumber(d) }) }
		return pts
	}
	// A stepped parameter whose file value is its index (4K compressor / gate release, Bus Compressor 2 attack and
	// release): a short list of distinct readings, and the index picks one.
	var pts = null
	if (v === Math.round(v) && v >= 0) {
		pts = probe()
		var steps = []
		for (var q = 0; q < pts.length; ++q) if (!steps.length || steps[steps.length - 1].text !== pts[q].text) steps.push(pts[q])
		if (steps.length <= 24 && v < steps.length) {
			// A number that is itself one of the readings (an HPF of "20" Hz) means the value, not the index.
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
	var tol = Math.max(Math.abs(v) * 0.02, 0.05)
	if (!isNaN(got) && Math.abs(got - v) <= tol) { ++presetLoad.applied; return }
	// Bisect between the two sampled points around the value (the ends may read "OFF": no number there).
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
function decodeName(message, from) {
	var out = '', parts = [], i = from
	for (; i + 2 < message.length && message[i] !== 0xF7; i += 3) {
		if (message[i] === 0x7F && message[i + 1] === 0x7F && message[i + 2] === 0x7F) { parts.push(out); out = ''; continue }
		out += String.fromCharCode((message[i] << 14) | (message[i + 1] << 7) | message[i + 2])
	}
	parts.push(out)
	return parts
}
function onPresetMessage(activeDevice, message) {
	var section = message[5]
	if (message[4] === 0x3C) {
		var parts = decodeName(message, 6)
		try { applyPresetValue(section, parts[0], parts[1] || '') } catch (e) { ++presetLoad.skipped; presetLoad.failed.push(parts[0] + ': ' + e) }
		return
	}
	var name = decodeName(message, 6)[0]
	var t = presetTarget(section)
	log('preset "' + name + '" into ' + (t ? '"' + t.name + '"' : 'nothing (no SSL plug-in in that section)') + ': ' + presetLoad.applied
		+ ' set, ' + presetLoad.skipped + ' not set, ' + presetLoad.unknown + ' not parameters'
		+ (presetLoad.failed.length ? ' (not set: ' + presetLoad.failed.slice(0, 12).join(', ') + ')' : ''))
	readout(activeDevice, section === 0 ? ZONE_CS : ZONE_BC, name, t ? 'LOADED' : 'NO SSL')
	presetLoad = { applied: 0, skipped: 0, unknown: 0, failed: [] }
	paintAll(activeDevice)
}

var sysexErrors = 0
midiInput.mOnSysex = function (activeDevice, message) {
	if (message.length < 7 || message[4] === undefined) return
	var cmd = message[4]
	if ((cmd === 0x3C || cmd === 0x3D) && am) { onPresetMessage(activeDevice, message); return }
	if (cmd === 0x43) { onFavList(message); return }
	if (cmd === 0x45 && am) { followUf1(activeDevice, message[5], message[6]); return }
	// The start setting, once: the bridge sends it with every favourites reply (the favourite cycle asks again
	// after a pause), which must not undo a job picked with the push.
	if (cmd === 0x4C) { if (!enc2.started) { enc2.started = true; enc2.mode = Math.min(ENC2_MODES.length - 1, message[5] || 0); log('secondary encoder at start: ' + ENC2_MODES[enc2.mode].name) } return }
	if (cmd === 0x4D && am) { try { extEvent(activeDevice, message[5], message[6] - 64) } catch (e) { log('EXT FUNCS: ' + e) } return }
	if (cmd === 0x44 && am) { try { favStep(activeDevice, message[5] === 0 ? 'cs' : 'bc', message[6] ? 1 : -1) } catch (e) { log('favourite step: ' + e) } return }
	if (cmd === 0x3E && am && message.length >= 11) { try { setRouting(activeDevice, message) } catch (e) { log('routing: ' + e) } return }
	if (cmd !== 0x20 && cmd !== 0x21) return
	if (!am) { if (++sysexErrors <= 3) log('UC1 event before the page was active; ignored'); return }
	try {
		if (cmd === 0x20) onKnob(activeDevice, message[5], message[6] - 64)
		else if (message[5] === B.DynIn && state.cs && state.cs.emptySlots && !picker) {
			// Cubase's strip: DYN IN acts on release; held for 0.6 s it opens the compressor picker instead
			// (to load a type other than Standard, or to change the loaded one).
			if (message[6] === 1) { dynPressedAt = Date.now(); return }
			if (!dynPressedAt) return
			var held = Date.now() - dynPressedAt
			dynPressedAt = 0
			if (held >= 600) openCompPicker(activeDevice, state.cs)
			else onButton(activeDevice, B.DynIn)
		}
		else if (message[5] === B.SecEncPush) enc2Push(activeDevice, message[6] === 1)
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
	if (target === state.cs && state.cs.routing) reportRouting(activeDevice)
	if (target === state.cs && ext.open) extDraw(activeDevice)   // EXT FUNCS shows the value
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
	state.trackName = title
	picker = null
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
	routingShown = null
	grShown = null
	labelShown = null
	log('page active')
	favAsk(activeDevice)
	state.rescanAt = Date.now() + 100
	state.mixScanAt = Date.now() + 200
	numberShown = -2
}
page.mOnIdle = function (activeDevice, activeMapping) {
	am = activeMapping
	flushLog(activeDevice)
	var now = Date.now()
	if (picker && now > picker.until) closePicker(activeDevice, 'Cancelled')
	if (state.restoreBcAt && now >= state.restoreBcAt) { state.restoreBcAt = 0; showBcNames(activeDevice) }
	if (numberBase >= 0 && now >= numberCheckAt) {
		numberCheckAt = now + 1000
		try { showNumber(activeDevice, numberBase) } catch (e) {}
	}
	if (fav.commitAt && now >= fav.commitAt) { try { favCommit(activeDevice) } catch (e) { log('favourite swap: ' + e) } }
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
