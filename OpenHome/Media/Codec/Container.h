#ifndef HEADER_PIPELINE_CONTAINER
#define HEADER_PIPELINE_CONTAINER

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Msg.h>

EXCEPTION(CodecStreamCorrupt);

namespace OpenHome {
namespace Media {
namespace Codec {

/*
Element which strips any container data from the start of a file.
Could (should) be extended later to handle more complex containers
...which read a stream, skip some data, read another stream repeatedly
...not just at the start of a file.
*/

class IContainer
{
public:
    virtual void Read(Bwx& aBuf, TUint aOffset, TUint aBytes) = 0;
};

class Container : public IPipelineElementUpstream, private IContainer, private IMsgProcessor, private INonCopyable
{
public:
    Container(IPipelineElementUpstream& aUpstreamElement);
public: // from IPipelineElementUpstream
    Msg* Pull();
private: // IContainer
    void Read(Bwx& aBuf, TUint aOffset, TUint aBytes);
private: // IMsgProcessor
    Msg* ProcessMsg(MsgAudioEncoded* aMsg);
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
    Msg* ProcessMsg(MsgSilence* aMsg);
    Msg* ProcessMsg(MsgPlayable* aMsg);
    Msg* ProcessMsg(MsgAudioFormat* aMsg);
    Msg* ProcessMsg(MsgTrack* aMsg);
    Msg* ProcessMsg(MsgAudioStream* aMsg);
    Msg* ProcessMsg(MsgMetaText* aMsg);
    Msg* ProcessMsg(MsgHalt* aMsg);
    Msg* ProcessMsg(MsgFlush* aMsg);
    Msg* ProcessMsg(MsgQuit* aMsg);
private:
    IPipelineElementUpstream& iUpstreamElement;
    TBool iCheckForContainer;
    MsgAudioEncoded* iAudioEncoded; /* FIXME - restricting container processing to a single Msg seems
                                       risky but its consistent with volkano so is hopefully safe
                                       in practice... */
    TByte iReadBuf[EncodedAudio::kMaxBytes];
};

}; // namespace Codec
}; // namespace Media
}; // namespace OpenHome

#endif // HEADER_PIPELINE_CONTAINER