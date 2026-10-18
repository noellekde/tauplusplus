// tau++ command line: runs a .tau program.
//   tau++ program.tau [--text file [--caret N]]
// --text provides a document to the Host functions (TextLen, CharAt, CaretLine,
// CaretCol, LineCount, Paint) so editor scripts can be tested without the IDE.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

#include "tauvm.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static std::string readAllStdin() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::string out;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, stdin)) > 0) out.append(buf, n);
    return out;
}

static bool readFile(const char* path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

struct TextHost : tau::Host {
    std::string text;
    long caret = 0;

    void call(const std::string& name, const std::vector<tau::Operand>& a, tau::Vm& vm) override {
        static const struct { const char* n; size_t argc; } table[] = {
            {"textlen", 1}, {"charat", 2}, {"caretline", 1}, {"caretcol", 1}, {"linecount", 1}, {"paint", 3}};
        size_t need = (size_t)-1;
        for (auto& t : table)
            if (name == t.n) need = t.argc;
        if (need == (size_t)-1) vm.fail("unknown host function '" + name + "'");
        if (a.size() != need) vm.fail("host function '" + name + "' takes " + std::to_string(need) + " argument(s)");
        long n = (long)text.size();
        if (name == "textlen") {
            vm.setReg(a[0], (int32_t)n);
        } else if (name == "charat") {
            int32_t i = vm.val(a[1]);
            vm.setReg(a[0], (i >= 0 && i < n) ? (uint8_t)text[(size_t)i] : 0);
        } else if (name == "caretline") {
            int32_t c = 1;
            for (long i = 0; i < caret && i < n; ++i)
                if (text[(size_t)i] == '\n') ++c;
            vm.setReg(a[0], c);
        } else if (name == "caretcol") {
            long ls = 0;
            for (long i = 0; i < caret && i < n; ++i)
                if (text[(size_t)i] == '\n') ls = i + 1;
            vm.setReg(a[0], (int32_t)(caret - ls + 1));
        } else if (name == "linecount") {
            int32_t c = 1;
            for (char ch : text)
                if (ch == '\n') ++c;
            vm.setReg(a[0], c);
        } else {
            vm.out += "paint " + std::to_string(vm.val(a[0])) + " " + std::to_string(vm.val(a[1])) + " " +
                      std::to_string(vm.val(a[2])) + "\n";
        }
    }
};

int main(int argc, char** argv) {
    const char* path = nullptr;
    const char* textPath = nullptr;
    long caret = 0, steps = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--text") && i + 1 < argc) textPath = argv[++i];
        else if (!strcmp(argv[i], "--caret") && i + 1 < argc) caret = atol(argv[++i]);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atol(argv[++i]);
        else if (!path) path = argv[i];
        else path = nullptr, i = argc;
    }
    if (!path) {
        fprintf(stderr, "usage: tau++ program.tau [--text file|- [--caret N]] [--steps N]\n");
        return 2;
    }
    TextHost host;
    if (textPath) {
        bool ok = true;
        if (!strcmp(textPath, "-")) host.text = readAllStdin();
        else ok = readFile(textPath, host.text);
        if (!ok) {
            fprintf(stderr, "cannot read %s\n", textPath);
            return 2;
        }
        host.caret = caret;
    }
    std::string source;
    if (!readFile(path, source)) {
        fprintf(stderr, "cannot read %s\n", path);
        return 2;
    }
    int status = 0;
    std::string output;
    try {
        tau::Program prog = tau::Program::parse(source);
        tau::Vm vm(prog, textPath ? &host : nullptr, steps > 0 ? steps : (textPath ? 50000000L : 5000000L));
        try {
            vm.run();
        } catch (const tau::Error& e) {
            fprintf(stderr, "Tau error: %s\n", e.what());
            status = 1;
        }
        output = vm.out;
    } catch (const tau::Error& e) {
        fprintf(stderr, "Tau error: %s\n", e.what());
        status = 1;
    }
    fwrite(output.data(), 1, output.size(), stdout);
    fflush(stdout);
    return status;
}
