#ifndef HEADER_PIPELINE_DRAINER
#define HEADER_PIPELINE_DRAINER

#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Standard.h>

namespace OpenHome {
namespace Media {

class Drainer : public PipelineElement, public IPipelineElementUpstream, private IStreamHandler, private INonCopyable
{
    friend class SuiteDrainer;

    static const TUint kSupportedMsgTypes;
public:
    Drainer(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream);
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // from PipelineElement
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryStop(TUint aStreamId) override;
    void NotifyStarving(const Brx& aMode, TUint aStreamId) override;
private:
    void PipelineDrained();
private:
    MsgFactory& iMsgFactory;
    IPipelineElementUpstream& iUpstream;
    Mutex iLock;
    Semaphore iSem;
    IStreamHandler* iStreamHandler;
    TBool iGenerateDrainMsg;
    TBool iWaitForDrained;
    TBool iIgnoreNextStarving;
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_DRAINER