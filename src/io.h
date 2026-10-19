#pragma once
#include "common.h"
namespace ttp::vqf {
inline constexpr GUID stream_read_id{
    0x83e2bbbf, 0x4fec, 0x4e00, {0xa6, 0x2c, 0x62, 0x61, 0xfe, 0x2b, 0xa2, 0xd4}};
inline constexpr GUID stream_callback_id{
    0x1717a4a7, 0xa3dc, 0x416b, {0xa2, 0x97, 0x72, 0x16, 0x74, 0x9b, 0xd7, 0xbf}};
struct Input {
    ComPtr<IStream> stream;
    ComPtr<IUnknown> extended;
    std::wstring path;
    DWORD mode{};
    bool seekable{}, remote{};
    uint64_t size{}, base{};
    HANDLE event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ~Input() {
        if (event)
            CloseHandle(event);
    }
    void open(IStream *value) {
        require(value, E_POINTER);
        require(event, E_OUTOFMEMORY);
        stream.reset(value);
        value->AddRef();
        value->QueryInterface(stream_read_id, reinterpret_cast<void **>(extended.put()));
        STATSTG st{};
        if (SUCCEEDED(value->Stat(&st, 0))) {
            mode = st.grfMode;
            size = st.cbSize.QuadPart;
            if (st.pwcsName)
                path = st.pwcsName;
            CoTaskMemFree(st.pwcsName);
        }
        remote = path.find(L"://") != std::wstring::npos;
        LARGE_INTEGER z{};
        ULARGE_INTEGER pos{};
        seekable = SUCCEEDED(value->Seek(z, STREAM_SEEK_CUR, &pos));
        base = pos.QuadPart;
    }
    ULONG read(void *p, ULONG n) {
        ULONG got{};
        HRESULT hr;
        if (extended) {
            using Fn =
                HRESULT(STDMETHODCALLTYPE *)(IUnknown *, void *, ULONG, ULONG *, HANDLE, DWORD);
            auto **v = *reinterpret_cast<void ***>(extended.p);
            hr = reinterpret_cast<Fn>(v[14])(extended.p, p, n, &got, event, 60000);
        } else
            hr = stream->Read(p, n, &got);
        check(hr);
        require(got <= n, STG_E_READFAULT);
        return got;
    }
    void exact(void *p, DWORD n) {
        auto *b = static_cast<BYTE *>(p);
        while (n) {
            auto got = read(b, n);
            require(got, STG_E_READFAULT);
            b += got;
            n -= got;
        }
    }
    void at(uint64_t pos) {
        require(seekable, STG_E_INVALIDFUNCTION);
        seek(stream.p, pos);
    }
    DWORD capabilities(bool cover) const {
        DWORD bits = (remote ? 1u : 0u) | (seekable ? 2u : 0u);
        if (!remote && seekable && !path.empty()) {
            DWORD a = GetFileAttributesW(path.c_str());
            if (a != INVALID_FILE_ATTRIBUTES &&
                !(a & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)))
                bits |= 4 | (cover ? 16 : 0);
        }
        return bits;
    }
    HRESULT callback(IUnknown *p) {
        ComPtr<IUnknown> c;
        auto hr = stream->QueryInterface(stream_callback_id, reinterpret_cast<void **>(c.put()));
        if (FAILED(hr))
            return S_OK;
        using Fn = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
        auto **v = *reinterpret_cast<void ***>(c.p);
        return reinterpret_cast<Fn>(v[4])(c.p, p);
    }
    // Native streams may carry a leading ID3v2 tag even without host helpers.
    void signature(const char *expected) {
        require(seekable, STG_E_INVALIDFUNCTION);
        base = tell(stream.p);
        BYTE b[10]{};
        exact(b, 4);
        if (!memcmp(b, "ID3", 3)) {
            exact(b + 4, 6);
            require(!(b[6] & 128) && !(b[7] & 128) && !(b[8] & 128) && !(b[9] & 128));
            uint64_t length =
                (uint32_t(b[6]) << 21) | (uint32_t(b[7]) << 14) | (uint32_t(b[8]) << 7) | b[9];
            require(length <= 16 * 1024 * 1024);
            base += 10 + length + ((b[3] == 4 && (b[5] & 16)) ? 10 : 0);
            at(base);
            exact(b, 4);
        }
        require(!memcmp(b, expected, 4), E_FAIL);
        at(base);
    }
};
inline uint16_t le16(const BYTE *p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t le32(const BYTE *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void put_le32(Bytes &b, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        b.push_back(BYTE(n >> (8 * i)));
}
} // namespace ttp::vqf
