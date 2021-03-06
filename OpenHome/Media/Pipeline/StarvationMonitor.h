#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <atomic>

namespace OpenHome {
namespace Media {

class IStarvationMonitorObserver
{
public:
    virtual void NotifyStarvationMonitorBuffering(TBool aBuffering) = 0;
};

/*
Fixed buffer which implements a delay (poss ~100ms) to allow time for songcast sending

- Pulls audio, stopping pulling whenever it grows above a 'normal use' threshold (NormalSize).
- After emptying (seeing a halt msg or starting to ramp down), enters 'buffering' mode and 
  pulls until it reaches NormalSize.  Doesn't deliver any audio to rhs while in buffering mode.
- If halt msg encountered, allows buffer to be exhausted without ramping down.
- If no halt msg, starts ramping down once less that StarvationThreshold of data remains.
- On exit from buffering mode, ramps up iff ramped down before buffering and still playing the same stream.
*/
    
class IPipelineElementObserverThread;
class IClockPuller;

class StarvationMonitor : private MsgReservoir, public IPipelineElementUpstream
{
    friend class SuiteStarvationMonitor;
public:
    StarvationMonitor(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement,
                      IStarvationMonitorObserver& aObserver, IPipelineElementObserverThread& aObserverThread,
                      TUint aThreadPriority, TUint aNormalSize, TUint aStarvationThreshold, TUint aRampUpSize, TUint aMaxStreamCount);
    ~StarvationMonitor();
public: // from IPipelineElementUpstream
    Msg* Pull() override;
private:
    enum EStatus
    {
        ERunning
       ,ERampingDown
       ,EBuffering
       ,ERampingUp
    };
private:
    void PullerThread();
    void Enqueue(Msg* aMsg);
    MsgAudio* ProcessAudioOut(MsgAudio* aMsg);
    void Ramp(MsgAudio* aMsg, Ramp::EDirection aDirection);
    void UpdateStatus(EStatus aStatus);
    void EventCallback();
private: // from MsgReservoir
    void ProcessMsgIn(MsgDrain* aMsg) override;
    void ProcessMsgIn(MsgStreamInterrupted* aMsg) override;
    void ProcessMsgIn(MsgHalt* aMsg) override;
    void ProcessMsgIn(MsgFlush* aMsg) override;
    void ProcessMsgIn(MsgWait* aMsg) override;
    void ProcessMsgIn(MsgQuit* aMsg) override;
    Msg* ProcessMsgOut(MsgMode* aMsg) override;
    Msg* ProcessMsgOut(MsgDrain* aMsg) override;
    Msg* ProcessMsgOut(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsgOut(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsgOut(MsgSilence* aMsg) override;
    Msg* ProcessMsgOut(MsgHalt* aMsg) override;
    Msg* ProcessMsgOut(MsgQuit* aMsg) override;
private: // test helpers
    TBool EnqueueWouldBlock() const;
    TBool PullWouldBlock() const;
private:
    static const TUint kMaxAudioPullSize = Jiffies::kPerMs * 5;
    static const TUint kUtilisationSamplePeriodJiffies = Jiffies::kPerSecond / 10;
    MsgFactory& iMsgFactory;
    IPipelineElementUpstream& iUpstreamElement;
    IStarvationMonitorObserver& iObserver;
    IPipelineElementObserverThread& iObserverThread;
    IClockPullerReservoir* iClockPuller;
    ThreadFunctor* iThread;
    const TUint iNormalMax;
    const TUint iStarvationThreshold;
    const TUint iRampUpSize;
    const TUint iMaxStreamCount;
    mutable Mutex iLock;
    Semaphore iSemIn;
    Semaphore iSemOut;
    EStatus iStatus;
    TUint iCurrentRampValue;
    TUint iRampDownDuration;
    TUint iRemainingRampSize;
    TBool iPlannedHalt;
    TBool iHaltDelivered;
    TBool iExit;
    TBool iTrackIsPullable;
    TUint iPriorityMsgCount; // number of queued msgs that must be delivered asap
    TUint64 iJiffiesUntilNextHistoryPoint;
    IStreamHandler* iStreamHandler;
    BwsMode iMode;
    TUint iStreamId;
    TUint iRampUntilStreamOutCount;
    TUint iEventId;
    std::atomic<bool> iEventBuffering;
    TBool iLastEventBuffering;
};

} // namespace Media
} // namespace OpenHome

