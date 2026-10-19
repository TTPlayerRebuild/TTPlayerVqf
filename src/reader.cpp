#include "ttpcomm/audio_format_probe_impl.h"
#include "io.h"
#include <cmath>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
}
namespace ttp::vqf {
namespace {
constexpr bool twin_vq=true;
struct Tag { std::string key; std::wstring value; };
class AudioReader final : public Reader, public Metadata, public ttp::format::InfoSupport {
    LONG refs_{1}; Input input_;
    AVFormatContext *container_{}; AVIOContext *io_{};
    AVCodecContext *decoder_{}; AVPacket *packet_{av_packet_alloc()}; AVFrame *frame_{av_frame_alloc()};
    int stream_index_{-1}; WAVEFORMATEX format_{};
    std::vector<Tag> tags_; Bytes pcm_; size_t consumed_{};
    uint64_t position_{}; DWORD duration_{},bitrate_{};
    HRESULT io_error_{S_OK}; bool opened_{},draining_{},eof_{}; LONG stopped_{};
    std::wstring codec_name_;bool primed_{};
    static int read_callback(void *opaque,uint8_t *data,int bytes) noexcept {
        auto &s=*static_cast<AudioReader*>(opaque);ULONG got{};
        HRESULT hr=protect([&]()->HRESULT{
            require(bytes>0,E_INVALIDARG);require(!InterlockedCompareExchange(&s.stopped_,0,0),HRESULT_FROM_WIN32(ERROR_CANCELLED));
            got=s.input_.read(data,DWORD(bytes));return S_OK;
        });
        if(FAILED(hr)){s.io_error_=hr;return AVERROR(EIO);}return got?int(got):AVERROR_EOF;
    }
    static int64_t seek_callback(void *opaque,int64_t offset,int whence) noexcept {
        auto &s=*static_cast<AudioReader*>(opaque);int64_t result=-1;
        HRESULT hr=protect([&]()->HRESULT{
            if(whence==AVSEEK_SIZE){require(s.input_.size>=s.input_.base);result=int64_t(s.input_.size-s.input_.base);return result?S_OK:E_NOTIMPL;}
            require(s.input_.seekable,STG_E_INVALIDFUNCTION);
            LARGE_INTEGER distance{};distance.QuadPart=offset;ULARGE_INTEGER pos{};
            if((whence&~AVSEEK_FORCE)==SEEK_SET){require(offset>=0&&uint64_t(offset)<=uint64_t(INT64_MAX)-s.input_.base);distance.QuadPart+=s.input_.base;}
            check(s.input_.stream->Seek(distance,DWORD(whence&~AVSEEK_FORCE),&pos));
            require(pos.QuadPart>=s.input_.base&&pos.QuadPart-s.input_.base<=INT64_MAX);result=int64_t(pos.QuadPart-s.input_.base);return S_OK;
        });
        if(FAILED(hr)){if(hr!=E_NOTIMPL)s.io_error_=hr;return AVERROR(EIO);}return result;
    }
    void check_av(int result){
        check(io_error_);
        if(result<0)throw Failure{result==AVERROR(ENOMEM)?E_OUTOFMEMORY:HRESULT_FROM_WIN32(ERROR_INVALID_DATA)};
    }
    void close_codec(){avcodec_free_context(&decoder_);av_packet_unref(packet_);av_frame_unref(frame_);}
    void close_container(){
        close_codec();avformat_close_input(&container_);
        if(io_){av_freep(&io_->buffer);avio_context_free(&io_);}
    }
    void open_container(){
        io_error_=S_OK;draining_=eof_=false;pcm_.clear();consumed_=0;
        auto *buffer=static_cast<unsigned char*>(av_malloc(32768));require(buffer,E_OUTOFMEMORY);
        io_=avio_alloc_context(buffer,32768,0,this,read_callback,nullptr,input_.seekable?seek_callback:nullptr);
        if(!io_){av_free(buffer);throw Failure{E_OUTOFMEMORY};}
        container_=avformat_alloc_context();require(container_,E_OUTOFMEMORY);
        container_->pb=io_;container_->flags|=AVFMT_FLAG_CUSTOM_IO;
        const auto *demux=av_find_input_format(twin_vq?"vqf":"rm");require(demux,E_NOTIMPL);
        check_av(avformat_open_input(&container_,nullptr,demux,nullptr));
        stream_index_=-1;
        for(unsigned i=0;i<container_->nb_streams;++i)
            if(container_->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO){stream_index_=int(i);break;}
        require(stream_index_>=0);const auto *p=container_->streams[stream_index_]->codecpar;
        require(p->sample_rate>0&&p->sample_rate<=384000&&p->ch_layout.nb_channels>0&&p->ch_layout.nb_channels<=8);
        format_.wFormatTag=twin_vq?WAVE_FORMAT_IEEE_FLOAT:WAVE_FORMAT_PCM;
        format_.nChannels=WORD(p->ch_layout.nb_channels);format_.nSamplesPerSec=DWORD(p->sample_rate);
        format_.wBitsPerSample=twin_vq?32:16;format_.nBlockAlign=WORD(format_.nChannels*(format_.wBitsPerSample/8));
        format_.nAvgBytesPerSec=format_.nSamplesPerSec*format_.nBlockAlign;
        bitrate_=dword(std::max<int64_t>(p->bit_rate,0));
        const auto *st=container_->streams[stream_index_];
        if(st->duration!=AV_NOPTS_VALUE&&st->duration>0)
            duration_=dword(uint64_t(av_rescale_q(st->duration,st->time_base,{1,1000})));
        else if(container_->duration!=AV_NOPTS_VALUE&&container_->duration>0)
            duration_=dword(uint64_t(container_->duration/1000));
        else if(bitrate_&&input_.size-input_.base>uint64_t(avio_tell(io_)))
            duration_=dword((input_.size-input_.base-uint64_t(avio_tell(io_)))*8000/bitrate_);
        codec_name_=twin_vq?L"VQF|VQF Audio":L"RA|Real Audio ("+wide(avcodec_get_name(p->codec_id))+L")";
    }
    void prepare(){
        if(decoder_)return;
        const auto *p=container_->streams[stream_index_]->codecpar;
        auto *codec=avcodec_find_decoder(p->codec_id);require(codec,E_NOTIMPL);
        decoder_=avcodec_alloc_context3(codec);require(decoder_,E_OUTOFMEMORY);
        check_av(avcodec_parameters_to_context(decoder_,p));decoder_->thread_count=1;
        check_av(avcodec_open2(decoder_,codec,nullptr));
    }
    static float sample(const AVFrame *f,int index,int channel){
        const auto fmt=AVSampleFormat(f->format);const auto packed=av_get_packed_sample_fmt(fmt);
        const bool planar=av_sample_fmt_is_planar(fmt)!=0;
        const auto *p=f->extended_data[planar?channel:0];
        const size_t n=planar?size_t(index):size_t(index)*f->ch_layout.nb_channels+channel;
        switch(packed){
        case AV_SAMPLE_FMT_FLT:return reinterpret_cast<const float*>(p)[n];
        case AV_SAMPLE_FMT_DBL:return float(reinterpret_cast<const double*>(p)[n]);
        case AV_SAMPLE_FMT_S16:return reinterpret_cast<const int16_t*>(p)[n]*(1.0f/32768);
        case AV_SAMPLE_FMT_S32:return float(reinterpret_cast<const int32_t*>(p)[n]*(1.0/2147483648.0));
        case AV_SAMPLE_FMT_U8:return (int(p[n])-128)*(1.0f/128);
        default:throw Failure{E_NOTIMPL};
        }
    }
    void convert(){
        require(frame_->sample_rate==int(format_.nSamplesPerSec)&&frame_->ch_layout.nb_channels==format_.nChannels,E_NOTIMPL);
        require(frame_->nb_samples>0&&frame_->nb_samples<=65536);
        pcm_.resize(size_t(frame_->nb_samples)*format_.nBlockAlign);consumed_=0;
        for(int i=0;i<frame_->nb_samples;++i)for(int c=0;c<format_.nChannels;++c){
            float value=sample(frame_,i,c);if(!std::isfinite(value))value=0;
            size_t n=size_t(i)*format_.nChannels+c;
            if(twin_vq){value=std::clamp(value,-32700.0f/32768,32700.0f/32768);memcpy(pcm_.data()+n*4,&value,4);}
            else{const auto v=int16_t(std::clamp(std::lrintf(value*32768.0f),-32768L,32767L));memcpy(pcm_.data()+n*2,&v,2);}
        }
    }
    bool next(){
        prepare();if(eof_)return false;
        for(unsigned attempts=0;attempts<100000;++attempts){
            auto result=avcodec_receive_frame(decoder_,frame_);
            if(result==0){convert();av_frame_unref(frame_);return true;}
            if(result==AVERROR_EOF){eof_=true;return false;}
            if(result!=AVERROR(EAGAIN))check_av(result);
            require(!draining_);
            do{av_packet_unref(packet_);result=av_read_frame(container_,packet_);check(io_error_);}
            while(result>=0&&packet_->stream_index!=stream_index_);
            // The legacy VQF wrapper treats a final incomplete compressed frame
            // as EOF. Keep that boundary without swallowing decoder/IStream faults.
            if(result==AVERROR_EOF||(twin_vq&&result==AVERROR_INVALIDDATA&&avio_feof(io_)&&!io_->error)){
                draining_=true;check_av(avcodec_send_packet(decoder_,nullptr));
            }
            else{check_av(result);check_av(avcodec_send_packet(decoder_,packet_));}
        }
        throw Failure{HRESULT_FROM_WIN32(ERROR_INVALID_DATA)};
    }
    ~AudioReader(){close_container();av_packet_free(&packet_);av_frame_free(&frame_);}
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) override{
        if (!out) return E_POINTER;
        if (IsEqualIID(id, ttp::format::iid_info)) { *out=static_cast<ttp::format::SourceInfo*>(this); AddRef(); return S_OK; }

        if(!out)return E_POINTER;*out=nullptr;
        if(same(id,IID_IUnknown)||same(id,iid_reader))*out=static_cast<Reader*>(this);
        else if(same(id,iid_metadata))*out=static_cast<Metadata*>(this);else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Open(IStream* probe_stream, DWORD probe_flags) override {
        const auto probed = PrepareFormatInfo(probe_stream, ttp::format::Vqf);
        if (FAILED(probed)) return probed;
        return CompleteFormatInfo(OpenImpl(probe_stream, probe_flags));
    }
    HRESULT OpenImpl(IStream *stream,DWORD flags){
        if(!stream)return E_POINTER;if(input_.stream)return E_UNEXPECTED;
        return protect([&]()->HRESULT{
            require(packet_&&frame_,E_OUTOFMEMORY);input_.open(stream);open_container();
            AVDictionaryEntry *t=nullptr;
            while((t=av_dict_get(container_->metadata,"",t,AV_DICT_IGNORE_SUFFIX))!=nullptr){
                std::string key=t->key;if(key=="author")key="Artist";
                if(twin_vq&&(key=="filename"||key=="size"))continue;
                std::string raw=t->value;
                UINT cp=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,raw.data(),int(raw.size()),nullptr,0)>0?CP_UTF8:CP_ACP;
                int n=MultiByteToWideChar(cp,0,raw.data(),int(raw.size()),nullptr,0);
                std::wstring value(n,L'\0');if(n)MultiByteToWideChar(cp,0,raw.data(),int(raw.size()),value.data(),n);
                tags_.push_back({key,std::move(value)});
            }
            for(auto &tag:tags_){if(!_stricmp(tag.key.c_str(),"title"))tag.key="Title";else if(!_stricmp(tag.key.c_str(),"copyright"))tag.key="Copyright";else if(!_stricmp(tag.key.c_str(),"comment"))tag.key="Comment";}
            const auto rank=[](const std::string &k){return k=="Title"?0:k=="Artist"?1:k=="Copyright"?2:k=="Comment"?3:4;};
            std::stable_sort(tags_.begin(),tags_.end(),[&](const auto&a,const auto&b){return rank(a.key)<rank(b.key);});
            if(flags&1)prepare();opened_=true;return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Capabilities(DWORD *out) override{if(!out)return E_POINTER;*out=(input_.remote?1:0)|(input_.seekable?2:0);return S_OK;}
    HRESULT STDMETHODCALLTYPE Duration(DWORD *out) override{if(!out)return E_POINTER;*out=duration_;return opened_?S_OK:E_UNEXPECTED;}
    HRESULT STDMETHODCALLTYPE Format(WAVEFORMATEX **out) override{
        if(!out)return E_POINTER;*out=nullptr;if(!opened_)return E_UNEXPECTED;
        auto *p=static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format_)));if(!p)return E_OUTOFMEMORY;
        *p=format_;*out=p;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BufferSize(DWORD *out) override{if(!out)return E_POINTER;*out=DWORD(decoder_&&decoder_->frame_size>0?decoder_->frame_size:65536)*format_.nBlockAlign;return opened_?S_OK:E_UNEXPECTED;}
    HRESULT STDMETHODCALLTYPE CodecName(wchar_t **out) override{return text(codec_name_,out);}
    HRESULT STDMETHODCALLTYPE Bitrate(DWORD *out) override{if(!out)return E_POINTER;*out=bitrate_;return S_OK;}
    HRESULT STDMETHODCALLTYPE BitDepth(WORD *out) override{if(!out)return E_POINTER;*out=format_.wBitsPerSample;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetCallback(IUnknown *p) override{
        if(!input_.stream)return E_UNEXPECTED;
        return protect([&]()->HRESULT{
            // 801D1AD7 primes the original external decoder by two frames once.
            // Keep raw Read-before-callback behavior separately for ABI callers.
            if(decoder_&&!primed_){for(int i=0;i<2;++i)if(!next())break;pcm_.clear();consumed_=0;primed_=true;}
            return input_.callback(p);
        });
    }
    HRESULT STDMETHODCALLTYPE Start() override{InterlockedExchange(&stopped_,0);ResetEvent(input_.event);return S_OK;}
    HRESULT STDMETHODCALLTYPE Stop() override{InterlockedExchange(&stopped_,1);SetEvent(input_.event);return S_OK;}
    HRESULT STDMETHODCALLTYPE Read(Buffer *buffer) override{
        if(!buffer)return E_POINTER;
        return protect([&]()->HRESULT{
            check(buffer->SetLength(0));require(opened_,E_UNEXPECTED);require(!InterlockedCompareExchange(&stopped_,0,0),HRESULT_FROM_WIN32(ERROR_CANCELLED));
            DWORD capacity{},unused{};BYTE *out{};check(buffer->Capacity(&capacity));capacity-=capacity%format_.nBlockAlign;require(capacity,E_INVALIDARG);
            check(buffer->Data(&out,&unused));require(out,E_POINTER);
            if(consumed_==pcm_.size()&&!next())return S_FALSE;
            auto n=DWORD(std::min<size_t>(capacity,pcm_.size()-consumed_));
            memcpy(out,pcm_.data()+consumed_,n);consumed_+=n;position_+=n/format_.nBlockAlign;return buffer->SetLength(n);
        });
    }
    HRESULT STDMETHODCALLTYPE Seek(DWORD *milliseconds) override{
        if(!milliseconds)return E_POINTER;
        return protect([&]()->HRESULT{
            require(opened_,E_UNEXPECTED);require(input_.seekable,STG_E_INVALIDFUNCTION);
            uint64_t desired=uint64_t(*milliseconds)*format_.nSamplesPerSec/1000;
            if(desired<position_){close_container();input_.at(input_.base);position_=0;open_container();
                if(primed_){for(int i=0;i<2;++i)if(!next())break;pcm_.clear();consumed_=0;}}
            while(position_<desired){
                if(consumed_==pcm_.size()&&!next())break;
                auto n=std::min<uint64_t>(desired-position_,(pcm_.size()-consumed_)/format_.nBlockAlign);
                consumed_+=size_t(n)*format_.nBlockAlign;position_+=n;
            }
            *milliseconds=ttp::vqf::milliseconds(position_,format_.nSamplesPerSec);return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Count(DWORD *out) override{if(!out)return E_POINTER;*out=DWORD(tags_.size());return S_OK;}
    HRESULT STDMETHODCALLTYPE At(DWORD n,wchar_t **key,wchar_t **value) override{
        if(!key||!value)return E_POINTER;*key=*value=nullptr;if(n>=tags_.size())return E_INVALIDARG;
        return protect([&]()->HRESULT{check(text(wide(tags_[n].key),key));auto hr=text(tags_[n].value,value);if(FAILED(hr)){CoTaskMemFree(*key);*key=nullptr;}return hr;});
    }
    HRESULT STDMETHODCALLTYPE Get(const char *key,wchar_t **out) override{
        if(!key||!out)return E_POINTER;*out=nullptr;
        for(const auto &t:tags_)if(!_stricmp(key,t.key.c_str()))return text(t.value,out);return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Set(const char *,const wchar_t *) override{return E_NOTIMPL;}
};
}
HRESULT make_reader(void **out){
    if(!out)return E_POINTER;*out=nullptr;
    return protect([&]()->HRESULT{*out=static_cast<Reader*>(new AudioReader);return S_OK;});
}
}
