#pragma once

#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/AlacBase.h>
#include <OpenHome/Types.h>

namespace OpenHome {
namespace Av {

class CodecRaop : public Media::Codec::CodecAlacBase
{
public:
    CodecRaop();
    ~CodecRaop();
private: // from CodecBase
    TBool Recognise(const Media::Codec::EncodedStreamInfo& aStreamInfo) override;
    void StreamInitialise() override;
    void Process() override;
    TBool TrySeek(TUint aStreamId, TUint64 aSample) override;
    void StreamCompleted() override;
private:
    void ParseFmtp(const Brx& aFmtp);
private:
    Bws<100> iCodecSpecificData;
};

} // namespace Av
} // namespace OpenHome

