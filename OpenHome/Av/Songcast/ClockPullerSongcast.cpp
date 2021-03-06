#include <OpenHome/Av/Songcast/ClockPullerSongcast.h>
#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/ClockPullerUtilisation.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Av/Debug.h>

#include <cstdlib>
#include <vector>
#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// ClockPullerNonTimestamped

ClockPullerNonTimestamped::ClockPullerNonTimestamped(Environment& aEnv)
    : iPuller(aEnv)
    , iLock("CPNT")
    , iEnabled(true)
{
}

inline IClockPullerReservoir& ClockPullerNonTimestamped::Puller()
{
    return static_cast<IClockPullerReservoir&>(iPuller);
}

void ClockPullerNonTimestamped::SetEnabled(TBool aEnabled)
{
    AutoMutex _(iLock);
    iEnabled = aEnabled;
    if (!aEnabled) {
        Puller().Reset();
    }
}

void ClockPullerNonTimestamped::NewStream(TUint aSampleRate)
{
    AutoMutex _(iLock);
    if (iEnabled) {
        Puller().NewStream(aSampleRate);
    }
}

void ClockPullerNonTimestamped::Reset()
{
    AutoMutex _(iLock);
    if (iEnabled) {
        Puller().Reset();
    }
}

void ClockPullerNonTimestamped::Stop()
{
    AutoMutex _(iLock);
    if (iEnabled) {
        Puller().Stop();
    }
}

void ClockPullerNonTimestamped::Start(TUint aNotificationFrequency)
{
    AutoMutex _(iLock);
    if (iEnabled) {
        Puller().Start(aNotificationFrequency);
    }
}

TUint ClockPullerNonTimestamped::NotifySize(TUint aJiffies)
{
    AutoMutex _(iLock);
    if (iEnabled) {
        return Puller().NotifySize(aJiffies);
    }
    return IPullableClock::kNominalFreq;
}


// ClockPullerSongcast

ClockPullerSongcast::ClockPullerSongcast(Environment& aEnv)
    : iPullerReservoirLeft(aEnv)
    , iPullerReservoirRight(aEnv)
{
    iTimestampHistory.reserve(kMaxHistoryElements);
}

ModeClockPullers ClockPullerSongcast::ClockPullers()
{
    return ModeClockPullers(&iPullerReservoirLeft, &iPullerReservoirRight, this);
}

void ClockPullerSongcast::ResetTimestampHistory()
{
    iStoreNetworkTimestamp = true;
    iTimestampHistory.clear();
    iNextHistoryIndex = 0;
    iTimestampTotalDrift = 0;
    iNetworkTimestampStart = 0;
    iNetworkTimestampLast = 0;
    iNetworkTimeOverflowCount = 0;
}

void ClockPullerSongcast::SmoothTimestamps(TInt& aDrift, TInt aIndexToSkip)
{
    iTimestampTotalDrift += aDrift;

    if (iTimestampHistory.size() == 0) {
        return;
    }

    const TUint absDeviation = (TUint)std::abs(aDrift);
    TInt i;

    if (absDeviation >= iTimestampHistory.size()) {
        TInt sharePerElement = aDrift / (TInt)iTimestampHistory.size();
        for (i=0; i<(TInt)iTimestampHistory.size(); i++) {
            if (i != aIndexToSkip) {
                iTimestampHistory[i] += sharePerElement;
                aDrift -= sharePerElement;
            }
        }
    }
    // share the remaining [0..size()) amongst the elements with longest to live
    TInt rem = aDrift % iTimestampHistory.size();
    if (rem != 0) {
        rem--; // leave aDrift == +/-1
        TInt adjustment = (aDrift > 0? 1 : -1);
        for (i=aIndexToSkip-1; i>=0 && rem>0; i--) {
            iTimestampHistory[i] += adjustment;
        }
        for (i=iTimestampHistory.size()-1; i>aIndexToSkip; i--) {
            iTimestampHistory[i] += adjustment;
        }
        aDrift = adjustment;
    }
}

void ClockPullerSongcast::NewStream(TUint aSampleRate)
{
    // FIXME - should only need to reset if aSampleRate implies a change in clock from the audio producer
    iTimestampMaxAllowedDrift = iTimestampHistory.capacity() *
        static_cast<TUint64>(
        Jiffies::ToSongcastTime(kMaxExpectedDeviation, aSampleRate));
    Reset();
    iSampleRate = aSampleRate;
}

void ClockPullerSongcast::Reset()
{
    iMultiplier = IPullableClock::kNominalFreq;
    ResetTimestampHistory();
}

void ClockPullerSongcast::Stop()
{
    iUseTimestamps = false;
    Reset();
    iPullerReservoirLeft.SetEnabled(true);
    iPullerReservoirRight.SetEnabled(true);
}

void ClockPullerSongcast::Start()
{
    iUseTimestamps = true;
    iPullerReservoirLeft.SetEnabled(false);
    iPullerReservoirRight.SetEnabled(false);
}

TUint ClockPullerSongcast::NotifyTimestamp(TInt aDrift, TUint aNetwork)
{
    if (iStoreNetworkTimestamp) {
        iNetworkTimestampStart = aNetwork;
        iStoreNetworkTimestamp = false;
    }
    else if (aNetwork < iNetworkTimestampLast) {
        if (iNetworkTimestampLast - aNetwork > UINT_MAX/2) {
            LOG2(kError, kSongcast, "WARNING: songcast sender clock appears unreliable (moved backwards)\n");
        }
        else {
            iNetworkTimeOverflowCount++;
        }
    }
    iNetworkTimestampLast = aNetwork;

    if (iTimestampHistory.size() < iTimestampHistory.capacity()) {
        SmoothTimestamps(aDrift, -1);
        iTimestampHistory.push_back(aDrift);
    }
    else {
        iTimestampTotalDrift -= iTimestampHistory[iNextHistoryIndex];
        SmoothTimestamps(aDrift, iNextHistoryIndex);
        iTimestampHistory[iNextHistoryIndex++] = aDrift;
        if (iNextHistoryIndex == iTimestampHistory.capacity()) {
            iNextHistoryIndex = 0;
        }

        if ((TUint)std::abs(iTimestampTotalDrift) > iTimestampMaxAllowedDrift) {
            const TUint64 networkTimeElapsed = (static_cast<TUint64>(iNetworkTimeOverflowCount)* UINT_MAX) +
                static_cast<TInt>(aNetwork - iNetworkTimestampStart);
            const TUint64 period = Jiffies::FromSongcastTime(networkTimeElapsed, iSampleRate);
            TInt drift = (TInt)Jiffies::FromSongcastTime(std::abs(iTimestampTotalDrift)/iTimestampHistory.size(), iSampleRate);
            if (iTimestampTotalDrift < 0) {
                drift *= -1;
            }
            ClockPullerUtils::PullClock(iMultiplier, drift, period);
            ResetTimestampHistory();
        }
    }
    return iMultiplier;
}
