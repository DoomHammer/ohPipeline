#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Av/UpnpAv/UpnpAv.h>
#include <OpenHome/Media/Tests/AllocatorInfoLogger.h>
#include <OpenHome/Media/Tests/SongcastingDriver.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>

// RadioDatabase
#include <OpenHome/Media/Msg.h>
#include <array>

#ifdef _WIN32
#if !defined(CDECL)
# define CDECL __cdecl
#endif

# include <conio.h>

int mygetch()
{
    return (_getch());
}

#elif defined(NOTERMIOS)

#define CDECL

int mygetch()
{
    return 0;
}

#else

# define CDECL

# include <termios.h>
# include <unistd.h>

int mygetch()
{
	struct termios oldt, newt;
	int ch;
	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	ch = getchar();
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	return ch;
}

#endif // _WIN32

namespace OpenHome {
namespace Av {
    
class RadioDatabase;

class TestRadio : private Media::IPipelineObserver
{
    static const TUint kTrackCount = 3;
public:
    TestRadio(Net::DvStack& aDvStack, TIpAddress aAdapter, const Brx& aSenderUdn, const TChar* aSenderFriendlyName, TUint aSenderChannel);
    ~TestRadio();
    void Run(RadioDatabase& aDb);
private: // from Media::IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState);
    void NotifyTrack(Media::Track& aTrack, TUint aIdPipeline);
    void NotifyMetaText(const Brx& aText);
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds);
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo);
private:
    Media::AllocatorInfoLogger iInfoLogger;
    Media::PipelineManager* iPipeline;
    Media::TrackFactory* iTrackFactory;
    Media::SimpleSongcastingDriver* iDriver;
    Media::UriProviderSingleTrack* iUriProvider;
    //DummySourceUpnpAv* iSourceUpnpAv;
};

class IRadioDatbaseObserver
{
public:
    ~IRadioDatbaseObserver() {}
    virtual void RadioDatabaseChanged() = 0;
};

class RadioDatabase
{
public:
    static const TUint kMaxPresets = 100;
    static const TUint kPresetIdNone = 0;
public:
    RadioDatabase(IRadioDatbaseObserver& aObserver);
    ~RadioDatabase();

    void GetIdArray(std::array<TUint, kMaxPresets>& aIdArray) const;
    TUint SequenceNumber() const;
    void BeginSetPresets();
    void GetPreset(TUint aIndex, TUint& aId, Bwx& aMetaData) const;
    TBool TryGetPresetById(TUint aId, Bwx& aMetaData) const;
    TBool TryGetPresetById(TUint aId, TUint aSequenceNumber, Bwx& aMetaData, TUint& aIndex) const;
    TUint SetPreset(TUint aIndex, const Brx& aMetaData); // returns preset id
    void ClearPreset(TUint aIndex); // FIXME - could be inlined if we care
    void EndSetPresets();
private:
    class Preset
    {
    public:
        Preset();
        void Set(TUint aId, const Brx& aMetaData);
        TUint Id() const { return iId; }
        const Brx& MetaData() const { return iMetaData; }
    private:
        TUint iId;
        Media::BwsTrackMetaData iMetaData;
    };
private:
    mutable Mutex iLock;
    IRadioDatbaseObserver& iObserver;
    Preset iPresets[kMaxPresets];
    TUint iNextId;
    TUint iSeq;
    TBool iUpdated;
};

class NullRadioDatbaseObserver : public IRadioDatbaseObserver
{
private: // from IRadioDatbaseObserver
    void RadioDatabaseChanged() {}
};

} // namespace Av
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Av;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

// TestRadio

TestRadio::TestRadio(DvStack& aDvStack, TIpAddress aAdapter, const Brx& aSenderUdn, const TChar* aSenderFriendlyName, TUint aSenderChannel)
{
    iPipeline = new PipelineManager(iInfoLogger, SimpleSongcastingDriver::kMaxDriverJiffies);
    iPipeline->AddObserver(*this);
    iPipeline->Add(Codec::CodecFactory::NewAac());
    iPipeline->Add(Codec::CodecFactory::NewAlac());
    iPipeline->Add(Codec::CodecFactory::NewFlac());
    iPipeline->Add(Codec::CodecFactory::NewMp3());
    iPipeline->Add(Codec::CodecFactory::NewVorbis());
    iPipeline->Add(Codec::CodecFactory::NewWav());
    iPipeline->Add(Codec::CodecFactory::NewWma());
    Environment& env = aDvStack.Env();
    iPipeline->Add(ProtocolFactory::NewHttp(env));
    iPipeline->Add(ProtocolFactory::NewHttp(env));
    iPipeline->Add(ProtocolFactory::NewHttp(env));
    iPipeline->Add(ProtocolFactory::NewHttp(env));
    iPipeline->Add(ProtocolFactory::NewHttp(env));
    iPipeline->Add(ContentProcessorFactory::NewM3u());
    iPipeline->Add(ContentProcessorFactory::NewM3u());
    iPipeline->Add(ContentProcessorFactory::NewPls());
    iPipeline->Add(ContentProcessorFactory::NewPls());
    iPipeline->Add(ContentProcessorFactory::NewOpml());
    iPipeline->Add(ContentProcessorFactory::NewOpml());
    iTrackFactory = new TrackFactory(iInfoLogger, kTrackCount);
    iDriver = new SimpleSongcastingDriver(aDvStack, *iPipeline, aAdapter, aSenderUdn, aSenderFriendlyName, aSenderChannel);
    iUriProvider = new UriProviderSingleTrack(*iTrackFactory);
    iPipeline->Add(iUriProvider);
    iPipeline->Start();
//    iSourceUpnpAv = new DummySourceUpnpAv(aDvStack, *iPipeline, *iUriProvider);
}

TestRadio::~TestRadio()
{
    delete iPipeline;
    delete iDriver;
//    delete iSourceUpnpAv;
    delete iUriProvider;
    delete iTrackFactory;
}

void TestRadio::Run(RadioDatabase& aDb)
{
    Log::Print("\nRadio test.  Press 'q' to quit:\n");
    Log::Print("\n");
    for (;;) {
        int ch = mygetch();
        if (ch == 'q') {
            break;
        }
        if (isdigit(ch)) {
            TUint index = ch - '0';
            Log::Print("Try to play preset index %u\n", index);
            Bws<1024> uri;
            TUint ignore;
            aDb.GetPreset(index, ignore, uri);
            iPipeline->Stop();
            iPipeline->Begin(iUriProvider->Style(), uri);
            iPipeline->Play();
        }
        else {
            Log::Print("Unsupported command - %c\n", (char)ch);
        }
    }
    while (mygetch() != 'q')
        ;
}

#define LOG_PIPELINE_OBSERVER
#ifndef LOG_PIPELINE_OBSERVER
# ifdef _WIN32
// suppress 'unreferenced formal parameter' warnings which come and go depending
// on the state of LOG_PIPELINE_OBSERVER
#  pragma warning(disable:4100)
# endif // _WIN32
#endif // !LOG_PIPELINE_OBSERVER
void TestRadio::NotifyPipelineState(EPipelineState aState)
{
#ifdef LOG_PIPELINE_OBSERVER
    const char* state = "";
    switch (aState)
    {
    case EPipelinePlaying:
        state = "playing";
        break;
    case EPipelinePaused:
        state = "paused";
        break;
    case EPipelineStopped:
        state = "stopped";
        break;
    case EPipelineBuffering:
        state = "buffering";
        break;
    default:
        ASSERTS();
    }
    Log::Print("Pipeline state change: %s\n", state);
#endif
}

void TestRadio::NotifyTrack(Track& aTrack, TUint aIdPipeline)
{
#ifdef LOG_PIPELINE_OBSERVER
    Log::Print("Pipeline report property: TRACK {uri=");
    Log::Print(aTrack.Uri());
    Log::Print("; metadata=");
    Log::Print(aTrack.MetaData());
    Log::Print("; style=");
    Log::Print(aTrack.Style());
    Log::Print("; providerId=");
    Log::Print(aTrack.ProviderId());
    Log::Print("; idPipeline=%u}\n", aIdPipeline);
#endif
}

void TestRadio::NotifyMetaText(const Brx& aText)
{
#ifdef LOG_PIPELINE_OBSERVER
    Log::Print("Pipeline report property: METATEXT {");
    Log::Print(aText);
    Log::Print("}\n");
#endif
}

void TestRadio::NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds)
{
#ifdef LOG_PIPELINE_OBSERVER
    Log::Print("Pipeline report property: TIME {secs=%u; duration=%u}\n", aSeconds, aTrackDurationSeconds);
#endif
}

void TestRadio::NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo)
{
#ifdef LOG_PIPELINE_OBSERVER
    Log::Print("Pipeline report property: FORMAT {bitRate=%u; bitDepth=%u, sampleRate=%u, numChannels=%u, codec=",
           aStreamInfo.BitRate(), aStreamInfo.BitDepth(), aStreamInfo.SampleRate(), aStreamInfo.NumChannels());
    Log::Print(aStreamInfo.CodecName());
    Log::Print("; trackLength=%llx, lossless=%u}\n", aStreamInfo.TrackLength(), aStreamInfo.Lossless());
#endif
}


// RadioDatabase

RadioDatabase::RadioDatabase(IRadioDatbaseObserver& aObserver)
    : iLock("RADB")
    , iObserver(aObserver)
    , iNextId(kPresetIdNone + 1)
    , iSeq(0)
    , iUpdated(false)
{
}

RadioDatabase::~RadioDatabase()
{
}

void RadioDatabase::GetIdArray(std::array<TUint, RadioDatabase::kMaxPresets>& aIdArray) const
{
    iLock.Wait();
    for (TUint i=0; i<kMaxPresets; i++) {
        aIdArray[i] = iPresets[i].Id();
    }
    iLock.Signal();
}

TUint RadioDatabase::SequenceNumber() const
{
    return iSeq;
}

void RadioDatabase::BeginSetPresets()
{
}

void RadioDatabase::GetPreset(TUint aIndex, TUint& aId, Bwx& aMetaData) const
{
    ASSERT(aIndex < kMaxPresets);
    iLock.Wait();
    const Preset& preset = iPresets[aIndex];
    aId = preset.Id();
    aMetaData.Replace(preset.MetaData());
    iLock.Signal();
}

TBool RadioDatabase::TryGetPresetById(TUint aId, Bwx& aMetaData) const
{
    AutoMutex a(iLock);
    for (TUint i=0; i<kMaxPresets; i++) {
        if (iPresets[i].Id() == aId) {
            aMetaData.Replace(iPresets[i].MetaData());
            return true;
        }
    }
    return false;
}

TBool RadioDatabase::TryGetPresetById(TUint aId, TUint aSequenceNumber, Bwx& aMetaData, TUint& aIndex) const
{
    AutoMutex a(iLock);
    if (iSeq != aSequenceNumber) {
        return TryGetPresetById(aId, aMetaData);
    }
    for (TUint i=aIndex+1; i<kMaxPresets; i++) {
        if (iPresets[i].Id() == aId) {
            aMetaData.Replace(iPresets[i].MetaData());
            aIndex = i;
            return true;
        }
    }
    return false;
}

TUint RadioDatabase::SetPreset(TUint aIndex, const Brx& aMetaData)
{
    iLock.Wait();
    Preset& preset = iPresets[aIndex];
    TUint id = preset.Id();
    if (preset.MetaData() != aMetaData) {
        id = iNextId++;
        preset.Set(id, aMetaData);
        iSeq++;
    }
    iLock.Signal();
    return id;
}

void RadioDatabase::ClearPreset(TUint aIndex)
{
    (void)SetPreset(aIndex, Brx::Empty());
}

void RadioDatabase::EndSetPresets()
{
    iLock.Wait();
    const TBool updated = iUpdated;
    iUpdated = false;
    iLock.Signal();
    if (updated) {
        iObserver.RadioDatabaseChanged();
    }
}


// RadioDatabase::Preset

RadioDatabase::Preset::Preset()
    : iId(kPresetIdNone)
{
}

void RadioDatabase::Preset::Set(TUint aId, const Brx& aMetaData)
{
    iId = aId;
    Brn metaData(aMetaData);
    if (metaData.Bytes() > iMetaData.MaxBytes()) {
        metaData.Split(iMetaData.MaxBytes());
    }
    iMetaData.Replace(metaData);
}


int CDECL main(int aArgc, char* aArgv[])
{
    OptionParser parser;
    OptionString optionUdn("-u", "--udn", Brn("TestRadioSender"), "[udn] udn for the upnp device");
    parser.AddOption(&optionUdn);
    OptionString optionName("-n", "--name", Brn("TestRadioSender"), "[name] name of the sender");
    parser.AddOption(&optionName);
    OptionUint optionChannel("-c", "--channel", 0, "[0..65535] sender channel");
    parser.AddOption(&optionChannel);
    OptionUint optionAdapter("-a", "--adapter", 0, "[adapter] index of network adapter to use");
    parser.AddOption(&optionAdapter);

    if (!parser.Parse(aArgc, aArgv)) {
        return 1;
    }

    InitialisationParams* initParams = InitialisationParams::Create();
    //Debug::SetLevel(Debug::kMedia);
	Net::Library* lib = new Net::Library(initParams);
    Net::DvStack* dvStack = lib->StartDv();
    std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();
    const TUint adapterIndex = optionAdapter.Value();
    if (subnetList->size() <= adapterIndex) {
		Log::Print("ERROR: adapter %u doesn't exist\n", adapterIndex);
		ASSERTS();
    }
    Log::Print ("adapter list:\n");
    for (unsigned i=0; i<subnetList->size(); ++i) {
		TIpAddress addr = (*subnetList)[i]->Address();
		Log::Print ("  %d: %d.%d.%d.%d\n", i, addr&0xff, (addr>>8)&0xff, (addr>>16)&0xff, (addr>>24)&0xff);
    }
    TIpAddress subnet = (*subnetList)[adapterIndex]->Subnet();
    TIpAddress adapter = (*subnetList)[adapterIndex]->Address();
    Library::DestroySubnetList(subnetList);
    lib->SetCurrentSubnet(subnet);
    Log::Print("using subnet %d.%d.%d.%d\n", subnet&0xff, (subnet>>8)&0xff, (subnet>>16)&0xff, (subnet>>24)&0xff);

    NullRadioDatbaseObserver obs;
    RadioDatabase* db = new RadioDatabase(obs);
    db->BeginSetPresets();
    const TChar* presets[] = {
        "http://opml.radiotime.com/Tune.ashx?id=s122119&formats=mp3,wma,aac,wmvideo,ogg&partnerId=ah2rjr68&username=chisholmsi&c=ebrowse" // (Linn Radio, MP3)
       ,"http://opml.radiotime.com/Tune.ashx?id=s2377&formats=mp3,wma,aac,wmvideo,ogg&partnerId=ah2rjr68&username=chisholmsi&c=ebrowse"   // (Planet Rock, unknown)
       ,"http://opml.radiotime.com/Tune.ashx?id=s24940&formats=mp3,wma,aac,wmvideo,ogg&partnerId=ah2rjr68&username=chisholmsi&c=ebrowse"  // (Radio 2, AAC)
       ,"http://opml.radiotime.com/Tune.ashx?id=s92182&formats=mp3,wma,aac,wmvideo,ogg&partnerId=ah2rjr68&username=chisholmsi&c=ebrowse"  // (Birdsong Radio, MP3)
       ,"http://opml.radiotime.com/Tune.ashx?id=s24945&formats=mp3,wma,aac,wmvideo,ogg&partnerId=ah2rjr68&username=chisholmsi&c=ebrowse"  // (Radio Scotland, WMA)
    };
    for (TUint i=0; i<sizeof(presets)/sizeof(presets[0]); i++) {
        Brn urlAsMetaData(presets[i]);
        (void)db->SetPreset(i, urlAsMetaData);
    }
    db->EndSetPresets();
    TestRadio* tr = new TestRadio(*dvStack, adapter, optionUdn.Value(), optionName.CString(), optionChannel.Value());
    tr->Run(*db);
    delete tr;
    delete db;
    
    delete lib;

    return 0;
}