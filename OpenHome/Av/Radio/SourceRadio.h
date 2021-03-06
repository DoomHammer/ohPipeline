#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/PipelineObserver.h>

namespace OpenHome {
    class Environment;
    class PowerManager;
namespace Net {
    class DvDevice;
}
namespace Media {
    class PipelineManager;
    class UriProviderSingleTrack;
    class MimeTypeList;
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
class Credentials;

class SourceRadio : public Source, private ISourceRadio, private Media::IPipelineObserver
{
public:
    SourceRadio(Environment& aEnv, Net::DvDevice& aDevice, Media::PipelineManager& aPipeline,
                Media::UriProviderSingleTrack& aUriProvider, const Brx& aTuneInPartnerId,
                Configuration::IConfigInitialiser& aConfigInit,
                Credentials& aCredentialsManager, Media::MimeTypeList& aMimeTypeList,
                IPowerManager& aPowerManager);
    ~SourceRadio();
private: // from ISource
    void Activate() override;
    void Deactivate() override;
    void StandbyEnabled() override;
    void PipelineStopped() override;
private: // from ISourceRadio
    void Fetch(const Brx& aUri, const Brx& aMetaData) override;
    void Play() override;
    void Pause() override;
    void Stop() override;
    void SeekAbsolute(TUint aSeconds) override;
    void SeekRelative(TUint aSeconds) override;
private: // from IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyMode(const Brx& aMode, const Media::ModeInfo& aInfo) override;
    void NotifyTrack(Media::Track& aTrack, const Brx& aMode, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
private:
    Mutex iLock;
    Media::UriProviderSingleTrack& iUriProvider;
    ProviderRadio* iProviderRadio;
    PresetDatabase* iPresetDatabase;
    RadioPresetsTuneIn* iTuneIn;
    Media::Track* iTrack;
    TUint iTrackPosSeconds;
    TUint iStreamId;
    TBool iLive;
};

} // namespace Av
} // namespace OpenHome

