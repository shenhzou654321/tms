#ifndef __TS_READER_H__
#define __TS_READER_H__

class BitBuffer;

class TsReader
{
public:
    TsReader();
    ~TsReader();

    int ParseTs(const uint8_t* data, const int& len);
    int ParseTsSegment(const uint8_t* data, const int& len);

private:
    int ParsePAT(BitBuffer& bit_buffer);
    int ParsePMT(BitBuffer& bit_buffer);
    int ParseAdaptation(BitBuffer& bit_buffer);
    int ParsePES(BitBuffer& bit_buffer);

private:
    uint16_t pmt_pid_;
    uint16_t video_pid_;
    uint16_t audio_pid_;
};

#endif // __TS_READER_H__