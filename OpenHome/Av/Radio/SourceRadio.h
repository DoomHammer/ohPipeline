#ifndef HEADER_SOURCE_RADIO
#define HEADER_SOURCE_RADIO

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Media/Pipeline/Msg.h>

namespace OpenHome {
    class Environment;
namespace Net {
    class DvDevice;
}
namespace Media {
    class PipelineManager;
    class UriProviderSingleTrack;
}
namespace Av {

class ISourceRadio
{
public:
    virtual ~ISourceRadio() {}
    virtual void Fetch(const Brx& aUri, const Brx& aMetaData) = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Stop() = 0;
    virtual void SeekAbsolute(TUint aSeconds) = 0;
    virtual void SeekRelative(TUint aSeconds) = 0;
};

class PresetDatabase;
class ProviderRadio;
class RadioPresetsTuneIn;
class IReadStore;

class SourceRadio : public Source, private ISourceRadio, private Media::IPipelineObserver
{
public:
    SourceRadio(Environment& aEnv, Net::DvDevice& aDevice, Media::PipelineManager& aPipeline, Media::UriProviderSingleTrack& aUriProvider, const Brx& aProtocolInfo, Configuration::IConfigManagerInitialiser& aConfigInit);
    ~SourceRadio();
private: // from ISource
    void Activate();
    void Deactivate();
        void PipelineStopped();
private: // from ISourceRadio
    void Fetch(const Brx& aUri, const Brx& aMetaData);
    void Play();
    void Pause();
    void Stop();
    void SeekAbsolute(TUint aSeconds);
    void SeekRelative(TUint aSeconds);
private: // from IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState);
    void NotifyTrack(Media::Track& aTrack, const Brx& aMode, TUint aIdPipeline);
    void NotifyMetaText(const Brx& aText);
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds);
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo);
private:
    Mutex iLock;
    Media::PipelineManager& iPipeline;
    Media::UriProviderSingleTrack& iUriProvider;
    ProviderRadio* iProviderRadio;
    PresetDatabase* iPresetDatabase;
    RadioPresetsTuneIn* iTuneIn;
    Media::Track* iTrack;
    TUint iTrackPosSeconds;
    TUint iPipelineTrackId;
    TUint iStreamId;
    Media::EPipelineState iTransportState;
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_SOURCE_RADIO
