// Runs a MIDI Remote script against a stub API that records every binding and callback, one per line, for comparing
// two versions of a script offline: node binding-snapshot.js <script.js> > a.txt (then sort both and diff).
const fs = require('fs')
const file = process.argv[2]
const log = []
const counters = {}
function mk(path) {
  const store = {}
  const fn = function () {}
  return new Proxy(fn, {
    get(t, k) {
      if (k === Symbol.toPrimitive) return () => path
      if (k === 'toString') return () => path
      if (typeof k === 'symbol') return undefined
      if (k in store) return store[k]
      const p = mk(path + '.' + k)
      store[k] = p
      return p
    },
    set(t, k, v) { store[k] = v; if (typeof v === 'function' && /^mOn/.test(k)) log.push('CB ' + path + '.' + k); return true },
    apply(t, self, args) {
      const a = args.map(x => (typeof x === 'function' || typeof x === 'object') && x !== null ? String(x.toString ? x : '?') : JSON.stringify(x))
      const name = path.split('.').pop()
      let base = path + '(' + (/make|getByIndex|getVU|bindTo|setInput|setOutput/.test(name) ? a.join(',') : '') + ')'
      if (/^make(Button|Fader|Knob|CustomValueVariable|MixerBankChannel|DirectAccess|InsertEffectViewer|SubPage|SubPageArea|Page|MixerBankZone)/.test(name)) {
        counters[base] = (counters[base] || 0) + 1
        base += '#' + counters[base]
      }
      if (/Binding$|^setSubPage|^setType|^setValueTakeOver|^trigger/.test(name)) log.push('CALL ' + path.replace(/^.*?\b(page[^.]*)/, '$1') + '(' + a.join(', ') + ')')
      return mk(base)
    }
  })
}
const api = mk('api')
global.require = () => api
global.console = { log: () => {} }
const src = fs.readFileSync(file, 'utf8')
new Function('require', src)(() => api)
process.stdout.write(log.join('\n') + '\n')
