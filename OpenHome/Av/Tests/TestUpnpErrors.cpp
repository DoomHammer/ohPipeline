#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Net/Core/CpDeviceUpnp.h>
#include <OpenHome/Av/UpnpAv/UpnpAv.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Net/Core/CpProxy.h>
#include <OpenHome/Net/Private/Error.h>
#include <Generated/CpUpnpOrgAVTransport1.h>
#include <OpenHome/Av/UpnpAv/ProviderAvTransport.h>
#include <Generated/CpUpnpOrgConnectionManager1.h>
#include <Generated/CpUpnpOrgRenderingControl1.h>
#include <OpenHome/Av/UpnpAv/ProviderRenderingControl.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

#define TEST_THROWS_PROXYERROR(aExp, aCode) \
    do { \
        const TChar* file = __FILE__; \
        try { \
            aExp; \
            OpenHome::TestFramework::Fail(file, __LINE__, #aExp, "ProxyError expected but not thrown"); \
        } \
        catch(ProxyError& aPe) { \
            if (aPe.Level() != Error::eUpnp) { \
                OpenHome::TestFramework::Fail(file, __LINE__, #aExp, "Wrong error level"); \
            } \
            else if (aPe.Code() == aCode) { \
                OpenHome::TestFramework::Succeed(file, __LINE__); \
            } \
            else { \
                char str[128]; \
                (void)sprintf(str, "Expected error code %d, got %d", aCode, (int)aPe.Code()); \
                OpenHome::TestFramework::Fail(file, __LINE__, #aExp, str); \
            } \
        } \
    } while(0)

#define TEST_THROWS_ACTION_NOT_IMPLEMENTED(aExp) TEST_THROWS_PROXYERROR(aExp, 501)

namespace OpenHome {
namespace Av {

class DummyAsyncOutput : private IAsyncOutput
{
public:
    void LogError(IAsync& aAsync);
    virtual ~DummyAsyncOutput() {}
private:
    void Output(const TChar* aKey, const TChar* aValue);
};

    
class DummySourceUpnpAv : public ISourceUpnpAv
{
private: // from ISourceUpnpAv
    void SetTrack(const Brx& aUri, const Brx& aMetaData);
    void Play();
    void Pause();
    void Stop();
    void Next();
    void Prev();
    void Seek(TUint aSecondsAbsolute);
};

class CpDevices : private INonCopyable
{
public:
    CpDevices(Semaphore& aAddedSem);
    ~CpDevices();
    void Start(CpStack& aCpStack, const Brx& aTargetUdn);
    CpDevice& Device();
private:
    void Added(CpDevice& aDevice);
    void Removed(CpDevice& aDevice);
private:
    Semaphore& iAddedSem;
    CpDevice* iDevice;
    CpDeviceList* iDeviceList;
};

class SuiteAvTransport : public TestFramework::Suite
{
public:
    SuiteAvTransport(CpDevice& aDevice);
private: // from Suite
    ~SuiteAvTransport();
    void Test();
private:
    CpProxyUpnpOrgAVTransport1* iProxy;
};

class SuiteConnectionManager : public TestFramework::Suite
{
public:
    SuiteConnectionManager(CpDevice& aDevice);
private: // from Suite
    ~SuiteConnectionManager();
    void Test();
private:
    CpProxyUpnpOrgConnectionManager1* iProxy;
};

class SuiteRenderingControl : public TestFramework::Suite
{
public:
    SuiteRenderingControl(CpDevice& aDevice);
private: // from Suite
    ~SuiteRenderingControl();
    void Test();
private:
    CpProxyUpnpOrgRenderingControl1* iProxy;
};

} // namespace Av
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Av;

static void RandomiseUdn(DvStack& aDvStack, Bwh& aUdn)
{
    aUdn.Grow(aUdn.Bytes() + 1 + Ascii::kMaxUintStringBytes + 1);
    aUdn.Append('-');
    Bws<Ascii::kMaxUintStringBytes> buf;
    srand((unsigned int)time(NULL));
    std::vector<NetworkAdapter*>* subnetList = aDvStack.Env().NetworkAdapterList().CreateSubnetList();
    TUint max = (*subnetList)[0]->Address();
    aDvStack.Env().NetworkAdapterList().DestroySubnetList(subnetList);
    (void)Ascii::AppendDec(buf, rand() % max);
    aUdn.Append(buf);
    aUdn.PtrZ();
}


// DummyAsyncOutput

void DummyAsyncOutput::LogError(IAsync& aAsync)
{
    aAsync.Output(*this);
}

void DummyAsyncOutput::Output(const TChar* /*aKey*/, const TChar* /*aValue*/)
{
}


// DummySourceUpnpAv

void DummySourceUpnpAv::SetTrack(const Brx& /*aUri*/, const Brx& /*aMetaData*/)
{
}

void DummySourceUpnpAv::Play()
{
}

void DummySourceUpnpAv::Pause()
{
}

void DummySourceUpnpAv::Stop()
{
}

void DummySourceUpnpAv::Next()
{
}

void DummySourceUpnpAv::Prev()
{
}

void DummySourceUpnpAv::Seek(TUint /*aSecondsAbsolute*/)
{
}


// CpDevices

CpDevices::CpDevices(Semaphore& aAddedSem)
    : iAddedSem(aAddedSem)
    , iDevice(NULL)
    , iDeviceList(NULL)
{
}

CpDevices::~CpDevices()
{
    delete iDeviceList;
    if (iDevice != NULL) {
        iDevice->RemoveRef();
    }
}

void CpDevices::Start(CpStack& aCpStack, const Brx& aTargetUdn)
{
    FunctorCpDevice added = MakeFunctorCpDevice(*this, &CpDevices::Added);
    FunctorCpDevice removed = MakeFunctorCpDevice(*this, &CpDevices::Removed);
    iDeviceList = new CpDeviceListUpnpUuid(aCpStack, aTargetUdn, added, removed);
}

CpDevice& CpDevices::Device()
{
    ASSERT(iDevice != NULL);
    return *iDevice;
}

void CpDevices::Added(CpDevice& aDevice)
{
    if (iDevice == NULL) {
        iDevice = &aDevice;
        iDevice->AddRef();
        iAddedSem.Signal();
    }
}

void CpDevices::Removed(CpDevice& /*aDevice*/)
{
}


// SuiteAvTransport

SuiteAvTransport::SuiteAvTransport(CpDevice& aDevice)
    : Suite("AVTransport")
{
    iProxy = new CpProxyUpnpOrgAVTransport1(aDevice);
}

SuiteAvTransport::~SuiteAvTransport()
{
    delete iProxy;
}

void SuiteAvTransport::Test()
{
    Brn empty(Brx::Empty());
    Brh outStr;
    TUint outUint;
    TInt outInt;

    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetNextAVTransportURI(0, empty, empty));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncRecord(0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetPlayMode(0, ProviderAvTransport::kCurrentPlayModeNormal));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetRecordQualityMode(0, empty));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetCurrentTransportActions(0, outStr));

    TEST_THROWS_PROXYERROR(iProxy->SyncSetAVTransportURI(1, empty, empty), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetAVTransportURI(UINT_MAX, empty, empty), 718);

    TEST_THROWS_PROXYERROR(iProxy->SyncGetMediaInfo(1, outUint, outStr, outStr, outStr, outStr, outStr, outStr, outStr, outStr), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMediaInfo(UINT_MAX, outUint, outStr, outStr, outStr, outStr, outStr, outStr, outStr, outStr), 718);

    TEST_THROWS_PROXYERROR(iProxy->SyncGetTransportInfo(1, outStr, outStr, outStr), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetTransportInfo(UINT_MAX, outStr, outStr, outStr), 718);

    TEST_THROWS_PROXYERROR(iProxy->SyncGetPositionInfo(1, outUint, outStr, outStr, outStr, outStr, outStr, outInt, outInt), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetPositionInfo(UINT_MAX, outUint, outStr, outStr, outStr, outStr, outStr, outInt, outInt), 718);

    TEST_THROWS_PROXYERROR(iProxy->SyncGetTransportSettings(1, outStr, outStr), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetTransportSettings(UINT_MAX, outStr, outStr), 718);

    TEST_THROWS_PROXYERROR(iProxy->SyncStop(1), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncStop(UINT_MAX), 718);

    TEST_THROWS_PROXYERROR(iProxy->SyncPlay(1, ProviderAvTransport::kTransportPlaySpeed1), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncPlay(UINT_MAX, ProviderAvTransport::kTransportPlaySpeed1), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncPlay(0, Brn("")), 717);
    TEST_THROWS_PROXYERROR(iProxy->SyncPlay(0, Brn("1/2")), 717);
    TEST_THROWS_PROXYERROR(iProxy->SyncPlay(0, Brn("2")), 717);
    TEST_THROWS_PROXYERROR(iProxy->SyncPlay(0, Brn("start")), 717);

    TEST_THROWS_PROXYERROR(iProxy->SyncPause(1), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncPause(UINT_MAX), 718);

    TEST_THROWS_PROXYERROR(iProxy->SyncSeek(1, ProviderAvTransport::kSeekModeTrackNr, Brn("0")), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncSeek(UINT_MAX, ProviderAvTransport::kSeekModeTrackNr, Brn("0")), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncSeek(0, ProviderAvTransport::kSeekModeTrackNr, Brn("2")), 711);
    TEST_THROWS_PROXYERROR(iProxy->SyncSeek(0, ProviderAvTransport::kSeekModeTrackNr, Brn("start")), 711);
    TEST_THROWS_PROXYERROR(iProxy->SyncSeek(0, ProviderAvTransport::kSeekModeAbsTime, Brn("1")), 711);
    TEST_THROWS_PROXYERROR(iProxy->SyncSeek(0, ProviderAvTransport::kSeekModeAbsTime, Brn("end")), 711);
    TEST_THROWS_PROXYERROR(iProxy->SyncSeek(0, ProviderAvTransport::kSeekModeRelTime, Brn("1")), 711);
    TEST_THROWS_PROXYERROR(iProxy->SyncSeek(0, ProviderAvTransport::kSeekModeRelTime, Brn("end")), 711);

    TEST_THROWS_PROXYERROR(iProxy->SyncNext(1), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncNext(UINT_MAX), 718);

    TEST_THROWS_PROXYERROR(iProxy->SyncPrevious(1), 718);
    TEST_THROWS_PROXYERROR(iProxy->SyncPrevious(UINT_MAX), 718);
}


// SuiteConnectionManager

SuiteConnectionManager::SuiteConnectionManager(CpDevice& aDevice)
    : Suite("ConnectionManager")
{
    iProxy = new CpProxyUpnpOrgConnectionManager1(aDevice);
}

SuiteConnectionManager::~SuiteConnectionManager()
{
    delete iProxy;
}

void SuiteConnectionManager::Test()
{
    Brn empty(Brx::Empty());
    Brh outStr;
    TInt outInt;

    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncPrepareForConnection(empty, empty, 0, Brn("Input"), outInt, outInt, outInt));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncConnectionComplete(0));

    TEST_THROWS_PROXYERROR(iProxy->SyncGetCurrentConnectionInfo(1, outInt, outInt, outStr, outStr, outInt, outStr, outStr), 706);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetCurrentConnectionInfo(UINT_MAX, outInt, outInt, outStr, outStr, outInt, outStr, outStr), 706);
}


// SuiteRenderingControl

SuiteRenderingControl::SuiteRenderingControl(CpDevice& aDevice)
    : Suite("RenderingControl")
{
    iProxy = new CpProxyUpnpOrgRenderingControl1(aDevice);
}

SuiteRenderingControl::~SuiteRenderingControl()
{
    delete iProxy;
}

void SuiteRenderingControl::Test()
{
    Brn empty(Brx::Empty());
    Brh outStr;
    TUint outUint;
    TInt outInt;
    TBool outBool;

    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetBrightness(0, outUint));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetBrightness(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetContrast(0, outUint));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetContrast(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetSharpness(0, outUint));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetSharpness(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetRedVideoGain(0, outUint));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetRedVideoGain(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetGreenVideoGain(0, outUint));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetGreenVideoGain(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetBlueVideoGain(0, outUint));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetBlueVideoGain(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetRedVideoBlackLevel(0, outUint));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetRedVideoBlackLevel(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetGreenVideoBlackLevel(0, outUint));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetGreenVideoBlackLevel(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetBlueVideoBlackLevel(0, outUint));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetBlueVideoBlackLevel(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetColorTemperature(0, outUint));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetColorTemperature(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetHorizontalKeystone(0, outInt));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetHorizontalKeystone(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetVerticalKeystone(0, outInt));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetVerticalKeystone(0, 0));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncGetLoudness(0, empty, outBool));
    TEST_THROWS_ACTION_NOT_IMPLEMENTED(iProxy->SyncSetLoudness(0, empty, false));

    TEST_THROWS_PROXYERROR(iProxy->SyncListPresets(1, outStr), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncListPresets(UINT_MAX, outStr), 702);

    TEST_THROWS_PROXYERROR(iProxy->SyncSelectPreset(1, ProviderRenderingControl::kPresetNameFactoryDefaults), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncSelectPreset(UINT_MAX, ProviderRenderingControl::kPresetNameFactoryDefaults), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncSelectPreset(0, Brn("")), 701);
    TEST_THROWS_PROXYERROR(iProxy->SyncSelectPreset(0, Brn("FactoryDefault")), 701);
    TEST_THROWS_PROXYERROR(iProxy->SyncSelectPreset(0, Brn("Default")), 701);
    TEST_THROWS_PROXYERROR(iProxy->SyncSelectPreset(0, Brn("Default")), 701);
    TEST_THROWS_PROXYERROR(iProxy->SyncSelectPreset(0, Brn("factorydefaults")), 701);

    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(1, ProviderRenderingControl::kChannelMaster, outBool), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(UINT_MAX, ProviderRenderingControl::kChannelMaster, outBool), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("master"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("LF"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("RF"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("CF"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("LFE"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("LS"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("RS"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("LFC"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("RFC"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("SD"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("SL"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("SR"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("T"), outBool), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetMute(0, Brn("E"), outBool), 402);

    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(1, ProviderRenderingControl::kChannelMaster, false), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(UINT_MAX, ProviderRenderingControl::kChannelMaster, false), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("master"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("LF"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("RF"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("CF"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("LFE"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("LS"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("RS"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("LFC"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("RFC"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("SD"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("SL"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("SR"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("T"), false), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetMute(0, Brn("E"), false), 402);

    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(1, ProviderRenderingControl::kChannelMaster, outUint), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(UINT_MAX, ProviderRenderingControl::kChannelMaster, outUint), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("master"), outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("LF"),  outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("RF"),  outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("CF"),  outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("LFE"), outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("LS"),  outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("RS"),  outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("LFC"), outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("RFC"), outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("SD"),  outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("SL"),  outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("SR"),  outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("T"),   outUint), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolume(0, Brn("E"),   outUint), 402);

    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(1, ProviderRenderingControl::kChannelMaster, 0), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(UINT_MAX, ProviderRenderingControl::kChannelMaster, 0), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("master"), 0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("LF"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("RF"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("CF"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("LFE"), 0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("LS"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("RS"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("LFC"), 0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("RFC"), 0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("SD"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("SL"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("SR"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("T"),   0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolume(0, Brn("E"),   0), 402);

    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(1, ProviderRenderingControl::kChannelMaster, outInt), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(UINT_MAX, ProviderRenderingControl::kChannelMaster, outInt), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("master"), outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("LF"),  outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("RF"),  outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("CF"),  outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("LFE"), outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("LS"),  outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("RS"),  outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("LFC"), outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("RFC"), outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("SD"),  outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("SL"),  outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("SR"),  outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("T"),   outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDB(0, Brn("E"),   outInt), 402);

    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(1, ProviderRenderingControl::kChannelMaster, 0), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(UINT_MAX, ProviderRenderingControl::kChannelMaster, 0), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("master"), 0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("LF"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("RF"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("CF"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("LFE"), 0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("LS"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("RS"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("LFC"), 0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("RFC"), 0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("SD"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("SL"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("SR"),  0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("T"),   0), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncSetVolumeDB(0, Brn("E"),   0), 402);

    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(1, ProviderRenderingControl::kChannelMaster, outInt, outInt), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(UINT_MAX, ProviderRenderingControl::kChannelMaster, outInt, outInt), 702);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("master"), outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("LF"),  outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("RF"),  outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("CF"),  outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("LFE"), outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("LS"),  outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("RS"),  outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("LFC"), outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("RFC"), outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("SD"),  outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("SL"),  outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("SR"),  outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("T"),   outInt, outInt), 402);
    TEST_THROWS_PROXYERROR(iProxy->SyncGetVolumeDBRange(0, Brn("E"),   outInt, outInt), 402);
}



void TestUpnpErrors(CpStack& aCpStack, DvStack& aDvStack)
{
    DummyAsyncOutput errorSuppressor;
    InitialisationParams& initParams = aDvStack.Env().InitParams();
    TUint oldMsearchTime = initParams.MsearchTimeSecs();
    FunctorAsync oldAsyncErrorHandler = initParams.AsyncErrorHandler();
    initParams.SetMsearchTime(1);
    initParams.SetAsyncErrorHandler(MakeFunctorAsync(errorSuppressor, &DummyAsyncOutput::LogError));

    DummySourceUpnpAv dummySource;
    Bwh udn("UpnpErrorTests");
    RandomiseUdn(aDvStack, udn);
    UpnpAv* upnpAv = new UpnpAv(aDvStack, dummySource, udn, "UpnpErrorTests", "None", "UpnpErrorTests", "");

    Semaphore sem("sem1", 0);
    CpDevices* cpDevices = new CpDevices(sem);
    cpDevices->Start(aCpStack, udn);
    sem.Wait(30*1000); // allow up to 30 seconds to find our one device
    CpDevice& cpDevice = cpDevices->Device();

    initParams.SetMsearchTime(oldMsearchTime);

    Runner runner("Upnp error reporting tests\n");
    runner.Add(new SuiteAvTransport(cpDevice));
    runner.Add(new SuiteConnectionManager(cpDevice));
    runner.Add(new SuiteRenderingControl(cpDevice));
    runner.Run();

    initParams.SetAsyncErrorHandler(oldAsyncErrorHandler);
    delete cpDevices;
    delete upnpAv;
}