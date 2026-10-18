#include "tauvm.hpp"

#include <cstddef>
#include <unordered_map>

namespace tau {

static const int SIZES[3] = {1, 50, 100};
static const char* const NAMES[3] = {"T", "Ta", "Tau"};
static const size_t MAX_CALLS = 256;

[[noreturn]] static void failAt(int line, const std::string& msg) {
    throw Error("line " + std::to_string(line) + ": " + msg);
}

static std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

static int typeOf(const std::string& low) {
    if (low == "t") return 0;
    if (low == "ta") return 1;
    if (low == "tau") return 2;
    return -1;
}

struct Tok {
    int kind = 0;  // 0 bare, 1 string, 2 char literal
    std::string s;
    int32_t v = 0;
};

static int escapeValue(char c, int line) {
    switch (c) {
        case 'n': return 10;
        case 't': return 9;
        case 'r': return 13;
        case '0': return 0;
        case '\\': return 92;
        case '\'': return 39;
        case '"': return 34;
    }
    failAt(line, std::string("unknown escape '\\") + c + "'");
}

static std::vector<Tok> tokenize(const std::string& line, int lineno) {
    std::vector<Tok> toks;
    size_t i = 0, n = line.size();
    while (i < n) {
        char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
        } else if (c == '#') {
            break;
        } else if (c == '"') {
            size_t j = i + 1;
            while (j < n && line[j] != '"') j += (line[j] == '\\') ? 2 : 1;
            if (j >= n) failAt(lineno, "unterminated string");
            Tok t;
            t.kind = 1;
            t.s = line.substr(i + 1, j - i - 1);
            toks.push_back(t);
            i = j + 1;
        } else if (c == '\'') {
            size_t end = (i + 1 < n && line[i + 1] == '\\') ? i + 3 : i + 2;
            if (end >= n || line[end] != '\'') failAt(lineno, "bad character literal");
            Tok t;
            t.kind = 2;
            if (end - i - 1 == 2)
                t.v = escapeValue(line[i + 2], lineno);
            else
                t.v = (uint8_t)line[i + 1];
            toks.push_back(t);
            i = end + 1;
        } else {
            size_t j = i;
            while (j < n && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') ++j;
            Tok t;
            t.s = line.substr(i, j - i);
            toks.push_back(t);
            i = j;
        }
    }
    return toks;
}

static int digitVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parseInt(const std::string& s, int32_t& out) {
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && s[i] == '-') {
        neg = true;
        ++i;
    }
    if (i >= s.size()) return false;
    int base = 10;
    if (s.size() - i > 2 && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    } else if (s.size() - i > 2 && s[i] == '0' && (s[i + 1] == 'b' || s[i + 1] == 'B')) {
        base = 2;
        i += 2;
    }
    uint64_t v = 0;
    for (; i < s.size(); ++i) {
        int d = digitVal(s[i]);
        if (d < 0 || d >= base) return false;
        v = v * (uint64_t)base + (uint64_t)d;
    }
    if (neg) v = 0 - v;
    out = (int32_t)(uint32_t)v;
    return true;
}

static Operand parseVal(const Tok& t, int line) {
    Operand o;
    if (t.kind == 2) {
        o.v = t.v;
        return o;
    }
    if (t.kind == 1) failAt(line, "unexpected string");
    const std::string& s = t.s;
    if (s.size() >= 2 && (s[0] == 'r' || s[0] == 'R')) {
        bool digits = true;
        for (size_t i = 1; i < s.size(); ++i)
            if (s[i] < '0' || s[i] > '9') digits = false;
        if (digits) {
            if (s.size() > 3 || std::stoi(s.substr(1)) > 15) failAt(line, "bad register '" + s + "'");
            o.isReg = true;
            o.v = std::stoi(s.substr(1));
            return o;
        }
    }
    if (!parseInt(s, o.v)) failAt(line, "bad value '" + s + "'");
    return o;
}

static std::vector<uint8_t> parseBits(const std::string& text, int line) {
    std::vector<uint8_t> out;
    size_t i = 0, n = text.size();
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
    while (i < n) {
        if (isSpace(text[i])) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && !isSpace(text[j])) ++j;
        std::string g = text.substr(i, j - i);
        bool ok = g.size() == 8;
        for (char c : g)
            if (c != '0' && c != '1') ok = false;
        if (!ok) failAt(line, "'" + g + "' is not an 8-bit binary group");
        out.push_back((uint8_t)std::stoi(g, nullptr, 2));
        i = j;
    }
    return out;
}

static std::vector<uint8_t> decodeText(const std::string& raw, int line) {
    std::vector<uint8_t> out;
    size_t n = raw.size();
    for (size_t i = 0; i < n; ++i) {
        char c = raw[i];
        if (c != '\\') {
            out.push_back((uint8_t)c);
            continue;
        }
        if (i + 1 >= n) failAt(line, "dangling backslash in string");
        char e = raw[++i];
        if (e == 'x') {
            if (i + 2 >= n) failAt(line, "bad \\x escape");
            int h1 = digitVal(raw[i + 1]), h2 = digitVal(raw[i + 2]);
            if (h1 < 0 || h2 < 0) failAt(line, "bad \\x escape");
            out.push_back((uint8_t)(h1 * 16 + h2));
            i += 2;
        } else {
            out.push_back((uint8_t)escapeValue(e, line));
        }
    }
    return out;
}

static bool isIdent(const std::string& s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        bool digit = c >= '0' && c <= '9';
        if (!(alpha || (i > 0 && digit))) return false;
    }
    return true;
}

struct Spec {
    Op op;
    const char* args;
};

static const std::unordered_map<std::string, Spec>& specs() {
    static const std::unordered_map<std::string, Spec> m = {
        {"define", {Op::Define, "Tvs"}}, {"text", {Op::Text, "Tvs"}},
        {"set", {Op::Set, "rv"}}, {"add", {Op::Add, "rv"}}, {"sub", {Op::Sub, "rv"}},
        {"mul", {Op::Mul, "rv"}}, {"div", {Op::Div, "rv"}}, {"mod", {Op::Mod, "rv"}},
        {"load", {Op::Load, "rvv"}}, {"store", {Op::Store, "vvv"}}, {"len", {Op::Len, "rv"}},
        {"jump", {Op::Jump, "l"}}, {"jeq", {Op::Jeq, "vvl"}}, {"jne", {Op::Jne, "vvl"}},
        {"jlt", {Op::Jlt, "vvl"}}, {"jgt", {Op::Jgt, "vvl"}}, {"jle", {Op::Jle, "vvl"}},
        {"jge", {Op::Jge, "vvl"}}, {"call", {Op::Call, "l"}},
        {"ret", {Op::Ret, ""}}, {"halt", {Op::Halt, ""}}, {"newline", {Op::Newline, ""}},
        {"print", {Op::Print, "v"}}, {"printchar", {Op::PrintChar, "v"}},
        {"printint", {Op::PrintInt, "v"}}, {"host", {Op::Host, "n*"}},
    };
    return m;
}

static bool isJump(Op op) {
    return op == Op::Jump || op == Op::Jeq || op == Op::Jne || op == Op::Jlt || op == Op::Jgt ||
           op == Op::Jle || op == Op::Jge || op == Op::Call;
}

Program Program::parse(const std::string& source) {
    Program p;
    std::unordered_map<std::string, int> labels;
    int lineno = 0;
    size_t pos = 0;
    while (pos <= source.size()) {
        size_t nl = source.find('\n', pos);
        std::string raw = source.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? source.size() + 1 : nl + 1;
        ++lineno;

        std::vector<Tok> toks = tokenize(raw, lineno);
        if (toks.empty()) continue;
        if (toks[0].kind != 0) failAt(lineno, "a line must start with a statement name");
        const std::string word = toks[0].s;
        const std::string low = lower(word);
        if (toks.size() == 1 && typeOf(low) >= 0) continue;
        if (toks.size() == 1 && word.back() == ':') {
            std::string name = word.substr(0, word.size() - 1);
            if (!isIdent(name)) failAt(lineno, "bad label '" + word + "'");
            if (labels.count(name)) failAt(lineno, "duplicate label '" + name + "'");
            labels[name] = (int)p.code.size();
            continue;
        }
        auto it = specs().find(low);
        if (it == specs().end()) failAt(lineno, "unknown statement '" + word + "'");
        std::string spec = it->second.args;
        bool rest = false;
        std::string fixed;
        for (char c : spec) {
            if (c == '*') rest = true;
            else fixed += c;
        }
        size_t nargs = toks.size() - 1;
        if (nargs < fixed.size() || (!rest && nargs > fixed.size()))
            failAt(lineno, "wrong number of arguments for " + low);

        Instr ins;
        ins.op = it->second.op;
        ins.line = lineno;
        std::string rawText;
        for (size_t k = 0; k < fixed.size(); ++k) {
            const Tok& tok = toks[k + 1];
            switch (fixed[k]) {
                case 'T': {
                    int t = tok.kind == 0 ? typeOf(lower(tok.s)) : -1;
                    if (t < 0) failAt(lineno, "unknown type (use T, Ta or Tau)");
                    ins.type = t;
                    break;
                }
                case 'v':
                    ins.args.push_back(parseVal(tok, lineno));
                    break;
                case 'r': {
                    Operand o = parseVal(tok, lineno);
                    if (!o.isReg) failAt(lineno, "expected a register");
                    ins.args.push_back(o);
                    break;
                }
                case 's':
                    if (tok.kind != 1) failAt(lineno, "expected a quoted string");
                    rawText = tok.s;
                    break;
                case 'l':
                case 'n':
                    if (tok.kind != 0) failAt(lineno, "expected a name");
                    ins.text = fixed[k] == 'n' ? lower(tok.s) : tok.s;
                    break;
            }
        }
        if (rest)
            for (size_t k = fixed.size() + 1; k < toks.size(); ++k) ins.args.push_back(parseVal(toks[k], lineno));
        if (ins.op == Op::Define || ins.op == Op::Text) {
            ins.bytes = ins.op == Op::Define ? parseBits(rawText, lineno) : decodeText(rawText, lineno);
            if ((int)ins.bytes.size() > SIZES[ins.type])
                failAt(lineno, std::to_string(ins.bytes.size()) + " bytes does not fit in " + NAMES[ins.type] +
                                   " (" + std::to_string(SIZES[ins.type]) + " bytes)");
        } else {
            p.isStatic = false;
        }
        p.code.push_back(ins);
    }
    for (auto& ins : p.code) {
        if (isJump(ins.op)) {
            auto f = labels.find(ins.text);
            if (f == labels.end()) failAt(ins.line, "undefined label '" + ins.text + "'");
            ins.target = f->second;
        }
    }
    return p;
}

void Vm::fail(const std::string& msg) const {
    throw Error("line " + std::to_string(line) + ": " + msg);
}

void Vm::setReg(const Operand& o, int32_t v) {
    if (!o.isReg) fail("host result argument must be a register");
    regs[o.v] = v;
}

Cell& Vm::cell(int32_t addr) {
    auto it = mem.find(addr);
    if (it == mem.end()) fail("no cell at address " + std::to_string(addr));
    return it->second;
}

static size_t textLen(const std::vector<uint8_t>& d) {
    size_t z = 0;
    while (z < d.size() && d[z] != 0) ++z;
    return z;
}

void Vm::run() {
    const std::vector<Instr>& code = prog_.code;
    size_t pc = 0;
    long steps = 0;
    std::vector<size_t> stack;
    while (pc < code.size()) {
        const Instr& in = code[pc];
        line = in.line;
        if (++steps > maxSteps_) fail("step limit exceeded");
        size_t next = pc + 1;
        switch (in.op) {
            case Op::Define:
            case Op::Text: {
                Cell c;
                c.type = in.type;
                c.data = in.bytes;
                c.data.resize((size_t)SIZES[in.type], 0);
                mem[val(in.args[0])] = c;
                break;
            }
            case Op::Set: case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod: {
                int32_t a = regs[in.args[0].v], b = val(in.args[1]), r = 0;
                uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
                if (in.op == Op::Set) r = b;
                else if (in.op == Op::Add) r = (int32_t)(ua + ub);
                else if (in.op == Op::Sub) r = (int32_t)(ua - ub);
                else if (in.op == Op::Mul) r = (int32_t)(ua * ub);
                else {
                    if (b == 0) fail("division by zero");
                    int64_t q = (int64_t)a / (int64_t)b;
                    r = in.op == Op::Div ? (int32_t)(uint32_t)(uint64_t)q : (int32_t)((int64_t)a % (int64_t)b);
                }
                regs[in.args[0].v] = r;
                break;
            }
            case Op::Load: {
                Cell& c = cell(val(in.args[1]));
                int32_t idx = val(in.args[2]);
                if (idx < 0 || (size_t)idx >= c.data.size())
                    fail("index " + std::to_string(idx) + " outside " + NAMES[c.type] + " cell");
                regs[in.args[0].v] = c.data[(size_t)idx];
                break;
            }
            case Op::Store: {
                Cell& c = cell(val(in.args[0]));
                int32_t idx = val(in.args[1]);
                if (idx < 0 || (size_t)idx >= c.data.size())
                    fail("index " + std::to_string(idx) + " outside " + NAMES[c.type] + " cell");
                c.data[(size_t)idx] = (uint8_t)(val(in.args[2]) & 0xFF);
                break;
            }
            case Op::Len:
                regs[in.args[0].v] = (int32_t)textLen(cell(val(in.args[1])).data);
                break;
            case Op::Jump:
                next = (size_t)in.target;
                break;
            case Op::Jeq: case Op::Jne: case Op::Jlt: case Op::Jgt: case Op::Jle: case Op::Jge: {
                int32_t a = val(in.args[0]), b = val(in.args[1]);
                bool t = in.op == Op::Jeq ? a == b : in.op == Op::Jne ? a != b : in.op == Op::Jlt ? a < b
                       : in.op == Op::Jgt ? a > b : in.op == Op::Jle ? a <= b : a >= b;
                if (t) next = (size_t)in.target;
                break;
            }
            case Op::Call:
                if (stack.size() >= MAX_CALLS) fail("call stack overflow");
                stack.push_back(next);
                next = (size_t)in.target;
                break;
            case Op::Ret:
                if (stack.empty()) fail("ret without call");
                next = stack.back();
                stack.pop_back();
                break;
            case Op::Halt:
                next = code.size();
                break;
            case Op::Newline:
                out += '\n';
                break;
            case Op::Print: {
                Cell& c = cell(val(in.args[0]));
                out.append((const char*)c.data.data(), textLen(c.data));
                break;
            }
            case Op::PrintChar:
                out += (char)(val(in.args[0]) & 0xFF);
                break;
            case Op::PrintInt:
                out += std::to_string(val(in.args[0]));
                break;
            case Op::Host:
                if (!host_) fail("no host function '" + in.text + "'");
                host_->call(in.text, in.args, *this);
                break;
        }
        pc = next;
    }
    if (prog_.isStatic) {
        std::string joined;
        bool any = false;
        for (auto& kv : mem) {
            if (kv.second.type != 2) continue;
            if (any) joined += '\n';
            joined.append((const char*)kv.second.data.data(), textLen(kv.second.data));
            any = true;
        }
        if (any) out += joined + "\n";
    }
}

}  // namespace tau
