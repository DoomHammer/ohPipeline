#include <OpenHome/Media/StarvationMonitor.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Media/ClockPuller.h>

#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// StarvationMonitor

StarvationMonitor::StarvationMonitor(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, IStarvationMonitorObserver& aObserver,
                                     TUint aNormalSize, TUint aStarvationThreshold, TUint aRampUpSize)
    : iMsgFactory(aMsgFactory)
    , iUpstreamElement(aUpstreamElement)
    , iObserver(aObserver)
    , iClockPuller(NULL)
    , iNormalMax(aNormalSize)
    , iStarvationThreshold(aStarvationThreshold)
    , iRampUpSize(aRampUpSize)
    , iLock("STRV")
    , iSemIn("STR1", 0)
    , iSemOut("STR2", 0)
    , iCurrentRampValue(Ramp::kRampMax)
    , iPlannedHalt(true)
    , iHaltDelivered(false)
    , iExit(false)
    , iJiffiesUntilNextHistoryPoint(kUtilisationSamplePeriodJiffies)
    , iStreamHandler(NULL)
    , iTrackId(UINT_MAX)
    , iStreamId(UINT_MAX)
{
    ASSERT(iStarvationThreshold < iNormalMax);
    UpdateStatus(EBuffering);
    iThread = new ThreadFunctor("StarvationMonitor", MakeFunctor(*this, &StarvationMonitor::PullerThread), kPriorityVeryHigh); // FIXME - review thread priorities
    iThread->Start();
}

StarvationMonitor::~StarvationMonitor()
{
    // FIXME - check that thread has exited
    delete iThread;
}

void StarvationMonitor::PullerThread()
{
    do {
        Msg* msg = iUpstreamElement.Pull();
        Enqueue(msg);
    } while (!iExit);
}

void StarvationMonitor::Enqueue(Msg* aMsg)
{
    ASSERT(aMsg != NULL);
    // Queue the next msg before checking how much data we already have in the buffer
    // This risks us going over the nominal max size for the buffer but guarantees that
    // we don't deadlock if a single message larger than iNormalMax is queued.
    DoEnqueue(aMsg);
    iLock.Wait();
    TBool isFull = (iStatus != EBuffering && Jiffies() >= iNormalMax);
    if (iStatus == EBuffering && Jiffies() >= iNormalMax) {
        iHaltDelivered = false;
        if (iPlannedHalt) {
            UpdateStatus(ERunning);
            iPlannedHalt = false;
        }
        else {
            UpdateStatus(ERampingUp);
            iCurrentRampValue = Ramp::kRampMin;
            iRemainingRampSize = iRampUpSize;
        }
        iSemOut.Signal();
        isFull = true;
    }
    if (iExit) {
        iSemOut.Signal();
    }
    iLock.Signal();
    if (isFull) {
        iSemIn.Wait();
    }
}

Msg* StarvationMonitor::Pull()
{
    Msg* msg;
    iLock.Wait();
    const TUint jiffies = Jiffies();
    if (iStatus == EBuffering && jiffies == 0 && !iPlannedHalt && !iHaltDelivered) {
        iHaltDelivered = true;
        iLock.Signal();
        return iMsgFactory.CreateMsgHalt(); // signal that we won't be providing any more audio for a while
    }
    TBool wait = false;
    if (iStatus == EBuffering && !iExit) {
        if (jiffies == 0 && iPlannedHalt && !iHaltDelivered) {
            wait = IsEmpty(); // allow reading of the halt msg which should be the last item in the queue
        }
        else {
            wait = true;
        }
        iSemOut.Clear(); /* Its possible for Enqueue to signal iSemOut repeatedly.
                            It is safe to clear any past signals here as Enqueue will try
                            to take iLock before signalling again. */
    }
    iLock.Signal();
    if (wait) {
        iSemOut.Wait();
    }
    msg = DoDequeue();
    return msg;
}

MsgAudio* StarvationMonitor::DoProcessMsgOut(MsgAudio* aMsg)
{
    if (aMsg->Jiffies() > kMaxAudioPullSize) {
        MsgAudio* remaining = aMsg->Split(kMaxAudioPullSize);
        EnqueueAtHead(remaining);
    }
    if (iClockPuller != NULL) {
        if (iJiffiesUntilNextHistoryPoint < aMsg->Jiffies()) {
            MsgAudio* remaining = aMsg->Split(static_cast<TUint>(iJiffiesUntilNextHistoryPoint));
            EnqueueAtHead(remaining);
        }
        iJiffiesUntilNextHistoryPoint -= aMsg->Jiffies();
        if (iJiffiesUntilNextHistoryPoint == 0) {
            iClockPuller->NotifySizeStarvationMonitor(Jiffies());
            iJiffiesUntilNextHistoryPoint = kUtilisationSamplePeriodJiffies;
        }
    }

    iLock.Wait();
    ASSERT(iExit || iStatus != EBuffering);
    TUint remainingSize = Jiffies();
    TBool enteredBuffering = false;
    if (!iPlannedHalt && (remainingSize < iStarvationThreshold) && (iStatus == ERunning)) {
        UpdateStatus(ERampingDown);
        iRampDownDuration = remainingSize + aMsg->Jiffies();
        iCurrentRampValue = Ramp::kRampMax;
        iRemainingRampSize = iRampDownDuration;
    }
    if (iStatus == ERampingDown) {
        Ramp(aMsg, Ramp::EDown);
        if (iCurrentRampValue == Ramp::kRampMin) {
            UpdateStatus(EBuffering);
            enteredBuffering = true;
        }
        if (remainingSize == 0) {
            ASSERT(iCurrentRampValue == Ramp::kRampMin);
        }
    }
    else if (iStatus == ERampingUp) {
        Ramp(aMsg, Ramp::EUp);
        /* don't check iCurrentRampValue here.  If our ramp up intersects with a ramp down
           from further up the pipeline, our ramp will end at a value less than Ramp::kRampMax */
        if (iRemainingRampSize == 0) {
            iCurrentRampValue = Ramp::kRampMax;
            UpdateStatus(ERunning);
        }
    }

    remainingSize = Jiffies(); // re-calculate this as Ramp() can cause a msg to be split with a fragment re-queued
    if (remainingSize == 0 && iStatus != EBuffering) {
        UpdateStatus(EBuffering);
        enteredBuffering = true;
    }
    if (((remainingSize < iNormalMax) && (remainingSize + aMsg->Jiffies() >= iNormalMax)) ||
        (enteredBuffering && (remainingSize >= iNormalMax))) {
        iSemIn.Signal();
    }
    iLock.Signal();

    return aMsg;
}

void StarvationMonitor::Ramp(MsgAudio* aMsg, Ramp::EDirection aDirection)
{
    if (aMsg->Jiffies() > iRemainingRampSize) {
        MsgAudio* remaining = aMsg->Split(iRemainingRampSize);
        EnqueueAtHead(remaining);
    }
    MsgAudio* split;
    iCurrentRampValue = aMsg->SetRamp(iCurrentRampValue, iRemainingRampSize, aDirection, split);
    if (split != NULL) {
        EnqueueAtHead(split);
    }
}

void StarvationMonitor::UpdateStatus(EStatus aStatus)
{
#if 0
    const TChar* status;
    switch (aStatus)
    {
    case ERunning:
        status = "Running";
        break;
    case ERampingDown:
        status = "RampingDown";
        break;
    case EBuffering:
        status = "Buffering";
        break;
    case ERampingUp:
        status = "RampingUp";
        break;
    default:
        status = "unknown(!)";
        break;
    }
    Log::Print("StarvationMonitor, updating status to %s\n", status);
#endif
    if (aStatus == EBuffering) {
        if (iStreamHandler != NULL) {
            iStreamHandler->NotifyStarving(iMode, iTrackId, iStreamId);
        }
        iObserver.NotifyStarvationMonitorBuffering(true);
    }
    else if (iStatus == EBuffering) {
        iObserver.NotifyStarvationMonitorBuffering(false);
    }
    iStatus = aStatus;
}

void StarvationMonitor::ProcessMsgIn(MsgHalt* /*aMsg*/)
{
    iLock.Wait();
    iPlannedHalt = true;
    iLock.Signal();
}

void StarvationMonitor::ProcessMsgIn(MsgFlush* /*aMsg*/)
{
    ASSERTS(); // don't expect flushes to propogate this far down the pipeline
}

void StarvationMonitor::ProcessMsgIn(MsgWait* /*aMsg*/)
{
    ASSERTS(); // don't expect MsgWait to propogate this far down the pipeline
}

void StarvationMonitor::ProcessMsgIn(MsgQuit* /*aMsg*/)
{
    iLock.Wait();
    iExit = true;
    iLock.Signal();
}

Msg* StarvationMonitor::ProcessMsgOut(MsgMode* aMsg)
{
    iMode.Replace(aMsg->Mode());
    iTrackId = UINT_MAX;
    iStreamId = UINT_MAX;
    if (iClockPuller != NULL) {
        iClockPuller->StopStarvationMonitor();
    }
    iClockPuller = aMsg->ClockPuller();
    return aMsg;
}

Msg* StarvationMonitor::ProcessMsgOut(MsgTrack* aMsg)
{
    iStreamHandler = NULL;
    iTrackId = aMsg->IdPipeline();
    iStreamId = UINT_MAX;
    return aMsg;
}

Msg* StarvationMonitor::ProcessMsgOut(MsgDecodedStream* aMsg)
{
    iStreamHandler = aMsg->StreamInfo().StreamHandler();
    iStreamId = aMsg->StreamInfo().StreamId();
    if (iClockPuller != NULL) {
        iClockPuller->NewStreamStarvationMonitor(iTrackId, iStreamId);
    }
    iJiffiesUntilNextHistoryPoint = kUtilisationSamplePeriodJiffies;
    return aMsg;
}

Msg* StarvationMonitor::ProcessMsgOut(MsgAudioPcm* aMsg)
{
    return DoProcessMsgOut(aMsg);
}

Msg* StarvationMonitor::ProcessMsgOut(MsgSilence* aMsg)
{
    return DoProcessMsgOut(aMsg);
}

Msg* StarvationMonitor::ProcessMsgOut(MsgHalt* aMsg)
{
    iLock.Wait();
    iHaltDelivered = true;
    iLock.Signal();
    return aMsg;
}

TBool StarvationMonitor::EnqueueWouldBlock() const
{
    AutoMutex a(iLock);
    return (Jiffies() >= iNormalMax);
}

TBool StarvationMonitor::PullWouldBlock() const
{
    AutoMutex a(iLock);
    if (IsEmpty() || (iStatus == EBuffering && Jiffies() < iNormalMax)) {
        return true;
    }
    return false;
}
