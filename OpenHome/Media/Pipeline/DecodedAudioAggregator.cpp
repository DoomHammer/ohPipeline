#include <OpenHome/Media/Pipeline/DecodedAudioAggregator.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// DecodedAudioAggregator

DecodedAudioAggregator::DecodedAudioAggregator(IPipelineElementDownstream& aDownstreamElement, MsgFactory& aMsgFactory)
    : iDownstreamElement(aDownstreamElement)
    , iMsgFactory(aMsgFactory)
    , iStreamHandler(NULL)
    , iDecodedAudio(NULL)
    , iChannels(0)
    , iSampleRate(0)
    , iBitDepth(0)
    , iExpectedFlushId(MsgFlush::kIdInvalid)
    , iLock("DAAL")
{
}

void DecodedAudioAggregator::Push(Msg* aMsg)
{
    ASSERT(aMsg != NULL);
    Msg* msg = aMsg->Process(*this);
    if (msg != NULL) {
        Queue(msg);
    }
}

EStreamPlay DecodedAudioAggregator::OkToPlay(TUint aTrackId, TUint aStreamId)
{
    if (iStreamHandler != NULL) {
        return iStreamHandler->OkToPlay(aTrackId, aStreamId);
    }
    else {
        return ePlayNo;
    }
}

TUint DecodedAudioAggregator::TrySeek(TUint aTrackId, TUint aStreamId, TUint64 aOffset)
{
    OutputAggregatedAudio();
    if (iStreamHandler != NULL) {
        TUint flushId = iStreamHandler->TrySeek(aTrackId, aStreamId, aOffset);
        AutoMutex a(iLock);
        iExpectedFlushId = flushId;
        return flushId;
    }
    return MsgFlush::kIdInvalid;
}

TUint DecodedAudioAggregator::TryStop(TUint aTrackId, TUint aStreamId)
{
    OutputAggregatedAudio();
    if (iStreamHandler != NULL) {
        TUint flushId = iStreamHandler->TryStop(aTrackId, aStreamId);
        AutoMutex a(iLock);
        iExpectedFlushId = flushId;
        return flushId;
    }
    return MsgFlush::kIdInvalid;
}

TBool DecodedAudioAggregator::TryGet(IWriter& aWriter, TUint aTrackId, TUint aStreamId, TUint64 aOffset, TUint aBytes)
{
    if (iStreamHandler != NULL) {
        return iStreamHandler->TryGet(aWriter, aTrackId, aStreamId, aOffset, aBytes);
    }
    return false;
}

void DecodedAudioAggregator::NotifyStarving(const Brx& aMode, TUint aTrackId, TUint aStreamId)
{
    if (iStreamHandler != NULL) {
        iStreamHandler->NotifyStarving(aMode, aTrackId, aStreamId);
    }
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgMode* aMsg)
{
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgSession* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgTrack* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgDelay* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgEncodedStream* aMsg)
{
    OutputAggregatedAudio();
    iStreamHandler = aMsg->StreamHandler();
    MsgEncodedStream* msg = iMsgFactory.CreateMsgEncodedStream(aMsg->Uri(), aMsg->MetaText(), aMsg->TotalBytes(), aMsg->StreamId(), aMsg->Seekable(), aMsg->Live(), this);
    aMsg->RemoveRef();
    return msg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgMetaText* aMsg)
{
    iLock.Wait();
    if (iExpectedFlushId != MsgFlush::kIdInvalid) {
        iLock.Signal();
        aMsg->RemoveRef();
        return NULL;
    }
    iLock.Signal();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgHalt* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgFlush* aMsg)
{
    AutoMutex a(iLock);
    if (iExpectedFlushId == aMsg->Id()) {
        iExpectedFlushId = MsgFlush::kIdInvalid;
    }
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgWait* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgDecodedStream* aMsg)
{
    //OutputAggregatedAudio();  // shouldn't ever have aggregated audio stored when this is called
    AutoMutex a(iLock);
    ASSERT(iDecodedAudio == NULL);
    const DecodedStreamInfo& info = aMsg->StreamInfo();
    iChannels = info.NumChannels();
    iSampleRate = info.SampleRate();
    iBitDepth = info.BitDepth();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLock.Wait();
    if (iExpectedFlushId != MsgFlush::kIdInvalid) {
        iLock.Signal();
        aMsg->RemoveRef();
        return NULL;
    }
    iLock.Signal();
    return TryAggregate(aMsg);
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgQuit* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

TBool DecodedAudioAggregator::AggregatorFull(TUint aBytes, TUint aJiffies)
{
    return (aBytes == DecodedAudio::kMaxBytes || aJiffies >= kMaxJiffies);
}

MsgAudioPcm* DecodedAudioAggregator::TryAggregate(MsgAudioPcm* aMsg)
{
    // This method only looks at the byte capacity when deciding whether to
    // buffer data. There is no point in chopping the data purely on a jiffy
    // limit when buffer space could be used to output all jiffies together.

    AutoMutex a(iLock);
    TUint jiffies = aMsg->Jiffies();
    const TUint jiffiesPerSample = Jiffies::JiffiesPerSample(iSampleRate);
    const TUint msgBytes = Jiffies::BytesFromJiffies(jiffies, jiffiesPerSample, iChannels, iBitDepth/8);
    ASSERT(jiffies == aMsg->Jiffies()); // refuse to handle msgs not terminating on sample boundaries

    if (iDecodedAudio == NULL) {
        if (AggregatorFull(msgBytes, aMsg->Jiffies())) {
            return aMsg;
        }
        else {
            iDecodedAudio = aMsg;
            return NULL;
        }
    }

    TUint aggregatedJiffies = iDecodedAudio->Jiffies();
    TUint aggregatedBytes = Jiffies::BytesFromJiffies(aggregatedJiffies, jiffiesPerSample, iChannels, iBitDepth/8);
    if (aggregatedBytes + msgBytes <= kMaxBytes) {
        // Have byte capacity to add new data.
        iDecodedAudio->Aggregate(*aMsg);

        aggregatedJiffies = iDecodedAudio->Jiffies();
        aggregatedBytes = Jiffies::BytesFromJiffies(aggregatedJiffies, jiffiesPerSample, iChannels, iBitDepth/8);
        if (AggregatorFull(aggregatedBytes, iDecodedAudio->Jiffies())) {
            MsgAudioPcm* msg = iDecodedAudio;
            iDecodedAudio = NULL;
            return msg;
        }
    }
    else {
        // Lazy approach here - if new aMsg can't be appended, just return
        // iDecodedAudio and set iDecodedAudio = aMsg.
        // Could add a method to MsgAudioPcm that chops audio when aggregating
        // to make even more efficient use of decoded audio msgs.
        MsgAudioPcm* msg = iDecodedAudio;
        iDecodedAudio = aMsg;
        return msg;
    }

    return NULL;
}

void DecodedAudioAggregator::Queue(Msg* aMsg)
{
    iDownstreamElement.Push(aMsg);
}

void DecodedAudioAggregator::OutputAggregatedAudio()
{
    AutoMutex a(iLock);
    if (iDecodedAudio != NULL) {
        Queue(iDecodedAudio);
        iDecodedAudio = NULL;
    }
}
