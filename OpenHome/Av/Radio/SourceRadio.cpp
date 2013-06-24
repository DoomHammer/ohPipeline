#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Av/Radio/PresetDatabase.h>
#include <OpenHome/Av/Radio/ProviderRadio.h>
#include <OpenHome/Av/Radio/TuneIn.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>
#include <OpenHome/Av/KvpStore.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/MediaPlayer.h>

#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;
using namespace OpenHome::Media;

// SourceFactory

ISource* SourceFactory::NewRadio(IMediaPlayer& aMediaPlayer, const TChar* aSupportedProtocols)
{
    UriProviderSingleTrack* radioUriProvider = new UriProviderSingleTrack("Radio", aMediaPlayer.TrackFactory());
    aMediaPlayer.Add(radioUriProvider);
    return new SourceRadio(aMediaPlayer.Env(), aMediaPlayer.Device(), aMediaPlayer.Pipeline(), *radioUriProvider, aSupportedProtocols, aMediaPlayer.ReadStore());
}


// SourceRadio

SourceRadio::SourceRadio(Environment& aEnv, DvDevice& aDevice, PipelineManager& aPipeline, UriProviderSingleTrack& aUriProvider, const TChar* aProtocolInfo, IReadStore& aReadStore)
    : Source("Radio", "Radio")
    , iLock("SRAD")
    , iPipeline(aPipeline)
    , iUriProvider(aUriProvider)
    , iTrack(NULL)
    , iTrackPosSeconds(0)
    , iPipelineTrackId(UINT_MAX)
    , iStreamId(UINT_MAX)
    , iTransportState(Media::EPipelineStopped)
{
    iPresetDatabase = new PresetDatabase();
    iProviderRadio = new ProviderRadio(aDevice, *this, *iPresetDatabase, aProtocolInfo);
    Bws<40> username;
    if (aReadStore.TryReadStoreItem(Brn("Radio.TuneInUserName"), username)) {
        iTuneIn = new RadioPresetsTuneIn(aEnv, *iPresetDatabase, username);
    }
    else {
        iTuneIn = NULL; // FIXME - should maybe just initialise iTuneIn anyway (once we allow runtime change of rwstore anyway)
    }
    iPipeline.AddObserver(*this);
}

SourceRadio::~SourceRadio()
{
    delete iTuneIn;
    delete iPresetDatabase;
    delete iProviderRadio;
}

void SourceRadio::Activate()
{
    iTrackPosSeconds = 0;
    iActive = true;
}

void SourceRadio::Deactivate()
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

void SourceRadio::Fetch(const Brx& aUri, const Brx& aMetaData)
{
    if (!IsActive()) {
        DoActivate();
    }
    if (iTrack == NULL || iTrack->Uri() != aUri) {
        iPipeline.RemoveAll();
        if (iTrack != NULL) {
            iTrack->RemoveRef();
        }
        iTrack = iUriProvider.SetTrack(aUri, aMetaData);
        iPipeline.Begin(iUriProvider.Mode(), iTrack->Id());
        if (iTransportState == Media::EPipelinePlaying) {
            iPipeline.Play();
        }
    }
}

void SourceRadio::Play()
{
    if (!IsActive()) {
        DoActivate();
    }
    iLock.Wait();
    iTransportState = Media::EPipelinePlaying;
    iLock.Signal();
    iPipeline.Play();
}

void SourceRadio::Pause()
{
    if (IsActive()) {
        iLock.Wait();
        iTransportState = Media::EPipelinePaused;
        iLock.Signal();
        iPipeline.Pause();
    }
}

void SourceRadio::Stop()
{
    if (IsActive()) {
        iLock.Wait();
        iTransportState = Media::EPipelineStopped;
        iLock.Signal();
        iPipeline.Stop();
    }
}

void SourceRadio::SeekAbsolute(TUint aSeconds)
{
    if (IsActive()) {
        (void)iPipeline.Seek(iPipelineTrackId, iStreamId, aSeconds);
    }
}

void SourceRadio::SeekRelative(TUint aSeconds)
{
    SeekAbsolute(aSeconds + iTrackPosSeconds);
}

void SourceRadio::NotifyPipelineState(EPipelineState aState)
{
    if (IsActive()) {
        iProviderRadio->SetTransportState(aState);
    }
}

void SourceRadio::NotifyTrack(Track& aTrack, const Brx& /*aMode*/, TUint aIdPipeline)
{
    iLock.Wait();
    if (iTrack != NULL) {
        iTrack->RemoveRef();
    }
    iTrack = &aTrack;
    iPipelineTrackId = aIdPipeline;
    iLock.Signal();
}

void SourceRadio::NotifyMetaText(const Brx& /*aText*/)
{
}

void SourceRadio::NotifyTime(TUint aSeconds, TUint /*aTrackDurationSeconds*/)
{
    iLock.Wait();
    iTrackPosSeconds = aSeconds;
    iLock.Signal();
}

void SourceRadio::NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo)
{
    iLock.Wait();
    iStreamId = aStreamInfo.StreamId();
    iLock.Signal();
}