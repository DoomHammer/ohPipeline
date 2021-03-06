#include <OpenHome/Media/Pipeline/Muter.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Media;

const TUint Muter::kSupportedMsgTypes =   eMode
                                        | eTrack
                                        | eDrain
                                        | eEncodedStream
                                        | eMetatext
                                        | eStreamInterrupted
                                        | eHalt
                                        | eDecodedStream
                                        | eAudioPcm
                                        | eSilence
                                        | eQuit;

Muter::Muter(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream, TUint aRampDuration)
    : PipelineElement(kSupportedMsgTypes)
    , iMsgFactory(aMsgFactory)
    , iUpstream(aUpstream)
    , iLock("MPMT")
    , iState(eRunning)
    , iRampDuration(aRampDuration)
    , iRemainingRampSize(0)
    , iCurrentRampValue(Ramp::kMax)
    , iHalted(true)
{
}

void Muter::Mute()
{
    LOG(kPipeline, "Muter::Mute\n");
    AutoMutex _(iLock);
    if (iState == eRunning) {
        if (iHalted) {
            iState = eMuted;
        }
        else {
            iState = eRampingDown;
            iRemainingRampSize = iRampDuration;
            iCurrentRampValue = Ramp::kMax;
        }
    }
    else if (iState == eRampingUp) {
        if (iRemainingRampSize == iRampDuration) {
            iState = eMuted;
        }
        else {
            iState = eRampingDown;
            iRemainingRampSize = iRampDuration - iRemainingRampSize;
        }
    }
}

void Muter::Unmute()
{
    LOG(kPipeline, "Muter::Unmute\n");
    AutoMutex _(iLock);
    if (iState == eMuted) {
        if (iHalted) {
            iState = eRunning;
        }
        else {
            iState = eRampingUp;
            iRemainingRampSize = iRampDuration;
            iCurrentRampValue = Ramp::kMin;
        }
    }
    else if (iState == eRampingDown) {
        if (iRemainingRampSize == iRampDuration) {
            iState = eRunning;
        }
        else {
            iState = eRampingUp;
            iRemainingRampSize = iRampDuration - iRemainingRampSize;
        }
    }
}

Msg* Muter::Pull()
{
    Msg* msg;
    if (!iQueue.IsEmpty()) {
        msg = iQueue.Dequeue();
    }
    else {
        msg = iUpstream.Pull();
    }
    iLock.Wait();
    msg = msg->Process(*this);
    iLock.Signal();
    ASSERT(msg != nullptr);
    return msg;
}

Msg* Muter::ProcessMsg(MsgHalt* aMsg)
{
    iHalted = true;
    if (iState == eRampingDown) {
        iState = eMuted;
        iRemainingRampSize = 0;
        iCurrentRampValue = Ramp::kMin;
    }
    return aMsg;
}

Msg* Muter::ProcessMsg(MsgAudioPcm* aMsg)
{
    iHalted = false;
    MsgAudio* msg = aMsg;
    switch (iState)
    {
    case eRunning:
        break;
    case eRampingDown:
    case eRampingUp:
    {
        MsgAudio* split;
        if (msg->Jiffies() > iRemainingRampSize && iRemainingRampSize > 0) {
            split = msg->Split(iRemainingRampSize);
            if (split != nullptr) {
                iQueue.EnqueueAtHead(split);
            }
        }
        split = nullptr;
        const Ramp::EDirection direction = (iState == eRampingDown? Ramp::EDown : Ramp::EUp);
        if (iRemainingRampSize > 0) {
            iCurrentRampValue = msg->SetRamp(iCurrentRampValue, iRemainingRampSize, direction, split);
        }
        if (iRemainingRampSize == 0) {
            iState = (iState == eRampingDown? eMuted : eRunning);
        }
        if (split != nullptr) {
            iQueue.EnqueueAtHead(split);
        }
    }
        break;
    case eMuted:
    {
        MsgSilence* silence = iMsgFactory.CreateMsgSilence(aMsg->Jiffies());
        msg->RemoveRef();
        msg = silence;
    }
        break;
    }

    return msg;
}

Msg* Muter::ProcessMsg(MsgSilence* aMsg)
{
    switch (iState)
    {
    case eRunning:
    case eMuted:
        break;
    case eRampingDown:
        iState = eMuted;
        break;
    case eRampingUp:
        iState = eRunning;
        break;
    }
    return aMsg;
}
