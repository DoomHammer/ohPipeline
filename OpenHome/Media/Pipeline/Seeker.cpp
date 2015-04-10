#include <OpenHome/Media/Pipeline/Seeker.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Media;

Seeker::Seeker(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, ISeeker& aSeeker, ISeekRestreamer& aRestreamer, TUint aRampDuration)
    : iMsgFactory(aMsgFactory)
    , iUpstreamElement(aUpstreamElement)
    , iSeeker(aSeeker)
    , iRestreamer(aRestreamer)
    , iLock("SEEK")
    , iState(ERunning)
    , iRampDuration(aRampDuration)
    , iRemainingRampSize(0)
    , iCurrentRampValue(Ramp::kMax)
    , iSeekSeconds(UINT_MAX)
    , iTargetFlushId(MsgFlush::kIdInvalid)
    , iTargetTrackId(Track::kIdNone)
    , iTrackId(Track::kIdNone)
    , iStreamId(IPipelineIdProvider::kStreamIdInvalid)
    , iSeekConsecutiveFailureCount(0)
    , iMsgStream(NULL)
{
}

Seeker::~Seeker()
{
    if (iMsgStream != NULL) {
        iMsgStream->RemoveRef();
    }
}

TBool Seeker::Seek(TUint aStreamId, TUint aSecondsAbsolute, TBool aRampDown)
{
    LOG(kPipeline, "> Seeker::Seek(%u, %u, %u)\n", aStreamId, aSecondsAbsolute, aRampDown);
    AutoMutex a(iLock);
    if (iState != ERunning || iStreamId != aStreamId || !iStreamIsSeekable) {
        LOG(kPipeline, "Seek request failed\n");
        return false;
    }

    iSeekSeconds = aSecondsAbsolute;
    iFlushEndJiffies = 0;

    if (iState == ERampingUp) {
        iState = ERampingDown;
        iRemainingRampSize = iRampDuration - iRemainingRampSize;
        // leave iCurrentRampValue unchanged
    }
    else if (!aRampDown || iState == EFlushing) {
        DoSeek();
    }
    else {
        LOG(kPipeline, "Seeker state -> RampingDown\n");
        iState = ERampingDown;
        iRemainingRampSize = iRampDuration;
        iCurrentRampValue = Ramp::kMax;
    }
    return true;
}

Msg* Seeker::Pull()
{
    Msg* msg;
    do {
        msg = (iQueue.IsEmpty()? iUpstreamElement.Pull() : iQueue.Dequeue());
        iLock.Wait();
        msg = msg->Process(*this);
        iLock.Signal();
    } while (msg == NULL);
    return msg;
}

Msg* Seeker::ProcessMsg(MsgMode* aMsg)
{
    iMode.Replace(aMsg->Mode());
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgSession* aMsg)
{
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgTrack* aMsg)
{
    iTrackId = aMsg->Track().Id();
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgDelay* aMsg)
{
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgEncodedStream* aMsg)
{
    NewStream();
    iStreamId = aMsg->StreamId();
    iStreamIsSeekable = aMsg->Seekable();
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* Seeker::ProcessMsg(MsgMetaText* aMsg)
{
    return ProcessFlushable(aMsg);
}

Msg* Seeker::ProcessMsg(MsgHalt* aMsg)
{
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgFlush* aMsg)
{
    if (iTargetFlushId != MsgFlush::kIdInvalid && iTargetFlushId == aMsg->Id()) {
        ASSERT(iState == EFlushing);
        aMsg->RemoveRef();
        iTargetFlushId = MsgFlush::kIdInvalid;
        iState = ERampingUp;
        iRemainingRampSize = iRampDuration;
        iCurrentRampValue = Ramp::kMin;
        return NULL;
    }
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgWait* aMsg)
{
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgDecodedStream* aMsg)
{
    if (iMsgStream != NULL) {
        iMsgStream->RemoveRef();
    }
    iMsgStream = aMsg;
    iMsgStream->AddRef();
    const DecodedStreamInfo& streamInfo = aMsg->StreamInfo();
    iStreamPosJiffies = Jiffies::JiffiesPerSample(streamInfo.SampleRate());
    iStreamPosJiffies *= streamInfo.SampleStart();
    if (iTargetTrackId != Track::kIdNone) {
        if (iTargetTrackId != iTrackId) {
            (void)streamInfo.StreamHandler()->TryStop(streamInfo.StreamId());
        }
        else if (iTargetFlushId == MsgFlush::kIdInvalid) {
            if (iTargetTrackId == iTrackId && iSeekSeconds > 0) {
                iTargetTrackId = Track::kIdNone;
                DoSeek();
            }
            else {
                /* If tracks match and iSeekSeconds==0, we're at the seek target point now.
                   If iTargetTrackId is non-null and tracks don't match, we're likely never going
                   to receive the target track.  This could happen after a seek request immediately
                   followed by a change in uri provider. */
                iState = ERunning;
                iRemainingRampSize = 0;
                iCurrentRampValue = Ramp::kMax;
            }
        }
    }
    else if (iState == EFlushing) {
        iState = ERampingUp;
        iRemainingRampSize = iRampDuration;
        iCurrentRampValue = Ramp::kMin;
    }
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgAudioPcm* aMsg)
{
    return ProcessAudio(aMsg);
}

Msg* Seeker::ProcessMsg(MsgSilence* aMsg)
{
    return ProcessAudio(aMsg);
}

Msg* Seeker::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* Seeker::ProcessMsg(MsgQuit* aMsg)
{
    return aMsg;
}

void Seeker::NotifySeekComplete(TUint aHandle, TUint aFlushId)
{
    LOG(kPipeline, "> Seeker::NotifySeekComplete(%u, %u))\n", aHandle, aFlushId);
    AutoMutex a(iLock);
    if (aHandle != iSeekHandle) {
        LOG(kPipeline, "> Seeker::NotifySeekComplete - ignoring (wrong handle)\n");
        return;
    }
    if (aFlushId == MsgFlush::kIdInvalid) {
        iTargetFlushId = aFlushId;
        HandleSeekFail();
    }
    else {
        iSeekConsecutiveFailureCount = 0;
    }
}

void Seeker::DoSeek()
{
    LOG(kPipeline, "> Seeker::DoSeek()\n");
    iState = EFlushing; /* set this before calling StartSeek as its possible NotifySeekComplete
                           could be called from another thread before StartSeek returns. */
    iSeeker.StartSeek(iStreamId, iSeekSeconds, *this, iSeekHandle);
    if (iSeekHandle == ISeeker::kHandleError) {
        HandleSeekFail();
    }
    else {
        iQueue.Clear();
        iQueue.Enqueue(iMsgFactory.CreateMsgHalt()); /* inform downstream parties (StarvationMonitor)
                                                        that any subsequent break in audio is expected */
    }
}

Msg* Seeker::ProcessFlushable(Msg* aMsg)
{
    if (iState == EFlushing) {
        aMsg->RemoveRef();
        return NULL;
    }
    return aMsg;
}

Msg* Seeker::ProcessAudio(MsgAudio* aMsg)
{
    iStreamPosJiffies += aMsg->Jiffies();
    if (iFlushEndJiffies != 0 && iFlushEndJiffies < iStreamPosJiffies) {
        ASSERT(iState == EFlushing);
        const TUint splitJiffies = (TUint)(iStreamPosJiffies - iFlushEndJiffies);
        if (splitJiffies < aMsg->Jiffies()) {
            MsgAudio* split = aMsg->Split(aMsg->Jiffies() - splitJiffies);
            if (split != NULL) {
                iQueue.EnqueueAtHead(split);
            }
        }
        iStreamPosJiffies = iFlushEndJiffies;
    }
    if (iTargetFlushId == MsgFlush::kIdInvalid && iFlushEndJiffies == iStreamPosJiffies) {
        ASSERT(iState == EFlushing);
        iState = ERampingUp;
        iRemainingRampSize = iRampDuration;
        iCurrentRampValue = Ramp::kMin;
        iFlushEndJiffies = 0;

        iQueue.EnqueueAtHead(aMsg);
        const DecodedStreamInfo& info = iMsgStream->StreamInfo();
        const TUint64 numSamples = iStreamPosJiffies / Jiffies::JiffiesPerSample(info.SampleRate());
        return iMsgFactory.CreateMsgDecodedStream(info.StreamId(), info.BitRate(), info.BitDepth(),
                                                    info.SampleRate(), info.NumChannels(), info.CodecName(),
                                                    info.TrackLength(), numSamples, info.Lossless(),
                                                    info.Seekable(), info.Live(), info.StreamHandler());
    }
    if (iState == ERampingDown || iState == ERampingUp) {
        MsgAudio* split;
        if (aMsg->Jiffies() > iRemainingRampSize) {
            split = aMsg->Split(iRemainingRampSize);
            if (split != NULL) {
                iQueue.EnqueueAtHead(split);
            }
        }
        split = NULL;
        const Ramp::EDirection direction = (iState == ERampingDown? Ramp::EDown : Ramp::EUp);
        iCurrentRampValue = aMsg->SetRamp(iCurrentRampValue, iRemainingRampSize, direction, split);
        if (split != NULL) {
            iQueue.EnqueueAtHead(split);
        }
        if (iRemainingRampSize == 0) {
            if (iState == ERampingDown) {
                DoSeek();
            }
            else { // iState == ERampingUp
                iState = ERunning;
            }
        }
        return aMsg;
    }

    return ProcessFlushable(aMsg);
}

void Seeker::NewStream()
{
    iRemainingRampSize = 0;
    iCurrentRampValue = Ramp::kMax;
    if (iTargetTrackId == Track::kIdNone && iState != EFlushing) {
        iState = ERunning;
    }
    else {
        iState = EFlushing;
    }
    iSeekHandle = ISeeker::kHandleError;
    iStreamIsSeekable = true;
    iStreamPosJiffies = 0;
    iFlushEndJiffies = 0;
}

void Seeker::HandleSeekFail()
{
    LOG(kPipeline, "> Seeker::HandleSeekFail()... ");
    TUint64 seekJiffies = ((TUint64)iSeekSeconds) * Jiffies::kPerSecond;
    if (seekJiffies > iStreamPosJiffies) {
        LOG(kPipeline, "flush until seek point\n");
        iFlushEndJiffies = seekJiffies;
        iState = EFlushing;
        iSeekConsecutiveFailureCount = 0;
    }
    else if (seekJiffies == iStreamPosJiffies) {
        LOG(kPipeline, "(implausible but) already at seek point\n");
        iState = ERampingUp;
        iRemainingRampSize = iRampDuration;
        iCurrentRampValue = Ramp::kMin;
        iSeekConsecutiveFailureCount = 0;
    }
    else {
        if (++iSeekConsecutiveFailureCount < 2) {
            iTargetTrackId = iTrackId;
            iTargetFlushId = iRestreamer.SeekRestream(iMode, iTargetTrackId);
            LOG(kPipeline, "SeekRestream returned %u\n", iTargetFlushId);
        }
        else {
            LOG(kPipeline, "give up, already failed to seek twice\n");
            iTargetTrackId = Track::kIdNone;
            iTargetFlushId = MsgFlush::kIdInvalid;
            iSeekConsecutiveFailureCount = 0;
        }
        if (iTargetFlushId == MsgFlush::kIdInvalid) {
            iState = ERampingUp;
            iRemainingRampSize = iRampDuration;
            iCurrentRampValue = Ramp::kMin;
            iTargetTrackId = Track::kIdNone;
        }
    }
}
