#ifndef HEADER_PROVIDER_AVTRANSPORT
#define HEADER_PROVIDER_AVTRANSPORT

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <Generated/DvUpnpOrgAVTransport1.h>
#include <OpenHome/Net/Core/DvInvocationResponse.h>
#include <OpenHome/Media/Pipeline.h> // for IPipelineObserver - FIXME this to a more appropriate header

namespace OpenHome {
using namespace Net;
using namespace Media;
namespace Av {

// FIXME - move this elsewhere
class ISourceUpnpAv
{
public:
    virtual ~ISourceUpnpAv() {}
    virtual void SetTrack(const Brx& aUri, const Brx& aMetaData) = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Stop() = 0;
    virtual void Next() = 0;
    virtual void Prev() = 0;
    virtual void Seek(TUint aSecondsAbsolute) = 0;
};

class ProviderAvTransport : public DvProviderUpnpOrgAVTransport1, public IPipelineObserver
{
    typedef Bws<7> BwsTime; // H:MM:SS format
public:
    static const Brn kNotImplemented;
    static const Brn kTransportStateStopped;
    static const Brn kTransportStatePlaying;
    static const Brn kTransportStatePausedPlayback;
    static const Brn kTransportStateTransitioning;
    static const Brn kTransportStateNoMediaPresent;
    static const Brn kTransportStatusOk;
    static const Brn kTransportStatusErrorOccurred;
    static const Brn kCurrentMediaCategoryNoMedia;
    static const Brn kCurrentMediaCategoryTrackAware;
    static const Brn kCurrentMediaCategoryTrackUnaware;
    static const Brn kPlaybackStorageMediumNone;
    static const Brn kPlaybackStorageMediumNetwork;
    static const Brn kRecordStorageMediumNotImplemented;
    static const Brn kCurrentPlayModeNormal;
    static const Brn kTransportPlaySpeed1;
    static const Brn kRecordMediumWriteStatusNotImplemented;
    static const Brn kCurrentRecordQualityModeNotImplemented;
    static const Brn kSeekModeTrackNr;
    static const Brn kSeekModeAbsTime;
    static const Brn kSeekModeRelTime;
public:
    ProviderAvTransport(Net::DvDevice& aDevice, Environment& aEnv, ISourceUpnpAv& aSourceUpnpAv);
    ~ProviderAvTransport();
private: // from DvProviderUpnpOrgAvTransport1
    void SetAVTransportURI(IDvInvocation& aInvocation, TUint aInstanceID, const Brx& aCurrentURI, const Brx& aCurrentURIMetaData);
    void GetMediaInfo(IDvInvocation& aInvocation, TUint aInstanceID, IDvInvocationResponseUint& aNrTracks,
                      IDvInvocationResponseString& aMediaDuration, IDvInvocationResponseString& aCurrentURI,
                      IDvInvocationResponseString& aCurrentURIMetaData, IDvInvocationResponseString& aNextURI,
                      IDvInvocationResponseString& aNextURIMetaData, IDvInvocationResponseString& aPlayMedium,
                      IDvInvocationResponseString& aRecordMedium, IDvInvocationResponseString& aWriteStatus);
    void GetTransportInfo(IDvInvocation& aInvocation, TUint aInstanceID, IDvInvocationResponseString& aCurrentTransportState,
                          IDvInvocationResponseString& aCurrentTransportStatus, IDvInvocationResponseString& aCurrentSpeed);
    void GetPositionInfo(IDvInvocation& aInvocation, TUint aInstanceID, IDvInvocationResponseUint& aTrack,
                         IDvInvocationResponseString& aTrackDuration, IDvInvocationResponseString& aTrackMetaData,
                         IDvInvocationResponseString& aTrackURI, IDvInvocationResponseString& aRelTime, IDvInvocationResponseString& aAbsTime,
                         IDvInvocationResponseInt& aRelCount, IDvInvocationResponseInt& aAbsCount);
    void GetDeviceCapabilities(IDvInvocation& aInvocation, TUint aInstanceID, IDvInvocationResponseString& aPlayMedia, 
                               IDvInvocationResponseString& aRecMedia, IDvInvocationResponseString& aRecQualityModes);
    void GetTransportSettings(IDvInvocation& aInvocation, TUint aInstanceID, IDvInvocationResponseString& aPlayMode, IDvInvocationResponseString& aRecQualityMode);
    void Stop(IDvInvocation& aInvocation, TUint aInstanceID);
    void Play(IDvInvocation& aInvocation, TUint aInstanceID, const Brx& aSpeed);
    void Pause(IDvInvocation& aInvocation, TUint aInstanceID);
    void Seek(IDvInvocation& aInvocation, TUint aInstanceID, const Brx& aUnit, const Brx& aTarget);
    void Next(IDvInvocation& aInvocation, TUint aInstanceID);
    void Previous(IDvInvocation& aInvocation, TUint aInstanceID);
private: // from IPipelineObserver
    void NotifyPipelineState(EPipelineState aState);
    void NotifyTrack(Track& aTrack, const Brx& aMode, TUint aIdPipeline);
    void NotifyMetaText(const Brx& aText);
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds);
    void NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo);
private:
    void QueueStateUpdate();
    void ModerationTimerExpired();
    void UpdateEventedState();
    void AddStateVariable(const Brx& aName, const Brx& aValue);
    void AddStateVariable(const Brx& aName, TUint aValue);
    void AddStateVariableEscaped(const Brx& aName, const Brx& aValue);
    static void SecondsToTimeString(TUint aSeconds, Bwx& aTime);
    static TUint TimeStringToSeconds(const Brx& aTime);
private:
    ISourceUpnpAv& iSourceUpnpAv;
    Mutex iLock;
    Timer* iModerationTimer;
    TBool iModerationTimerStarted;
    Bws<6*1024> iXmlEscapedStateVar;

    // These state variables are currently implemented and their values change
    Brn iTransportState;
    Brn iTransportStatus;
    Brn iCurrentMediaCategory;
    Brn iPlaybackStorageMedium;
    TUint32 iNumberOfTracks;
    TUint32 iCurrentTrack;
    BwsTime iTrackDuration;
    BwsTrackUri iCurrentTrackUri;
    BwsTrackUri iAvTransportUri;
    BwsTrackMetaData iCurrentTrackMetaData;
    BwsTrackMetaData iAvTransportUriMetaData;

    // These state variables are currently implemented but their values do not change
    const Brn iPossiblePlaybackStorageMedia;
    const Brn iCurrentPlayMode;
    const Brn iTransportPlaySpeed;

    // These state variables are currently unimplemented but may be in the future
    const Brn iNextAvTransportUri;
    const Brn iNextAvTransportUriMetaData;

    // These state variables will not be implemented in the forseeable future
    const Brn iRecordStorageMedium;
    const Brn iPossibleRecordStorageMedia;
    const Brn iRecordMediumWriteStatus;
    const Brn iCurrentRecordQualityMode;
    const Brn iPossibleRecordQualityModes;

    // These state variables should never be evented
    TUint iRelativeTimeSeconds;
    const Bws<16> iAbsoluteTimePosition;
    const TInt32 iRelativeCounterPosition;
    const TInt32 iAbsoluteCounterPosition;

    // buffer storing an aggregated version of the above state variables
    Bws<20*1024> iEventedState;
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_PROVIDER_AVTRANSPORT
