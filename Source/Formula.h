#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// A tiny expression language for the Formula module. Text is compiled on the
// message thread into a fixed-size program (no allocation), which the audio
// thread evaluates with a small fixed stack.
//
//   variables  t (0..1 across the block), x (phase within the current cycle),
//              c (cycle index), a / b (the module's A and B knobs), pi, e
//   operators  + - * / % ^ and parentheses
//   functions  sin cos tan abs sqrt exp log floor fract sign
//              tri saw sqr noise (all take a phase / position)  min max pow (two arguments)
namespace modlanes::formula
{

constexpr int kMaxText = 96;
constexpr int kMaxOps = 64;
constexpr int kMaxStack = 16;

enum class Op : std::uint8_t
{
    End = 0, Const, T, X, C, A, B,
    Add, Sub, Mul, Div, Mod, Pow, Neg,
    Sin, Cos, Tan, Abs, Sqrt, Exp, Log, Floor, Fract, Sign, Tri, Saw, Sqr, Noise,
    Min, Max, Pow2
};

struct Instruction
{
    Op op = Op::End;
    float value = 0.0f;
};

struct Program
{
    std::array<Instruction, kMaxOps> code {};
    int length = 0; // 0 = empty or invalid
};

struct Inputs
{
    float t, x, c, a, b;
};

//==============================================================================
namespace detail
{
inline float hash01 (std::uint32_t v)
{
    v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; v ^= v >> 16;
    return (float) (v & 0xffffffU) / (float) 0xffffffU;
}

// Smooth value noise in -1..1.
inline float noise (float x)
{
    const float i = std::floor (x), f = x - i;
    const auto at = [] (float n) { return hash01 ((std::uint32_t) (std::int64_t) n * 2654435761U + 977U) * 2.0f - 1.0f; };
    const float s = f * f * (3.0f - 2.0f * f);
    return at (i) + s * (at (i + 1.0f) - at (i));
}

inline float wrap01 (float x) { return x - std::floor (x); }

struct Function
{
    const char* name;
    Op op;
    int arity;
};

constexpr Function functions[] = {
    { "sin", Op::Sin, 1 }, { "cos", Op::Cos, 1 }, { "tan", Op::Tan, 1 }, { "abs", Op::Abs, 1 },
    { "sqrt", Op::Sqrt, 1 }, { "exp", Op::Exp, 1 }, { "log", Op::Log, 1 }, { "floor", Op::Floor, 1 },
    { "fract", Op::Fract, 1 }, { "sign", Op::Sign, 1 }, { "tri", Op::Tri, 1 }, { "saw", Op::Saw, 1 },
    { "sqr", Op::Sqr, 1 }, { "noise", Op::Noise, 1 }, { "min", Op::Min, 2 }, { "max", Op::Max, 2 },
    { "pow", Op::Pow2, 2 },
};

inline int precedence (Op op)
{
    switch (op)
    {
        case Op::Add: case Op::Sub: return 1;
        case Op::Mul: case Op::Div: case Op::Mod: return 2;
        case Op::Neg: return 3;
        case Op::Pow: return 4;
        default: return 0;
    }
}

inline bool rightAssociative (Op op) { return op == Op::Pow || op == Op::Neg; }

inline int stackEffect (Op op)
{
    switch (op)
    {
        case Op::Const: case Op::T: case Op::X: case Op::C: case Op::A: case Op::B: return 1;
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod: case Op::Pow:
        case Op::Min: case Op::Max: case Op::Pow2: return -1;
        default: return 0; // unary operators and functions
    }
}
} // namespace detail

//==============================================================================
// Shunting-yard compiler. Returns false (and an error message) for invalid input.
inline bool compile (const char* text, Program& out, const char** error = nullptr)
{
    using namespace detail;

    struct Pending { Op op; bool isParen; int arity; };
    std::array<Pending, 32> stack {};
    int depth = 0;
    Program program;

    const auto fail = [&] (const char* message) { if (error) *error = message; out = {}; return false; };
    const auto emit = [&] (Op op, float value = 0.0f)
    {
        if (program.length >= kMaxOps - 1)
            return false;
        program.code[(size_t) program.length++] = { op, value };
        return true;
    };

    bool expectOperand = true;
    const char* p = text;

    while (*p != 0)
    {
        const char ch = *p;

        if (ch == ' ' || ch == '\t') { ++p; continue; }

        if ((ch >= '0' && ch <= '9') || ch == '.')
        {
            if (! expectOperand)
                return fail ("missing operator before a number");
            char* end = nullptr;
            const float value = std::strtof (p, &end);
            if (end == p)
                return fail ("bad number");
            p = end;
            if (! emit (Op::Const, value))
                return fail ("formula too long");
            expectOperand = false;
            continue;
        }

        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
        {
            if (! expectOperand)
                return fail ("missing operator before a name");

            char name[16] {};
            int n = 0;
            while (((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) && n < 15)
                name[n++] = (char) (*p >= 'A' && *p <= 'Z' ? *p - 'A' + 'a' : *p), ++p;

            const auto is = [&name] (const char* s) { return std::strcmp (name, s) == 0; };
            Op variable = Op::End;
            if (is ("t")) variable = Op::T;
            else if (is ("x")) variable = Op::X;
            else if (is ("c")) variable = Op::C;
            else if (is ("a")) variable = Op::A;
            else if (is ("b")) variable = Op::B;

            if (variable != Op::End || is ("pi") || is ("e"))
            {
                const bool ok = variable != Op::End ? emit (variable) : emit (Op::Const, is ("pi") ? 3.14159265f : 2.71828183f);
                if (! ok)
                    return fail ("formula too long");
                expectOperand = false;
                continue;
            }

            const Function* fn = nullptr;
            for (const auto& f : functions)
                if (is (f.name))
                    fn = &f;

            if (fn == nullptr)
                return fail ("unknown name");

            while (*p == ' ') ++p;
            if (*p != '(')
                return fail ("function needs ( )");
            if (depth + 2 > (int) stack.size())
                return fail ("too deeply nested");

            stack[(size_t) depth++] = { fn->op, false, fn->arity };
            stack[(size_t) depth++] = { Op::End, true, 0 };
            ++p;
            expectOperand = true;
            continue;
        }

        if (ch == '(')
        {
            if (! expectOperand)
                return fail ("missing operator before (");
            if (depth >= (int) stack.size())
                return fail ("too deeply nested");
            stack[(size_t) depth++] = { Op::End, true, 0 };
            ++p;
            continue;
        }

        if (ch == ')' || ch == ',')
        {
            if (expectOperand)
                return fail ("missing value");

            while (depth > 0 && ! stack[(size_t) depth - 1].isParen)
                if (! emit (stack[(size_t) --depth].op))
                    return fail ("formula too long");

            if (depth == 0)
                return fail ("unbalanced brackets");

            if (ch == ',')
            {
                // Argument separator: keep the parenthesis, expect the next argument.
                ++p;
                expectOperand = true;
                continue;
            }

            --depth; // pop "("
            if (depth > 0 && ! stack[(size_t) depth - 1].isParen && stack[(size_t) depth - 1].arity > 0)
                if (! emit (stack[(size_t) --depth].op))
                    return fail ("formula too long");

            ++p;
            expectOperand = false;
            continue;
        }

        Op op = Op::End;
        switch (ch)
        {
            case '+': op = Op::Add; break;
            case '-': op = expectOperand ? Op::Neg : Op::Sub; break;
            case '*': op = Op::Mul; break;
            case '/': op = Op::Div; break;
            case '%': op = Op::Mod; break;
            case '^': op = Op::Pow; break;
            default:  return fail ("unexpected character");
        }

        if (expectOperand && op == Op::Add) { ++p; continue; } // unary plus
        if (expectOperand && op != Op::Neg)
            return fail ("missing value");

        // Prefix minus binds to what follows, so it never pops pending operators.
        while (op != Op::Neg && depth > 0 && ! stack[(size_t) depth - 1].isParen && stack[(size_t) depth - 1].arity == 0)
        {
            const Op top = stack[(size_t) depth - 1].op;
            if (precedence (top) > precedence (op) || (precedence (top) == precedence (op) && ! rightAssociative (op)))
            {
                if (! emit (top))
                    return fail ("formula too long");
                --depth;
            }
            else
            {
                break;
            }
        }

        if (depth >= (int) stack.size())
            return fail ("too deeply nested");
        stack[(size_t) depth++] = { op, false, 0 };
        ++p;
        expectOperand = true;
    }

    if (expectOperand)
        return fail (program.length == 0 ? "empty formula" : "missing value at the end");

    while (depth > 0)
    {
        if (stack[(size_t) depth - 1].isParen)
            return fail ("unbalanced brackets");
        if (! emit (stack[(size_t) --depth].op))
            return fail ("formula too long");
    }

    // Check the stack never under- or overflows and ends with exactly one value.
    int height = 0;
    for (int i = 0; i < program.length; ++i)
    {
        const Op op = program.code[(size_t) i].op;
        const bool unary = stackEffect (op) == 0;
        if ((unary && height < 1) || (stackEffect (op) < 0 && height < 2))
            return fail ("wrong number of arguments");
        height += stackEffect (op);
        if (height > kMaxStack)
            return fail ("formula too complex");
    }
    if (height != 1)
        return fail ("wrong number of arguments");

    program.code[(size_t) program.length] = { Op::End, 0.0f };
    out = program;
    if (error) *error = nullptr;
    return true;
}

// Evaluates a compiled program. Invalid results (NaN, inf) become 0.
inline float evaluate (const Program& program, const Inputs& in)
{
    using namespace detail;
    if (program.length == 0)
        return 0.0f;

    float s[kMaxStack + 1];
    int top = -1;

    for (int i = 0; i < program.length; ++i)
    {
        const auto& ins = program.code[(size_t) i];
        switch (ins.op)
        {
            case Op::Const: s[++top] = ins.value; break;
            case Op::T:     s[++top] = in.t; break;
            case Op::X:     s[++top] = in.x; break;
            case Op::C:     s[++top] = in.c; break;
            case Op::A:     s[++top] = in.a; break;
            case Op::B:     s[++top] = in.b; break;

            case Op::Add:  s[top - 1] += s[top]; --top; break;
            case Op::Sub:  s[top - 1] -= s[top]; --top; break;
            case Op::Mul:  s[top - 1] *= s[top]; --top; break;
            case Op::Div:  s[top - 1] = s[top] != 0.0f ? s[top - 1] / s[top] : 0.0f; --top; break;
            case Op::Mod:  s[top - 1] = s[top] != 0.0f ? std::fmod (s[top - 1], s[top]) : 0.0f; --top; break;
            case Op::Pow:
            case Op::Pow2: s[top - 1] = std::pow (s[top - 1], s[top]); --top; break;
            case Op::Min:  s[top - 1] = std::fmin (s[top - 1], s[top]); --top; break;
            case Op::Max:  s[top - 1] = std::fmax (s[top - 1], s[top]); --top; break;

            case Op::Neg:   s[top] = -s[top]; break;
            case Op::Sin:   s[top] = std::sin (s[top]); break;
            case Op::Cos:   s[top] = std::cos (s[top]); break;
            case Op::Tan:   s[top] = std::tan (s[top]); break;
            case Op::Abs:   s[top] = std::abs (s[top]); break;
            case Op::Sqrt:  s[top] = std::sqrt (std::abs (s[top])); break;
            case Op::Exp:   s[top] = std::exp (std::fmin (s[top], 30.0f)); break;
            case Op::Log:   s[top] = s[top] > 0.0f ? std::log (s[top]) : 0.0f; break;
            case Op::Floor: s[top] = std::floor (s[top]); break;
            case Op::Fract: s[top] = wrap01 (s[top]); break;
            case Op::Sign:  s[top] = s[top] > 0.0f ? 1.0f : s[top] < 0.0f ? -1.0f : 0.0f; break;
            case Op::Tri:   s[top] = 1.0f - 4.0f * std::abs (wrap01 (s[top] + 0.25f) - 0.5f); break;
            case Op::Saw:   s[top] = wrap01 (s[top]) * 2.0f - 1.0f; break;
            case Op::Sqr:   s[top] = wrap01 (s[top]) < 0.5f ? 1.0f : -1.0f; break;
            case Op::Noise: s[top] = noise (s[top]); break;

            case Op::End:
                break;
        }
    }

    const float result = s[0];
    return std::isfinite (result) ? result : 0.0f;
}

// Clip modes for the Formula module: 0 hard, 1 soft (tanh), 2 fold.
inline float clip (float v, int mode)
{
    switch (mode)
    {
        case 1:  return std::tanh (v);
        case 2:
        {
            // Triangle fold back into -1..1.
            const float f = v * 0.25f + 0.25f;
            return 1.0f - 4.0f * std::abs ((f - std::floor (f)) - 0.5f);
        }
        default: return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
    }
}

// Examples offered in the inspector.
struct Example
{
    const char* name;
    const char* text;
};

constexpr Example examples[] = {
    { "Sine",               "sin(2*pi*x)" },
    { "Decaying sine",      "sin(2*pi*x)*exp(-4*t)" },
    { "Bumps",              "abs(sin(pi*x))" },
    { "Exponential rise",   "x^(1+a*6)" },
    { "Wavefolder",         "sin(2*pi*x)*(1+b*6)" },
    { "Noise drift",        "noise(t*8 + a*20)" },
    { "Accelerating pulse", "sqr(t*t*8)" },
    { "Chirp",              "sin(2*pi*(t*t*(4+a*16)))" },
    { "Stepped random",     "noise(floor(t*8)*3.7)" },
    { "Swell",              "tri(x)*(0.5-0.5*cos(2*pi*t))" },
};

} // namespace modlanes::formula
