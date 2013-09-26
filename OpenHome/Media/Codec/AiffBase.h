#ifndef HEADER_PIPELINE_CODEC_AIFF_BASE
#define HEADER_PIPELINE_CODEC_AIFF_BASE

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Codec/CodecController.h>

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecAiffBase : public CodecBase
{
public:
    CodecAiffBase(const Brx& aName, EMediaDataEndian aEndian);
    ~CodecAiffBase();
private: // from CodecBase
    TBool SupportsMimeType(const Brx& aMimeType);
    TBool Recognise(const Brx& aData);
    void StreamInitialise();
    void Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
protected:
    TUint FindChunk(const Brx& aChunkId);
    virtual void ProcessMiscChunks();
    virtual TUint GetCommChunkHeader() = 0;
    virtual void ProcessCommChunkExtra(); // for e.g., getting compression format for AIFC
private:
    TUint DetermineRate(TUint16 aExponent, TUint32 aMantissa);
    void ProcessHeader();
    void ProcessFormChunk();
    void ParseCommChunk(TUint aChunkSize); // helper for ProcessCommChunk
    void ProcessCommChunk();
    void ProcessSsndChunk();
    void SendMsgDecodedStream(TUint64 aStartSample);
protected:
    Bws<DecodedAudio::kMaxBytes> iReadBuf;
    TUint64 iTrackStart;
private:
    Brn iName;
    TUint iNumChannels;
    TUint64 iSamplesTotal;
    TUint iBitDepth;
    TUint iSampleRate;
    TUint iBitRate;
    TUint iAudioBytesTotal;
    TUint iAudioBytesRemaining;
    TUint64 iTrackLengthJiffies;
    EMediaDataEndian iEndian;
    TUint64 iTrackOffset;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_CODEC_AIFF_BASE
