#ifndef HEADER_MEDIAPLAYER
#define HEADER_MEDIAPLAYER

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/MuteManager.h>
#include <OpenHome/Media/VolumeManager.h>

namespace OpenHome {
    class Environment;
    class IPowerManager;
    class PowerManager;
namespace Net {
    class DvStack;
    class DvDeviceStandard;
}
namespace Media {
    class PipelineManager;
    class PipelineInitParams;
    class IPipelineDriver;
    class IMuteManager;
    class IVolume;  // XXX dummy volume hardware
    class IVolumeManagerLimits;
    class UriProvider;
    class Protocol;
    namespace Codec {
        class CodecBase;
    }
    class AllocatorInfoLogger;
    class LoggingPipelineObserver;
    class TrackFactory;
    class IPullableClock;
}
namespace Configuration {
    class ConfigManager;
    class IConfigManager;
    class IConfigInitialiser;
    class IStoreReadWrite;
    class ConfigText;
    class ProviderConfig;
}
namespace Net {
    class DvProvider;
}
namespace Av {

class ConfigInitialiserVolume;
class IReadStore;
class ISource;
class IStaticDataSource;
class IPersister;
class IProvider;
class Product;
class ProviderTime;
class ProviderInfo;
class ProviderVolume;
class KvpStore;
class NetworkMonitor;
class Credentials;

class IMediaPlayer
{
public:
    virtual ~IMediaPlayer() {}
    virtual Environment& Env() = 0;
    virtual Net::DvStack& DvStack() = 0;
    virtual Net::DvDeviceStandard& Device() = 0;
    virtual Media::PipelineManager& Pipeline() = 0;
    virtual Media::TrackFactory& TrackFactory() = 0;
    virtual IReadStore& ReadStore() = 0;
    virtual Configuration::IStoreReadWrite& ReadWriteStore() = 0;
    virtual Media::IPullableClock* PullableClock() = 0;
    virtual Configuration::IConfigManager& ConfigManager() = 0;
    virtual Configuration::IConfigInitialiser& ConfigInitialiser() = 0;
    virtual IPowerManager& PowerManager() = 0;
    virtual Av::Product& Product() = 0;
    virtual Credentials& CredentialsManager() = 0;
    virtual void Add(Media::UriProvider* aUriProvider) = 0;
    virtual void AddAttribute(const TChar* aAttribute) = 0;
};

class MediaPlayer : public IMediaPlayer, private INonCopyable
{
    static const TUint kTrackCount = 1200;
public:
    MediaPlayer(Net::DvStack& aDvStack, Net::DvDeviceStandard& aDevice,
                IStaticDataSource& aStaticDataSource,
                Configuration::IStoreReadWrite& aReadWriteStore,
                Media::PipelineInitParams* aPipelineInitParams,
                Media::IPipelineDriver& aPipelineDriver,
                Media::IPullableClock* aPullableClock,
                Media::IVolume& aVolumeLeft,
                Media::IVolume& aVolumeRight,
                const Brx& aEntropy,
                const Brx& aDefaultRoom,
                const Brx& aDefaultName);
    ~MediaPlayer();
    void Quit();
    void Add(Media::Codec::CodecBase* aCodec);
    void Add(Media::Protocol* aProtocol);
    void Add(ISource* aSource);
    void Start();
public: // from IMediaPlayer
    Environment& Env() override;
    Net::DvStack& DvStack() override;
    Net::DvDeviceStandard& Device() override;
    Media::PipelineManager& Pipeline() override;
    Media::TrackFactory& TrackFactory() override;
    IReadStore& ReadStore() override;
    Configuration::IStoreReadWrite& ReadWriteStore() override;
    Media::IPullableClock* PullableClock() override;
    Configuration::IConfigManager& ConfigManager() override;
    Configuration::IConfigInitialiser& ConfigInitialiser() override;
    IPowerManager& PowerManager() override;
    Av::Product& Product() override;
    Credentials& CredentialsManager() override;
    void Add(Media::UriProvider* aUriProvider) override;
    void AddAttribute(const TChar* aAttribute) override;
private:
    Net::DvStack& iDvStack;
    Net::DvDeviceStandard& iDevice;
    Media::AllocatorInfoLogger* iInfoLogger;
    KvpStore* iKvpStore;
    Media::PipelineManager* iPipeline;
    Media::TrackFactory* iTrackFactory;
    Configuration::IStoreReadWrite& iReadWriteStore;
    Media::IPullableClock* iPullableClock;
    Media::VolumeLimitNull iVolumeLimit;
    Media::VolumeBalanceStereo iVolumeBalanceStereo;
    Media::VolumeProfile iVolumeProfile;
    Media::MuteNull iMute;
    Configuration::ConfigManager* iConfigManager;
    OpenHome::PowerManager* iPowerManager;
    Configuration::ConfigText* iConfigProductRoom;
    Configuration::ConfigText* iConfigProductName;
    Av::Product* iProduct;
    Credentials* iCredentials;
    ProviderTime* iProviderTime;
    ProviderInfo* iProviderInfo;
    ConfigInitialiserVolume* iConfigInitVolume;
    IProvider* iProviderVolume;
    Configuration::ProviderConfig* iProviderConfig;
    NetworkMonitor* iNetworkMonitor;
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_MEDIAPLAYER
