#include "SettingsServer.h"

#include "Favourites.h"
#include "ScriptLog.h"
#include "Settings.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Sock = SOCKET;
static const Sock kBad = INVALID_SOCKET;
static void closeSock(Sock s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Sock = int;
static const Sock kBad = -1;
static void closeSock(Sock s) { close(s); }
#endif

#ifndef CS_VERSION
#define CS_VERSION "dev"
#endif

namespace cs { bool netInit(); }   // NetInit.cpp

namespace cs::settingsServer {
namespace {

// The page (settings-page.html), as C++ string pieces: SettingsPage.inc is made from it by tools/embed-page.py.
const char* const kPagePieces[] = {
#include "SettingsPage.inc"
};

constexpr int kFirstPort = 23660;
std::atomic<bool> g_run{false};
Sock g_listen = kBad;
int g_port = 0;
std::string g_token;
std::function<std::string()> g_status;
std::function<void()> g_favouritesSaved;
std::function<void(const std::string&)> g_changed;
std::thread g_thread;
std::string g_logsDir;

std::string page()
{
    std::string p;
    for (const char* piece : kPagePieces) p += piece;
    const std::string mark = "__TOKEN__";
    for (size_t at = p.find(mark); at != std::string::npos; at = p.find(mark, at + g_token.size())) p.replace(at, mark.size(), g_token);
    return p;
}

std::string json(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else o += char(c);
        }
    }
    return o + "\"";
}

std::string jsonList(const std::vector<std::string>& v)
{
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + json(v[i]);
    return o + "]";
}

std::string state()
{
    std::string o = "{\"status\":" + (g_status ? g_status() : std::string("{}"));
    o += ",\"settings\":[";
    bool first = true;
    for (const auto& d : settings::schema()) {
        if (!first) o += ",";
        first = false;
        o += "{\"key\":" + json(d.key) + ",\"section\":" + json(d.section) + ",\"label\":" + json(d.label) +
             ",\"help\":" + json(d.help) + ",\"def\":" + json(d.def) + ",\"live\":" + (d.live ? "true" : "false") +
             ",\"value\":" + json(settings::get(d.key, d.def)) + ",\"choices\":[";
        for (size_t i = 0; i < d.choices.size(); ++i)
            o += (i ? "," : "") + std::string("{\"value\":") + json(d.choices[i].value) + ",\"label\":" + json(d.choices[i].label) + "}";
        o += "]}";
    }
    o += "],\"favourites\":{\"strips\":" + jsonList(favourites::list(0)) + ",\"busComps\":" + jsonList(favourites::list(1)) +
         ",\"knownStrips\":" + jsonList(favourites::known(0)) + ",\"knownBusComps\":" + jsonList(favourites::known(1)) + "}";
    o += ",\"paths\":{\"logs\":" + json(g_logsDir) + ",\"settings\":" + json(settings::dir().string()) + "},\"version\":" + json(CS_VERSION) + "}";
    return o;
}

// application/x-www-form-urlencoded
std::string urlDecode(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) { o += char(std::stoi(s.substr(i + 1, 2), nullptr, 16)); i += 2; }
        else o += s[i];
    }
    return o;
}
std::string formField(const std::string& body, const std::string& name)
{
    size_t at = 0;
    while (at <= body.size()) {
        size_t end = body.find('&', at);
        if (end == std::string::npos) end = body.size();
        const std::string pair = body.substr(at, end - at);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos && urlDecode(pair.substr(0, eq)) == name) return urlDecode(pair.substr(eq + 1));
        at = end + 1;
    }
    return {};
}
std::vector<std::string> lines(const std::string& s)
{
    std::vector<std::string> v;
    std::stringstream in(s);
    for (std::string l; std::getline(in, l); ) { if (!l.empty() && l.back() == '\r') l.pop_back(); v.push_back(l); }
    return v;
}

void reply(Sock c, int code, const char* type, const std::string& body)
{
    const char* text = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" : "Not Found";
    std::string h = "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\nContent-Type: " + type +
                    "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
    h += body;
    for (size_t sent = 0; sent < h.size(); ) {
        const int n = send(c, h.data() + sent, int(h.size() - sent), 0);
        if (n <= 0) break;
        sent += size_t(n);
    }
}

std::string header(const std::string& head, const char* name)
{
    std::string lower = head;
    for (auto& ch : lower) ch = char(std::tolower((unsigned char)ch));
    const std::string key = "\r\n" + std::string(name) + ":";
    const size_t at = lower.find(key);
    if (at == std::string::npos) return {};
    size_t v = at + key.size();
    while (v < head.size() && head[v] == ' ') ++v;
    return head.substr(v, head.find("\r\n", v) - v);
}

void handle(Sock c)
{
    // The request: headers, then Content-Length bytes (64 KB at most; the page sends little).
    std::string req;
    char buf[4096];
    size_t headEnd = std::string::npos;
    while (req.size() < 65536) {
        const int n = recv(c, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, size_t(n));
        if (headEnd == std::string::npos) headEnd = req.find("\r\n\r\n");
        if (headEnd != std::string::npos) {
            const std::string len = header(req.substr(0, headEnd + 2), "content-length");
            const size_t want = headEnd + 4 + (len.empty() ? 0 : size_t(std::strtoul(len.c_str(), nullptr, 10)));
            if (req.size() >= want) break;
        }
    }
    if (headEnd == std::string::npos) return;
    const std::string head = req.substr(0, headEnd + 2);
    const std::string body = req.substr(headEnd + 4);
    const size_t sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) { reply(c, 400, "text/plain", "bad request"); return; }
    const std::string method = head.substr(0, sp1);
    std::string path = head.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string query;
    if (const size_t q = path.find('?'); q != std::string::npos) { query = path.substr(q + 1); path.resize(q); }

    // Only for this computer by its own name (a page elsewhere cannot point a name of its own here).
    const std::string host = header(head, "host");
    const std::string port = std::to_string(g_port);
    if (host != "127.0.0.1:" + port && host != "localhost:" + port) { reply(c, 403, "text/plain", "forbidden"); return; }

    if (method == "GET" && (path == "/" || path == "/index.html")) { reply(c, 200, "text/html; charset=utf-8", page()); return; }
    if (path.rfind("/api/", 0) != 0) { reply(c, 404, "text/plain", "not found"); return; }

    // Everything under /api/ needs the page's token (another web page cannot read it).
    const std::string token = method == "GET" ? formField(query, "token") : formField(body, "token");
    if (token != g_token) { reply(c, 403, "application/json", "{\"ok\":false,\"error\":\"not allowed\"}"); return; }

    if (method == "GET" && path == "/api/state") { reply(c, 200, "application/json", state()); return; }
    if (method == "POST" && path == "/api/set") {
        std::string why;
        const std::string key = formField(body, "key");
        const bool ok = settings::set(key, formField(body, "value"), &why);
        if (ok && g_changed) g_changed(key);
        reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":" + json(why) + "}");
        return;
    }
    if (method == "POST" && path == "/api/favourites") {
        std::string why;
        const bool ok = favourites::write(lines(formField(body, "strips")), lines(formField(body, "busComps")), &why);
        if (ok && g_favouritesSaved) g_favouritesSaved();
        reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":" + json(why) + "}");
        return;
    }
    reply(c, 404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
}

void loop()
{
    while (g_run) {
        sockaddr_in from{};
#ifdef _WIN32
        int len = sizeof from;
#else
        socklen_t len = sizeof from;
#endif
        Sock c = accept(g_listen, reinterpret_cast<sockaddr*>(&from), &len);
        if (c == kBad) { if (!g_run) break; continue; }
        try { handle(c); } catch (...) {}
        closeSock(c);
    }
}

} // namespace

bool start(std::function<std::string()> status, std::function<void()> favouritesSaved, const std::string& logsDir,
           std::function<void(const std::string&)> changed)
{
    g_changed = std::move(changed);
    netInit();
    g_logsDir = logsDir;
    g_status = std::move(status);
    g_favouritesSaved = std::move(favouritesSaved);
    std::random_device rd;
    char hex[33];
    for (int i = 0; i < 16; ++i) std::snprintf(hex + 2 * i, 3, "%02x", unsigned(rd() & 0xFF));
    g_token = hex;
    for (int port = kFirstPort; port < kFirstPort + 10; ++port) {
        Sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == kBad) break;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(uint16_t(port));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // this computer only
        if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && listen(s, 8) == 0) {
            g_listen = s;
            g_port = port;
            g_run = true;
            g_thread = std::thread(loop);
            std::printf("Settings page: http://127.0.0.1:%d/\n", port);   // (the tray app opens this address)
            return true;
        }
        closeSock(s);
    }
    std::fprintf(stderr, "Settings page: no free port from %d to %d; the page is not available.\n", kFirstPort, kFirstPort + 9);
    return false;
}

void stop()
{
    if (!g_run) return;
    g_run = false;
    closeSock(g_listen);   // ends the accept
    if (g_thread.joinable()) g_thread.join();
}

} // namespace cs::settingsServer
