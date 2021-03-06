#pragma once

#include <OpenHome/Buffer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Media/Codec/AlacBase.h>
#include <OpenHome/Media/Codec/Mpeg4.h>

extern "C" {
#include <decomp.h>
}

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecAlacBase : public CodecBase
{
public:
    static const Brn kCodecAlac;
protected:
    CodecAlacBase(const TChar* aId);
    ~CodecAlacBase();
protected: // from CodecBase
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    void StreamCompleted();
private:
    void BigEndianData(TUint toWrite, TUint aSamplesWritten);
protected:
    // FIXME - implement StreamInitialise() (from CodecBase) here, which does some work and calls into a virtual void Initialise() = 0; that deriving classes must implement (e.g., for ALAC codec to initialise sample size table)
    void Initialise();
    void Decode();
    void OutputFinal();
protected:
    alac_file *alac;
    TBool iStreamStarted;
    TBool iStreamEnded;
    Bws<4*10240> iInBuf;          // how big can these go and need to go ?
    Bws<16*10240> iDecodedBuf;
    Bws<DecodedAudio::kMaxBytes> iOutBuf;
    TUint iChannels;
    TUint iBitDepth;          // alac decoder may redefine the bit depth
    TUint iBytesPerSample;
    TUint iSampleRate;
    TUint64 iSamplesWrittenTotal;
    TUint64 iTrackLengthJiffies;
    TUint64 iTrackOffset;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

