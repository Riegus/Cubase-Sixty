#include "MidiPorts.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>

#include "RtMidi.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace cs {
namespace {

std::atomic<bool> g_preferLoop{false};  // default: ports created by the bridge; --loopmidi uses ports made in loopMIDI instead
std::atomic<bool> g_anyVirtual{false};
std::atomic<bool> g_trace{false};

#ifdef _WIN32
// The few teVirtualMIDI entry points we use, loaded at run time so the bridge starts without the library.
struct VmPort;   // opaque
using VmDataCb = void(CALLBACK*)(VmPort*, LPBYTE, DWORD, DWORD_PTR);
using FnCreate = VmPort*(CALLBACK*)(LPCWSTR, VmDataCb, DWORD_PTR, DWORD, DWORD);
using FnClose  = void(CALLBACK*)(VmPort*);
using FnSend   = BOOL(CALLBACK*)(VmPort*, LPBYTE, DWORD);
using FnLogging = DWORD(CALLBACK*)(DWORD);
using FnVersion = LPWSTR(CALLBACK*)(PWORD, PWORD, PWORD, PWORD);

constexpr DWORD kParseRx = 1;           // TE_VM_FLAGS_PARSE_RX
constexpr DWORD kRxOnly  = 4;           // TE_VM_FLAGS_INSTANTIATE_RX_ONLY: the port apps WRITE to
constexpr DWORD kTxOnly  = 8;           // TE_VM_FLAGS_INSTANTIATE_TX_ONLY: the port apps READ from
constexpr DWORD kBoth    = 12;          // TE_VM_FLAGS_INSTANTIATE_BOTH: an ordinary two-way port (the way loopMIDI makes them)
constexpr DWORD kMaxSysex = 65535;

struct Vm {
    HMODULE lib = nullptr;
    FnCreate create = nullptr;
    FnClose  close  = nullptr;
    FnSend   send   = nullptr;
    FnLogging logging = nullptr;
    FnVersion version = nullptr;
    FnVersion driverVersion = nullptr;
    bool ok() const { return create && close && send; }
};

Vm& vm()
{
    static Vm v = [] {
        Vm r;
        r.lib = LoadLibraryW(L"teVirtualMIDI64.dll");
        if (r.lib) {
            r.create = reinterpret_cast<FnCreate>(GetProcAddress(r.lib, "virtualMIDICreatePortEx2"));
            r.close  = reinterpret_cast<FnClose>(GetProcAddress(r.lib, "virtualMIDIClosePort"));
            r.send   = reinterpret_cast<FnSend>(GetProcAddress(r.lib, "virtualMIDISendData"));
            r.logging = reinterpret_cast<FnLogging>(GetProcAddress(r.lib, "virtualMIDILogging"));
            r.version = reinterpret_cast<FnVersion>(GetProcAddress(r.lib, "virtualMIDIGetVersion"));
            r.driverVersion = reinterpret_cast<FnVersion>(GetProcAddress(r.lib, "virtualMIDIGetDriverVersion"));
        }
        return r;
    }();
    return v;
}

// Prints the library and driver versions and switches the driver's debug log on (it goes to OutputDebugString:
// watch it with Sysinternals DebugView, "Capture Global Win32"). Once, the first time a port is created.
void announceOnce()
{
    static bool done = false;
    if (done) return;
    done = true;
    WORD a = 0, b = 0, c = 0, d = 0;
    if (vm().version) {
        LPWSTR v = vm().version(&a, &b, &c, &d);
        std::printf("teVirtualMIDI library %u.%u.%u.%u (%ls)\n", a, b, c, d, v ? v : L"?");
    }
    if (vm().driverVersion) {
        LPWSTR v = vm().driverVersion(&a, &b, &c, &d);
        std::printf("teVirtualMIDI driver  %u.%u.%u.%u (%ls)\n", a, b, c, d, v ? v : L"?");
    }
    if (g_trace && vm().logging) {
        const DWORD old = vm().logging(1 | 2 | 4);   // MISC | RX | TX
        std::printf("Driver logging on (previous mask %lu): see DebugView.\n", (unsigned long)old);
    }
}

std::wstring widen(const std::string& s)
{
    return std::wstring(s.begin(), s.end());   // port names are ASCII
}

void CALLBACK ignoreData(VmPort*, LPBYTE, DWORD, DWORD_PTR) {}

void CALLBACK onVmData(VmPort*, LPBYTE data, DWORD length, DWORD_PTR user)
{
    auto* cb = reinterpret_cast<MidiMessageCallback*>(user);
    if (g_trace) std::printf("[virtual port in] %lu bytes, first %02X\n", (unsigned long)length, length ? data[0] : 0);
    if (!cb || !*cb || length == 0) return;
    std::vector<unsigned char> m(data, data + length);
    (*cb)(m);
}
#endif

int findPortNow(RtMidi& api, const std::string& needle)
{
    for (unsigned i = 0; i < api.getPortCount(); ++i)
        if (api.getPortName(i).find(needle) != std::string::npos) return int(i);
    return -1;
}

// A port made just before the bridge started (by the tray app, which holds them) shows up a moment later: wait for
// it, up to 10 s counted from the first port asked for, so missing ports do not add up to minutes.
int findPort(RtMidi& api, const std::string& needle)
{
    using clock = std::chrono::steady_clock;
    static const clock::time_point deadline = clock::now() + std::chrono::seconds(10);
    for (;;) {
        const int p = findPortNow(api, needle);
        if (p >= 0 || clock::now() >= deadline) return p;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

} // namespace

void midiPortsPreferLoopMidi(bool on) { g_preferLoop = on; }
void midiPortsTrace(bool on) { g_trace = on; }
bool midiPortsAreVirtual() { return g_anyVirtual; }

// ---------------------------------------------------------------------------

struct MidiOutPort::Impl {
    RtMidiOut rt;
    bool rtOpen = false;
#ifdef _WIN32
    VmPort* vp = nullptr;
#endif
};

MidiOutPort::MidiOutPort() : impl_(new Impl) {}
MidiOutPort::~MidiOutPort()
{
#ifdef _WIN32
    if (impl_->vp) vm().close(impl_->vp);
#endif
}

bool MidiOutPort::open(const std::string& name)
{
#ifdef _WIN32
    if (!g_preferLoop && vm().ok()) {
        announceOnce();
        const std::wstring w = widen(name);
        impl_->vp = vm().create(w.c_str(), &ignoreData, 0, kMaxSysex, kBoth);
        if (impl_->vp) { g_anyVirtual = true; return true; }
        std::fprintf(stderr, "Could not create the virtual port \"%s\" (error %lu); looking for a loopMIDI port instead.\n",
                     name.c_str(), GetLastError());
    }
#endif
    const int p = findPort(impl_->rt, name);
    if (p < 0) { std::fprintf(stderr, "MIDI port \"%s\" not found.\n", name.c_str()); return false; }
    impl_->rt.openPort(unsigned(p), name);
    impl_->rtOpen = true;
    return true;
}

void MidiOutPort::send(std::vector<unsigned char>& msg)
{
#ifdef _WIN32
    if (impl_->vp) {
        if (!vm().send(impl_->vp, msg.data(), DWORD(msg.size()))) {
            static std::atomic<int> failures{0};
            if (failures++ < 5) std::fprintf(stderr, "Virtual port send failed (error %lu)\n", GetLastError());
        } else if (g_trace) {
            static std::atomic<int> sent{0};
            if (sent++ < 5) std::printf("[virtual port out] %zu bytes sent ok\n", msg.size());
        }
        return;
    }
#endif
    if (impl_->rtOpen) impl_->rt.sendMessage(&msg);
}

// ---------------------------------------------------------------------------

struct MidiInPort::Impl {
    RtMidiIn rt;
    MidiMessageCallback cb;
#ifdef _WIN32
    VmPort* vp = nullptr;
#endif
    static void rtCallback(double, std::vector<unsigned char>* msg, void* user)
    {
        auto* self = static_cast<Impl*>(user);
        if (self->cb && msg) self->cb(*msg);
    }
};

MidiInPort::MidiInPort() : impl_(new Impl) {}
MidiInPort::~MidiInPort()
{
#ifdef _WIN32
    if (impl_->vp) vm().close(impl_->vp);
#endif
}

bool MidiInPort::open(const std::string& name, MidiMessageCallback cb)
{
    impl_->cb = std::move(cb);
#ifdef _WIN32
    if (!g_preferLoop && vm().ok()) {
        announceOnce();
        const std::wstring w = widen(name);
        impl_->vp = vm().create(w.c_str(), &onVmData, reinterpret_cast<DWORD_PTR>(&impl_->cb), kMaxSysex, kBoth | kParseRx);
        if (impl_->vp) { g_anyVirtual = true; return true; }
        std::fprintf(stderr, "Could not create the virtual port \"%s\" (error %lu); looking for a loopMIDI port instead.\n",
                     name.c_str(), GetLastError());
    }
#endif
    const int p = findPort(impl_->rt, name);
    if (p < 0) { std::fprintf(stderr, "MIDI port \"%s\" not found.\n", name.c_str()); return false; }
    impl_->rt.ignoreTypes(false, true, true);   // keep SysEx
    impl_->rt.setCallback(&Impl::rtCallback, impl_.get());
    impl_->rt.openPort(unsigned(p), name);
    return true;
}

} // namespace cs
