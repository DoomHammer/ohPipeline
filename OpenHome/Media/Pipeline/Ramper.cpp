#include <OpenHome/Media/Pipeline/Ramper.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>

using namespace OpenHome;
using namespace OpenHome::Media;

Ramper::Ramper(IPipelineElementUpstream& aUpstreamElement, TUint aRampDuration)
    : iUpstreamElement(aUpstreamElement)
    , iTrackId(IPipelineIdProvider::kTrackIdInvalid)
    , iStreamId(IPipelineIdProvider::kStreamIdInvalid)
    , iRamping(false)
    , iRampDuration(aRampDuration)
    , iRemainingRampSize(0)
    , iCurrentRampValue(Ramp::kMin)
{
}

Ramper::~Ramper()
{
}

Msg* Ramper::Pull()
{
    Msg* msg;
    if (!iQueue.IsEmpty()) {
        msg = iQueue.Dequeue();
    }
    else {
        msg = iUpstreamElement.Pull();
    }
    msg = msg->Process(*this);
    ASSERT(msg != NULL);
    return msg;
}

Msg* Ramper::ProcessAudio(MsgAudio* aMsg)
{
    if (iRamping) {
        MsgAudio* split;
        if (aMsg->Jiffies() > iRemainingRampSize) {
            split = aMsg->Split(iRemainingRampSize);
            if (split != NULL) {
                iQueue.Enqueue(split);
            }
        }
        split = NULL;
        iCurrentRampValue = aMsg->SetRamp(iCurrentRampValue, iRemainingRampSize, Ramp::EUp, split);
        if (split != NULL) {
            iQueue.EnqueueAtHead(split);
        }
        if (iRemainingRampSize == 0) {
            iRamping = false;
        }
    }
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgMode* aMsg)
{
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgSession* aMsg)
{
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgTrack* aMsg)
{
    iTrackId = aMsg->IdPipeline();
    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgDelay* aMsg)
{
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgEncodedStream* aMsg)
{
    ASSERTS();
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgAudioEncoded* aMsg)
{
    ASSERTS();
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgMetaText* aMsg)
{
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgHalt* aMsg)
{
    iRamping = false;
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgFlush* aMsg)
{
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgWait* aMsg)
{
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& info = aMsg->StreamInfo();
    const TBool newStream = (info.StreamId() != iStreamId);
    iStreamId = info.StreamId();
    if (info.Live() || (newStream && info.SampleStart() > 0)) {
        iRamping = true;
        iCurrentRampValue = Ramp::kMin;
        iRemainingRampSize = iRampDuration;
    }
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgAudioPcm* aMsg)
{
    return ProcessAudio(aMsg);
}

Msg* Ramper::ProcessMsg(MsgSilence* aMsg)
{
    return ProcessAudio(aMsg);
}

Msg* Ramper::ProcessMsg(MsgPlayable* aMsg)
{
    ASSERTS();
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgQuit* aMsg)
{
    return aMsg;
}
