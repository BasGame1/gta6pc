// Checks the registered AcpHal.dll, xapu.dll, chad.dll and DirectStorage shims
// against the call shapes measured in the game executable (runtime/shims/audio_hal.cpp).
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "shims.h"

namespace {

using CreateAcpHalFn = int32_t(__stdcall*)(void**, uint32_t);
using AllocShapeFn = int32_t(__stdcall*)(void**, uint32_t, void*, void*);
using ReleaseShapeFn = int32_t(__stdcall*)(void*);
using CreateHeapFn = int32_t(__stdcall*)(void*, uint32_t, void*, void*);
using ApuAllocFn = void*(__stdcall*)(void*, uint32_t, size_t, uint32_t);
using ApuFreeFn = void(__stdcall*)(void*);
using ApuValidFn = uint8_t(__stdcall*)(const void*, size_t);
using ApuMapVaFn = uint32_t(__stdcall*)(const void*, size_t);
using ApuMapApuFn = void*(__stdcall*)(uint32_t);

using XApuConnectFn = int32_t(__stdcall*)(void*, void*, void*, void*);
using XApuDisconnectFn = int32_t(__stdcall*)(void*, uint32_t);
using XApuEnqueueFn = int32_t(__stdcall*)(void*, const void*, uint32_t);
using XApuDequeueFn = int32_t(__stdcall*)(void*, void*);
using XApuCountFn = uint32_t(__stdcall*)(void*);

using XDspConnectFn = int32_t(__stdcall*)(void*, void*, void*, uint32_t);
using XDspActivateFn = int32_t(__stdcall*)(void*);
using XDspSubmitFn = int32_t(__stdcall*)(void*, void*);
using XDspDeactivateFn = int32_t(__stdcall*)(void*);
using XDspDisconnectFn = int32_t(__stdcall*)(void*);

using DStorageGetFactoryFn = HRESULT(__stdcall*)(const void*, void**);

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* what) {
    ++g_checks;
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) ++g_failures;
}

}  // namespace

int main() {
    check(rt::shims::owns_dll("acphal.dll"), "the registry owns acphal.dll");
    check(rt::shims::owns_dll("xapu.dll"), "the registry owns xapu.dll");
    check(rt::shims::owns_dll("chad.dll"), "the registry owns chad.dll");
    check(rt::shims::owns_dll("xcrdapi.dll"), "the registry owns xcrdapi.dll");

    // AcpHal.dll checks
    auto acp_create = reinterpret_cast<CreateAcpHalFn>(rt::shims::find("acphal.dll", "AcpHalCreate"));
    check(acp_create != nullptr, "AcpHalCreate registered");
    if (acp_create) {
        void* handle = reinterpret_cast<void*>(0x12345678);
        int32_t res = acp_create(&handle, 0);
        check(res < 0, "AcpHalCreate returns negative result (jumps to cleanup path)");
        check(handle == nullptr, "AcpHalCreate zeroes out handle");
    }

    auto acp_alloc_shape = reinterpret_cast<AllocShapeFn>(rt::shims::find("acphal.dll", "AcpHalAllocateShapeContexts"));
    check(acp_alloc_shape != nullptr, "AcpHalAllocateShapeContexts registered");
    if (acp_alloc_shape) {
        void* ctx = reinterpret_cast<void*>(0x12345678);
        int32_t res = acp_alloc_shape(&ctx, 1, nullptr, nullptr);
        check(res < 0, "AcpHalAllocateShapeContexts returns negative result");
        check(ctx == nullptr, "AcpHalAllocateShapeContexts zeroes out context");
    }

    auto acp_release_shape = reinterpret_cast<ReleaseShapeFn>(rt::shims::find("acphal.dll", "AcpHalReleaseShapeContexts"));
    check(acp_release_shape != nullptr, "AcpHalReleaseShapeContexts registered");
    if (acp_release_shape) {
        check(acp_release_shape(nullptr) == 0, "AcpHalReleaseShapeContexts returns 0");
    }

    auto apu_create_heap = reinterpret_cast<CreateHeapFn>(rt::shims::find("acphal.dll", "ApuCreateHeap"));
    check(apu_create_heap != nullptr, "ApuCreateHeap registered");
    if (apu_create_heap) {
        check(apu_create_heap(nullptr, 0x226e00, nullptr, nullptr) < 0, "ApuCreateHeap returns negative");
    }

    auto apu_alloc = reinterpret_cast<ApuAllocFn>(rt::shims::find("acphal.dll", "ApuAlloc"));
    check(apu_alloc != nullptr, "ApuAlloc registered");
    if (apu_alloc) {
        check(apu_alloc(nullptr, 0, 1024, 0x800) == nullptr, "ApuAlloc returns nullptr");
    }

    auto apu_free = reinterpret_cast<ApuFreeFn>(rt::shims::find("acphal.dll", "ApuFree"));
    check(apu_free != nullptr, "ApuFree registered");
    if (apu_free) {
        apu_free(nullptr);
    }

    auto apu_valid = reinterpret_cast<ApuValidFn>(rt::shims::find("acphal.dll", "ApuIsVirtualAddressValid"));
    check(apu_valid != nullptr, "ApuIsVirtualAddressValid registered");
    if (apu_valid) {
        check(apu_valid(nullptr, 0x800) == 0, "ApuIsVirtualAddressValid returns 0");
    }

    auto apu_map_va = reinterpret_cast<ApuMapVaFn>(rt::shims::find("acphal.dll", "ApuMapVirtualAddress"));
    check(apu_map_va != nullptr, "ApuMapVirtualAddress registered");
    if (apu_map_va) {
        check(apu_map_va(nullptr, 0x800) == 0, "ApuMapVirtualAddress returns 0");
    }

    auto apu_map_apu = reinterpret_cast<ApuMapApuFn>(rt::shims::find("acphal.dll", "ApuMapApuAddress"));
    check(apu_map_apu != nullptr, "ApuMapApuAddress registered");
    if (apu_map_apu) {
        check(apu_map_apu(0) == nullptr, "ApuMapApuAddress returns nullptr");
    }

    // xapu.dll checks
    auto xapu_connect = reinterpret_cast<XApuConnectFn>(rt::shims::find("xapu.dll", "XApuConnect"));
    check(xapu_connect != nullptr, "XApuConnect registered");
    if (xapu_connect) {
        check(xapu_connect(nullptr, nullptr, nullptr, nullptr) < 0, "XApuConnect returns negative");
    }

    auto xapu_disconnect = reinterpret_cast<XApuDisconnectFn>(rt::shims::find("xapu.dll", "XApuDisconnect"));
    check(xapu_disconnect != nullptr, "XApuDisconnect registered");
    if (xapu_disconnect) {
        check(xapu_disconnect(nullptr, 0x20) == 0, "XApuDisconnect returns 0");
    }

    auto xapu_enqueue = reinterpret_cast<XApuEnqueueFn>(rt::shims::find("xapu.dll", "XApuEnqueueCommand"));
    check(xapu_enqueue != nullptr, "XApuEnqueueCommand registered");
    if (xapu_enqueue) {
        check(xapu_enqueue(nullptr, nullptr, 0) == 0, "XApuEnqueueCommand returns 0");
    }

    auto xapu_dequeue = reinterpret_cast<XApuDequeueFn>(rt::shims::find("xapu.dll", "XApuDequeueResult"));
    check(xapu_dequeue != nullptr, "XApuDequeueResult registered");
    if (xapu_dequeue) {
        check(xapu_dequeue(nullptr, nullptr) < 0, "XApuDequeueResult returns negative");
    }

    auto xapu_count = reinterpret_cast<XApuCountFn>(rt::shims::find("xapu.dll", "XApuGetResultCount"));
    check(xapu_count != nullptr, "XApuGetResultCount registered");
    if (xapu_count) {
        check(xapu_count(nullptr) == 0, "XApuGetResultCount returns 0");
    }

    // chad.dll checks
    auto dsp_connect = reinterpret_cast<XDspConnectFn>(rt::shims::find("chad.dll", "XDspConnectWithMaximumStreamLimit"));
    check(dsp_connect != nullptr, "XDspConnectWithMaximumStreamLimit registered");
    if (dsp_connect) {
        check(dsp_connect(nullptr, nullptr, nullptr, 0x80) != 0, "XDspConnect returns non-zero (bool == false)");
    }

    auto dsp_activate = reinterpret_cast<XDspActivateFn>(rt::shims::find("chad.dll", "XDspActivate"));
    check(dsp_activate != nullptr, "XDspActivate registered");
    if (dsp_activate) {
        check(dsp_activate(nullptr) < 0, "XDspActivate returns negative");
    }

    auto dsp_submit = reinterpret_cast<XDspSubmitFn>(rt::shims::find("chad.dll", "XDspSubmitCommand"));
    check(dsp_submit != nullptr, "XDspSubmitCommand registered");
    if (dsp_submit) {
        check(dsp_submit(nullptr, nullptr) < 0, "XDspSubmitCommand returns negative");
    }

    auto dsp_deactivate = reinterpret_cast<XDspDeactivateFn>(rt::shims::find("chad.dll", "XDspDeactivate"));
    check(dsp_deactivate != nullptr, "XDspDeactivate registered");
    if (dsp_deactivate) {
        check(dsp_deactivate(nullptr) == 0, "XDspDeactivate returns 0");
    }

    auto dsp_disconnect = reinterpret_cast<XDspDisconnectFn>(rt::shims::find("chad.dll", "XDspDisconnect"));
    check(dsp_disconnect != nullptr, "XDspDisconnect registered");
    if (dsp_disconnect) {
        check(dsp_disconnect(nullptr) == 0, "XDspDisconnect returns 0");
    }

    // DirectStorage checks
    auto ds_factory = reinterpret_cast<DStorageGetFactoryFn>(rt::shims::find("xcrdapi.dll", "DStorageGetFactory"));
    check(ds_factory != nullptr, "DStorageGetFactory registered for xcrdapi.dll");
    if (ds_factory) {
        void* factory = nullptr;
        HRESULT hr = ds_factory(nullptr, &factory);
        check(SUCCEEDED(hr) && factory != nullptr, "DStorageGetFactory returns COM factory");
        if (factory) {
            auto** fvt = *reinterpret_cast<void***>(factory);
            auto set_staging = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*, uint32_t)>(fvt[8]);
            check(SUCCEEDED(set_staging(factory, 32u << 20)), "IDStorageFactoryX::SetStagingBufferSize succeeds");

            void* status = nullptr;
            auto create_status =
                reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*, uint32_t, const char*, const void*, void**)>(
                    fvt[5]);
            check(SUCCEEDED(create_status(factory, 4, "test", nullptr, &status)) && status != nullptr,
                  "CreateStatusArray succeeds");

            struct {
                uint64_t source_type;
                uint16_t capacity;
                uint8_t priority;
                uint8_t reserved[5];
                const char* name;
                void* device;
            } qdesc{1, 64, 0, {}, "testq", nullptr};
            void* queue = nullptr;
            auto create_q1 =
                reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*, const void*, const void*, void**)>(fvt[10]);
            check(SUCCEEDED(create_q1(factory, &qdesc, nullptr, &queue)) && queue != nullptr,
                  "CreateQueue1 succeeds");

            if (queue && status) {
                auto** qvt = *reinterpret_cast<void***>(queue);
                auto** svt = *reinterpret_cast<void***>(status);
                const unsigned char src[8] = {'L', 'E', 'O', 'N', 'I', 'D', 'A', '!'};
                unsigned char dst[8] = {};
                struct {
                    uint64_t options;
                    void* destination;
                    uint64_t reserved_dest;
                    uint32_t destination_size;
                    uint32_t reserved_size;
                    const void* source;
                    uint64_t file_offset;
                    uint32_t source_size;
                    uint32_t flags;
                    uint64_t tag;
                    const char* name;
                } req{1ull << 22, dst, 0, 8, 0, src, 0, 8, 0, 0, nullptr};
                HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                reinterpret_cast<void(STDMETHODCALLTYPE*)(void*, const void*)>(qvt[3])(queue, &req);
                reinterpret_cast<void(STDMETHODCALLTYPE*)(void*, void*, uint32_t)>(qvt[4])(queue, status, 0);
                reinterpret_cast<void(STDMETHODCALLTYPE*)(void*, HANDLE)>(qvt[6])(queue, ev);
                reinterpret_cast<void(STDMETHODCALLTYPE*)(void*)>(qvt[7])(queue);
                const BOOLEAN done = reinterpret_cast<BOOLEAN(STDMETHODCALLTYPE*)(void*, uint32_t)>(svt[3])(status, 0);
                const HRESULT shr = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*, uint32_t)>(svt[4])(status, 0);
                check(done == TRUE && shr == S_OK, "DirectStorage status completed S_OK");
                check(std::memcmp(dst, src, 8) == 0, "DirectStorage memory request copied bytes");
                check(WaitForSingleObject(ev, 0) == WAIT_OBJECT_0, "DirectStorage EnqueueSetEvent signalled");
                CloseHandle(ev);
                reinterpret_cast<void(STDMETHODCALLTYPE*)(void*)>(qvt[8])(queue);
                reinterpret_cast<unsigned long(STDMETHODCALLTYPE*)(void*)>(qvt[2])(queue);
                reinterpret_cast<unsigned long(STDMETHODCALLTYPE*)(void*)>(svt[2])(status);
            }
            reinterpret_cast<unsigned long(STDMETHODCALLTYPE*)(void*)>(fvt[2])(factory);
        }
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
