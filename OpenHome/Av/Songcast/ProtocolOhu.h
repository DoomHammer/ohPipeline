#ifndef HEADER_PROTOCOL_OHU
#define HEADER_PROTOCOL_OHU

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Songcast/ProtocolOhBase.h>
#include <OpenHome/Private/Network.h>

namespace OpenHome {
    class Environment;
    class IPowerManager;
    class Timer;
namespace Media {
    class TrackFactory;
}
namespace Av {

class IOhmMsgFactory;
class IOhmTimestamper;

class ProtocolOhu : public ProtocolOhBase
{
    static const TUint kTimerLeaveTimeoutMs = 50;
    static const TUint kMaxSlaveCount = 4;
public:
    ProtocolOhu(Environment& aEnv, IOhmMsgFactory& aFactory, Media::TrackFactory& aTrackFactory, IOhmTimestamper& aTimestamper, const Brx& aMode, IPowerManager& aPowerManager);
    ~ProtocolOhu();
private: // from ProtocolOhBase
    Media::ProtocolStreamResult Play(TIpAddress aInterface, TUint aTtl, const Endpoint& aEndpoint);
private: // from Media::Protocol
    void Interrupt(TBool aInterrupt);
private: // from IStreamHandler
    TUint TryStop(TUint aTrackId, TUint aStreamId);
    void NotifyStarving(const Brx& aMode, TUint aTrackId, TUint aStreamId);
private:
    void HandleAudio(const OhmHeader& aHeader);
    void HandleTrack(const OhmHeader& aHeader);
    void HandleMetatext(const OhmHeader& aHeader);
    void HandleSlave(const OhmHeader& aHeader);
    void Broadcast(OhmMsg* aMsg);
    void EmergencyStop();
    void SendLeave();
    void TimerLeaveExpired();
private:
    Mutex iLeaveLock;
    Timer* iTimerLeave;
    TBool iLeaving;
    TBool iStopped;
    TBool iActive;
    TBool iStarving;
    TUint iSlaveCount;
    Endpoint iSlaveList[kMaxSlaveCount];
    Bws<kMaxFrameBytes> iMessageBuffer;
    TUint iNextFlushId;
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_PROTOCOL_OHU
