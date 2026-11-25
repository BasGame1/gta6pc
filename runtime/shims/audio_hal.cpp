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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <span>
#include <vector>

#include "../src/shader_library.h"
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

// xcrdapi.dll / dstorage_xs.dll / dstorage.dll DirectStorage COM shim (IDStorageFactoryX2, QueueX2, StatusArrayX, FileX)
struct DStorageFileImpl;
struct DStorageStatusArrayImpl;

struct DStorageRequestX {
    uint64_t options;
    void* destination;
    uint64_t reserved_dest;
    uint32_t destination_size;
    uint32_t reserved_size;
    void* source;  // IDStorageFileImpl* when SourceType==0 (File), const void* when SourceType==1 (Memory)
    uint64_t file_offset;
    uint32_t source_size;
    uint32_t flags;
    uint64_t cancellation_tag;
    const char* name;
};
static_assert(offsetof(DStorageRequestX, destination) == 0x08);
static_assert(offsetof(DStorageRequestX, destination_size) == 0x18);
static_assert(offsetof(DStorageRequestX, source) == 0x20);
static_assert(offsetof(DStorageRequestX, file_offset) == 0x28);
static_assert(offsetof(DStorageRequestX, source_size) == 0x30);

struct DStorageQueueDescX {
    uint64_t source_type;
    uint16_t capacity;
    uint8_t priority;
    uint8_t reserved[5];
    const char* name;
    void* device;
};

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

HANDLE open_console_read_file(const wchar_t* path) {
    if (!path || !*path) return INVALID_HANDLE_VALUE;
    if (void* p = rt::shims::find("api-ms-win-core-file-l1-1-0.dll", "CreateFileW")) {
        auto fn = reinterpret_cast<CreateFileWFn>(p);
        return fn(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                  nullptr);
    }
    return CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

struct DStorageFileImpl {
    const void* const* vtbl;
    std::atomic<uint32_t> refs{1};
    std::mutex mu;
    HANDLE handle = INVALID_HANDLE_VALUE;

    static HRESULT STDMETHODCALLTYPE qi(DStorageFileImpl* self, REFIID /*riid*/, void** ppv) {
        if (!ppv) return E_POINTER;
        self->refs.fetch_add(1, std::memory_order_relaxed);
        *ppv = self;
        return S_OK;
    }
    static ULONG STDMETHODCALLTYPE add_ref(DStorageFileImpl* self) {
        return self->refs.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    static ULONG STDMETHODCALLTYPE release(DStorageFileImpl* self) {
        const uint32_t r = self->refs.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (r == 0) {
            if (self->handle != INVALID_HANDLE_VALUE) CloseHandle(self->handle);
            delete self;
        }
        return r;
    }
    static void STDMETHODCALLTYPE close(DStorageFileImpl* self) {
        std::lock_guard lock(self->mu);
        if (self->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(self->handle);
            self->handle = INVALID_HANDLE_VALUE;
        }
    }
    static HRESULT STDMETHODCALLTYPE get_file_info(DStorageFileImpl* self, BY_HANDLE_FILE_INFORMATION* info) {
        if (!info) return E_POINTER;
        std::lock_guard lock(self->mu);
        if (self->handle == INVALID_HANDLE_VALUE) return E_HANDLE;
        return GetFileInformationByHandle(self->handle, info) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    static HANDLE STDMETHODCALLTYPE get_handle(DStorageFileImpl* self) {
        std::lock_guard lock(self->mu);
        return self->handle;
    }
};

const void* const kFileVtbl[] = {
    reinterpret_cast<const void*>(&DStorageFileImpl::qi),
    reinterpret_cast<const void*>(&DStorageFileImpl::add_ref),
    reinterpret_cast<const void*>(&DStorageFileImpl::release),
    reinterpret_cast<const void*>(&DStorageFileImpl::close),
    reinterpret_cast<const void*>(&DStorageFileImpl::get_file_info),
    reinterpret_cast<const void*>(&DStorageFileImpl::get_handle),
};

struct DStorageStatusArrayImpl {
    const void* const* vtbl;
    std::atomic<uint32_t> refs{1};
    std::mutex mu;
    std::vector<uint8_t> complete;
    std::vector<HRESULT> hrs;

    static HRESULT STDMETHODCALLTYPE qi(DStorageStatusArrayImpl* self, REFIID /*riid*/, void** ppv) {
        if (!ppv) return E_POINTER;
        self->refs.fetch_add(1, std::memory_order_relaxed);
        *ppv = self;
        return S_OK;
    }
    static ULONG STDMETHODCALLTYPE add_ref(DStorageStatusArrayImpl* self) {
        return self->refs.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    static ULONG STDMETHODCALLTYPE release(DStorageStatusArrayImpl* self) {
        const uint32_t r = self->refs.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (r == 0) delete self;
        return r;
    }
    static BOOLEAN STDMETHODCALLTYPE is_complete(DStorageStatusArrayImpl* self, uint32_t index) {
        std::lock_guard lock(self->mu);
        if (index >= self->complete.size()) return TRUE;
        return self->complete[index] ? TRUE : FALSE;
    }
    static HRESULT STDMETHODCALLTYPE get_hresult(DStorageStatusArrayImpl* self, uint32_t index) {
        std::lock_guard lock(self->mu);
        if (index >= self->hrs.size()) return E_INVALIDARG;
        return self->hrs[index];
    }
    void mark_enqueued(uint32_t index) {
        std::lock_guard lock(mu);
        if (index < complete.size()) {
            complete[index] = 0;
            hrs[index] = E_PENDING;
        }
    }
    void set_result(uint32_t index, HRESULT hr) {
        std::lock_guard lock(mu);
        if (index < complete.size()) {
            complete[index] = 1;
            hrs[index] = hr;
        }
    }
};

const void* const kStatusArrayVtbl[] = {
    reinterpret_cast<const void*>(&DStorageStatusArrayImpl::qi),
    reinterpret_cast<const void*>(&DStorageStatusArrayImpl::add_ref),
    reinterpret_cast<const void*>(&DStorageStatusArrayImpl::release),
    reinterpret_cast<const void*>(&DStorageStatusArrayImpl::is_complete),
    reinterpret_cast<const void*>(&DStorageStatusArrayImpl::get_hresult),
};

HRESULT execute_dstorage_request(const DStorageRequestX& req) {
    if (!req.destination && req.destination_size > 0) return E_POINTER;
    if ((req.options >> 21) & 1u) {
        std::fprintf(stderr, "[rt] DirectStorage: DestinationIsPhysicalPages unsupported (tag=0x%llx)\n",
                     static_cast<unsigned long long>(req.cancellation_tag));
        return kNotImpl;
    }
    const uint32_t bcpack = static_cast<uint32_t>((req.options >> 1) & 0xFu);
    if (bcpack != 0) {
        std::fprintf(stderr, "[rt] DirectStorage: BcpackMode=%u unsupported (size=%u)\n", bcpack, req.source_size);
        return kNotImpl;
    }

    const bool from_memory = ((req.options >> 22) & 1u) != 0;
    const bool zlib = (req.options & 1u) != 0;
    std::vector<uint8_t> scratch;
    const uint8_t* src_ptr = nullptr;
    uint32_t src_size = req.source_size;

    if (from_memory) {
        src_ptr = static_cast<const uint8_t*>(req.source);
        if (!src_ptr && src_size > 0) return E_POINTER;
    } else {
        auto* file = static_cast<DStorageFileImpl*>(req.source);
        if (!file) return E_INVALIDARG;
        scratch.resize(src_size);
        if (src_size > 0) {
            std::lock_guard lock(file->mu);
            if (file->handle == INVALID_HANDLE_VALUE) return E_HANDLE;
            LARGE_INTEGER li{};
            li.QuadPart = static_cast<LONGLONG>(req.file_offset);
            if (!SetFilePointerEx(file->handle, li, nullptr, FILE_BEGIN)) return HRESULT_FROM_WIN32(GetLastError());
            uint32_t total = 0;
            while (total < src_size) {
                DWORD got = 0;
                if (!ReadFile(file->handle, scratch.data() + total, src_size - total, &got, nullptr) || got == 0) {
                    return got == 0 ? HRESULT_FROM_WIN32(ERROR_HANDLE_EOF) : HRESULT_FROM_WIN32(GetLastError());
                }
                total += got;
            }
        }
        src_ptr = scratch.data();
    }

    if (zlib) {
        std::vector<uint8_t> inflated;
        if (!leonida::zlib_inflate(std::span<const uint8_t>(src_ptr, src_size), inflated, req.destination_size)) {
            std::fprintf(stderr, "[rt] DirectStorage: zlib_inflate failed (src=%u, dst=%u)\n", src_size,
                         req.destination_size);
            return E_FAIL;
        }
        const size_t copy_n = (std::min)(inflated.size(), static_cast<size_t>(req.destination_size));
        if (copy_n > 0) std::memcpy(req.destination, inflated.data(), copy_n);
    } else {
        const size_t copy_n = (std::min)(static_cast<size_t>(src_size), static_cast<size_t>(req.destination_size));
        if (copy_n > 0) std::memcpy(req.destination, src_ptr, copy_n);
    }
    return S_OK;
}

struct DStorageQueueImpl {
    const void* const* vtbl;
    std::atomic<uint32_t> refs{1};
    std::mutex mu;
    HANDLE error_event = nullptr;
    HRESULT batch_hr = S_OK;

    struct Item {
        enum Kind { Request, Status, Signal, Event } kind;
        DStorageRequestX req{};
        DStorageFileImpl* held_file = nullptr;
        DStorageStatusArrayImpl* status = nullptr;
        uint32_t status_index = 0;
        void* fence = nullptr;
        uint64_t fence_val = 0;
        HANDLE event_handle = nullptr;
    };
    std::vector<Item> pending;
    uint16_t capacity = 256;

    void flush_locked() {
        for (Item& it : pending) {
            if (it.kind == Item::Request) {
                const HRESULT hr = execute_dstorage_request(it.req);
                if (FAILED(hr) && SUCCEEDED(batch_hr)) batch_hr = hr;
                if (it.held_file) DStorageFileImpl::release(it.held_file);
            } else if (it.kind == Item::Status) {
                it.status->set_result(it.status_index, batch_hr);
                DStorageStatusArrayImpl::release(it.status);
                batch_hr = S_OK;
            } else if (it.kind == Item::Signal) {
                if (it.fence) {
                    auto** v = *reinterpret_cast<void***>(it.fence);
                    using SignalFn = HRESULT(STDMETHODCALLTYPE*)(void*, uint64_t);
                    reinterpret_cast<SignalFn>(v[10])(it.fence, it.fence_val);
                    using RelFn = ULONG(STDMETHODCALLTYPE*)(void*);
                    reinterpret_cast<RelFn>(v[2])(it.fence);
                }
            } else if (it.kind == Item::Event) {
                if (it.event_handle) SetEvent(it.event_handle);
            }
        }
        pending.clear();
    }

    static HRESULT STDMETHODCALLTYPE qi(DStorageQueueImpl* self, REFIID /*riid*/, void** ppv) {
        if (!ppv) return E_POINTER;
        self->refs.fetch_add(1, std::memory_order_relaxed);
        *ppv = self;
        return S_OK;
    }
    static ULONG STDMETHODCALLTYPE add_ref(DStorageQueueImpl* self) {
        return self->refs.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    static ULONG STDMETHODCALLTYPE release(DStorageQueueImpl* self) {
        const uint32_t r = self->refs.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (r == 0) {
            {
                std::lock_guard lock(self->mu);
                self->flush_locked();
            }
            if (self->error_event) CloseHandle(self->error_event);
            delete self;
        }
        return r;
    }
    static void STDMETHODCALLTYPE enqueue_request(DStorageQueueImpl* self, const DStorageRequestX* req) {
        if (!req) return;
        std::lock_guard lock(self->mu);
        Item it{};
        it.kind = Item::Request;
        it.req = *req;
        const bool from_memory = ((req->options >> 22) & 1u) != 0;
        if (!from_memory && req->source) {
            it.held_file = static_cast<DStorageFileImpl*>(req->source);
            DStorageFileImpl::add_ref(it.held_file);
        }
        self->pending.push_back(it);
        if (self->pending.size() >= (std::max)(size_t{1}, static_cast<size_t>(self->capacity) / 2))
            self->flush_locked();
    }
    static void STDMETHODCALLTYPE enqueue_status(DStorageQueueImpl* self, DStorageStatusArrayImpl* status, uint32_t idx) {
        if (!status) return;
        status->mark_enqueued(idx);
        DStorageStatusArrayImpl::add_ref(status);
        std::lock_guard lock(self->mu);
        Item it{};
        it.kind = Item::Status;
        it.status = status;
        it.status_index = idx;
        self->pending.push_back(it);
        if (self->pending.size() >= (std::max)(size_t{1}, static_cast<size_t>(self->capacity) / 2))
            self->flush_locked();
    }
    static void STDMETHODCALLTYPE enqueue_signal(DStorageQueueImpl* self, void* fence, uint64_t value) {
        if (fence) {
            auto** v = *reinterpret_cast<void***>(fence);
            using AddRefFn = ULONG(STDMETHODCALLTYPE*)(void*);
            reinterpret_cast<AddRefFn>(v[1])(fence);
        }
        std::lock_guard lock(self->mu);
        Item it{};
        it.kind = Item::Signal;
        it.fence = fence;
        it.fence_val = value;
        self->pending.push_back(it);
    }
    static void STDMETHODCALLTYPE enqueue_set_event(DStorageQueueImpl* self, HANDLE handle) {
        std::lock_guard lock(self->mu);
        Item it{};
        it.kind = Item::Event;
        it.event_handle = handle;
        self->pending.push_back(it);
    }
    static void STDMETHODCALLTYPE submit(DStorageQueueImpl* self) {
        std::lock_guard lock(self->mu);
        self->flush_locked();
    }
    static void STDMETHODCALLTYPE close(DStorageQueueImpl* self) {
        std::lock_guard lock(self->mu);
        self->flush_locked();
    }
    static void STDMETHODCALLTYPE cancel_tag(DStorageQueueImpl* /*self*/, uint64_t /*mask*/, uint64_t /*val*/) {}
    static HANDLE STDMETHODCALLTYPE get_error_event(DStorageQueueImpl* self) {
        std::lock_guard lock(self->mu);
        if (!self->error_event) self->error_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return self->error_event;
    }
    static void STDMETHODCALLTYPE retrieve_error(DStorageQueueImpl* /*self*/, void* record) {
        if (record) std::memset(record, 0, 128);
    }
    static void STDMETHODCALLTYPE query(DStorageQueueImpl* self, void* info) {
        if (!info) return;
        std::memset(info, 0, 48);
        std::memcpy(info, &self->capacity, 2);
    }
};

const void* const kQueueVtbl[] = {
    reinterpret_cast<const void*>(&DStorageQueueImpl::qi),
    reinterpret_cast<const void*>(&DStorageQueueImpl::add_ref),
    reinterpret_cast<const void*>(&DStorageQueueImpl::release),
    reinterpret_cast<const void*>(&DStorageQueueImpl::enqueue_request),
    reinterpret_cast<const void*>(&DStorageQueueImpl::enqueue_status),
    reinterpret_cast<const void*>(&DStorageQueueImpl::enqueue_signal),
    reinterpret_cast<const void*>(&DStorageQueueImpl::enqueue_set_event),
    reinterpret_cast<const void*>(&DStorageQueueImpl::submit),
    reinterpret_cast<const void*>(&DStorageQueueImpl::close),
    reinterpret_cast<const void*>(&DStorageQueueImpl::cancel_tag),
    reinterpret_cast<const void*>(&DStorageQueueImpl::get_error_event),
    reinterpret_cast<const void*>(&DStorageQueueImpl::retrieve_error),
    reinterpret_cast<const void*>(&DStorageQueueImpl::query),
    reinterpret_cast<const void*>(&DStorageQueueImpl::query),
};

struct DStorageFactoryImpl {
    const void* const* vtbl;
    std::atomic<uint32_t> refs{1};
    uint32_t staging_bytes = 32u * 1024u * 1024u;

    static HRESULT STDMETHODCALLTYPE qi(DStorageFactoryImpl* self, REFIID /*riid*/, void** ppv) {
        if (!ppv) return E_POINTER;
        self->refs.fetch_add(1, std::memory_order_relaxed);
        *ppv = self;
        return S_OK;
    }
    static ULONG STDMETHODCALLTYPE add_ref(DStorageFactoryImpl* self) {
        return self->refs.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    static ULONG STDMETHODCALLTYPE release(DStorageFactoryImpl* self) {
        return self->refs.fetch_sub(1, std::memory_order_acq_rel) - 1;  // singleton lifetime
    }
    static HRESULT STDMETHODCALLTYPE create_queue(DStorageFactoryImpl* /*self*/, const DStorageQueueDescX* desc,
                                                  REFIID /*riid*/, void** ppv) {
        if (!ppv) return E_POINTER;
        auto* q = new DStorageQueueImpl{};
        q->vtbl = kQueueVtbl;
        if (desc && desc->capacity) q->capacity = desc->capacity;
        *ppv = q;
        return S_OK;
    }
    static HRESULT STDMETHODCALLTYPE open_file(DStorageFactoryImpl* /*self*/, const wchar_t* path, REFIID /*riid*/,
                                               void** ppv) {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        HANDLE h = open_console_read_file(path);
        if (h == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
        auto* f = new DStorageFileImpl{};
        f->vtbl = kFileVtbl;
        f->handle = h;
        *ppv = f;
        return S_OK;
    }
    static HRESULT STDMETHODCALLTYPE create_status_array(DStorageFactoryImpl* /*self*/, uint32_t capacity,
                                                         const char* /*name*/, REFIID /*riid*/, void** ppv) {
        if (!ppv) return E_POINTER;
        auto* s = new DStorageStatusArrayImpl{};
        s->vtbl = kStatusArrayVtbl;
        s->complete.assign(capacity, 1);
        s->hrs.assign(capacity, S_OK);
        *ppv = s;
        return S_OK;
    }
    static HRESULT STDMETHODCALLTYPE set_cpu_affinity(DStorageFactoryImpl* /*self*/, uint64_t /*affinity*/) {
        return S_OK;
    }
    static void STDMETHODCALLTYPE set_debug_flags(DStorageFactoryImpl* /*self*/, uint32_t /*flags*/) {}
    static HRESULT STDMETHODCALLTYPE set_staging_buffer_size(DStorageFactoryImpl* self, uint32_t size) {
        self->staging_bytes = size;
        return S_OK;
    }
    static HRESULT STDMETHODCALLTYPE get_stats(DStorageFactoryImpl* /*self*/, void* stats) {
        if (stats) std::memset(stats, 0, 64);
        return S_OK;
    }
    static HRESULT STDMETHODCALLTYPE create_queue1(DStorageFactoryImpl* self, const DStorageQueueDescX* desc,
                                                   REFIID riid, void** ppv) {
        return create_queue(self, desc, riid, ppv);
    }
};

const void* const kFactoryVtbl[] = {
    reinterpret_cast<const void*>(&DStorageFactoryImpl::qi),
    reinterpret_cast<const void*>(&DStorageFactoryImpl::add_ref),
    reinterpret_cast<const void*>(&DStorageFactoryImpl::release),
    reinterpret_cast<const void*>(&DStorageFactoryImpl::create_queue),
    reinterpret_cast<const void*>(&DStorageFactoryImpl::open_file),
    reinterpret_cast<const void*>(&DStorageFactoryImpl::create_status_array),
    reinterpret_cast<const void*>(&DStorageFactoryImpl::set_cpu_affinity),
    reinterpret_cast<const void*>(&DStorageFactoryImpl::set_debug_flags),
    reinterpret_cast<const void*>(&DStorageFactoryImpl::set_staging_buffer_size),
    reinterpret_cast<const void*>(&DStorageFactoryImpl::get_stats),
    reinterpret_cast<const void*>(&DStorageFactoryImpl::create_queue1),
};

DStorageFactoryImpl* factory_singleton() {
    static DStorageFactoryImpl* f = [] {
        auto* p = new DStorageFactoryImpl{};
        p->vtbl = kFactoryVtbl;
        return p;
    }();
    return f;
}

// xcrdapi.dll / dstorage_xs.dll / dstorage.dll DirectStorage export
HRESULT __stdcall DStorageGetFactory(const void* /*riid*/, void** out_factory) {
    if (!out_factory) return E_POINTER;
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr, "[rt] DirectStorage: DStorageGetFactory -> COM shim (caller %s)\n",
                     rt::shims::module_offset(reinterpret_cast<uint64_t>(_ReturnAddress())).c_str());
    }
    auto* f = factory_singleton();
    DStorageFactoryImpl::add_ref(f);
    *out_factory = f;
    return S_OK;
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
