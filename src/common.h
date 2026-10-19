#pragma once
#include "ttp_vqf_abi.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttp::vqf {
using Bytes = std::vector<BYTE>;
struct Failure {
    HRESULT code;
};
inline void require(bool condition, HRESULT hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA)) {
    if (!condition)
        throw Failure{hr};
}
inline void check(HRESULT hr) {
    if (FAILED(hr))
        throw Failure{hr};
}
inline void commit_stream(IStream *stream) {
    // Legacy direct file streams write immediately and do not implement the
    // optional transaction/flush method. Real I/O failures must still propagate.
    const HRESULT hr = stream->Commit(STGC_DEFAULT);
    if (hr != E_NOTIMPL && hr != STG_E_INVALIDFUNCTION)
        check(hr);
}
template <class F> HRESULT protect(F &&f) noexcept {
    try {
        return f();
    } catch (const Failure &e) {
        return e.code;
    } catch (const std::bad_alloc &) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}
template <class T> struct ComPtr {
    T *p{};
    ComPtr() = default;
    explicit ComPtr(T *value, bool retain = false) : p(value) {
        if (p && retain)
            p->AddRef();
    }
    ~ComPtr() {
        if (p)
            p->Release();
    }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    ComPtr(ComPtr &&other) noexcept : p(std::exchange(other.p, nullptr)) {}
    ComPtr &operator=(ComPtr &&other) noexcept {
        if (this != &other) {
            reset();
            p = std::exchange(other.p, nullptr);
        }
        return *this;
    }
    void reset(T *value = nullptr) {
        if (p)
            p->Release();
        p = value;
    }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    T **put() {
        reset();
        return &p;
    }
};
inline HRESULT text(std::wstring_view s, wchar_t **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    auto *p = static_cast<wchar_t *>(CoTaskMemAlloc((s.size() + 1) * sizeof(wchar_t)));
    if (!p)
        return E_OUTOFMEMORY;
    std::memcpy(p, s.data(), s.size() * sizeof(wchar_t));
    p[s.size()] = L'\0';
    *out = p;
    return S_OK;
}
inline std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    require(s.size() <= INT_MAX, E_INVALIDARG);
    const UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(cp, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    if (n)
        MultiByteToWideChar(cp, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}
inline std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    require(s.size() <= INT_MAX, E_INVALIDARG);
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    if (n)
        WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
inline DWORD dword(std::uint64_t x) { return DWORD(std::min<std::uint64_t>(x, MAXDWORD)); }
inline DWORD milliseconds(std::uint64_t ticks, DWORD scale) {
    require(scale != 0);
    auto seconds = ticks / scale;
    if (seconds > MAXDWORD / 1000)
        return MAXDWORD;
    return dword(seconds * 1000 + (ticks % scale) * 1000 / scale);
}
inline std::uint64_t tell(IStream *s) {
    ULARGE_INTEGER p{};
    LARGE_INTEGER z{};
    check(s->Seek(z, STREAM_SEEK_CUR, &p));
    return p.QuadPart;
}
inline void seek(IStream *s, std::uint64_t p) {
    require(p <= INT64_MAX);
    LARGE_INTEGER x{};
    x.QuadPart = static_cast<LONGLONG>(p);
    check(s->Seek(x, STREAM_SEEK_SET, nullptr));
}
inline std::uint64_t stream_size(IStream *s) {
    STATSTG st{};
    check(s->Stat(&st, STATFLAG_NONAME));
    return st.cbSize.QuadPart;
}
inline void read_exact(IStream *s, void *p, DWORD n) {
    auto *out = static_cast<BYTE *>(p);
    while (n) {
        ULONG got{};
        check(s->Read(out, n, &got));
        require(got && got <= n, STG_E_READFAULT);
        out += got;
        n -= got;
    }
}
inline void write_exact(IStream *s, const void *p, DWORD n) {
    ULONG got{};
    check(s->Write(p, n, &got));
    require(got == n, STG_E_WRITEFAULT);
}
inline Bytes read_at(IStream *s, std::uint64_t at, std::size_t n) {
    require(n <= 64 * 1024 * 1024);
    Bytes b(n);
    seek(s, at);
    if (n)
        read_exact(s, b.data(), DWORD(n));
    return b;
}
inline bool same(REFGUID a, REFGUID b) { return !!InlineIsEqualGUID(a, b); }
inline WORD be16(const BYTE *p) { return WORD((p[0] << 8) | p[1]); }
inline DWORD be32(const BYTE *p) {
    return (DWORD(p[0]) << 24) | (DWORD(p[1]) << 16) | (DWORD(p[2]) << 8) | p[3];
}
inline std::uint64_t be64(const BYTE *p) { return (std::uint64_t(be32(p)) << 32) | be32(p + 4); }
inline void put32(Bytes &b, DWORD n) {
    b.push_back(BYTE(n >> 24));
    b.push_back(BYTE(n >> 16));
    b.push_back(BYTE(n >> 8));
    b.push_back(BYTE(n));
}
inline void set32(Bytes &b, std::size_t p, DWORD n) {
    require(p <= b.size() && b.size() - p >= 4);
    for (int i = 0; i < 4; ++i)
        b[p + i] = BYTE(n >> (24 - 8 * i));
}
constexpr DWORD fourcc(char a, char b, char c, char d) {
    return (DWORD(BYTE(a)) << 24) | (DWORD(BYTE(b)) << 16) | (DWORD(BYTE(c)) << 8) | BYTE(d);
}
extern HMODULE module;
using StandardContent = HRESULT(WINAPI *)(IStream *, DWORD, Metadata **, ULONGLONG *);
StandardContent standard_content();
HRESULT make_reader(void **out);
} // namespace ttp::vqf
