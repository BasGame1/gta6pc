// AcpHal.dll, xapu.dll, chad.dll: console audio coprocessor and DSP shims.
// Also covers xcrdapi.dll (DirectStorage) console import.
//
// Measured from the call sites in game_scarlett_master.exe (RT-5, 1.0.2.1; docs/audio_hal_exe_calls.md):
//   - AcpHalCreate: out parameter at arg 0, flags at arg 1; tested for negative result -> cleanup path.
//   - AcpHalAllocateShapeContexts: out parameter at arg 0; tested for negative result -> cleanup path.
//   - AcpHalReleaseShapeContexts: handle at arg 0; result unused.
//   - ApuCreateHeap: size 0x226e00 at arg 1; tested for negative result.
//   - ApuAlloc: returns pointer; result unused at call site.
//   - ApuFree: tail jump wrapper.
//   - ApuIsVirtualAddressValid: tested byte result (0 = invalid).
//   - ApuMapVirtualAddress: stores 32-bit APU address in record.
//   - ApuMapApuAddress: 32-bit APU address input, tested for null pointer result.
//   - XApuConnect: no arguments loaded, tested for negative result.
//   - XApuDisconnect: clears handle global, returns 0.
//   - XApuEnqueueCommand: handle, command pointer, flags.
//   - XApuDequeueResult: handle and result buffer; tested for negative result.
//   - XApuGetResultCount: returns count.
//   - XDspConnectWithMaximumStreamLimit: arg 3 is 0x80; compared with 0 for bool (0 = success, != 0 = false).
//   - XDspActivate: tested for negative result.
//   - XDspSubmitCommand: two arguments.
//   - XDspDeactivate, XDspDisconnect: tail jumps, return 0.
//   - DStorageGetFactory: arg 0 riid, arg 1 out factory pointer; loaded and tested for null.
#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "shims.h"

namespace {

constexpr HRESULT kNotImpl = static_cast<HRESULT>(rt::shims::kENotImpl);

void warn_once(std::atomic<bool>& warned, const char* dll, const char* what, const void* caller) {
    if (warned.exchange(true, std::memory_order_relaxed)) return;
    std::fprintf(stderr, "[rt] %s: %s: console audio hardware unavailable; returning clean fallback (caller %s, logged once)\n",
                 dll, what, rt::shims::module_offset(reinterpret_cast<uint64_t>(caller)).c_str());
}

// AcpHal.dll exports
int32_t __stdcall AcpHalCreate(void** out_handle, uint32_t /*flags*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "AcpHal", "AcpHalCreate", _ReturnAddress());
    if (out_handle) *out_handle = nullptr;
    return static_cast<int32_t>(kNotImpl);
}

int32_t __stdcall AcpHalAllocateShapeContexts(void** out_ctx, uint32_t /*count*/, void* /*param3*/, void* /*param4*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "AcpHal", "AcpHalAllocateShapeContexts", _ReturnAddress());
    if (out_ctx) *out_ctx = nullptr;
    return static_cast<int32_t>(kNotImpl);
}

int32_t __stdcall AcpHalReleaseShapeContexts(void* /*handle*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "AcpHal", "AcpHalReleaseShapeContexts", _ReturnAddress());
    return 0;
}

int32_t __stdcall ApuCreateHeap(void* /*desc*/, uint32_t /*size*/, void* /*param3*/, void* /*param4*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "AcpHal", "ApuCreateHeap", _ReturnAddress());
    return static_cast<int32_t>(kNotImpl);
}

void* __stdcall ApuAlloc(void* /*heap*/, uint32_t /*flags*/, size_t /*size*/, uint32_t /*alignment*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "AcpHal", "ApuAlloc", _ReturnAddress());
    return nullptr;
}

void __stdcall ApuFree(void* /*ptr*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "AcpHal", "ApuFree", _ReturnAddress());
}

uint8_t __stdcall ApuIsVirtualAddressValid(const void* /*addr*/, size_t /*size*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "AcpHal", "ApuIsVirtualAddressValid", _ReturnAddress());
    return 0;
}

uint32_t __stdcall ApuMapVirtualAddress(const void* /*addr*/, size_t /*size*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "AcpHal", "ApuMapVirtualAddress", _ReturnAddress());
    return 0;
}

void* __stdcall ApuMapApuAddress(uint32_t /*apu_addr*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "AcpHal", "ApuMapApuAddress", _ReturnAddress());
    return nullptr;
}

// xapu.dll exports
int32_t __stdcall XApuConnect(void* /*a*/, void* /*b*/, void* /*c*/, void* /*d*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "xapu", "XApuConnect", _ReturnAddress());
    return static_cast<int32_t>(kNotImpl);
}

int32_t __stdcall XApuDisconnect(void* /*handle*/, uint32_t /*flags*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "xapu", "XApuDisconnect", _ReturnAddress());
    return 0;
}

int32_t __stdcall XApuEnqueueCommand(void* /*handle*/, const void* /*cmd*/, uint32_t /*flags*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "xapu", "XApuEnqueueCommand", _ReturnAddress());
    return 0;
}

int32_t __stdcall XApuDequeueResult(void* /*handle*/, void* /*result*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "xapu", "XApuDequeueResult", _ReturnAddress());
    return static_cast<int32_t>(kNotImpl);
}

uint32_t __stdcall XApuGetResultCount(void* /*handle*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "xapu", "XApuGetResultCount", _ReturnAddress());
    return 0;
}

// chad.dll exports
int32_t __stdcall XDspConnectWithMaximumStreamLimit(void* /*a*/, void* /*b*/, void* /*c*/, uint32_t /*limit*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "chad", "XDspConnectWithMaximumStreamLimit", _ReturnAddress());
    return static_cast<int32_t>(kNotImpl);
}

int32_t __stdcall XDspActivate(void* /*handle*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "chad", "XDspActivate", _ReturnAddress());
    return static_cast<int32_t>(kNotImpl);
}

int32_t __stdcall XDspSubmitCommand(void* /*a*/, void* /*b*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "chad", "XDspSubmitCommand", _ReturnAddress());
    return static_cast<int32_t>(kNotImpl);
}

int32_t __stdcall XDspDeactivate(void* /*handle*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "chad", "XDspDeactivate", _ReturnAddress());
    return 0;
}

int32_t __stdcall XDspDisconnect(void* /*handle*/) {
    static std::atomic<bool> warned{false};
    warn_once(warned, "chad", "XDspDisconnect", _ReturnAddress());
    return 0;
}

// xcrdapi.dll / dstorage_xs.dll DirectStorage export
HRESULT __stdcall DStorageGetFactory(const void* /*riid*/, void** out_factory) {
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr, "[rt] DirectStorage: DStorageGetFactory: returning E_NOTIMPL for fallback (caller %s, logged once)\n",
                     rt::shims::module_offset(reinterpret_cast<uint64_t>(_ReturnAddress())).c_str());
    }
    if (out_factory) *out_factory = nullptr;
    return kNotImpl;
}

}  // namespace

SHIM_TABLE(audio_hal,
    {"acphal.dll", "AcpHalCreate", reinterpret_cast<void*>(&AcpHalCreate)},
    {"acphal.dll", "AcpHalAllocateShapeContexts", reinterpret_cast<void*>(&AcpHalAllocateShapeContexts)},
    {"acphal.dll", "AcpHalReleaseShapeContexts", reinterpret_cast<void*>(&AcpHalReleaseShapeContexts)},
    {"acphal.dll", "ApuCreateHeap", reinterpret_cast<void*>(&ApuCreateHeap)},
    {"acphal.dll", "ApuAlloc", reinterpret_cast<void*>(&ApuAlloc)},
    {"acphal.dll", "ApuFree", reinterpret_cast<void*>(&ApuFree)},
    {"acphal.dll", "ApuIsVirtualAddressValid", reinterpret_cast<void*>(&ApuIsVirtualAddressValid)},
    {"acphal.dll", "ApuMapVirtualAddress", reinterpret_cast<void*>(&ApuMapVirtualAddress)},
    {"acphal.dll", "ApuMapApuAddress", reinterpret_cast<void*>(&ApuMapApuAddress)},
    {"xapu.dll", "XApuConnect", reinterpret_cast<void*>(&XApuConnect)},
    {"xapu.dll", "XApuDisconnect", reinterpret_cast<void*>(&XApuDisconnect)},
    {"xapu.dll", "XApuEnqueueCommand", reinterpret_cast<void*>(&XApuEnqueueCommand)},
    {"xapu.dll", "XApuDequeueResult", reinterpret_cast<void*>(&XApuDequeueResult)},
    {"xapu.dll", "XApuGetResultCount", reinterpret_cast<void*>(&XApuGetResultCount)},
    {"chad.dll", "XDspConnectWithMaximumStreamLimit", reinterpret_cast<void*>(&XDspConnectWithMaximumStreamLimit)},
    {"chad.dll", "XDspActivate", reinterpret_cast<void*>(&XDspActivate)},
    {"chad.dll", "XDspSubmitCommand", reinterpret_cast<void*>(&XDspSubmitCommand)},
    {"chad.dll", "XDspDeactivate", reinterpret_cast<void*>(&XDspDeactivate)},
    {"chad.dll", "XDspDisconnect", reinterpret_cast<void*>(&XDspDisconnect)},
    {"xcrdapi.dll", "DStorageGetFactory", reinterpret_cast<void*>(&DStorageGetFactory)},
    {"dstorage_xs.dll", "DStorageGetFactory", reinterpret_cast<void*>(&DStorageGetFactory)},
    {"dstorage.dll", "DStorageGetFactory", reinterpret_cast<void*>(&DStorageGetFactory)},
);
