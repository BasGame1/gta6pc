// VEH fallback emulator for x86-64 extensions missing on older CPUs (AVX, AVX2, FMA, F16C, BMI1, BMI2,
// POPCNT, MOVBE, SSE4.1/SSE4.2). Portable decoder/executor; Windows VEH wrapper (#ifdef _WIN32) activates only when
// the host CPU lacks any of those extensions or LEONIDA_CPU_EMU=1 is set.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#endif

namespace rt::cpu_emu {

struct alignas(32) CpuState {
    uint64_t gpr[16]{};  // ModRM order: 0=RAX,1=RCX,2=RDX,3=RBX,4=RSP,5=RBP,6=RSI,7=RDI,8..15=R8..R15
    uint64_t rip = 0;
    uint64_t rflags = 0;
    uint32_t mxcsr = 0x1F80;
    alignas(32) uint8_t ymm[16][32]{};
};

namespace detail {

inline uint16_t bswap16(uint16_t v) { return static_cast<uint16_t>((v >> 8) | (v << 8)); }
inline uint32_t bswap32(uint32_t v) {
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) | ((v >> 8) & 0xFF00u) | ((v >> 24) & 0xFFu);
}
inline uint64_t bswap64(uint64_t v) {
    return (uint64_t(bswap32(uint32_t(v))) << 32) | bswap32(uint32_t(v >> 32));
}

template <typename T>
inline T read_unaligned(const void* p) {
    T v{};
    std::memcpy(&v, p, sizeof(T));
    return v;
}

template <typename T>
inline void write_unaligned(void* p, T v) {
    std::memcpy(p, &v, sizeof(T));
}

inline void* ptr_at(uint64_t addr) { return reinterpret_cast<void*>(static_cast<uintptr_t>(addr)); }
inline const void* cptr_at(uint64_t addr) { return reinterpret_cast<const void*>(static_cast<uintptr_t>(addr)); }

struct Decoded {
    uint8_t pp = 0;   // 0=none, 1=66, 2=F3, 3=F2
    uint8_t map = 1;  // 1=0F, 2=0F38, 3=0F3A
    bool has_vex = false;
    bool w = false;
    bool l = false;
    bool rex_r = false;
    bool rex_x = false;
    bool rex_b = false;
    bool has_66 = false;
    bool has_f2 = false;
    bool has_f3 = false;
    bool addr32 = false;
    uint8_t vvvv = 0;
    uint8_t opcode = 0;
    uint8_t modrm = 0;
    uint8_t mod = 0;
    uint8_t reg = 0;
    uint8_t rm = 0;
    bool has_sib = false;
    uint8_t sib = 0;
    uint8_t disp_bytes = 0;
    int32_t disp = 0;
    uint8_t insn_len = 0;
    uint8_t imm8 = 0;
};

// Computes effective address for memory operands (mod != 3). `next_rip` is state.rip + total_insn_len.
inline uint64_t effective_address(const CpuState& s, const Decoded& d, uint64_t next_rip) {
    if (d.mod == 0 && (d.rm & 7) == 5) {
        const uint64_t ea = next_rip + static_cast<int64_t>(d.disp);
        return d.addr32 ? (ea & 0xFFFFFFFFull) : ea;
    }
    uint64_t base = 0;
    uint64_t index = 0;
    if (d.has_sib) {
        const uint8_t b = (d.sib & 7) | (d.rex_b ? 8 : 0);
        const uint8_t idx = ((d.sib >> 3) & 7) | (d.rex_x ? 8 : 0);
        const uint8_t scale = (d.sib >> 6) & 3;
        if (!(d.mod == 0 && (d.sib & 7) == 5)) base = s.gpr[b];
        if (idx != 4) index = s.gpr[idx] << scale;
    } else {
        base = s.gpr[d.rm];
    }
    const uint64_t ea = base + index + static_cast<int64_t>(d.disp);
    return d.addr32 ? (ea & 0xFFFFFFFFull) : ea;
}

inline void read_rm_vec(const CpuState& s, const Decoded& d, uint64_t next_rip, uint8_t out[32], size_t bytes) {
    std::memset(out, 0, 32);
    if (d.mod == 3) {
        std::memcpy(out, s.ymm[d.rm], bytes);
    } else {
        const uint64_t ea = effective_address(s, d, next_rip);
        std::memcpy(out, cptr_at(ea), bytes);
    }
}

inline void write_vec(CpuState& s, uint8_t dst, const uint8_t val[32], size_t bytes, bool clear_hi) {
    std::memcpy(s.ymm[dst], val, bytes);
    if (clear_hi && bytes <= 16) std::memset(s.ymm[dst] + 16, 0, 16);
}

inline uint64_t read_rm_gpr(const CpuState& s, const Decoded& d, uint64_t next_rip, size_t width) {
    if (d.mod == 3) {
        const uint64_t v = s.gpr[d.rm];
        if (width == 1) return v & 0xFFu;
        if (width == 2) return v & 0xFFFFu;
        if (width == 4) return v & 0xFFFFFFFFull;
        return v;
    }
    const uint64_t ea = effective_address(s, d, next_rip);
    if (width == 1) return read_unaligned<uint8_t>(cptr_at(ea));
    if (width == 2) return read_unaligned<uint16_t>(cptr_at(ea));
    if (width == 4) return read_unaligned<uint32_t>(cptr_at(ea));
    return read_unaligned<uint64_t>(cptr_at(ea));
}

inline void write_gpr(CpuState& s, uint8_t r, uint64_t v, size_t width) {
    if (width == 1) s.gpr[r] = (s.gpr[r] & ~0xFFull) | (v & 0xFFull);
    else if (width == 2) s.gpr[r] = (s.gpr[r] & ~0xFFFFull) | (v & 0xFFFFull);
    else if (width == 4) s.gpr[r] = v & 0xFFFFFFFFull;
    else s.gpr[r] = v;
}

inline void write_rm_gpr(CpuState& s, const Decoded& d, uint64_t next_rip, uint64_t v, size_t width) {
    if (d.mod == 3) {
        write_gpr(s, d.rm, v, width);
        return;
    }
    const uint64_t ea = effective_address(s, d, next_rip);
    if (width == 1) write_unaligned<uint8_t>(ptr_at(ea), static_cast<uint8_t>(v));
    else if (width == 2) write_unaligned<uint16_t>(ptr_at(ea), static_cast<uint16_t>(v));
    else if (width == 4) write_unaligned<uint32_t>(ptr_at(ea), static_cast<uint32_t>(v));
    else write_unaligned<uint64_t>(ptr_at(ea), v);
}

inline void set_zf_cf_flags(CpuState& s, bool zf, bool cf, bool clear_rest = true) {
    constexpr uint64_t ZF = 1ull << 6;
    constexpr uint64_t CF = 1ull << 0;
    constexpr uint64_t ARITH = (1ull << 11) | (1ull << 7) | (1ull << 6) | (1ull << 4) | (1ull << 2) | (1ull << 0);
    if (clear_rest) s.rflags &= ~ARITH;
    else s.rflags &= ~(ZF | CF);
    if (zf) s.rflags |= ZF;
    if (cf) s.rflags |= CF;
}

inline bool float_cmp_pred(float a, float b, uint8_t pred) {
    const bool unord = std::isnan(a) || std::isnan(b);
    const bool lt = !unord && (a < b);
    const bool eq = !unord && (a == b);
    const bool gt = !unord && (a > b);
    switch (pred & 0x1F) {
        case 0x00: case 0x10: return eq;
        case 0x01: case 0x11: return lt;
        case 0x02: case 0x12: return lt || eq;
        case 0x03: case 0x13: return unord;
        case 0x04: case 0x14: return !eq;
        case 0x05: case 0x15: return !lt;
        case 0x06: case 0x16: return !(lt || eq);
        case 0x07: case 0x17: return !unord;
        case 0x08: case 0x18: return eq || unord;
        case 0x09: case 0x19: return lt || unord;
        case 0x0A: case 0x1A: return lt || eq || unord;
        case 0x0B: case 0x1B: return false;
        case 0x0C: case 0x1C: return !eq && !unord;
        case 0x0D: case 0x1D: return gt || eq;
        case 0x0E: case 0x1E: return gt;
        case 0x0F: case 0x1F: return true;
        default: return false;
    }
}

inline bool double_cmp_pred(double a, double b, uint8_t pred) {
    const bool unord = std::isnan(a) || std::isnan(b);
    const bool lt = !unord && (a < b);
    const bool eq = !unord && (a == b);
    const bool gt = !unord && (a > b);
    switch (pred & 0x1F) {
        case 0x00: case 0x10: return eq;
        case 0x01: case 0x11: return lt;
        case 0x02: case 0x12: return lt || eq;
        case 0x03: case 0x13: return unord;
        case 0x04: case 0x14: return !eq;
        case 0x05: case 0x15: return !lt;
        case 0x06: case 0x16: return !(lt || eq);
        case 0x07: case 0x17: return !unord;
        case 0x08: case 0x18: return eq || unord;
        case 0x09: case 0x19: return lt || unord;
        case 0x0A: case 0x1A: return lt || eq || unord;
        case 0x0B: case 0x1B: return false;
        case 0x0C: case 0x1C: return !eq && !unord;
        case 0x0D: case 0x1D: return gt || eq;
        case 0x0E: case 0x1E: return gt;
        case 0x0F: case 0x1F: return true;
        default: return false;
    }
}

inline float fp16_to_fp32(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    const uint32_t exp = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x3FFu;
    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            uint32_t m = mant;
            int e = -14;
            while ((m & 0x400u) == 0) {
                m <<= 1;
                --e;
            }
            m &= 0x3FFu;
            bits = sign | (uint32_t(e + 127) << 23) | (m << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant ? (0x400000u | (mant << 13)) : 0u);
    } else {
        bits = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, 4);
    return out;
}

inline uint16_t fp32_to_fp16(float f, uint8_t rc) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
    const uint32_t absb = bits & 0x7FFFFFFFu;
    if (absb > 0x7F800000u) return static_cast<uint16_t>(sign | 0x7E00u | ((absb >> 13) & 0x3FFu));
    if (absb == 0x7F800000u) return static_cast<uint16_t>(sign | 0x7C00u);
    const int exp = int((absb >> 23) & 0xFFu) - 127;
    uint32_t mant = absb & 0x7FFFFFu;
    if (exp > 15) {
        if ((rc == 1 && !sign) || (rc == 2 && sign) || rc == 3) return static_cast<uint16_t>(sign | 0x7BFFu);
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (exp < -24) return sign;
    uint32_t h_exp = 0;
    uint32_t h_mant = 0;
    uint32_t rem = 0;
    int shift = 13;
    if (exp >= -14) {
        h_exp = uint32_t(exp + 15);
        h_mant = mant >> 13;
        rem = mant & 0x1FFFu;
    } else {
        shift = 13 + (-14 - exp);
        const uint32_t full = 0x800000u | mant;
        h_mant = full >> shift;
        rem = full & ((1u << shift) - 1u);
    }
    const uint32_t half_rem = 1u << (shift - 1);
    bool round_up = false;
    if (rc == 0) round_up = (rem > half_rem) || (rem == half_rem && (h_mant & 1u));
    else if (rc == 1) round_up = (sign != 0) && (rem != 0);
    else if (rc == 2) round_up = (sign == 0) && (rem != 0);
    uint32_t res = (h_exp << 10) | h_mant;
    if (round_up) ++res;
    if (res >= 0x7C00u) res = 0x7C00u;
    return static_cast<uint16_t>(sign | res);
}

template <typename T>
inline T round_fp(T v, uint8_t imm, uint32_t mxcsr) {
    const uint8_t mode = (imm & 4) ? static_cast<uint8_t>((mxcsr >> 13) & 3) : static_cast<uint8_t>(imm & 3);
    switch (mode) {
        case 0: return std::nearbyint(v);
        case 1: return std::floor(v);
        case 2: return std::ceil(v);
        case 3: return std::trunc(v);
        default: return v;
    }
}

inline uint32_t crc32c_step(uint32_t crc, uint64_t val, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) {
        crc ^= static_cast<uint8_t>(val >> (i * 8));
        for (int b = 0; b < 8; ++b) crc = (crc & 1u) ? ((crc >> 1) ^ 0x82F63B78u) : (crc >> 1);
    }
    return crc;
}

inline bool parse_modrm(const uint8_t* code, Decoded& d, size_t off, size_t extra_imm) {
    const uint8_t m = code[off++];
    d.modrm = m;
    d.mod = (m >> 6) & 3;
    d.reg = ((m >> 3) & 7) | (d.rex_r ? 8 : 0);
    d.rm = (m & 7) | (d.rex_b ? 8 : 0);
    if (d.mod != 3 && (m & 7) == 4) {
        d.has_sib = true;
        d.sib = code[off++];
    }
    if (d.mod == 1) {
        d.disp_bytes = 1;
        d.disp = static_cast<int8_t>(code[off]);
        off += 1;
    } else if (d.mod == 2 || (d.mod == 0 && (m & 7) == 5) ||
               (d.mod == 0 && d.has_sib && (d.sib & 7) == 5)) {
        d.disp_bytes = 4;
        d.disp = read_unaligned<int32_t>(code + off);
        off += 4;
    }
    if (extra_imm) d.imm8 = code[off];
    d.insn_len = static_cast<uint8_t>(off + extra_imm);
    return true;
}

inline uint8_t pp_for_legacy(const Decoded& d) {
    if (d.has_66) return 1;
    if (d.has_f3) return 2;
    if (d.has_f2) return 3;
    return 0;
}

inline bool exec_bmi(CpuState& s, const Decoded& d, uint64_t next_rip) {
    const size_t width = d.w ? 8 : 4;
    const uint64_t mask = d.w ? ~0ull : 0xFFFFFFFFull;
    const uint8_t bits = d.w ? 64 : 32;
    if (d.map == 2 && d.pp == 0 && d.opcode == 0xF2) {  // ANDN
        const uint64_t src1 = s.gpr[d.vvvv] & mask;
        const uint64_t src2 = read_rm_gpr(s, d, next_rip, width) & mask;
        const uint64_t res = (~src1) & src2 & mask;
        write_gpr(s, d.reg, res, width);
        const uint64_t sign = 1ull << (bits - 1);
        s.rflags &= ~((1ull << 11) | (1ull << 7) | (1ull << 6) | (1ull << 0));
        if (res == 0) s.rflags |= (1ull << 6);
        if (res & sign) s.rflags |= (1ull << 7);
        return true;
    }
    if (d.map == 2 && d.pp == 0 && d.opcode == 0xF3) {  // BLSR (/1), BLSMSK (/2), BLSI (/3)
        const uint64_t src = read_rm_gpr(s, d, next_rip, width) & mask;
        uint64_t res = 0;
        bool cf = false;
        const uint8_t sub = (d.modrm >> 3) & 7;
        if (sub == 1) { res = src & (src - 1); cf = (src == 0); }
        else if (sub == 2) { res = src ^ (src - 1); cf = (src == 0); }
        else if (sub == 3) { res = src & (~src + 1); cf = (src != 0); }
        else return false;
        res &= mask;
        write_gpr(s, d.vvvv, res, width);
        s.rflags &= ~((1ull << 11) | (1ull << 7) | (1ull << 6) | (1ull << 0));
        if (res == 0) s.rflags |= (1ull << 6);
        if (res & (1ull << (bits - 1))) s.rflags |= (1ull << 7);
        if (cf) s.rflags |= (1ull << 0);
        return true;
    }
    if (d.map == 2 && d.pp == 0 && d.opcode == 0xF5) {  // BZHI
        const uint64_t src = read_rm_gpr(s, d, next_rip, width) & mask;
        const uint8_t idx = static_cast<uint8_t>(s.gpr[d.vvvv] & 0xFFu);
        const uint64_t res = (idx >= bits) ? src : (src & ((1ull << idx) - 1ull));
        write_gpr(s, d.reg, res, width);
        s.rflags &= ~((1ull << 11) | (1ull << 7) | (1ull << 6) | (1ull << 0));
        if (res == 0) s.rflags |= (1ull << 6);
        if (res & (1ull << (bits - 1))) s.rflags |= (1ull << 7);
        if (idx >= bits) s.rflags |= (1ull << 0);
        return true;
    }
    if (d.map == 2 && (d.pp == 2 || d.pp == 3) && d.opcode == 0xF5) {  // PEXT (F3), PDEP (F2)
        const uint64_t src1 = s.gpr[d.vvvv] & mask;
        const uint64_t src2 = read_rm_gpr(s, d, next_rip, width) & mask;
        uint64_t res = 0;
        if (d.pp == 2) {
            uint64_t out_bit = 1;
            for (int i = 0; i < bits; ++i) {
                if ((src2 >> i) & 1ull) {
                    if ((src1 >> i) & 1ull) res |= out_bit;
                    out_bit <<= 1;
                }
            }
        } else {
            uint64_t in_bit = 1;
            for (int i = 0; i < bits; ++i) {
                if ((src2 >> i) & 1ull) {
                    if (src1 & in_bit) res |= (1ull << i);
                    in_bit <<= 1;
                }
            }
        }
        write_gpr(s, d.reg, res, width);
        return true;
    }
    if (d.map == 2 && d.pp == 3 && d.opcode == 0xF6) {  // MULX
        const uint64_t a = s.gpr[2] & mask;
        const uint64_t b = read_rm_gpr(s, d, next_rip, width) & mask;
        if (d.w) {
            const uint64_t a_lo = static_cast<uint32_t>(a), a_hi = a >> 32;
            const uint64_t b_lo = static_cast<uint32_t>(b), b_hi = b >> 32;
            const uint64_t p0 = a_lo * b_lo;
            const uint64_t p1 = a_lo * b_hi;
            const uint64_t p2 = a_hi * b_lo;
            const uint64_t p3 = a_hi * b_hi;
            const uint64_t mid = (p0 >> 32) + static_cast<uint32_t>(p1) + static_cast<uint32_t>(p2);
            const uint64_t lo = (p0 & 0xFFFFFFFFull) | (mid << 32);
            const uint64_t hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
            write_gpr(s, d.vvvv, lo, 8);
            write_gpr(s, d.reg, hi, 8);
        } else {
            const uint64_t prod = a * b;
            write_gpr(s, d.vvvv, prod & 0xFFFFFFFFull, 4);
            write_gpr(s, d.reg, prod >> 32, 4);
        }
        return true;
    }
    if (d.map == 2 && d.opcode == 0xF7) {
        const uint64_t src = read_rm_gpr(s, d, next_rip, width) & mask;
        if (d.pp == 0) {  // BEXTR
            const uint64_t ctrl = s.gpr[d.vvvv];
            const uint8_t start = static_cast<uint8_t>(ctrl & 0xFFu);
            const uint8_t len = static_cast<uint8_t>((ctrl >> 8) & 0xFFu);
            uint64_t res = 0;
            if (start < bits && len > 0) {
                const uint64_t shifted = src >> start;
                res = (len >= bits) ? shifted : (shifted & ((1ull << len) - 1ull));
            }
            write_gpr(s, d.reg, res, width);
            s.rflags &= ~((1ull << 11) | (1ull << 6) | (1ull << 0));
            if (res == 0) s.rflags |= (1ull << 6);
            return true;
        }
        const uint8_t cnt = static_cast<uint8_t>(s.gpr[d.vvvv] & (d.w ? 63u : 31u));
        uint64_t res = 0;
        if (d.pp == 1) res = (src << cnt) & mask;                                            // SHLX
        else if (d.pp == 2) {                                                                 // SARX
            if (d.w) res = static_cast<uint64_t>(static_cast<int64_t>(src) >> cnt);
            else res = static_cast<uint32_t>(static_cast<int32_t>(uint32_t(src)) >> cnt);
        } else if (d.pp == 3) res = (src >> cnt) & mask;                                      // SHRX
        write_gpr(s, d.reg, res, width);
        return true;
    }
    if (d.map == 3 && d.pp == 3 && d.opcode == 0xF0) {  // RORX
        const uint64_t src = read_rm_gpr(s, d, next_rip, width) & mask;
        const uint8_t cnt = d.imm8 & (d.w ? 63u : 31u);
        const uint64_t res = d.w ? std::rotr(src, cnt) : std::rotr(static_cast<uint32_t>(src), cnt);
        write_gpr(s, d.reg, res, width);
        return true;
    }
    return false;
}

inline bool exec_scalar_legacy(CpuState& s, const Decoded& d, uint64_t next_rip) {
    const size_t width = d.w ? 8 : (d.has_66 ? 2 : 4);
    const uint64_t mask = (width == 8) ? ~0ull : ((1ull << (width * 8)) - 1ull);
    const int bits = static_cast<int>(width * 8);
    if (d.map == 1 && d.has_f3 && d.opcode == 0xB8) {  // POPCNT
        const uint64_t src = read_rm_gpr(s, d, next_rip, width) & mask;
        const int c = std::popcount(src);
        write_gpr(s, d.reg, static_cast<uint64_t>(c), width);
        set_zf_cf_flags(s, c == 0, false, true);
        return true;
    }
    if (d.map == 1 && d.has_f3 && (d.opcode == 0xBC || d.opcode == 0xBD)) {  // TZCNT / LZCNT
        const uint64_t src = read_rm_gpr(s, d, next_rip, width) & mask;
        int c = bits;
        if (src != 0) {
            if (d.opcode == 0xBC) c = std::countr_zero(src);
            else c = std::countl_zero(src) - (64 - bits);
        }
        write_gpr(s, d.reg, static_cast<uint64_t>(c), width);
        set_zf_cf_flags(s, c == 0, src == 0, false);
        return true;
    }
    if (d.map == 2 && !d.has_f2 && (d.opcode == 0xF0 || d.opcode == 0xF1)) {  // MOVBE
        if (d.mod == 3) return false;
        auto swap = [&](uint64_t v) -> uint64_t {
            if (width == 2) return bswap16(static_cast<uint16_t>(v));
            if (width == 4) return bswap32(static_cast<uint32_t>(v));
            return bswap64(v);
        };
        if (d.opcode == 0xF0) write_gpr(s, d.reg, swap(read_rm_gpr(s, d, next_rip, width)), width);
        else write_rm_gpr(s, d, next_rip, swap(s.gpr[d.reg]), width);
        return true;
    }
    if (d.map == 2 && d.has_f2 && (d.opcode == 0xF0 || d.opcode == 0xF1)) {  // CRC32
        const size_t sw = (d.opcode == 0xF0) ? 1 : width;
        const uint64_t src = read_rm_gpr(s, d, next_rip, sw);
        const uint32_t crc = crc32c_step(static_cast<uint32_t>(s.gpr[d.reg]), src, sw);
        write_gpr(s, d.reg, crc, 4);
        return true;
    }
    return false;
}

template <typename T>
inline T read_lane(const uint8_t buf[32], size_t i) {
    T v{};
    std::memcpy(&v, buf + i * sizeof(T), sizeof(T));
    return v;
}

template <typename T>
inline void write_lane(uint8_t buf[32], size_t i, T v) {
    std::memcpy(buf + i * sizeof(T), &v, sizeof(T));
}

inline bool exec_fma(CpuState& s, const Decoded& d, const uint8_t a_raw[32], const uint8_t b_raw[32],
                     const uint8_t c_raw[32], uint8_t out[32], size_t bytes) {
    const uint8_t op = d.opcode;
    const bool scalar = (op >= 0x99 && (op & 1)) || (op >= 0xA9 && (op & 1)) || (op >= 0xB9 && (op & 1));
    const int family = ((op & 0x0F) >= 8) ? (((op >> 4) - 9) * 2 + ((op & 0x0F) >= 0x0C ? 1 : 0))
                                          : (((op >> 4) - 9) * 2 + 1);
    (void)family;
    // Forms: 96..9F = 132, A6..AF = 213, B6..BF = 231.
    const uint8_t low = op & 0x0F;
    const int form = (op >> 4) - 9;  // 0=132, 1=213, 2=231
    const bool is_addsub = (low == 6 || low == 7);
    const bool sub_add = (low == 7);
    const bool neg_prod = (low == 0xC || low == 0xD || low == 0xE || low == 0xF);
    const bool neg_add = (low == 0xA || low == 0xB || low == 0xE || low == 0xF);
    const bool sc = (low & 1) && !is_addsub;

    auto pick_abc = [&](auto& x, auto& y, auto& z, const auto& v1, const auto& v2, const auto& v3) {
        if (form == 0) { x = v1; y = v3; z = v2; }       // 132: x*z +- y
        else if (form == 1) { x = v2; y = v1; z = v3; }  // 213: x*z +- y -> v2*v1 +- v3 (a*b + c)
        else { x = v2; y = v3; z = v1; }                 // 231: v2*v3 +- v1
    };

    if (d.w) {
        const size_t lanes = sc ? 1 : (bytes / 8);
        if (sc) std::memcpy(out, a_raw, 16);
        for (size_t i = 0; i < lanes; ++i) {
            const double va = read_lane<double>(a_raw, i), vb = read_lane<double>(b_raw, i),
                         vc = read_lane<double>(c_raw, i);
            double x = 0, y = 0, z = 0;
            pick_abc(x, y, z, va, vb, vc);
            double prod = neg_prod ? -x : x;
            bool sub = neg_add;
            if (is_addsub) sub = ((i & 1) == 0) ? !sub_add : sub_add;
            const double add = sub ? -z : z;
            write_lane<double>(out, i, std::fma(prod, y, add));
        }
    } else {
        const size_t lanes = sc ? 1 : (bytes / 4);
        if (sc) std::memcpy(out, a_raw, 16);
        for (size_t i = 0; i < lanes; ++i) {
            const float va = read_lane<float>(a_raw, i), vb = read_lane<float>(b_raw, i),
                        vc = read_lane<float>(c_raw, i);
            float x = 0, y = 0, z = 0;
            pick_abc(x, y, z, va, vb, vc);
            float prod = neg_prod ? -x : x;
            bool sub = neg_add;
            if (is_addsub) sub = ((i & 1) == 0) ? !sub_add : sub_add;
            const float add = sub ? -z : z;
            write_lane<float>(out, i, std::fma(prod, y, add));
        }
    }
    (void)scalar;
    (void)s;
    return true;
}

inline bool exec_simd(CpuState& s, const Decoded& d, uint64_t next_rip) {
    const size_t bytes = (d.has_vex && d.l) ? 32 : 16;
    const bool vex = d.has_vex;
    uint8_t a[32]{}, b[32]{}, c[32]{}, out[32]{};
    std::memcpy(a, s.ymm[d.reg], 32);
    std::memcpy(b, s.ymm[d.vvvv], 32);

    // 256-bit / 128-bit VEX/SSE instructions.
    if (d.map == 1) {
        if (vex && d.opcode == 0x77) {  // VZEROUPPER / VZEROALL
            for (int i = 0; i < 16; ++i) {
                if (d.l) std::memset(s.ymm[i], 0, 32);
                else std::memset(s.ymm[i] + 16, 0, 16);
            }
            return true;
        }
        // Pure stores (opcode reg -> rm)
        if (d.opcode == 0x11 || d.opcode == 0x29 || d.opcode == 0x7F || d.opcode == 0xE7 || d.opcode == 0x2B) {
            if (d.opcode == 0x11 && (d.pp == 2 || d.pp == 3)) {
                const size_t w = (d.pp == 2) ? 4 : 8;
                if (d.mod == 3) {
                    std::memcpy(out, s.ymm[d.rm], 16);
                    std::memcpy(out, a, w);
                    write_vec(s, d.rm, out, 16, vex);
                } else {
                    std::memcpy(ptr_at(effective_address(s, d, next_rip)), a, w);
                }
                return true;
            }
            if (d.mod == 3) write_vec(s, d.rm, a, bytes, vex);
            else std::memcpy(ptr_at(effective_address(s, d, next_rip)), a, bytes);
            return true;
        }
        if (d.opcode == 0x13 || d.opcode == 0x17) {  // vmovlps/lpd / vmovhps/hpd m64, xmm
            if (d.mod == 3) return false;
            const size_t src_off = (d.opcode == 0x17) ? 8 : 0;
            std::memcpy(ptr_at(effective_address(s, d, next_rip)), a + src_off, 8);
            return true;
        }
        if (d.opcode == 0x7E && (d.pp == 1)) {  // vmovd/vmovq r/m, xmm
            const size_t w = d.w ? 8 : 4;
            write_rm_gpr(s, d, next_rip, read_lane<uint64_t>(a, 0), w);
            return true;
        }
        if (d.opcode == 0x6E) {  // vmovd/vmovq xmm, r/m
            const size_t w = d.w ? 8 : 4;
            const uint64_t val = read_rm_gpr(s, d, next_rip, w);
            std::memcpy(out, &val, w);
            write_vec(s, d.reg, out, 16, vex);
            return true;
        }
        if (d.opcode == 0x7E && d.pp == 2) {  // vmovq xmm, xmm/m64
            read_rm_vec(s, d, next_rip, c, 8);
            std::memcpy(out, c, 8);
            write_vec(s, d.reg, out, 16, vex);
            return true;
        }
        if (d.opcode == 0xD6 && d.pp == 1) {  // vmovq xmm/m64, xmm
            if (d.mod == 3) write_vec(s, d.rm, a, 8, vex);
            else std::memcpy(ptr_at(effective_address(s, d, next_rip)), a, 8);
            return true;
        }
        if (d.opcode == 0x50) {  // vmovmskps / vmovmskpd
            uint32_t msk = 0;
            if (d.pp == 0) {
                for (size_t i = 0; i < bytes / 4; ++i)
                    if (read_lane<uint32_t>(s.ymm[d.rm], i) & 0x80000000u) msk |= (1u << i);
            } else if (d.pp == 1) {
                for (size_t i = 0; i < bytes / 8; ++i)
                    if (read_lane<uint64_t>(s.ymm[d.rm], i) & 0x8000000000000000ull) msk |= (1u << i);
            } else return false;
            write_gpr(s, d.reg, msk, 4);
            return true;
        }
        if (d.opcode == 0xD7 && d.pp == 1) {  // vpmovmskb
            uint32_t msk = 0;
            for (size_t i = 0; i < bytes; ++i)
                if (s.ymm[d.rm][i] & 0x80u) msk |= (1u << i);
            write_gpr(s, d.reg, msk, 4);
            return true;
        }
        if (d.opcode == 0x2E || d.opcode == 0x2F) {  // vcomiss/d, vucomiss/d
            bool unord = false, lt = false, eq = false;
            if (d.pp == 0) {
                read_rm_vec(s, d, next_rip, c, 4);
                const float x = read_lane<float>(a, 0), y = read_lane<float>(c, 0);
                unord = std::isnan(x) || std::isnan(y);
                lt = !unord && (x < y);
                eq = !unord && (x == y);
            } else {
                read_rm_vec(s, d, next_rip, c, 8);
                const double x = read_lane<double>(a, 0), y = read_lane<double>(c, 0);
                unord = std::isnan(x) || std::isnan(y);
                lt = !unord && (x < y);
                eq = !unord && (x == y);
            }
            constexpr uint64_t PF = 1ull << 2;
            s.rflags &= ~((1ull << 11) | (1ull << 7) | (1ull << 6) | (1ull << 4) | PF | 1ull);
            if (unord) s.rflags |= ((1ull << 6) | PF | 1ull);
            else if (lt) s.rflags |= 1ull;
            else if (eq) s.rflags |= (1ull << 6);
            return true;
        }
        if (d.opcode == 0x2C || d.opcode == 0x2D) {  // vcvt{t}ss2si / vcvt{t}sd2si
            const bool trunc_m = (d.opcode == 0x2C);
            const size_t w = d.w ? 8 : 4;
            int64_t iv = 0;
            if (d.pp == 2) {
                read_rm_vec(s, d, next_rip, c, 4);
                const float v = read_lane<float>(c, 0);
                iv = static_cast<int64_t>(trunc_m ? std::trunc(v) : std::nearbyint(v));
            } else {
                read_rm_vec(s, d, next_rip, c, 8);
                const double v = read_lane<double>(c, 0);
                iv = static_cast<int64_t>(trunc_m ? std::trunc(v) : std::nearbyint(v));
            }
            write_gpr(s, d.reg, static_cast<uint64_t>(iv), w);
            return true;
        }
        if (d.opcode == 0x2A) {  // vcvtsi2ss / vcvtsi2sd
            const size_t w = d.w ? 8 : 4;
            const uint64_t raw = read_rm_gpr(s, d, next_rip, w);
            const int64_t sv = d.w ? static_cast<int64_t>(raw) : static_cast<int32_t>(raw);
            std::memcpy(out, b, 16);
            if (d.pp == 2) write_lane<float>(out, 0, static_cast<float>(sv));
            else if (d.pp == 3) write_lane<double>(out, 0, static_cast<double>(sv));
            else return false;
            write_vec(s, d.reg, out, 16, vex);
            return true;
        }

        // Load src into c (bytes or scalar).
        if (d.opcode == 0x10 && (d.pp == 2 || d.pp == 3)) {
            const size_t w = (d.pp == 2) ? 4 : 8;
            read_rm_vec(s, d, next_rip, c, w);
            if (d.mod == 3) std::memcpy(out, b, 16);
            std::memcpy(out, c, w);
            write_vec(s, d.reg, out, 16, vex);
            return true;
        }
        if (d.opcode == 0x10 || d.opcode == 0x28 || d.opcode == 0x6F || d.opcode == 0xF0) {
            read_rm_vec(s, d, next_rip, c, bytes);
            write_vec(s, d.reg, c, bytes, vex);
            return true;
        }
        if (d.opcode == 0x12) {
            if (d.pp == 0 && d.mod == 3) {  // vmovhlps
                std::memcpy(out, s.ymm[d.rm] + 8, 8);
                std::memcpy(out + 8, b + 8, 8);
            } else if (d.pp == 0 || d.pp == 1) {  // vmovlps / vmovlpd m64
                read_rm_vec(s, d, next_rip, c, 8);
                std::memcpy(out, c, 8);
                std::memcpy(out + 8, b + 8, 8);
            } else if (d.pp == 2) {  // vmovsldup
                read_rm_vec(s, d, next_rip, c, bytes);
                for (size_t i = 0; i < bytes / 4; i += 2) {
                    write_lane<uint32_t>(out, i, read_lane<uint32_t>(c, i));
                    write_lane<uint32_t>(out, i + 1, read_lane<uint32_t>(c, i));
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            } else if (d.pp == 3) {  // vmovddup
                read_rm_vec(s, d, next_rip, c, bytes == 32 ? 32 : 8);
                for (size_t i = 0; i < bytes / 8; i += 2) {
                    write_lane<uint64_t>(out, i, read_lane<uint64_t>(c, i));
                    write_lane<uint64_t>(out, i + 1, read_lane<uint64_t>(c, i));
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            write_vec(s, d.reg, out, 16, vex);
            return true;
        }
        if (d.opcode == 0x16) {
            if (d.pp == 0 && d.mod == 3) {  // vmovlhps
                std::memcpy(out, b, 8);
                std::memcpy(out + 8, s.ymm[d.rm], 8);
                write_vec(s, d.reg, out, 16, vex);
                return true;
            } else if (d.pp == 0 || d.pp == 1) {  // vmovhps / vmovhpd m64
                read_rm_vec(s, d, next_rip, c, 8);
                std::memcpy(out, b, 8);
                std::memcpy(out + 8, c, 8);
                write_vec(s, d.reg, out, 16, vex);
                return true;
            } else if (d.pp == 2) {  // vmovshdup
                read_rm_vec(s, d, next_rip, c, bytes);
                for (size_t i = 0; i < bytes / 4; i += 2) {
                    write_lane<uint32_t>(out, i, read_lane<uint32_t>(c, i + 1));
                    write_lane<uint32_t>(out, i + 1, read_lane<uint32_t>(c, i + 1));
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            return false;
        }

        read_rm_vec(s, d, next_rip, c, bytes);

        // Shift immediate forms (0x71 / 0x72 / 0x73)
        if (d.opcode == 0x71 || d.opcode == 0x72 || d.opcode == 0x73) {
            const uint8_t sub = (d.modrm >> 3) & 7;
            const uint8_t dst = vex ? d.vvvv : d.reg;
            const uint8_t* src_v = s.ymm[d.rm];
            if (d.opcode == 0x73 && (sub == 3 || sub == 7)) {  // psrldq / pslldq
                const size_t sh = (std::min<size_t>)(d.imm8, 16);
                for (size_t lane = 0; lane < bytes; lane += 16) {
                    if (sub == 3) std::memcpy(out + lane, src_v + lane + sh, 16 - sh);
                    else std::memcpy(out + lane + sh, src_v + lane, 16 - sh);
                }
                write_vec(s, dst, out, bytes, vex);
                return true;
            }
            const size_t ew = (d.opcode == 0x71) ? 2 : (d.opcode == 0x72) ? 4 : 8;
            const uint8_t sh = d.imm8;
            for (size_t i = 0; i < bytes / ew; ++i) {
                if (ew == 2) {
                    uint16_t v = read_lane<uint16_t>(src_v, i);
                    uint16_t r = (sh >= 16) ? (sub == 4 ? uint16_t(int16_t(v) >> 15) : uint16_t(0))
                                            : (sub == 2 ? uint16_t(v >> sh)
                                               : sub == 4 ? uint16_t(int16_t(v) >> sh)
                                                          : uint16_t(v << sh));
                    write_lane<uint16_t>(out, i, r);
                } else if (ew == 4) {
                    uint32_t v = read_lane<uint32_t>(src_v, i);
                    uint32_t r = (sh >= 32) ? (sub == 4 ? uint32_t(int32_t(v) >> 31) : 0u)
                                            : (sub == 2 ? (v >> sh)
                                               : sub == 4 ? uint32_t(int32_t(v) >> sh)
                                                          : (v << sh));
                    write_lane<uint32_t>(out, i, r);
                } else {
                    uint64_t v = read_lane<uint64_t>(src_v, i);
                    uint64_t r = (sh >= 64) ? 0ull : (sub == 2 ? (v >> sh) : (v << sh));
                    write_lane<uint64_t>(out, i, r);
                }
            }
            write_vec(s, dst, out, bytes, vex);
            return true;
        }

        // FP / Int arithmetic & bitwise
        const uint8_t* lhs = vex ? b : a;
        switch (d.opcode) {
            case 0x54: case 0xDB:  // vandps/pd, vpand
                for (size_t i = 0; i < bytes; ++i) out[i] = lhs[i] & c[i];
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            case 0x55: case 0xDF:  // vandnps/pd, vpandn
                for (size_t i = 0; i < bytes; ++i) out[i] = (~lhs[i]) & c[i];
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            case 0x56: case 0xEB:  // vorps/pd, vpor
                for (size_t i = 0; i < bytes; ++i) out[i] = lhs[i] | c[i];
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            case 0x57: case 0xEF:  // vxorps/pd, vpxor
                for (size_t i = 0; i < bytes; ++i) out[i] = lhs[i] ^ c[i];
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            case 0x58: case 0x59: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
                const bool scalar = (d.pp == 2 || d.pp == 3);
                const bool dbl = (d.pp == 1 || d.pp == 3);
                if (scalar) std::memcpy(out, lhs, 16);
                auto fop32 = [&](float x, float y) -> float {
                    if (d.opcode == 0x58) return x + y;
                    if (d.opcode == 0x59) return x * y;
                    if (d.opcode == 0x5C) return x - y;
                    if (d.opcode == 0x5E) return x / y;
                    if (d.opcode == 0x5D) return (x < y) ? x : y;
                    return (x > y) ? x : y;
                };
                auto fop64 = [&](double x, double y) -> double {
                    if (d.opcode == 0x58) return x + y;
                    if (d.opcode == 0x59) return x * y;
                    if (d.opcode == 0x5C) return x - y;
                    if (d.opcode == 0x5E) return x / y;
                    if (d.opcode == 0x5D) return (x < y) ? x : y;
                    return (x > y) ? x : y;
                };
                if (dbl) {
                    const size_t n = scalar ? 1 : (bytes / 8);
                    for (size_t i = 0; i < n; ++i)
                        write_lane<double>(out, i, fop64(read_lane<double>(lhs, i), read_lane<double>(c, i)));
                } else {
                    const size_t n = scalar ? 1 : (bytes / 4);
                    for (size_t i = 0; i < n; ++i)
                        write_lane<float>(out, i, fop32(read_lane<float>(lhs, i), read_lane<float>(c, i)));
                }
                write_vec(s, d.reg, out, scalar ? 16 : bytes, vex);
                return true;
            }
            case 0xD0: {  // vaddsubps/pd
                if (d.pp == 3) {
                    for (size_t i = 0; i < bytes / 4; ++i) {
                        const float x = read_lane<float>(lhs, i), y = read_lane<float>(c, i);
                        write_lane<float>(out, i, (i & 1) ? (x + y) : (x - y));
                    }
                } else {
                    for (size_t i = 0; i < bytes / 8; ++i) {
                        const double x = read_lane<double>(lhs, i), y = read_lane<double>(c, i);
                        write_lane<double>(out, i, (i & 1) ? (x + y) : (x - y));
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0x51: case 0x52: case 0x53: {  // sqrt, rsqrt, rcp
                const bool scalar = (d.pp == 2 || d.pp == 3);
                const bool dbl = (d.pp == 1 || d.pp == 3);
                if (scalar) std::memcpy(out, lhs, 16);
                const size_t n = scalar ? 1 : (dbl ? bytes / 8 : bytes / 4);
                for (size_t i = 0; i < n; ++i) {
                    if (dbl) {
                        write_lane<double>(out, i, std::sqrt(read_lane<double>(c, i)));
                    } else {
                        const float v = read_lane<float>(c, i);
                        float r = (d.opcode == 0x51) ? std::sqrt(v)
                                  : (d.opcode == 0x52) ? (1.0f / std::sqrt(v))
                                                       : (1.0f / v);
                        write_lane<float>(out, i, r);
                    }
                }
                write_vec(s, d.reg, out, scalar ? 16 : bytes, vex);
                return true;
            }
            case 0x5A: {
                if (d.pp == 0) {  // vcvtps2pd
                    const size_t n = bytes / 8;
                    for (size_t i = 0; i < n; ++i) write_lane<double>(out, i, double(read_lane<float>(c, i)));
                    write_vec(s, d.reg, out, bytes, vex);
                    return true;
                }
                if (d.pp == 1) {  // vcvtpd2ps
                    const size_t n = bytes / 8;
                    for (size_t i = 0; i < n; ++i) write_lane<float>(out, i, float(read_lane<double>(c, i)));
                    write_vec(s, d.reg, out, 16, vex);
                    return true;
                }
                std::memcpy(out, lhs, 16);
                if (d.pp == 2) write_lane<double>(out, 0, double(read_lane<float>(c, 0)));
                else write_lane<float>(out, 0, float(read_lane<double>(c, 0)));
                write_vec(s, d.reg, out, 16, vex);
                return true;
            }
            case 0x5B: {  // vcvtdq2ps / vcvtps2dq / vcvttps2dq
                for (size_t i = 0; i < bytes / 4; ++i) {
                    if (d.pp == 0) write_lane<float>(out, i, float(read_lane<int32_t>(c, i)));
                    else if (d.pp == 1) write_lane<int32_t>(out, i, int32_t(std::nearbyint(read_lane<float>(c, i))));
                    else write_lane<int32_t>(out, i, int32_t(std::trunc(read_lane<float>(c, i))));
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0xE6: {  // vcvtdq2pd / vcvtpd2dq / vcvttpd2dq
                const size_t n = bytes / 8;
                if (d.pp == 2) {
                    for (size_t i = 0; i < n; ++i) write_lane<double>(out, i, double(read_lane<int32_t>(c, i)));
                    write_vec(s, d.reg, out, bytes, vex);
                    return true;
                }
                for (size_t i = 0; i < n; ++i) {
                    const double v = read_lane<double>(c, i);
                    write_lane<int32_t>(out, i, int32_t(d.pp == 3 ? std::nearbyint(v) : std::trunc(v)));
                }
                write_vec(s, d.reg, out, 16, vex);
                return true;
            }
            case 0xC2: {  // vcmpps/pd/ss/sd
                const bool scalar = (d.pp == 2 || d.pp == 3);
                const bool dbl = (d.pp == 1 || d.pp == 3);
                if (scalar) std::memcpy(out, lhs, 16);
                const size_t n = scalar ? 1 : (dbl ? bytes / 8 : bytes / 4);
                for (size_t i = 0; i < n; ++i) {
                    if (dbl) write_lane<uint64_t>(out, i, double_cmp_pred(read_lane<double>(lhs, i), read_lane<double>(c, i), d.imm8) ? ~0ull : 0ull);
                    else write_lane<uint32_t>(out, i, float_cmp_pred(read_lane<float>(lhs, i), read_lane<float>(c, i), d.imm8) ? ~0u : 0u);
                }
                write_vec(s, d.reg, out, scalar ? 16 : bytes, vex);
                return true;
            }
            case 0x14: case 0x15: {  // vunpcklps/pd, vunpckhps/pd
                const bool hi = (d.opcode == 0x15);
                for (size_t lane = 0; lane < bytes; lane += 16) {
                    if (d.pp == 0) {
                        const size_t base = (lane / 4) + (hi ? 2 : 0);
                        write_lane<uint32_t>(out, lane / 4 + 0, read_lane<uint32_t>(lhs, base + 0));
                        write_lane<uint32_t>(out, lane / 4 + 1, read_lane<uint32_t>(c, base + 0));
                        write_lane<uint32_t>(out, lane / 4 + 2, read_lane<uint32_t>(lhs, base + 1));
                        write_lane<uint32_t>(out, lane / 4 + 3, read_lane<uint32_t>(c, base + 1));
                    } else {
                        const size_t base = (lane / 8) + (hi ? 1 : 0);
                        write_lane<uint64_t>(out, lane / 8 + 0, read_lane<uint64_t>(lhs, base));
                        write_lane<uint64_t>(out, lane / 8 + 1, read_lane<uint64_t>(c, base));
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0xC6: {  // vshufps / vshufpd
                for (size_t lane = 0; lane < bytes; lane += 16) {
                    if (d.pp == 0) {
                        const size_t base = lane / 4;
                        write_lane<uint32_t>(out, base + 0, read_lane<uint32_t>(lhs, base + ((d.imm8 >> 0) & 3)));
                        write_lane<uint32_t>(out, base + 1, read_lane<uint32_t>(lhs, base + ((d.imm8 >> 2) & 3)));
                        write_lane<uint32_t>(out, base + 2, read_lane<uint32_t>(c, base + ((d.imm8 >> 4) & 3)));
                        write_lane<uint32_t>(out, base + 3, read_lane<uint32_t>(c, base + ((d.imm8 >> 6) & 3)));
                    } else {
                        const size_t base = lane / 8;
                        const int sh = int(lane / 8);
                        write_lane<uint64_t>(out, base + 0, read_lane<uint64_t>(lhs, base + ((d.imm8 >> (0 + sh)) & 1)));
                        write_lane<uint64_t>(out, base + 1, read_lane<uint64_t>(c, base + ((d.imm8 >> (1 + sh)) & 1)));
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0x70: {  // vpshufd / vpshufhw / vpshuflw
                for (size_t lane = 0; lane < bytes; lane += 16) {
                    if (d.pp == 1) {
                        const size_t base = lane / 4;
                        for (size_t k = 0; k < 4; ++k)
                            write_lane<uint32_t>(out, base + k, read_lane<uint32_t>(c, base + ((d.imm8 >> (2 * k)) & 3)));
                    } else if (d.pp == 2 || d.pp == 3) {
                        const size_t base = lane / 2;
                        const bool hi = (d.pp == 2);
                        for (size_t k = 0; k < 8; ++k) {
                            const bool sel = hi ? (k >= 4) : (k < 4);
                            const size_t idx = sel ? (hi ? 4 : 0) + ((d.imm8 >> (2 * (k & 3))) & 3) : k;
                            write_lane<uint16_t>(out, base + k, read_lane<uint16_t>(c, base + idx));
                        }
                    } else {
                        return false;
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0x60: case 0x61: case 0x62: case 0x6C:
            case 0x68: case 0x69: case 0x6A: case 0x6D: {  // vpunpck*
                const bool hi = (d.opcode >= 0x68 && d.opcode != 0x6C);
                const size_t ew = (d.opcode == 0x60 || d.opcode == 0x68) ? 1
                                  : (d.opcode == 0x61 || d.opcode == 0x69) ? 2
                                  : (d.opcode == 0x62 || d.opcode == 0x6A) ? 4
                                                                           : 8;
                const size_t per_lane = 16 / ew;
                const size_t half = per_lane / 2;
                for (size_t lane = 0; lane < bytes; lane += 16) {
                    const size_t base = lane / ew;
                    for (size_t k = 0; k < half; ++k) {
                        const size_t si = base + (hi ? half : 0) + k;
                        std::memcpy(out + (base + 2 * k) * ew, lhs + si * ew, ew);
                        std::memcpy(out + (base + 2 * k + 1) * ew, c + si * ew, ew);
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0x63: case 0x67: case 0x6B: {  // packsswb, packuswb, packssdw
                const bool to8 = (d.opcode != 0x6B);
                const bool us = (d.opcode == 0x67);
                for (size_t lane = 0; lane < bytes; lane += 16) {
                    if (to8) {
                        const size_t b_w = lane / 2, b_b = lane;
                        for (size_t k = 0; k < 16; ++k) {
                            const int16_t v = read_lane<int16_t>(k < 8 ? lhs : c, b_w + (k & 7));
                            if (us) out[b_b + k] = static_cast<uint8_t>(std::clamp<int>(v, 0, 255));
                            else out[b_b + k] = static_cast<uint8_t>(static_cast<int8_t>(std::clamp<int>(v, -128, 127)));
                        }
                    } else {
                        const size_t b_d = lane / 4, b_w = lane / 2;
                        for (size_t k = 0; k < 8; ++k) {
                            const int32_t v = read_lane<int32_t>(k < 4 ? lhs : c, b_d + (k & 3));
                            write_lane<int16_t>(out, b_w + k, static_cast<int16_t>(std::clamp<int>(v, -32768, 32767)));
                        }
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0xC4: {  // vpinsrw
                std::memcpy(out, b, 16);
                const uint16_t v = static_cast<uint16_t>(read_rm_gpr(s, d, next_rip, 2));
                write_lane<uint16_t>(out, d.imm8 & 7, v);
                write_vec(s, d.reg, out, 16, vex);
                return true;
            }
            case 0xC5: {  // vpextrw
                const uint16_t v = read_lane<uint16_t>(s.ymm[d.rm], d.imm8 & 7);
                write_gpr(s, d.reg, v, 4);
                return true;
            }
            case 0x64: case 0x65: case 0x66: case 0x74: case 0x75: case 0x76: {  // vpcmpgt/eq b/w/d
                const bool eq = (d.opcode >= 0x74);
                const int type = d.opcode & 3;  // 0=b, 1=w, 2=d
                if (type == 0) {
                    for (size_t i = 0; i < bytes; ++i) {
                        const int8_t x = static_cast<int8_t>(lhs[i]), y = static_cast<int8_t>(c[i]);
                        out[i] = (eq ? (x == y) : (x > y)) ? 0xFFu : 0u;
                    }
                } else if (type == 1) {
                    for (size_t i = 0; i < bytes / 2; ++i) {
                        const int16_t x = read_lane<int16_t>(lhs, i), y = read_lane<int16_t>(c, i);
                        write_lane<uint16_t>(out, i, (eq ? (x == y) : (x > y)) ? 0xFFFFu : 0u);
                    }
                } else {
                    for (size_t i = 0; i < bytes / 4; ++i) {
                        const int32_t x = read_lane<int32_t>(lhs, i), y = read_lane<int32_t>(c, i);
                        write_lane<uint32_t>(out, i, (eq ? (x == y) : (x > y)) ? ~0u : 0u);
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0xFC: case 0xFD: case 0xFE: case 0xD4:  // vpaddb/w/d/q
            case 0xF8: case 0xF9: case 0xFA: case 0xFB:  // vpsubb/w/d/q
            case 0xDC: case 0xDD: case 0xEC: case 0xED:  // vpaddusb/w, vpaddsb/w
            case 0xD8: case 0xD9: case 0xE8: case 0xE9:  // vpsubusb/w, vpsubsb/w
            case 0xE0: case 0xE3: case 0xDA: case 0xDE: case 0xEA: case 0xEE: {  // vpavg, vpminub/sw, vpmaxub/sw
                const uint8_t op = d.opcode;
                if (op == 0xFC || op == 0xF8 || op == 0xDC || op == 0xEC || op == 0xD8 || op == 0xE8 ||
                    op == 0xE0 || op == 0xDA || op == 0xDE) {
                    for (size_t i = 0; i < bytes; ++i) {
                        const uint8_t ux = lhs[i], uy = c[i];
                        const int sx = static_cast<int8_t>(ux), sy = static_cast<int8_t>(uy);
                        uint8_t r = 0;
                        if (op == 0xFC) r = uint8_t(ux + uy);
                        else if (op == 0xF8) r = uint8_t(ux - uy);
                        else if (op == 0xDC) r = uint8_t((std::min<int>)(ux + uy, 255));
                        else if (op == 0xEC) r = uint8_t(int8_t(std::clamp(sx + sy, -128, 127)));
                        else if (op == 0xD8) r = uint8_t((std::max<int>)(int(ux) - int(uy), 0));
                        else if (op == 0xE8) r = uint8_t(int8_t(std::clamp(sx - sy, -128, 127)));
                        else if (op == 0xE0) r = uint8_t((int(ux) + int(uy) + 1) >> 1);
                        else if (op == 0xDA) r = (std::min)(ux, uy);
                        else r = (std::max)(ux, uy);
                        out[i] = r;
                    }
                } else if (op == 0xFD || op == 0xF9 || op == 0xDD || op == 0xED || op == 0xD9 || op == 0xE9 ||
                           op == 0xE3 || op == 0xEA || op == 0xEE) {
                    for (size_t i = 0; i < bytes / 2; ++i) {
                        const uint16_t ux = read_lane<uint16_t>(lhs, i), uy = read_lane<uint16_t>(c, i);
                        const int sx = static_cast<int16_t>(ux), sy = static_cast<int16_t>(uy);
                        uint16_t r = 0;
                        if (op == 0xFD) r = uint16_t(ux + uy);
                        else if (op == 0xF9) r = uint16_t(ux - uy);
                        else if (op == 0xDD) r = uint16_t((std::min<int>)(int(ux) + int(uy), 65535));
                        else if (op == 0xED) r = uint16_t(int16_t(std::clamp(sx + sy, -32768, 32767)));
                        else if (op == 0xD9) r = uint16_t((std::max<int>)(int(ux) - int(uy), 0));
                        else if (op == 0xE9) r = uint16_t(int16_t(std::clamp(sx - sy, -32768, 32767)));
                        else if (op == 0xE3) r = uint16_t((uint32_t(ux) + uint32_t(uy) + 1u) >> 1);
                        else if (op == 0xEA) r = uint16_t((std::min)(sx, sy));
                        else r = uint16_t((std::max)(sx, sy));
                        write_lane<uint16_t>(out, i, r);
                    }
                } else if (op == 0xFE || op == 0xFA) {
                    for (size_t i = 0; i < bytes / 4; ++i) {
                        const uint32_t ux = read_lane<uint32_t>(lhs, i), uy = read_lane<uint32_t>(c, i);
                        write_lane<uint32_t>(out, i, op == 0xFE ? (ux + uy) : (ux - uy));
                    }
                } else {
                    for (size_t i = 0; i < bytes / 8; ++i) {
                        const uint64_t ux = read_lane<uint64_t>(lhs, i), uy = read_lane<uint64_t>(c, i);
                        write_lane<uint64_t>(out, i, op == 0xD4 ? (ux + uy) : (ux - uy));
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0xD5: case 0xE4: case 0xE5: case 0xF4: case 0xF5: {  // vpmullw, vpmulhuw, vpmulhw, vpmuludq, vpmaddwd
                if (d.opcode == 0xF4) {
                    for (size_t i = 0; i < bytes / 8; ++i)
                        write_lane<uint64_t>(out, i, uint64_t(read_lane<uint32_t>(lhs, i * 2)) *
                                                            read_lane<uint32_t>(c, i * 2));
                } else if (d.opcode == 0xF5) {
                    for (size_t i = 0; i < bytes / 4; ++i) {
                        const int32_t p0 = int32_t(read_lane<int16_t>(lhs, i * 2)) * read_lane<int16_t>(c, i * 2);
                        const int32_t p1 = int32_t(read_lane<int16_t>(lhs, i * 2 + 1)) * read_lane<int16_t>(c, i * 2 + 1);
                        write_lane<int32_t>(out, i, p0 + p1);
                    }
                } else {
                    for (size_t i = 0; i < bytes / 2; ++i) {
                        const int32_t sx = read_lane<int16_t>(lhs, i), sy = read_lane<int16_t>(c, i);
                        const uint32_t ux = read_lane<uint16_t>(lhs, i), uy = read_lane<uint16_t>(c, i);
                        uint16_t r = 0;
                        if (d.opcode == 0xD5) r = uint16_t(ux * uy);
                        else if (d.opcode == 0xE4) r = uint16_t((ux * uy) >> 16);
                        else r = uint16_t((sx * sy) >> 16);
                        write_lane<uint16_t>(out, i, r);
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0xF6: {  // vpsadbw
                for (size_t i = 0; i < bytes / 8; ++i) {
                    uint16_t sum = 0;
                    for (size_t k = 0; k < 8; ++k) {
                        const int a0 = lhs[i * 8 + k], b0 = c[i * 8 + k];
                        sum = static_cast<uint16_t>(sum + std::abs(a0 - b0));
                    }
                    write_lane<uint64_t>(out, i, sum);
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            case 0xD1: case 0xD2: case 0xD3: case 0xE1: case 0xE2: case 0xF1: case 0xF2: case 0xF3: {
                const uint64_t sh = read_lane<uint64_t>(c, 0);
                const bool srl = (d.opcode & 0xF0) == 0xD0;
                const bool sra = (d.opcode & 0xF0) == 0xE0;
                const size_t ew = ((d.opcode & 3) == 1) ? 2 : ((d.opcode & 3) == 2) ? 4 : 8;
                for (size_t i = 0; i < bytes / ew; ++i) {
                    if (ew == 2) {
                        uint16_t v = read_lane<uint16_t>(lhs, i);
                        uint16_t r = (sh >= 16) ? (sra ? uint16_t(int16_t(v) >> 15) : uint16_t(0))
                                                : (srl ? uint16_t(v >> sh) : sra ? uint16_t(int16_t(v) >> sh) : uint16_t(v << sh));
                        write_lane<uint16_t>(out, i, r);
                    } else if (ew == 4) {
                        uint32_t v = read_lane<uint32_t>(lhs, i);
                        uint32_t r = (sh >= 32) ? (sra ? uint32_t(int32_t(v) >> 31) : 0u)
                                                : (srl ? (v >> sh) : sra ? uint32_t(int32_t(v) >> sh) : (v << sh));
                        write_lane<uint32_t>(out, i, r);
                    } else {
                        uint64_t v = read_lane<uint64_t>(lhs, i);
                        uint64_t r = (sh >= 64) ? 0ull : (srl ? (v >> sh) : (v << sh));
                        write_lane<uint64_t>(out, i, r);
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
                return true;
            }
            default: break;
        }
        return false;
    }

    if (d.map == 2) {  // 0F 38
        if (vex && d.pp == 1 && (d.opcode & 0x0F) >= 6 &&
            ((d.opcode & 0xF0) == 0x90 || (d.opcode & 0xF0) == 0xA0 || (d.opcode & 0xF0) == 0xB0)) {
            const bool scalar = ((d.opcode & 0x0F) & 1) && (d.opcode & 0x0F) != 7;
            read_rm_vec(s, d, next_rip, c, scalar ? (d.w ? 8 : 4) : bytes);
            exec_fma(s, d, a, b, c, out, bytes);
            write_vec(s, d.reg, out, scalar ? 16 : bytes, vex);
            return true;
        }
        if (vex && (d.opcode == 0x8C || d.opcode == 0x8E || d.opcode == 0x2C || d.opcode == 0x2D ||
                    d.opcode == 0x2E || d.opcode == 0x2F)) {  // vmaskmov* / vpmaskmov*
            if (d.mod == 3) return false;
            const bool is_store = (d.opcode == 0x8E || d.opcode == 0x2E || d.opcode == 0x2F);
            const bool elem64 = (d.opcode == 0x2D || d.opcode == 0x2F || ((d.opcode == 0x8C || d.opcode == 0x8E) && d.w));
            const uint8_t* msk = is_store ? b : b;
            const uint64_t ea = effective_address(s, d, next_rip);
            if (!is_store) {
                for (size_t i = 0; i < (elem64 ? bytes / 8 : bytes / 4); ++i) {
                    if (elem64) {
                        if (read_lane<uint64_t>(msk, i) & (1ull << 63))
                            write_lane<uint64_t>(out, i, read_unaligned<uint64_t>(cptr_at(ea + i * 8)));
                    } else {
                        if (read_lane<uint32_t>(msk, i) & (1u << 31))
                            write_lane<uint32_t>(out, i, read_unaligned<uint32_t>(cptr_at(ea + i * 4)));
                    }
                }
                write_vec(s, d.reg, out, bytes, vex);
            } else {
                for (size_t i = 0; i < (elem64 ? bytes / 8 : bytes / 4); ++i) {
                    if (elem64) {
                        if (read_lane<uint64_t>(msk, i) & (1ull << 63))
                            write_unaligned<uint64_t>(ptr_at(ea + i * 8), read_lane<uint64_t>(a, i));
                    } else {
                        if (read_lane<uint32_t>(msk, i) & (1u << 31))
                            write_unaligned<uint32_t>(ptr_at(ea + i * 4), read_lane<uint32_t>(a, i));
                    }
                }
            }
            return true;
        }
        if (vex && d.opcode >= 0x90 && d.opcode <= 0x93) {  // vgather*
            const uint8_t idx_reg = (d.has_sib) ? (((d.sib >> 3) & 7) | (d.rex_x ? 8 : 0)) : d.rm;
            const uint8_t scale = d.has_sib ? ((d.sib >> 6) & 3) : 0;
            uint64_t base = 0;
            if (d.has_sib && !(d.mod == 0 && (d.sib & 7) == 5)) base = s.gpr[(d.sib & 7) | (d.rex_b ? 8 : 0)];
            base += static_cast<int64_t>(d.disp);
            const bool data64 = d.w;
            const bool idx64 = (d.opcode == 0x91 || d.opcode == 0x93);
            std::memcpy(out, a, bytes);
            const uint8_t* idx_v = s.ymm[idx_reg];
            const size_t n = data64 ? (bytes / 8) : (idx64 ? (bytes / 8) : (bytes / 4));
            for (size_t i = 0; i < n; ++i) {
                const bool active = data64 ? (read_lane<uint64_t>(b, i) >> 63) != 0
                                           : (read_lane<uint32_t>(b, i) >> 31) != 0;
                if (!active) continue;
                const int64_t iv = idx64 ? read_lane<int64_t>(idx_v, i) : int64_t(read_lane<int32_t>(idx_v, i));
                const uint64_t addr = base + (static_cast<uint64_t>(iv) << scale);
                if (data64) write_lane<uint64_t>(out, i, read_unaligned<uint64_t>(cptr_at(addr)));
                else write_lane<uint32_t>(out, i, read_unaligned<uint32_t>(cptr_at(addr)));
            }
            if (!data64 && idx64) std::memset(out + n * 4, 0, 32 - n * 4);
            std::memset(s.ymm[d.vvvv], 0, 32);
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (vex && d.opcode == 0x13) {  // vcvtph2ps
            const size_t n = bytes / 4;
            read_rm_vec(s, d, next_rip, c, n * 2);
            for (size_t i = 0; i < n; ++i) write_lane<float>(out, i, fp16_to_fp32(read_lane<uint16_t>(c, i)));
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (vex && (d.opcode == 0x18 || d.opcode == 0x19 || d.opcode == 0x1A || d.opcode == 0x5A ||
                    d.opcode == 0x58 || d.opcode == 0x59 || d.opcode == 0x78 || d.opcode == 0x79)) {
            const size_t ew = (d.opcode == 0x78) ? 1 : (d.opcode == 0x79) ? 2
                              : (d.opcode == 0x18 || d.opcode == 0x58) ? 4
                              : (d.opcode == 0x19 || d.opcode == 0x59) ? 8 : 16;
            read_rm_vec(s, d, next_rip, c, ew);
            for (size_t off = 0; off < bytes; off += ew) std::memcpy(out + off, c, ew);
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }

        const uint8_t* lhs = vex ? b : a;
        read_rm_vec(s, d, next_rip, c, bytes);

        if (d.opcode == 0x17) {  // ptest / vptest
            uint8_t or_and = 0, or_andn = 0;
            for (size_t i = 0; i < bytes; ++i) {
                or_and |= (a[i] & c[i]);
                or_andn |= ((~a[i]) & c[i]);
            }
            set_zf_cf_flags(s, or_and == 0, or_andn == 0, true);
            return true;
        }
        if (d.opcode == 0x00) {  // vpshufb
            for (size_t lane = 0; lane < bytes; lane += 16) {
                for (size_t k = 0; k < 16; ++k) {
                    const uint8_t ctl = c[lane + k];
                    out[lane + k] = (ctl & 0x80u) ? 0u : lhs[lane + (ctl & 0x0Fu)];
                }
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (d.opcode == 0x16 || d.opcode == 0x36) {  // vpermps, vpermd
            for (size_t i = 0; i < 8; ++i)
                write_lane<uint32_t>(out, i, read_lane<uint32_t>(c, read_lane<uint32_t>(lhs, i) & 7u));
            write_vec(s, d.reg, out, 32, vex);
            return true;
        }
        if (d.opcode >= 0x20 && d.opcode <= 0x25) {  // vpmovsx*
            const size_t n = (d.opcode == 0x20) ? (bytes / 2)
                             : (d.opcode == 0x21 || d.opcode == 0x23) ? (bytes / 4) : (bytes / 8);
            const size_t sw = (d.opcode <= 0x22) ? 1 : (d.opcode <= 0x24) ? 2 : 4;
            const size_t dw = (d.opcode == 0x20) ? 2 : (d.opcode == 0x21 || d.opcode == 0x23) ? 4 : 8;
            read_rm_vec(s, d, next_rip, c, n * sw);
            for (size_t i = 0; i < n; ++i) {
                int64_t sv = (sw == 1) ? int64_t(static_cast<int8_t>(c[i]))
                             : (sw == 2) ? int64_t(read_lane<int16_t>(c, i)) : int64_t(read_lane<int32_t>(c, i));
                if (dw == 2) write_lane<int16_t>(out, i, int16_t(sv));
                else if (dw == 4) write_lane<int32_t>(out, i, int32_t(sv));
                else write_lane<int64_t>(out, i, sv);
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (d.opcode >= 0x30 && d.opcode <= 0x35) {  // vpmovzx*
            const size_t n = (d.opcode == 0x30) ? (bytes / 2)
                             : (d.opcode == 0x31 || d.opcode == 0x33) ? (bytes / 4) : (bytes / 8);
            const size_t sw = (d.opcode <= 0x32) ? 1 : (d.opcode <= 0x34) ? 2 : 4;
            const size_t dw = (d.opcode == 0x30) ? 2 : (d.opcode == 0x31 || d.opcode == 0x33) ? 4 : 8;
            read_rm_vec(s, d, next_rip, c, n * sw);
            for (size_t i = 0; i < n; ++i) {
                uint64_t uv = (sw == 1) ? uint64_t(c[i])
                              : (sw == 2) ? uint64_t(read_lane<uint16_t>(c, i)) : uint64_t(read_lane<uint32_t>(c, i));
                if (dw == 2) write_lane<uint16_t>(out, i, uint16_t(uv));
                else if (dw == 4) write_lane<uint32_t>(out, i, uint32_t(uv));
                else write_lane<uint64_t>(out, i, uv);
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (d.opcode == 0x01 || d.opcode == 0x02 || d.opcode == 0x03 ||
            d.opcode == 0x05 || d.opcode == 0x06 || d.opcode == 0x07) {  // vphadd/sub w/d/sw
            const bool sub = (d.opcode >= 0x05);
            const bool dword = (d.opcode == 0x02 || d.opcode == 0x06);
            const bool sat = (d.opcode == 0x03 || d.opcode == 0x07);
            for (size_t lane = 0; lane < bytes; lane += 16) {
                if (dword) {
                    const size_t base = lane / 4;
                    for (size_t k = 0; k < 4; ++k) {
                        const uint8_t* src = k < 2 ? lhs : c;
                        const size_t idx = base + (k & 1) * 2;
                        const int32_t x = read_lane<int32_t>(src, idx), y = read_lane<int32_t>(src, idx + 1);
                        write_lane<int32_t>(out, base + k, sub ? (x - y) : (x + y));
                    }
                } else {
                    const size_t base = lane / 2;
                    for (size_t k = 0; k < 8; ++k) {
                        const uint8_t* src = k < 4 ? lhs : c;
                        const size_t idx = base + (k & 3) * 2;
                        const int x = read_lane<int16_t>(src, idx), y = read_lane<int16_t>(src, idx + 1);
                        const int r = sub ? (x - y) : (x + y);
                        write_lane<int16_t>(out, base + k, int16_t(sat ? std::clamp(r, -32768, 32767) : int16_t(r)));
                    }
                }
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (d.opcode == 0x04) {  // vpmaddubsw
            for (size_t i = 0; i < bytes / 2; ++i) {
                const int p0 = int(lhs[i * 2]) * int(static_cast<int8_t>(c[i * 2]));
                const int p1 = int(lhs[i * 2 + 1]) * int(static_cast<int8_t>(c[i * 2 + 1]));
                write_lane<int16_t>(out, i, int16_t(std::clamp(p0 + p1, -32768, 32767)));
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (d.opcode == 0x10 || d.opcode == 0x14 || d.opcode == 0x15) {  // pblendvb / blendvps / blendvpd
            if (vex) return false;  // VEX uses map 3 (0x4A/0x4B/0x4C)
            const uint8_t* mask_v = s.ymm[0];
            if (d.opcode == 0x10) {
                for (size_t i = 0; i < 16; ++i) out[i] = (mask_v[i] & 0x80u) ? c[i] : a[i];
            } else if (d.opcode == 0x14) {
                for (size_t i = 0; i < 4; ++i)
                    write_lane<uint32_t>(out, i, (read_lane<uint32_t>(mask_v, i) & 0x80000000u) ? read_lane<uint32_t>(c, i) : read_lane<uint32_t>(a, i));
            } else {
                for (size_t i = 0; i < 2; ++i)
                    write_lane<uint64_t>(out, i, (read_lane<uint64_t>(mask_v, i) & (1ull << 63)) ? read_lane<uint64_t>(c, i) : read_lane<uint64_t>(a, i));
            }
            write_vec(s, d.reg, out, 16, false);
            return true;
        }
        if (d.opcode == 0x28 || d.opcode == 0x29 || d.opcode == 0x2B || d.opcode == 0x37 ||
            (d.opcode >= 0x38 && d.opcode <= 0x40)) {
            if (d.opcode == 0x29 || d.opcode == 0x37) {  // pcmpeqq / pcmpgtq
                for (size_t i = 0; i < bytes / 8; ++i) {
                    const int64_t x = read_lane<int64_t>(lhs, i), y = read_lane<int64_t>(c, i);
                    write_lane<uint64_t>(out, i, (d.opcode == 0x29 ? (x == y) : (x > y)) ? ~0ull : 0ull);
                }
            } else if (d.opcode == 0x28) {  // pmuldq
                for (size_t i = 0; i < bytes / 8; ++i)
                    write_lane<int64_t>(out, i, int64_t(read_lane<int32_t>(lhs, i * 2)) * read_lane<int32_t>(c, i * 2));
            } else if (d.opcode == 0x2B) {  // packusdw
                for (size_t lane = 0; lane < bytes; lane += 16) {
                    const size_t b_d = lane / 4, b_w = lane / 2;
                    for (size_t k = 0; k < 8; ++k) {
                        const int32_t v = read_lane<int32_t>(k < 4 ? lhs : c, b_d + (k & 3));
                        write_lane<uint16_t>(out, b_w + k, uint16_t(std::clamp<int>(v, 0, 65535)));
                    }
                }
            } else if (d.opcode == 0x40) {  // pmulld
                for (size_t i = 0; i < bytes / 4; ++i)
                    write_lane<uint32_t>(out, i, read_lane<uint32_t>(lhs, i) * read_lane<uint32_t>(c, i));
            } else if (d.opcode == 0x38 || d.opcode == 0x3C) {  // pminsb / pmaxsb
                for (size_t i = 0; i < bytes; ++i) {
                    const int8_t x = static_cast<int8_t>(lhs[i]), y = static_cast<int8_t>(c[i]);
                    out[i] = uint8_t(d.opcode == 0x38 ? (std::min)(x, y) : (std::max)(x, y));
                }
            } else if (d.opcode == 0x39 || d.opcode == 0x3D) {  // pminsd / pmaxsd
                for (size_t i = 0; i < bytes / 4; ++i) {
                    const int32_t x = read_lane<int32_t>(lhs, i), y = read_lane<int32_t>(c, i);
                    write_lane<int32_t>(out, i, d.opcode == 0x39 ? (std::min)(x, y) : (std::max)(x, y));
                }
            } else if (d.opcode == 0x3A || d.opcode == 0x3E) {  // pminuw / pmaxuw
                for (size_t i = 0; i < bytes / 2; ++i) {
                    const uint16_t x = read_lane<uint16_t>(lhs, i), y = read_lane<uint16_t>(c, i);
                    write_lane<uint16_t>(out, i, d.opcode == 0x3A ? (std::min)(x, y) : (std::max)(x, y));
                }
            } else {  // pminud / pmaxud
                for (size_t i = 0; i < bytes / 4; ++i) {
                    const uint32_t x = read_lane<uint32_t>(lhs, i), y = read_lane<uint32_t>(c, i);
                    write_lane<uint32_t>(out, i, d.opcode == 0x3B ? (std::min)(x, y) : (std::max)(x, y));
                }
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (vex && (d.opcode == 0x45 || d.opcode == 0x46 || d.opcode == 0x47)) {  // vpsrlv/vpsrav/vpsllv
            const bool srl = (d.opcode == 0x45), sra = (d.opcode == 0x46);
            if (!d.w && !sra) {
                for (size_t i = 0; i < bytes / 4; ++i) {
                    const uint32_t v = read_lane<uint32_t>(lhs, i), sh = read_lane<uint32_t>(c, i);
                    write_lane<uint32_t>(out, i, sh >= 32 ? 0u : (srl ? (v >> sh) : (v << sh)));
                }
            } else if (sra) {
                for (size_t i = 0; i < bytes / 4; ++i) {
                    const int32_t v = read_lane<int32_t>(lhs, i);
                    const uint32_t sh = read_lane<uint32_t>(c, i);
                    write_lane<int32_t>(out, i, sh >= 32 ? (v >> 31) : (v >> sh));
                }
            } else {
                for (size_t i = 0; i < bytes / 8; ++i) {
                    const uint64_t v = read_lane<uint64_t>(lhs, i), sh = read_lane<uint64_t>(c, i);
                    write_lane<uint64_t>(out, i, sh >= 64 ? 0ull : (srl ? (v >> sh) : (v << sh)));
                }
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        return false;
    }

    if (d.map == 3) {  // 0F 3A
        if (d.opcode == 0x1D && vex) {  // vcvtps2ph
            const size_t n = bytes / 4;
            for (size_t i = 0; i < n; ++i) write_lane<uint16_t>(out, i, fp32_to_fp16(read_lane<float>(a, i), d.imm8 & 3));
            if (d.mod == 3) write_vec(s, d.rm, out, 16, true);
            else std::memcpy(ptr_at(effective_address(s, d, next_rip)), out, n * 2);
            return true;
        }
        if (d.opcode == 0x14 || d.opcode == 0x15 || d.opcode == 0x16 || d.opcode == 0x17) {  // pextrb/w/d/q / extractps
            const size_t w = (d.opcode == 0x14) ? 1 : (d.opcode == 0x15) ? 2 : (d.opcode == 0x16 && d.w) ? 8 : 4;
            const size_t idx = d.imm8 & ((16 / w) - 1);
            uint64_t val = 0;
            std::memcpy(&val, a + idx * w, w);
            if (d.mod == 3 && w < 4) write_gpr(s, d.rm, val, d.w ? 8 : 4);
            else write_rm_gpr(s, d, next_rip, val, w);
            return true;
        }
        if (d.opcode == 0x20 || d.opcode == 0x22) {  // pinsrb / pinsrd / pinsrq
            const size_t w = (d.opcode == 0x20) ? 1 : (d.w ? 8 : 4);
            const size_t idx = d.imm8 & ((16 / w) - 1);
            std::memcpy(out, vex ? b : a, 16);
            const uint64_t val = read_rm_gpr(s, d, next_rip, w);
            std::memcpy(out + idx * w, &val, w);
            write_vec(s, d.reg, out, 16, vex);
            return true;
        }
        if (d.opcode == 0x21) {  // insertps / vinsertps
            read_rm_vec(s, d, next_rip, c, d.mod == 3 ? 16 : 4);
            const size_t count_s = (d.mod == 3) ? ((d.imm8 >> 6) & 3) : 0;
            const size_t count_d = (d.imm8 >> 4) & 3;
            std::memcpy(out, vex ? b : a, 16);
            write_lane<uint32_t>(out, count_d, read_lane<uint32_t>(c, count_s));
            for (size_t k = 0; k < 4; ++k)
                if ((d.imm8 >> k) & 1) write_lane<uint32_t>(out, k, 0u);
            write_vec(s, d.reg, out, 16, vex);
            return true;
        }
        if (vex && (d.opcode == 0x19 || d.opcode == 0x39)) {  // vextractf128 / vextracti128
            const uint8_t* src_half = a + ((d.imm8 & 1) ? 16 : 0);
            if (d.mod == 3) write_vec(s, d.rm, src_half, 16, true);
            else std::memcpy(ptr_at(effective_address(s, d, next_rip)), src_half, 16);
            return true;
        }
        if (vex && (d.opcode == 0x18 || d.opcode == 0x38)) {  // vinsertf128 / vinserti128
            read_rm_vec(s, d, next_rip, c, 16);
            std::memcpy(out, b, 32);
            std::memcpy(out + ((d.imm8 & 1) ? 16 : 0), c, 16);
            write_vec(s, d.reg, out, 32, vex);
            return true;
        }
        const uint8_t* lhs = vex ? b : a;
        read_rm_vec(s, d, next_rip, c, bytes);
        if (vex && (d.opcode == 0x00 || d.opcode == 0x01)) {  // vpermq / vpermpd
            for (size_t i = 0; i < 4; ++i)
                write_lane<uint64_t>(out, i, read_lane<uint64_t>(c, (d.imm8 >> (2 * i)) & 3));
            write_vec(s, d.reg, out, 32, vex);
            return true;
        }
        if (vex && (d.opcode == 0x06 || d.opcode == 0x46)) {  // vperm2f128 / vperm2i128
            auto sel128 = [&](uint8_t n, uint8_t* dst16) {
                if (n & 8) std::memset(dst16, 0, 16);
                else std::memcpy(dst16, ((n & 2) ? c : lhs) + ((n & 1) ? 16 : 0), 16);
            };
            sel128(d.imm8 & 0xF, out);
            sel128((d.imm8 >> 4) & 0xF, out + 16);
            write_vec(s, d.reg, out, 32, vex);
            return true;
        }
        if (vex && (d.opcode == 0x04 || d.opcode == 0x05)) {  // vpermilps / vpermilpd imm
            for (size_t lane = 0; lane < bytes; lane += 16) {
                if (d.opcode == 0x04) {
                    const size_t base = lane / 4;
                    for (size_t k = 0; k < 4; ++k)
                        write_lane<uint32_t>(out, base + k, read_lane<uint32_t>(c, base + ((d.imm8 >> (2 * k)) & 3)));
                } else {
                    const size_t base = lane / 8;
                    const size_t s0 = (d.imm8 >> (lane / 8)) & 1;
                    const size_t s1 = (d.imm8 >> (lane / 8 + 1)) & 1;
                    write_lane<uint64_t>(out, base + 0, read_lane<uint64_t>(c, base + s0));
                    write_lane<uint64_t>(out, base + 1, read_lane<uint64_t>(c, base + s1));
                }
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (d.opcode >= 0x08 && d.opcode <= 0x0B) {  // round ps/pd/ss/sd
            const bool sc = (d.opcode >= 0x0A);
            const bool dbl = (d.opcode == 0x09 || d.opcode == 0x0B);
            if (sc) std::memcpy(out, lhs, 16);
            const size_t n = sc ? 1 : (dbl ? bytes / 8 : bytes / 4);
            for (size_t i = 0; i < n; ++i) {
                if (dbl) write_lane<double>(out, i, round_fp(read_lane<double>(c, i), d.imm8, s.mxcsr));
                else write_lane<float>(out, i, round_fp(read_lane<float>(c, i), d.imm8, s.mxcsr));
            }
            write_vec(s, d.reg, out, sc ? 16 : bytes, vex);
            return true;
        }
        if (d.opcode == 0x02 || d.opcode == 0x0C || d.opcode == 0x0D || d.opcode == 0x0E) {  // blend*
            if (d.opcode == 0x02 || d.opcode == 0x0C) {
                for (size_t i = 0; i < bytes / 4; ++i)
                    write_lane<uint32_t>(out, i, ((d.imm8 >> i) & 1) ? read_lane<uint32_t>(c, i) : read_lane<uint32_t>(lhs, i));
            } else if (d.opcode == 0x0D) {
                for (size_t i = 0; i < bytes / 8; ++i)
                    write_lane<uint64_t>(out, i, ((d.imm8 >> i) & 1) ? read_lane<uint64_t>(c, i) : read_lane<uint64_t>(lhs, i));
            } else {
                for (size_t i = 0; i < bytes / 2; ++i)
                    write_lane<uint16_t>(out, i, ((d.imm8 >> (i & 7)) & 1) ? read_lane<uint16_t>(c, i) : read_lane<uint16_t>(lhs, i));
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (d.opcode == 0x0F) {  // vpalignr
            for (size_t lane = 0; lane < bytes; lane += 16) {
                uint8_t cat[32];
                std::memcpy(cat, c + lane, 16);
                std::memcpy(cat + 16, lhs + lane, 16);
                for (size_t k = 0; k < 16; ++k) {
                    const size_t idx = d.imm8 + k;
                    out[lane + k] = (idx < 32) ? cat[idx] : 0u;
                }
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (vex && (d.opcode == 0x4A || d.opcode == 0x4B || d.opcode == 0x4C)) {  // vblendvps/pd/pblendvb
            const uint8_t* msk = s.ymm[(d.imm8 >> 4) & 0xF];
            if (d.opcode == 0x4C) {
                for (size_t i = 0; i < bytes; ++i) out[i] = (msk[i] & 0x80u) ? c[i] : lhs[i];
            } else if (d.opcode == 0x4A) {
                for (size_t i = 0; i < bytes / 4; ++i)
                    write_lane<uint32_t>(out, i, (read_lane<uint32_t>(msk, i) >> 31) ? read_lane<uint32_t>(c, i) : read_lane<uint32_t>(lhs, i));
            } else {
                for (size_t i = 0; i < bytes / 8; ++i)
                    write_lane<uint64_t>(out, i, (read_lane<uint64_t>(msk, i) >> 63) ? read_lane<uint64_t>(c, i) : read_lane<uint64_t>(lhs, i));
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
        if (d.opcode == 0x40 || d.opcode == 0x41) {  // vdpps / vdppd
            for (size_t lane = 0; lane < bytes; lane += 16) {
                if (d.opcode == 0x40) {
                    float sum = 0.0f;
                    const size_t base = lane / 4;
                    for (size_t k = 0; k < 4; ++k)
                        if ((d.imm8 >> (4 + k)) & 1) sum += read_lane<float>(lhs, base + k) * read_lane<float>(c, base + k);
                    for (size_t k = 0; k < 4; ++k)
                        write_lane<float>(out, base + k, ((d.imm8 >> k) & 1) ? sum : 0.0f);
                } else {
                    double sum = 0.0;
                    const size_t base = lane / 8;
                    for (size_t k = 0; k < 2; ++k)
                        if ((d.imm8 >> (4 + k)) & 1) sum += read_lane<double>(lhs, base + k) * read_lane<double>(c, base + k);
                    for (size_t k = 0; k < 2; ++k)
                        write_lane<double>(out, base + k, ((d.imm8 >> k) & 1) ? sum : 0.0);
                }
            }
            write_vec(s, d.reg, out, bytes, vex);
            return true;
        }
    }
    return false;
}

inline size_t extra_imm_bytes(const Decoded& d) {
    if (d.has_vex && d.opcode == 0x77 && d.map == 1) return 0;  // VZEROUPPER/VZEROALL has no ModRM
    if (d.map == 3) return 1;                                    // 0F 3A opcodes always take imm8
    if (d.map == 1 && (d.opcode == 0x70 || d.opcode == 0x71 || d.opcode == 0x72 || d.opcode == 0x73 ||
                       d.opcode == 0xC2 || d.opcode == 0xC4 || d.opcode == 0xC5 || d.opcode == 0xC6))
        return 1;
    return 0;
}

inline bool decode_and_exec(CpuState& s, const uint8_t* code) {
    Decoded d{};
    size_t off = 0;
    while (off < 10) {
        const uint8_t b = code[off];
        if (b == 0x66) d.has_66 = true;
        else if (b == 0xF2) d.has_f2 = true;
        else if (b == 0xF3) d.has_f3 = true;
        else if (b == 0x67) d.addr32 = true;
        else if (b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x64 || b == 0x65 || b == 0xF0) {}
        else break;
        ++off;
    }
    if (code[off] >= 0x40 && code[off] <= 0x4F) {
        const uint8_t rex = code[off++];
        d.w = (rex & 8) != 0;
        d.rex_r = (rex & 4) != 0;
        d.rex_x = (rex & 2) != 0;
        d.rex_b = (rex & 1) != 0;
    }
    if (code[off] == 0xC5) {
        d.has_vex = true;
        const uint8_t b1 = code[off + 1];
        d.rex_r = !(b1 & 0x80);
        d.rex_x = false;
        d.rex_b = false;
        d.map = 1;
        d.w = false;
        d.vvvv = static_cast<uint8_t>((~(b1 >> 3)) & 0xFu);
        d.l = (b1 & 0x04) != 0;
        d.pp = b1 & 3;
        off += 2;
        d.opcode = code[off++];
    } else if (code[off] == 0xC4) {
        d.has_vex = true;
        const uint8_t b1 = code[off + 1];
        const uint8_t b2 = code[off + 2];
        d.rex_r = !(b1 & 0x80);
        d.rex_x = !(b1 & 0x40);
        d.rex_b = !(b1 & 0x20);
        d.map = b1 & 0x1F;
        d.w = (b2 & 0x80) != 0;
        d.vvvv = static_cast<uint8_t>((~(b2 >> 3)) & 0xFu);
        d.l = (b2 & 0x04) != 0;
        d.pp = b2 & 3;
        off += 3;
        d.opcode = code[off++];
    } else {
        if (code[off] != 0x0F) return false;
        ++off;
        if (code[off] == 0x38) { d.map = 2; ++off; }
        else if (code[off] == 0x3A) { d.map = 3; ++off; }
        else d.map = 1;
        d.opcode = code[off++];
        d.pp = pp_for_legacy(d);
    }

    if (d.has_vex && d.map == 1 && d.opcode == 0x77) {
        d.insn_len = static_cast<uint8_t>(off);
    } else {
        parse_modrm(code, d, off, extra_imm_bytes(d));
    }

    const uint64_t next_rip = s.rip + d.insn_len;
    bool ok = false;
    if (d.has_vex) {
        if (d.map >= 2 && (d.opcode >= 0xF0 && d.opcode <= 0xF7)) ok = exec_bmi(s, d, next_rip);
        if (!ok) ok = exec_simd(s, d, next_rip);
    } else {
        ok = exec_scalar_legacy(s, d, next_rip);
        if (!ok) ok = exec_simd(s, d, next_rip);
    }
    if (ok) s.rip = next_rip;
    return ok;
}

}  // namespace detail

// Emulates the instruction at state.rip using bytes read from state.rip; returns true and advances state.rip.
inline bool emulate(CpuState& state) {
    return detail::decode_and_exec(state, reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(state.rip)));
}

#ifdef _WIN32

struct HostFeatures {
    bool sse41 = false;
    bool sse42 = false;
    bool popcnt = false;
    bool movbe = false;
    bool avx = false;
    bool f16c = false;
    bool fma = false;
    bool avx2 = false;
    bool bmi1 = false;
    bool bmi2 = false;
    bool missing_any() const {
        return !(sse41 && sse42 && popcnt && movbe && avx && f16c && fma && avx2 && bmi1 && bmi2);
    }
};

inline HostFeatures detect_host_features() {
    HostFeatures f{};
    int info[4]{};
    __cpuid(info, 0);
    const int max_leaf = info[0];
    if (max_leaf >= 1) {
        __cpuid(info, 1);
        f.sse41 = (info[2] & (1 << 19)) != 0;
        f.sse42 = (info[2] & (1 << 20)) != 0;
        f.movbe = (info[2] & (1 << 22)) != 0;
        f.popcnt = (info[2] & (1 << 23)) != 0;
        const bool osxsave = (info[2] & (1 << 27)) != 0;
        const bool avx_cpu = (info[2] & (1 << 28)) != 0;
        const bool f16c_cpu = (info[2] & (1 << 29)) != 0;
        const bool fma_cpu = (info[2] & (1 << 12)) != 0;
        if (osxsave) {
            const uint64_t xcr0 = _xgetbv(0);
            const bool ymm_os = (xcr0 & 0x6) == 0x6;
            f.avx = ymm_os && avx_cpu;
            f.f16c = ymm_os && f16c_cpu;
            f.fma = ymm_os && fma_cpu;
        }
    }
    if (max_leaf >= 7) {
        __cpuidex(info, 7, 0);
        f.bmi1 = (info[1] & (1 << 3)) != 0;
        f.avx2 = f.avx && ((info[1] & (1 << 5)) != 0);
        f.bmi2 = (info[1] & (1 << 8)) != 0;
    }
    return f;
}

namespace win {

struct YmmHiThread {
    alignas(16) uint8_t hi[16][16]{};
};

inline YmmHiThread& thread_ymm_hi() {
    thread_local YmmHiThread state;
    return state;
}

inline void context_to_state(const CONTEXT& c, CpuState& s) {
    s.gpr[0] = c.Rax; s.gpr[1] = c.Rcx; s.gpr[2] = c.Rdx; s.gpr[3] = c.Rbx;
    s.gpr[4] = c.Rsp; s.gpr[5] = c.Rbp; s.gpr[6] = c.Rsi; s.gpr[7] = c.Rdi;
    s.gpr[8] = c.R8;  s.gpr[9] = c.R9;  s.gpr[10] = c.R10; s.gpr[11] = c.R11;
    s.gpr[12] = c.R12; s.gpr[13] = c.R13; s.gpr[14] = c.R14; s.gpr[15] = c.R15;
    s.rip = c.Rip;
    s.rflags = c.EFlags;
    s.mxcsr = c.MxCsr;
    const M128A* xmms[16] = {&c.Xmm0, &c.Xmm1, &c.Xmm2,  &c.Xmm3,  &c.Xmm4,  &c.Xmm5,  &c.Xmm6,  &c.Xmm7,
                             &c.Xmm8, &c.Xmm9, &c.Xmm10, &c.Xmm11, &c.Xmm12, &c.Xmm13, &c.Xmm14, &c.Xmm15};
    auto& hi = thread_ymm_hi();
    for (int i = 0; i < 16; ++i) {
        std::memcpy(s.ymm[i], xmms[i], 16);
        std::memcpy(s.ymm[i] + 16, hi.hi[i], 16);
    }
}

inline void state_to_context(const CpuState& s, CONTEXT& c) {
    c.Rax = s.gpr[0]; c.Rcx = s.gpr[1]; c.Rdx = s.gpr[2]; c.Rbx = s.gpr[3];
    c.Rsp = s.gpr[4]; c.Rbp = s.gpr[5]; c.Rsi = s.gpr[6]; c.Rdi = s.gpr[7];
    c.R8  = s.gpr[8]; c.R9  = s.gpr[9]; c.R10 = s.gpr[10]; c.R11 = s.gpr[11];
    c.R12 = s.gpr[12]; c.R13 = s.gpr[13]; c.R14 = s.gpr[14]; c.R15 = s.gpr[15];
    c.Rip = s.rip;
    c.EFlags = static_cast<DWORD>(s.rflags);
    c.MxCsr = s.mxcsr;
    M128A* xmms[16] = {&c.Xmm0, &c.Xmm1, &c.Xmm2,  &c.Xmm3,  &c.Xmm4,  &c.Xmm5,  &c.Xmm6,  &c.Xmm7,
                       &c.Xmm8, &c.Xmm9, &c.Xmm10, &c.Xmm11, &c.Xmm12, &c.Xmm13, &c.Xmm14, &c.Xmm15};
    auto& hi = thread_ymm_hi();
    for (int i = 0; i < 16; ++i) {
        std::memcpy(xmms[i], s.ymm[i], 16);
        std::memcpy(hi.hi[i], s.ymm[i] + 16, 16);
    }
}

inline LONG CALLBACK veh_handler(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord || !info->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    CpuState s{};
    context_to_state(*info->ContextRecord, s);
    if (emulate(s)) {
        state_to_context(s, *info->ContextRecord);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    const auto* b = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(s.rip));
    std::fprintf(stderr, "[rt] cpu_emu: unhandled #UD at 0x%llx (%02x %02x %02x %02x %02x %02x)\n",
                 static_cast<unsigned long long>(s.rip), b[0], b[1], b[2], b[3], b[4], b[5]);
    return EXCEPTION_CONTINUE_SEARCH;
}

inline PVOID& handle_slot() {
    static PVOID h = nullptr;
    return h;
}

}  // namespace win

// Installs the #UD instruction emulator when the host CPU lacks any Xbox Series extension or LEONIDA_CPU_EMU=1.
inline bool install(bool force = false) {
    if (win::handle_slot()) return true;
    const HostFeatures f = detect_host_features();
    const bool env_force = GetEnvironmentVariableW(L"LEONIDA_CPU_EMU", nullptr, 0) != 0;
    if (!force && !env_force && !f.missing_any()) return false;
    win::handle_slot() = AddVectoredExceptionHandler(1, &win::veh_handler);
    if (win::handle_slot()) {
        std::fprintf(stderr,
                     "[rt] cpu_emu: active (avx=%d avx2=%d fma=%d f16c=%d bmi1=%d bmi2=%d popcnt=%d sse4.1=%d)\n",
                     f.avx, f.avx2, f.fma, f.f16c, f.bmi1, f.bmi2, f.popcnt, f.sse41);
    }
    return win::handle_slot() != nullptr;
}

inline void uninstall() {
    if (win::handle_slot()) {
        RemoveVectoredExceptionHandler(win::handle_slot());
        win::handle_slot() = nullptr;
    }
}

#endif  // _WIN32

}  // namespace rt::cpu_emu
