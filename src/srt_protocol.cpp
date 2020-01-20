#include "global.h"
#include "io_buffer.h"
#include "rtmp_protocol.h"
#include "socket_util.h"
#include "srt_protocol.h"
#include "srt_socket.h"

using namespace socket_util;

extern LocalStreamCenter g_local_stream_center;

SrtProtocol::SrtProtocol(IoLoop* io_loop, Fd* socket)
    : MediaSubscriber(kSrt)
    , io_loop_(io_loop)
    , socket_(socket)
    , media_publisher_(NULL)
    , register_publisher_stream_(false)
    , dump_fd_(-1)
{
    cout << LMSG << "new srt protocol, fd=" << socket->fd() << ", socket=" << (void*)socket_ 
         << ", stream=" << GetSrtSocket()->GetStreamId() << endl;

	MediaPublisher* media_publisher = g_local_stream_center.GetMediaPublisherByAppStream("srt", GetSrtSocket()->GetStreamId());

    if (media_publisher != NULL)
    {    
        SetMediaPublisher(media_publisher);
        media_publisher->AddSubscriber(this);
        cout << LMSG << "publisher " << media_publisher << " add subscriber for stream " << GetSrtSocket()->GetStreamId() << endl;
    }    
    else
    {
        cout << LMSG << "can't find stream " << GetSrtSocket()->GetStreamId() << ", choose random one to debug"<< endl;
        string app, stream;
        MediaPublisher* media_publisher = g_local_stream_center._DebugGetRandomMediaPublisher(app, stream);
        if (media_publisher)
        {    
            SetMediaPublisher(media_publisher);
            media_publisher->AddSubscriber(this);
            cout << LMSG << "random publisher " << media_publisher << " add subscriber for app " << app << ", stream " << stream << endl;
        }
    }

    ts_reader_.SetFrameCallback(std::bind(&SrtProtocol::OnFrame, this, std::placeholders::_1));
    ts_reader_.SetHeaderCallback(std::bind(&SrtProtocol::OnHeader, this, std::placeholders::_1));
}

SrtProtocol::~SrtProtocol()
{
}

int SrtProtocol::HandleRead(IoBuffer& io_buffer, Fd& socket)
{
    int ret = kError;
    do  
    {   
        ret = Parse(io_buffer);
    } while (ret == kSuccess);

    return ret;
}

int SrtProtocol::Parse(IoBuffer& io_buffer)
{
    uint8_t* data = NULL;
    int len = io_buffer.Read(data, io_buffer.Size());

    OpenDumpFile();
    Dump(data, len);

    if (len > 0)
    {
        if (! register_publisher_stream_)
        {
            g_local_stream_center.RegisterStream("srt", GetSrtSocket()->GetStreamId(), this);
            cout << LMSG << "register publisher " << this << ", streamid=" << GetSrtSocket()->GetStreamId() << endl;
            register_publisher_stream_ = true;
        }

        ts_reader_.ParseTs(data, len);

        string media_payload((const char*)data, len);
		for (auto& sub : subscriber_)
        {
            cout << LMSG << "srt route to subscriber " << &sub << endl;
            sub->SendData(media_payload);
        }

        //for (auto& pending_sub : wait_header_subscriber_)
        //{
        //    cout << LMSG << "srt route to pending subscriber " << &pending_sub << endl;
        //    pending_sub->SendData(media_payload);
        //}

        return kSuccess;
    }

    return kNoEnoughData;
}

int SrtProtocol::HandleClose(IoBuffer& io_buffer, Fd& socket)
{
    UNUSED(io_buffer);
    UNUSED(socket);

    cout << LMSG << "srt protocol stop, fd=" << socket_->fd() << ", socket=" << (void*)socket_ 
         << ", stream=" << GetSrtSocket()->GetStreamId() << endl;

    if (media_publisher_)
    {
        media_publisher_->RemoveSubscriber(this);
    }

    return kSuccess;
}

int SrtProtocol::EveryNSecond(const uint64_t& now_in_ms, const uint32_t& interval, const uint64_t& count)
{
    UNUSED(now_in_ms);
    UNUSED(interval);
    UNUSED(count);

    return kSuccess;
}

int SrtProtocol::SendData(const std::string& data)
{
    return GetSrtSocket()->Send((const uint8_t*)data.data(), data.size());
}

void SrtProtocol::OpenDumpFile()
{
    if (dump_fd_ == -1)
    {
        ostringstream os;
        os << GetSrtSocket()->GetStreamId() << ".ts";
        dump_fd_ = open(os.str().c_str(), O_CREAT|O_TRUNC|O_RDWR, 0664);
    }
}

void SrtProtocol::Dump(const uint8_t* data, const int& len)
{
    if (dump_fd_ != -1)
    {
        int nbytes = write(dump_fd_, data, len);
        UNUSED(nbytes);
    }
}

void SrtProtocol::OnFrame(const Payload& frame)
{
    if (frame.IsVideo())
    {
        cout << LMSG << (frame.IsIFrame() ? "I" : (frame.IsPFrame() ? "P" : (frame.IsBFrame() ? "B" : "Unknown"))) 
             << ",pts=" << frame.GetPts() << ",dts=" << frame.GetDts() << endl;
        media_muxer_.OnVideo(frame);
    }
    else if (frame.IsAudio())
    {
        cout << LMSG << "audio, dts=" << frame.GetDts() << endl;
        media_muxer_.OnAudio(frame);
    }

	for (auto& sub : subscriber_)
    {    
        sub->SendMediaData(frame);
    }
}

void SrtProtocol::OnHeader(const Payload& header_frame)
{
    cout << LMSG << "header=" << Util::Bin2Hex(header_frame.GetAllData(), header_frame.GetAllLen()) << endl;

    if (header_frame.IsVideo())
    {
        string video_header((const char*)header_frame.GetAllData(), header_frame.GetAllLen());
        media_muxer_.OnVideoHeader(video_header);
    }
    else if (header_frame.IsAudio())
    {
        string audio_header((const char*)header_frame.GetAllData(), header_frame.GetAllLen());
        media_muxer_.OnAudioHeader(audio_header);
    }
}
