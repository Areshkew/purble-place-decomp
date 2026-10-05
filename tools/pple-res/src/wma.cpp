#include "pple/wma.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>

#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <objbase.h>

// MinGW's headers declare every other MF factory used here but not this one.  The symbol is
// exported by mfplat (`nm libmfplat.a` shows MFCreateMFByteStreamOnStream and its __imp_
// thunk), so declaring it is enough; the SDK prototype is
//   STDAPI MFCreateMFByteStreamOnStream(IStream *pStream, IMFByteStream **ppByteStream);
extern "C" HRESULT WINAPI MFCreateMFByteStreamOnStream(IStream* stream, IMFByteStream** out);

namespace pple {
namespace {

int g_mf_users = 0;
bool g_mf_ready = false;

// A read-only, seekable IStream over a caller-owned buffer.  MFCreateMFByteStreamOnStream turns
// it into the IMFByteStream the source reader wants, which avoids spilling the resource to a
// temporary file.
class MemoryStream final : public IStream {
public:
    MemoryStream(std::span<const std::uint8_t> data) : data_(data) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IStream) ||
            IsEqualIID(riid, IID_ISequentialStream)) {
            *out = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --refs_;
        if (left == 0) delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE Read(void* buffer, ULONG count, ULONG* read) override {
        if (buffer == nullptr) return STG_E_INVALIDPOINTER;
        const std::uint64_t left = data_.size() > pos_ ? data_.size() - pos_ : 0;
        const ULONG take = static_cast<ULONG>(left < count ? left : count);
        if (take != 0) std::memcpy(buffer, data_.data() + pos_, take);
        pos_ += take;
        if (read != nullptr) *read = take;
        return take < count ? S_FALSE : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override {
        return STG_E_ACCESSDENIED;
    }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* new_pos) override {
        std::int64_t base = 0;
        switch (origin) {
            case STREAM_SEEK_SET: base = 0; break;
            case STREAM_SEEK_CUR: base = static_cast<std::int64_t>(pos_); break;
            case STREAM_SEEK_END: base = static_cast<std::int64_t>(data_.size()); break;
            default: return STG_E_INVALIDFUNCTION;
        }
        const std::int64_t target = base + move.QuadPart;
        if (target < 0) return STG_E_INVALIDFUNCTION;
        pos_ = static_cast<std::uint64_t>(target);
        if (new_pos != nullptr) new_pos->QuadPart = pos_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (stat == nullptr) return STG_E_INVALIDPOINTER;
        std::memset(stat, 0, sizeof(*stat));
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = data_.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** out) override {
        if (out == nullptr) return E_POINTER;
        *out = new MemoryStream(data_);
        (*out)->AddRef();
        return S_OK;
    }

private:
    ~MemoryStream() = default;
    std::atomic<ULONG> refs_{1};
    std::span<const std::uint8_t> data_;
    std::uint64_t pos_ = 0;
};

std::string hex(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08lx", static_cast<unsigned long>(hr));
    return buf;
}

}  // namespace

bool mf_start(std::string& error) {
    if (g_mf_ready) {
        ++g_mf_users;
        return true;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE means someone already initialised COM with another model: usable.
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        error = "CoInitializeEx failed " + hex(com);
        return false;
    }
    const HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(mf)) {
        error = "MFStartup failed " + hex(mf);
        return false;
    }
    g_mf_ready = true;
    ++g_mf_users;
    return true;
}

void mf_stop() {
    if (!g_mf_ready) return;
    if (--g_mf_users > 0) return;
    MFShutdown();
    g_mf_ready = false;
}

bool wma_decode(std::span<const std::uint8_t> data, PcmAudio& out, std::string& error) {
    out = PcmAudio{};
    std::string start_error;
    if (!mf_start(start_error)) {
        error = start_error;
        return false;
    }
    const auto done = [&](bool ok) {
        mf_stop();
        return ok;
    };

    IMFSourceReader* reader = nullptr;
    IMFByteStream* bytes = nullptr;
    IMFMediaType* out_type = nullptr;
    IMFMediaType* actual = nullptr;
    auto* stream = new MemoryStream(data);
    HRESULT hr = MFCreateMFByteStreamOnStream(stream, &bytes);
    stream->Release();
    if (FAILED(hr)) {
        error = "MFCreateMFByteStreamOnStream " + hex(hr);
        return done(false);
    }
    // The 3-argument form takes a configuration; passing none keeps the default attributes.
    hr = MFCreateSourceReaderFromByteStream(bytes, nullptr, &reader);
    if (FAILED(hr)) {
        error = "MFCreateSourceReaderFromByteStream " + hex(hr);
        bytes->Release();
        return done(false);
    }
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    hr = MFCreateMediaType(&out_type);
    if (SUCCEEDED(hr)) hr = out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = out_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (SUCCEEDED(hr)) hr = out_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(hr))
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, out_type);
    if (FAILED(hr)) {
        error = "SetCurrentMediaType(PCM) " + hex(hr);
        out_type->Release();
        reader->Release();
        bytes->Release();
        return done(false);
    }
    out_type->Release();

    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual);
    if (FAILED(hr)) {
        error = "GetCurrentMediaType " + hex(hr);
        reader->Release();
        bytes->Release();
        return done(false);
    }
    actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &out.sample_rate);
    actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &out.channels);
    actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &out.bits);
    actual->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, reinterpret_cast<UINT32*>(&out.bytes_per_second));
    actual->Release();
    if (out.bits != 16 || out.channels == 0 || out.sample_rate == 0) {
        error = "decoder produced an unexpected output type";
        reader->Release();
        bytes->Release();
        return done(false);
    }

    for (;;) {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* sample = nullptr;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &timestamp,
                                &sample);
        if (FAILED(hr)) {
            error = "ReadSample " + hex(hr);
            reader->Release();
            bytes->Release();
            return done(false);
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            if (sample != nullptr) sample->Release();
            break;
        }
        if (sample == nullptr) continue;
        IMFMediaBuffer* buffer = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (SUCCEEDED(hr)) {
            BYTE* raw = nullptr;
            DWORD current = 0;
            hr = buffer->Lock(&raw, nullptr, &current);
            if (SUCCEEDED(hr)) {
                const std::size_t count = current / sizeof(std::int16_t);
                const auto* samples = reinterpret_cast<const std::int16_t*>(raw);
                out.samples.insert(out.samples.end(), samples, samples + count);
                buffer->Unlock();
            }
            buffer->Release();
        }
        sample->Release();
    }
    reader->Release();
    bytes->Release();
    out.ok = !out.samples.empty();
    if (!out.ok) error = "decoder produced no samples";
    return done(out.ok);
}

bool wav_write(const std::string& path, const PcmAudio& audio) {
    if (!audio.ok) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(audio.samples.size() * 2);
    const std::uint16_t block_align = static_cast<std::uint16_t>(audio.channels * 2);
    const std::uint32_t byte_rate = audio.sample_rate * block_align;
    const auto u32 = [&](std::uint32_t v) {
        const char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                           static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
        out.write(b, 4);
    };
    const auto u16 = [&](std::uint16_t v) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
        out.write(b, 2);
    };
    out.write("RIFF", 4);
    u32(36 + data_bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);  // PCM
    u16(static_cast<std::uint16_t>(audio.channels));
    u32(audio.sample_rate);
    u32(byte_rate);
    u16(block_align);
    u16(16);
    out.write("data", 4);
    u32(data_bytes);
    out.write(reinterpret_cast<const char*>(audio.samples.data()),
              static_cast<std::streamsize>(data_bytes));
    return out.good();
}

}  // namespace pple
