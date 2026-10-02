// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: runs the SingleStepTests 8088 suite against i8086core

// Usage: run8088 <dir with *.json.gz> <metadata.json> [opcode file prefix...] [-n N] [-v]
// Each test file is unpacked through "gzip -dc", so gzip has to be on PATH.
// For every file it prints how many tests passed on registers and memory and
// how the clock count compares with the chip; -v lists the first failures.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <dirent.h>

#include "picojson/picojson.h"
#include "emulator/devices/cpu/i8086core.h"

class TestCore: public i8086core
{
public:
    std::vector<uint8_t> ram;
    std::set<uint32_t> written;
    TestCore(): i8086core(I8086_FAMILY_8088), ram(0x100000, 0) {}
    uint8_t  mem_read8(uint32_t a) override { return ram[a & 0xFFFFF]; }
    void     mem_write8(uint32_t a, uint8_t v) override { ram[a & 0xFFFFF] = v; written.insert(a & 0xFFFFF); }
    uint16_t mem_read16(uint32_t a) override { return (uint16_t)(ram[a & 0xFFFFF] | (ram[(a + 1) & 0xFFFFF] << 8)); }
    void     mem_write16(uint32_t a, uint16_t v) override { mem_write8(a, (uint8_t)v); mem_write8(a + 1, (uint8_t)(v >> 8)); }
    //The tests have nothing on the I/O bus: reads float high
    uint8_t  io_read8(uint16_t) override { return 0xFF; }
    void     io_write8(uint16_t, uint8_t) override {}
    uint16_t io_read16(uint16_t) override { return 0xFFFF; }
    void     io_write16(uint16_t, uint16_t) override {}
    uint8_t  int_ack() override { return 0xFF; }
};

static const char * REG_NAMES[] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
static const char * SEG_NAMES[] = {"es", "cs", "ss", "ds"};

static std::string read_gz(const std::string &path)
{
    std::string cmd = "gzip -dc \"" + path + "\"";
#ifdef _WIN32
    //The C library of MinGW translates line ends in text mode
    FILE * f = popen(cmd.c_str(), "rb");
#else
    //glibc and macOS take only "r" or "w" and are binary anyway
    FILE * f = popen(cmd.c_str(), "r");
#endif
    if (f == nullptr) return std::string();
    std::string s;
    char buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    pclose(f);
    return s;
}

static std::string read_file(const std::string &path)
{
    FILE * f = fopen(path.c_str(), "rb");
    if (f == nullptr) return std::string();
    std::string s;
    char buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

static unsigned num(const picojson::value &v) { return (unsigned)v.get<double>(); }

//The mask of the flags the chip leaves undefined for this file: "F6.4" looks
//under the reg field of F6
static unsigned flags_mask(const picojson::value &meta, const std::string &stem)
{
    const picojson::object &ops = meta.get("opcodes").get<picojson::object>();
    std::string op = stem.substr(0, 2);
    auto it = ops.find(op);
    if (it == ops.end()) return 0xFFFF;
    const picojson::value &e = it->second;
    if (stem.size() > 3 && e.contains("reg")) {
        const picojson::value &r = e.get("reg");
        std::string reg = stem.substr(3, 1);
        if (r.contains(reg) && r.get(reg).contains("flags-mask"))
            return num(r.get(reg).get("flags-mask"));
    }
    if (e.contains("flags-mask")) return num(e.get("flags-mask"));
    return 0xFFFF;
}

struct Totals {
    unsigned tests = 0, passed = 0, clocks_exact = 0;
    long long clocks_diff = 0;
};

int main(int argc, char ** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: run8088 <dir> <metadata.json> [prefix...] [-n N] [-v]\n");
        return 2;
    }
    std::string dir = argv[1];
    std::string meta_text = read_file(argv[2]);
    picojson::value meta;
    std::string err = picojson::parse(meta, meta_text);
    if (!err.empty()) { fprintf(stderr, "metadata: %s\n", err.c_str()); return 2; }

    std::vector<std::string> prefixes;
    unsigned limit = 0;
    bool verbose = false;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) limit = (unsigned)atoi(argv[++i]);
        else if (strcmp(argv[i], "-v") == 0) verbose = true;
        else prefixes.push_back(argv[i]);
    }

    std::vector<std::string> files;
    DIR * d = opendir(dir.c_str());
    if (d == nullptr) { fprintf(stderr, "cannot open %s\n", dir.c_str()); return 2; }
    while (dirent * e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() < 8 || n.substr(n.size() - 8) != ".json.gz") continue;
        bool take = prefixes.empty();
        for (const std::string &p : prefixes) if (n.compare(0, p.size(), p) == 0) take = true;
        if (take) files.push_back(n);
    }
    closedir(d);
    std::sort(files.begin(), files.end());

    Totals all;
    int failed_files = 0;
    for (const std::string &file : files) {
        const std::string stem = file.substr(0, file.size() - 8);
        const unsigned fmask = flags_mask(meta, stem);
        picojson::value tests;
        err = picojson::parse(tests, read_gz(dir + "/" + file));
        if (!err.empty()) { printf("%-8s parse error: %s\n", stem.c_str(), err.c_str()); failed_files++; continue; }

        Totals t;
        unsigned shown = 0;
        std::map<int, unsigned> diffs;
        for (const picojson::value &test : tests.get<picojson::array>()) {
            if (limit != 0 && t.tests >= limit) break;
            t.tests++;
            TestCore c;
            const picojson::value &ini = test.get("initial");
            const picojson::value &fin = test.get("final");
            i8086context * x = c.get_context();
            const picojson::value &ir = ini.get("regs");
            for (unsigned i = 0; i < 8; i++) x->r[i] = (uint16_t)num(ir.get(REG_NAMES[i]));
            for (unsigned i = 0; i < 4; i++) x->s[i] = (uint16_t)num(ir.get(SEG_NAMES[i]));
            c.set_flags((uint16_t)num(ir.get("flags")));
            for (const picojson::value &m : ini.get("ram").get<picojson::array>()) {
                const picojson::array &p = m.get<picojson::array>();
                c.ram[num(p[0])] = (uint8_t)num(p[1]);
            }
            c.set_ip((uint16_t)num(ir.get("ip")));
            std::vector<uint8_t> q;
            if (ini.contains("queue"))
                for (const picojson::value &b : ini.get("queue").get<picojson::array>()) q.push_back((uint8_t)num(b));
            if (!q.empty()) c.preload_queue(q.data(), (unsigned)q.size());

            //A repeated string instruction runs to its end, one pass per call
            c.execute();
            const uint64_t t0 = c.first_byte_time();
            while (c.get_bus_state()->rep_active) c.execute();
            const uint64_t t1 = c.take_next_byte();
            const int ours = (int)(t1 - t0);
            const int chip = (int)test.get("cycles").get<picojson::array>().size();

            std::string why;
            const picojson::value &fr = fin.get("regs");
            for (unsigned i = 0; i < 8; i++) {
                const unsigned want = fr.contains(REG_NAMES[i]) ? num(fr.get(REG_NAMES[i])) : num(ir.get(REG_NAMES[i]));
                if (x->r[i] != want) why += std::string(" ") + REG_NAMES[i];
            }
            for (unsigned i = 0; i < 4; i++) {
                const unsigned want = fr.contains(SEG_NAMES[i]) ? num(fr.get(SEG_NAMES[i])) : num(ir.get(SEG_NAMES[i]));
                if (x->s[i] != want) why += std::string(" ") + SEG_NAMES[i];
            }
            {
                //IP of the next instruction; take_next_byte() moved past one byte
                const unsigned want = fr.contains("ip") ? num(fr.get("ip")) : num(ir.get("ip"));
                if ((uint16_t)(x->ip - 1) != want) why += " ip";
                const unsigned wf = (fr.contains("flags") ? num(fr.get("flags")) : num(ir.get("flags"))) & fmask;
                if ((c.get_flags() & fmask) != wf) {
                    char b[64];
                    snprintf(b, sizeof(b), " flags(%04X/%04X)", c.get_flags() & fmask, wf);
                    why += b;
                }
            }
            std::set<uint32_t> expected;
            for (const picojson::value &m : fin.get("ram").get<picojson::array>()) {
                const picojson::array &p = m.get<picojson::array>();
                expected.insert(num(p[0]));
                if (c.ram[num(p[0])] != num(p[1])) {
                    char b[64];
                    snprintf(b, sizeof(b), " ram[%05X]=%02X/%02X", num(p[0]), c.ram[num(p[0])], num(p[1]));
                    why += b;
                }
            }
            for (uint32_t a : c.written) {
                if (expected.count(a) != 0) continue;
                //Written with the value it already had is invisible to the test
                bool same = false;
                for (const picojson::value &m : ini.get("ram").get<picojson::array>()) {
                    const picojson::array &p = m.get<picojson::array>();
                    if (num(p[0]) == a) same = c.ram[a] == num(p[1]);
                }
                if (!same && c.ram[a] != 0) {
                    char b[64];
                    snprintf(b, sizeof(b), " stray[%05X]", a);
                    why += b;
                }
            }

            if (why.empty()) t.passed++;
            else if (verbose && shown < 5) {
                shown++;
                printf("  FAIL %s #%u: %s:%s\n", stem.c_str(), num(test.get("idx")),
                       test.get("name").get<std::string>().c_str(), why.c_str());
            }
            if (ours == chip) t.clocks_exact++;
            t.clocks_diff += std::abs(ours - chip);
            diffs[ours - chip]++;
        }

        //The most frequent clock differences, ours minus the chip's
        std::vector<std::pair<unsigned, int>> top;
        for (auto &e : diffs) top.push_back(std::make_pair(e.second, e.first));
        std::sort(top.rbegin(), top.rend());
        std::string hist;
        for (size_t i = 0; i < top.size() && i < 3; i++) {
            char b[32];
            snprintf(b, sizeof(b), " %+d:%u%%", top[i].second, top[i].first * 100 / (t.tests ? t.tests : 1));
            hist += b;
        }
        printf("%-6s %s %5u/%-5u clocks exact %3u%% mean|d| %5.1f %s\n",
               stem.c_str(), t.passed == t.tests ? "ok  " : "FAIL", t.passed, t.tests,
               t.tests ? t.clocks_exact * 100 / t.tests : 0,
               t.tests ? (double)t.clocks_diff / t.tests : 0.0, hist.c_str());
        fflush(stdout);
        if (t.passed != t.tests) failed_files++;
        all.tests += t.tests;
        all.passed += t.passed;
        all.clocks_exact += t.clocks_exact;
        all.clocks_diff += t.clocks_diff;
    }
    printf("TOTAL %u/%u passed, %d files with failures, clocks exact %u%%, mean|d| %.2f\n",
           all.passed, all.tests, failed_files,
           all.tests ? all.clocks_exact * 100 / all.tests : 0,
           all.tests ? (double)all.clocks_diff / all.tests : 0.0);
    return failed_files == 0 ? 0 : 1;
}
