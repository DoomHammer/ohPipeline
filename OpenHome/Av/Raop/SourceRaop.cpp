#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/Raop/SourceRaop.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Av/Raop/Raop.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/MediaPlayer.h>

#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;
using namespace OpenHome::Net;


// SourceFactory
ISource* SourceFactory::NewRaop(IMediaPlayer& aMediaPlayer, const Brx& aDeviceName, TUint aDiscoveryPort)
{ // static
    UriProviderSingleTrack* raopUriProvider = new UriProviderSingleTrack("RAOP", aMediaPlayer.TrackFactory());
    aMediaPlayer.Add(raopUriProvider);
    return new SourceRaop(aMediaPlayer.Env(), aMediaPlayer.DvStack(), aMediaPlayer.Pipeline(), *raopUriProvider, aDeviceName, aDiscoveryPort);
}


// SourceRaop

SourceRaop::SourceRaop(Environment& aEnv, Net::DvStack& aDvStack, Media::PipelineManager& aPipeline, Media::UriProviderSingleTrack& aUriProvider, const Brx& aDeviceName, TUint aDiscoveryPort)
    : Source("Net Aux", "Net Aux")
    , iLock("SRAO")
    , iPipeline(aPipeline)
    , iUriProvider(aUriProvider)
    , iTrack(NULL)
    , iTrackPosSeconds(0)
    , iPipelineTrackId(UINT_MAX)
    , iStreamId(UINT_MAX)
    , iTransportState(Media::EPipelineStopped)
{
    iRaopDiscovery = new RaopDiscovery(aEnv, aDvStack, *this, aDeviceName, aDiscoveryPort);
    iPipeline.Add(ProtocolFactory::NewRaop(aEnv, *iRaopDiscovery)); // bypassing MediaPlayer
    iPipeline.AddObserver(*this);
}

SourceRaop::~SourceRaop()
{
    delete iRaopDiscovery;
}

IRaopDiscovery& SourceRaop::Discovery()
{
    return *iRaopDiscovery;
}

void SourceRaop::Activate()
{
    iTrackPosSeconds = 0;
    iActive = true;
}

void SourceRaop::Deactivate()
{
    iLock.Wait();
    iTransportState = Media::EPipelineStopped;
    if (iTrack != NULL) {
        iTrack->RemoveRef();
        iTrack = NULL;
    }
    iLock.Signal();
    Source::Deactivate();
}

void SourceRaop::NotifyStreamStart()
    // FIXME - should probably reconstruct the uri from params to this method (or take the constructed uri as a param)
    // and check if the existing track->uri() matches it (if not, or if doesn't exist, should clear and generate new one, if using
    // it to communicate udp port, otherwise reuse existing one)
{
    if (!IsActive()) {
        DoActivate();
    }
    if (iTrack == NULL) {
        iPipeline.RemoveAll();
        if (iTrack != NULL) {
            iTrack->RemoveRef();
        }
        iTrack = iUriProvider.SetTrack(Brn("raop://dummyuri"), Brn(""));
        iPipeline.Begin(iUriProvider.Mode(), iTrack->Id());
    }
    iLock.Wait();
    iTransportState = Media::EPipelinePlaying;
    iLock.Signal();
    iPipeline.Play();
}

void SourceRaop::NotifyPipelineState(Media::EPipelineState aState)
{
    iLock.Wait();
    iTransportState = aState;
    iLock.Signal();
}

void SourceRaop::NotifyTrack(Media::Track& aTrack, const Brx& /*aMode*/, TUint aIdPipeline)
{
    iLock.Wait();
    if (iTrack != NULL) {
        iTrack->RemoveRef();
    }
    iTrack = &aTrack;
    iPipelineTrackId = aIdPipeline;
    iLock.Signal();
}

void SourceRaop::NotifyMetaText(const Brx& /*aText*/)
{
}

void SourceRaop::NotifyTime(TUint aSeconds, TUint /*aTrackDurationSeconds*/)
{
    iLock.Wait();
    iTrackPosSeconds = aSeconds;
    iLock.Signal();
}

void SourceRaop::NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo)
{
    iLock.Wait();
    iStreamId = aStreamInfo.StreamId();
    iLock.Signal();
}