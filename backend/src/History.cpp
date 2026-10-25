#include "History.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "Metrics.h"   // for nowMs()

namespace {

// The history file is tab-separated, so tabs / new lines inside text are escaped.
std::string escapeField(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\')      out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') { /* drop */ }
        else                out += c;
    }
    return out;
}

std::string unescapeField(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            out += (s[i] == 't') ? '\t' : (s[i] == 'n') ? '\n' : s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    parts.push_back(cur);
    return parts;
}

// Cuts the text to at most `max` bytes without splitting a UTF-8 character.
std::string shorten(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut);
}

}  // namespace

void History::load(const std::string& path) {
    path_ = path;
    entries_.clear();
    std::ifstream in(path);
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> f = splitTabs(line);
        if (f.size() < 10) continue;
        HistoryEntry e;
        e.id = std::atoll(f[0].c_str());
        e.timestampMs = std::atoll(f[1].c_str());
        e.type = f[2];
        e.words = std::atoi(f[3].c_str());
        e.errors = std::atoi(f[4].c_str());
        e.accuracy = std::atof(f[5].c_str());
        e.from = unescapeField(f[6]);
        e.to = unescapeField(f[7]);
        e.rank = std::atoi(f[8].c_str());
        e.text = unescapeField(f[9]);
        entries_.push_back(e);
        if (e.id >= nextId_) nextId_ = e.id + 1;
    }
}

void History::save() const {
    if (path_.empty()) return;
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    out << "# id\ttime\ttype\twords\terrors\taccuracy\tfrom\tto\trank\ttext\n";
    for (const HistoryEntry& e : entries_) {
        out << e.id << '\t' << e.timestampMs << '\t' << e.type << '\t' << e.words << '\t' << e.errors << '\t'
            << e.accuracy << '\t' << escapeField(e.from) << '\t' << escapeField(e.to) << '\t' << e.rank << '\t'
            << escapeField(e.text) << '\n';
    }
}

void History::add(HistoryEntry entry) {
    entry.id = nextId_++;
    entry.timestampMs = nowMs();
    entries_.push_back(std::move(entry));
    if (entries_.size() > kMaxEntries) entries_.erase(entries_.begin());
    save();
}

void History::addCheck(const std::string& text, int words, int errors, double accuracy) {
    HistoryEntry e;
    e.type = "check";
    e.text = shorten(text, kMaxTextLength);
    e.words = words;
    e.errors = errors;
    e.accuracy = accuracy;
    add(std::move(e));
}

void History::addCorrection(const std::string& from, const std::string& to, int rank) {
    HistoryEntry e;
    e.type = "correction";
    e.from = shorten(from, 80);
    e.to = shorten(to, 80);
    e.rank = rank;
    add(std::move(e));
}

void History::clear() {
    entries_.clear();
    save();
}

std::vector<HistoryEntry> History::list(const std::string& type, size_t limit) const {
    std::vector<HistoryEntry> out;
    for (auto it = entries_.rbegin(); it != entries_.rend() && out.size() < limit; ++it) {
        if (type.empty() || it->type == type) out.push_back(*it);
    }
    return out;
}
