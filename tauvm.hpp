// tau++ : the C++ implementation of Tau v0.2 (same language as tau.py).
#pragma once
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace tau {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Operand {
    bool isReg = false;
    int32_t v = 0;  // register number, or immediate value
};

class Vm;

// Implemented by whatever embeds Tau (the IDE, the test CLI). "Host name args..."
// statements end up here; name is lower-case.
struct Host {
    virtual ~Host() {}
    virtual void call(const std::string& name, const std::vector<Operand>& args, Vm& vm) = 0;
};

enum class Op {
    Define, Text, Set, Add, Sub, Mul, Div, Mod, Load, Store, Len,
    Jump, Jeq, Jne, Jlt, Jgt, Jle, Jge, Call, Ret, Halt,
    Newline, Print, PrintChar, PrintInt, Host, Blob
};

struct Instr {
    Op op = Op::Halt;
    std::vector<Operand> args;
    int type = 0;                // 0 = T, 1 = Ta, 2 = Tau
    std::string text;            // label name or host function name
    std::vector<uint8_t> bytes;  // data for Define / Text
    int target = -1;
    int line = 0;
};

struct Program {
    std::vector<Instr> code;
    bool isStatic = true;  // only Define/Text statements (old v0.1 style)
    static Program parse(const std::string& source);  // throws Error
};

struct Cell {
    int type = 0;
    std::vector<uint8_t> data;
};

class Vm {
public:
    explicit Vm(const Program& program, Host* host = nullptr, long maxSteps = 5000000)
        : prog_(program), host_(host), maxSteps_(maxSteps) {}

    void run();  // throws Error; output produced so far stays in out

    std::string out;
    int line = 0;
    int32_t regs[16] = {0};
    std::map<int32_t, Cell> mem;

    int32_t val(const Operand& o) const { return o.isReg ? regs[o.v] : o.v; }
    void setReg(const Operand& o, int32_t v);
    [[noreturn]] void fail(const std::string& msg) const;

private:
    Cell& cell(int32_t addr);
    const Program& prog_;
    Host* host_;
    long maxSteps_;
};

}  // namespace tau
