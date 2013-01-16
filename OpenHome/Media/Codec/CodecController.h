#ifndef HEADER_PIPELINE_CODEC
#define HEADER_PIPELINE_CODEC

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Msg.h>

#include <vector>

EXCEPTION(CodecStreamStart);
EXCEPTION(CodecStreamEnded);
EXCEPTION(CodecStreamFlush);
EXCEPTION(CodecStreamFeatureUnsupported);

namespace OpenHome {
namespace Media {
namespace Codec {

/*
Element which holds all audio codecs.
Recognises the format of audio data then decodes it.
Accepts MsgAudioEncoded; outputs MsgAudioPcm.
MsgTrack and MsgStartOfStream are expected and passed through unchanged.
FIXME - no reporting of corrupt/unrecognised file errors.
*/

class ICodecController
{
public:
    virtual void Read(Bwx& aBuf, TUint aBytes) = 0;
    virtual void Output(MsgAudioFormat* aMsg) = 0;
    virtual void Output(MsgAudioPcm* aMsg) = 0;
};
    
class CodecBase
{
    friend class CodecController;
public:
    virtual ~CodecBase();
public:
    virtual TBool Recognise(const Brx& aData) = 0;
    virtual void Process() = 0;
    virtual void StreamCompleted();
protected:
    CodecBase();
private:
    void Construct(ICodecController& aController, MsgFactory& aMsgFactory);
protected:
    ICodecController* iController;
    MsgFactory* iMsgFactory;
};

class CodecController : private ICodecController, private IMsgProcessor, private INonCopyable
{
public:
    CodecController(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, IPipelineElementDownstream& aDownstreamElement);
    ~CodecController();
    void AddCodec(CodecBase* aCodec);
private:
    void CodecThread();
    void PullMsg();
    void Queue(Msg* aMsg);
private: // ICodecController
    void Read(Bwx& aBuf, TUint aBytes);
    void Output(MsgAudioFormat* aMsg);
    void Output(MsgAudioPcm* aMsg);
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
    static const TUint kMaxRecogniseBytes = 6 * 1024;
    MsgFactory& iMsgFactory;
    IPipelineElementUpstream& iUpstreamElement;
    IPipelineElementDownstream& iDownstreamElement;
    std::vector<CodecBase*> iCodecs;
    ThreadFunctor* iDecoderThread;
    CodecBase* iActiveCodec;
    MsgQueue iQueue;
    Msg* iPendingMsg;
    TBool iQueueTrackData;
    TBool iStreamStarted;
    TBool iStreamEnded;
    TBool iQuit;
    MsgAudioEncoded* iAudioEncoded;
    TByte iReadBuf[kMaxRecogniseBytes];
};


}; // namespace Codec
}; // namespace Media
}; // namespace OpenHome

#endif // HEADER_PIPELINE_CODEC