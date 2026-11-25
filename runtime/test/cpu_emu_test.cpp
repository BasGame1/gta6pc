#include "cpu_emu.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int fail_at(int line, const char* msg) {
    std::fprintf(stderr, "cpu_emu_test failed at line %d: %s\n", line, msg);
    return 1;
}

#define CHECK(cond)                                    \
    do {                                               \
        if (!(cond)) return fail_at(__LINE__, #cond);  \
    } while (0)

bool exec_bytes(rt::cpu_emu::CpuState& s, const std::vector<uint8_t>& code) {
    s.rip = reinterpret_cast<uint64_t>(code.data());
    const uint64_t end = s.rip + code.size();
    return rt::cpu_emu::emulate(s) && s.rip == end;
}

template <typename T>
T lane(const rt::cpu_emu::CpuState& s, size_t reg, size_t idx) {
    T v{};
    std::memcpy(&v, s.ymm[reg] + idx * sizeof(T), sizeof(T));
    return v;
}

template <typename T>
void set_lane(rt::cpu_emu::CpuState& s, size_t reg, size_t idx, T v) {
    std::memcpy(s.ymm[reg] + idx * sizeof(T), &v, sizeof(T));
}

int test_scalar_bmi_popcnt_lzcnt() {
    using rt::cpu_emu::CpuState;
    {
        CpuState s{};
        s.gpr[1] = 0x0F0F0F0Full;
        // POPCNT RAX, RCX: F3 48 0F B8 C1
        CHECK(exec_bytes(s, {0xF3, 0x48, 0x0F, 0xB8, 0xC1}));
        CHECK(s.gpr[0] == 16);
        CHECK((s.rflags & (1ull << 6)) == 0);
    }
    {
        CpuState s{};
        s.gpr[1] = 0;
        // TZCNT EAX, ECX: F3 0F BC C1
        CHECK(exec_bytes(s, {0xF3, 0x0F, 0xBC, 0xC1}));
        CHECK(s.gpr[0] == 32);
        CHECK((s.rflags & 1ull) != 0);
        // LZCNT RAX, RCX (0 -> 64): F3 48 0F BD C1
        CHECK(exec_bytes(s, {0xF3, 0x48, 0x0F, 0xBD, 0xC1}));
        CHECK(s.gpr[0] == 64);
        CHECK((s.rflags & 1ull) != 0);
    }
    {
        CpuState s{};
        s.gpr[1] = 0x00FF00FFu;
        s.gpr[2] = 0xFFFFFFFFu;
        // ANDN EAX, ECX, EDX: VEX.NDS.LZ.0F38.W0 F2 /r -> C4 E2 70 F2 C2
        CHECK(exec_bytes(s, {0xC4, 0xE2, 0x70, 0xF2, 0xC2}));
        CHECK(s.gpr[0] == 0xFF00FF00u);
    }
    {
        CpuState s{};
        s.gpr[1] = 0b101100u;
        // BLSI EAX, ECX: C4 E2 78 F3 D9 (vvvv=~0=rax, /3 modrm=11 011 001 = D9)
        CHECK(exec_bytes(s, {0xC4, 0xE2, 0x78, 0xF3, 0xD9}));
        CHECK(s.gpr[0] == 0b000100u);
        // BLSR RAX, RCX: C4 E2 F8 F3 C9 (/1)
        CHECK(exec_bytes(s, {0xC4, 0xE2, 0xF8, 0xF3, 0xC9}));
        CHECK(s.gpr[0] == 0b101000u);
    }
    {
        CpuState s{};
        s.gpr[1] = 0xF0F0F0F0u;
        s.gpr[2] = 0x0000FF00u;
        // PEXT EAX, ECX, EDX: C4 E2 72 F5 C2 (pp=2 F3, vvvv=1 RCX, rm=2 EDX)
        CHECK(exec_bytes(s, {0xC4, 0xE2, 0x72, 0xF5, 0xC2}));
        CHECK(s.gpr[0] == 0xF0u);
        // PDEP EAX, EAX, EDX: C4 E2 7B F5 C2 (pp=3 F2, vvvv=0 RAX)
        CHECK(exec_bytes(s, {0xC4, 0xE2, 0x7B, 0xF5, 0xC2}));
        CHECK(s.gpr[0] == 0x0000F000u);
    }
    {
        CpuState s{};
        s.gpr[2] = 0x100000000ull;  // RDX
        s.gpr[1] = 0x200000000ull;  // RCX
        // MULX R8, R9, RCX: RDX * RCX -> R8(hi):R9(lo): C4 62 B3 F6 C1 (R=0->R8, vvvv=~9=6, W=1)
        CHECK(exec_bytes(s, {0xC4, 0x62, 0xB3, 0xF6, 0xC1}));
        CHECK(s.gpr[9] == 0ull);
        CHECK(s.gpr[8] == 2ull);
    }
    {
        CpuState s{};
        s.gpr[1] = 0x12345678u;
        // RORX EAX, ECX, 8: C4 E3 7B F0 C1 08
        CHECK(exec_bytes(s, {0xC4, 0xE3, 0x7B, 0xF0, 0xC1, 0x08}));
        CHECK(s.gpr[0] == 0x78123456u);
    }
    {
        CpuState s{};
        uint32_t mem = 0x11223344u;
        s.gpr[1] = reinterpret_cast<uint64_t>(&mem);
        // MOVBE EAX, [RCX]: 0F 38 F0 01
        CHECK(exec_bytes(s, {0x0F, 0x38, 0xF0, 0x01}));
        CHECK(s.gpr[0] == 0x44332211u);
    }
    return 0;
}

int test_avx_avx2_fma_f16c() {
    using rt::cpu_emu::CpuState;
    {
        // VADDPS YMM0, YMM1, YMM2: C5 F4 58 C2 (L=1, pp=0, vvvv=1)
        CpuState s{};
        for (size_t i = 0; i < 8; ++i) {
            set_lane<float>(s, 1, i, float(i) + 1.0f);
            set_lane<float>(s, 2, i, 10.0f);
        }
        CHECK(exec_bytes(s, {0xC5, 0xF4, 0x58, 0xC2}));
        for (size_t i = 0; i < 8; ++i) CHECK(lane<float>(s, 0, i) == float(i) + 11.0f);

        // 128-bit VEX zeros upper half of YMM0: VXORPS XMM0, XMM0, XMM0: C5 F8 57 C0
        CHECK(exec_bytes(s, {0xC5, 0xF8, 0x57, 0xC0}));
        for (size_t i = 0; i < 8; ++i) CHECK(lane<uint32_t>(s, 0, i) == 0u);
    }
    {
        // VFMADD213PS YMM0, YMM1, YMM2: YMM0 = YMM1 * YMM0 + YMM2
        // C4 E2 75 A8 C2 (map=2, W=0, vvvv=1, L=1, pp=1)
        CpuState s{};
        for (size_t i = 0; i < 8; ++i) {
            set_lane<float>(s, 0, i, 3.0f);
            set_lane<float>(s, 1, i, float(i));
            set_lane<float>(s, 2, i, 0.5f);
        }
        CHECK(exec_bytes(s, {0xC4, 0xE2, 0x75, 0xA8, 0xC2}));
        for (size_t i = 0; i < 8; ++i) CHECK(lane<float>(s, 0, i) == float(i) * 3.0f + 0.5f);
    }
    {
        // VCVTPS2PH XMM1, YMM0, 0: C4 E3 7D 1D C1 00 -> VCVTPH2PS YMM2, XMM1: C4 E2 7D 13 D1
        CpuState s{};
        for (size_t i = 0; i < 8; ++i) set_lane<float>(s, 0, i, float(i) * 0.5f - 1.5f);
        CHECK(exec_bytes(s, {0xC4, 0xE3, 0x7D, 0x1D, 0xC1, 0x00}));
        CHECK(exec_bytes(s, {0xC4, 0xE2, 0x7D, 0x13, 0xD1}));
        for (size_t i = 0; i < 8; ++i) CHECK(lane<float>(s, 2, i) == float(i) * 0.5f - 1.5f);
    }
    {
        // VPERMD YMM0, YMM1, YMM2: C4 E2 75 36 C2
        CpuState s{};
        for (uint32_t i = 0; i < 8; ++i) {
            set_lane<uint32_t>(s, 1, i, 7u - i);
            set_lane<uint32_t>(s, 2, i, 100u + i);
        }
        CHECK(exec_bytes(s, {0xC4, 0xE2, 0x75, 0x36, 0xC2}));
        for (uint32_t i = 0; i < 8; ++i) CHECK(lane<uint32_t>(s, 0, i) == 100u + (7u - i));
    }
    {
        // VPGATHERDD YMM0, [RCX + YMM1*4], YMM2: C4 E2 6D 90 04 89 (vvvv=2 mask, W=0, L=1, pp=1, SIB=89: scale=2 idx=1 base=1)
        CpuState s{};
        uint32_t table[8] = {10, 20, 30, 40, 50, 60, 70, 80};
        s.gpr[1] = reinterpret_cast<uint64_t>(table);
        for (uint32_t i = 0; i < 8; ++i) {
            set_lane<uint32_t>(s, 0, i, 999u);
            set_lane<uint32_t>(s, 1, i, (i * 3u) & 7u);
            set_lane<uint32_t>(s, 2, i, (i & 1u) ? 0x80000000u : 0u);
        }
        CHECK(exec_bytes(s, {0xC4, 0xE2, 0x6D, 0x90, 0x04, 0x89}));
        for (uint32_t i = 0; i < 8; ++i) {
            const uint32_t expected = (i & 1u) ? table[(i * 3u) & 7u] : 999u;
            CHECK(lane<uint32_t>(s, 0, i) == expected);
            CHECK(lane<uint32_t>(s, 2, i) == 0u);
        }
    }
    {
        // VPTEST YMM0, YMM1: C4 E2 7D 17 C1
        CpuState s{};
        set_lane<uint64_t>(s, 0, 0, 0x0F);
        set_lane<uint64_t>(s, 1, 0, 0xF0);
        CHECK(exec_bytes(s, {0xC4, 0xE2, 0x7D, 0x17, 0xC1}));
        CHECK((s.rflags & (1ull << 6)) != 0);  // ZF=1 (AND==0)
        CHECK((s.rflags & 1ull) == 0);          // CF=0 (~a & c != 0)
    }
    return 0;
}

#ifdef _WIN32
int test_win_veh_roundtrip() {
    // Exercise CONTEXT <-> CpuState (including thread-local YMM upper half) via veh_handler directly.
    const uint8_t vaddps_ymm0[] = {0xC5, 0xF4, 0x58, 0xC2};  // VADDPS YMM0, YMM1, YMM2
    EXCEPTION_RECORD rec{};
    rec.ExceptionCode = EXCEPTION_ILLEGAL_INSTRUCTION;
    rec.ExceptionAddress = const_cast<uint8_t*>(vaddps_ymm0);
    CONTEXT ctx{};
    ctx.Rip = reinterpret_cast<DWORD64>(vaddps_ymm0);
    // Low 128 in CONTEXT Xmm1/Xmm2, high 128 in thread_ymm_hi().
    float lo1[4] = {1, 2, 3, 4}, lo2[4] = {10, 20, 30, 40};
    float hi1[4] = {5, 6, 7, 8}, hi2[4] = {50, 60, 70, 80};
    std::memcpy(&ctx.Xmm1, lo1, 16);
    std::memcpy(&ctx.Xmm2, lo2, 16);
    auto& ymm_hi = rt::cpu_emu::win::thread_ymm_hi();
    std::memcpy(ymm_hi.hi[1], hi1, 16);
    std::memcpy(ymm_hi.hi[2], hi2, 16);
    EXCEPTION_POINTERS ep{&rec, &ctx};
    CHECK(rt::cpu_emu::win::veh_handler(&ep) == EXCEPTION_CONTINUE_EXECUTION);
    CHECK(ctx.Rip == reinterpret_cast<DWORD64>(vaddps_ymm0 + sizeof(vaddps_ymm0)));
    float out_lo[4]{}, out_hi[4]{};
    std::memcpy(out_lo, &ctx.Xmm0, 16);
    std::memcpy(out_hi, ymm_hi.hi[0], 16);
    for (int i = 0; i < 4; ++i) {
        CHECK(out_lo[i] == float(i + 1) * 11.0f);
        CHECK(out_hi[i] == float(i + 5) * 11.0f);
    }
    return 0;
}
#endif

}  // namespace

int main() {
    if (int rc = test_scalar_bmi_popcnt_lzcnt()) return rc;
    if (int rc = test_avx_avx2_fma_f16c()) return rc;
#ifdef _WIN32
    if (int rc = test_win_veh_roundtrip()) return rc;
    const auto f = rt::cpu_emu::detect_host_features();
    (void)f;
#endif
    std::printf("cpu_emu_test: all checks passed\n");
    return 0;
}
