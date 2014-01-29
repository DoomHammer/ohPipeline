#ifndef HEADER_PRODUCT
#define HEADER_PRODUCT

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Av/InfoProvider.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/PowerManager.h>

#include <vector>

EXCEPTION(AvSourceNotFound);

namespace OpenHome {
namespace Configuration {
    class ConfigText;
    class IConfigManagerWriter;
    class IStoreReadWrite;
}
namespace Av {

class IReadStore;
class ISource;
class ProviderProduct;

class IProduct
{
public:
    virtual ~IProduct() {}
    virtual void Activate(ISource& aSource) = 0;
    virtual void NotifySourceNameChanged(ISource& aSource) = 0;
};

class IProductObserver
{
public:
    virtual ~IProductObserver() {}
    virtual void Started() = 0;
    virtual void RoomChanged() = 0;
    virtual void NameChanged() = 0;
    virtual void StandbyChanged() = 0;
    virtual void SourceIndexChanged() = 0;
    virtual void SourceXmlChanged() = 0;
};

class Product : private IProduct, private IInfoProvider, private INonCopyable
{
private:
    static const Brn kStartupSourceBase;
    static const TUint kMaxAttributeBytes = 1024;
public:
    static const TUint kConfigPrefixMaxBytes = 12;
    static const Brn kConfigIdRoomBase;
    static const Brn kConfigIdNameBase;
    static const TUint kMaxNameBytes = 20;
    static const TUint kMaxRoomBytes = 20;
public:
    Product(Net::DvDevice& aDevice, IReadStore& aReadStore, Configuration::IStoreReadWrite& aReadWriteStore,
            Configuration::IConfigManagerReader& aConfigReader, Configuration::IConfigManagerWriter& aConfigWriter,
            IPowerManager& aPowerManager, const Brx& aConfigPrefix);
    ~Product();
    void AddObserver(IProductObserver& aObserver);
    void Start();
    void AddSource(ISource* aSource);
    void AddAttribute(const TChar* aAttribute);
    void AddAttribute(const Brx& aAttribute);
    void GetManufacturerDetails(Brn& aName, Brn& aInfo, Brn& aUrl, Brn& aImageUri);
    void GetModelDetails(Brn& aName, Brn& aInfo, Brn& aUrl, Brn& aImageUri);
    void GetProductDetails(Bwx& aRoom, Bwx& aName, Brn& aInfo, Brn& aImageUri);
    TBool StandbyEnabled() const;
    void SetStandby(TBool aStandby);
    TUint SourceCount() const;
    TUint CurrentSourceIndex() const;
    void GetSourceXml(Bwx& aXml);
    void SetCurrentSource(TUint aIndex);
    void SetCurrentSource(const Brx& aName);
    void GetSourceDetails(TUint aIndex, Bwx& aSystemName, Bwx& aType, Bwx& aName, TBool& aVisible);
    const Brx& Attributes() const; // not thread-safe.  Assumes attributes are all set on a single thread during startup
    TUint SourceXmlChangeCount();
private:
    void AppendTag(Bwx& aXml, const TChar* aTag, const Brx& aValue);
    void GetConfigText(const Brx& aId, Bwx& aDest, const Brx& aDefault);
    void ProductRoomChanged(const Brx& aValue);
    void ProductNameChanged(const Brx& aValue);
private: // from IProduct
    void Activate(ISource& aSource);
    void NotifySourceNameChanged(ISource& aSource);
private: // from IInfoProvider
    void QueryInfo(const Brx& aQuery, IWriter& aWriter);
private:
    Net::DvDevice& iDevice; // do we need to store this?
    IReadStore& iReadStore;
    Configuration::IConfigManagerWriter& iConfigWriter;
    mutable Mutex iLock;
    Mutex iLockDetails;
    ProviderProduct* iProviderProduct;
    std::vector<IProductObserver*> iObservers;
    std::vector<ISource*> iSources;
    Bws<kMaxAttributeBytes> iAttributes;
    TBool iStarted;
    TBool iStandby;
    StoreText* iStartupSource;
    TUint iCurrentSource;
    TUint iSourceXmlChangeCount; // FIXME - isn't updated when source names/visibility change
    Bws<kConfigPrefixMaxBytes> iConfigPrefix;
    Configuration::ConfigText* iConfigProductRoom;
    Configuration::ConfigText* iConfigProductName;
    Bws<kMaxRoomBytes> iProductRoom;
    TUint iListenerIdProductRoom;
    Bws<kMaxNameBytes> iProductName;
    TUint iListenerIdProductName;
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_PRODUCT
