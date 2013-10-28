#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Media/Pipeline.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/DriverSongcastSender.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Av/InfoProvider.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Private/Debug.h>
#include "AllocatorInfoLogger.h"

namespace OpenHome {
namespace Media {

class DummyFiller : public Thread, private IPipelineIdProvider
{
public:
    DummyFiller(Environment& aEnv, ISupply& aSupply, IFlushIdProvider& aFlushIdProvider, Av::IInfoAggregator& aInfoAggregator);
    ~DummyFiller();
    void Start(const Brx& aUrl);
private: // from Thread
    void Run();
private: // from IPipelineIdProvider
    TUint NextTrackId();
    TUint NextStreamId();
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
    void InvalidateAt(TUint aId);
    void InvalidateAfter(TUint aId);
    void InvalidatePending();
    void InvalidateAll();
private:
    ProtocolManager* iProtocolManager;
    TrackFactory* iTrackFactory;
    Brn iUrl;
    TUint iNextTrackId;
    TUint iNextStreamId;
    static const TUint kInvalidPipelineId = 0;
};

class TestProtocol : private IPipelineObserver
{
    static const TUint kMaxDriverJiffies = Jiffies::kJiffiesPerMs * 5;
    static const TUint kSeekStepSeconds = 10;
public:
    TestProtocol(Environment& aEnv, const Brx& aUrl);
    virtual ~TestProtocol();
    int Run();
    IPipelineElementUpstream& GetPipeline() const;
    TUint GetMaxDriverJiffies() const;
private: // from IPipelineObserver
    void NotifyPipelineState(EPipelineState aState);
    void NotifyTrack(const Brx& aUri, const Brx& aMode, TUint aIdPipeline);
    void NotifyMetaText(const Brx& aText);
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds);
    void NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo);
private:
    DummyFiller* iFiller;
    AllocatorInfoLogger iInfoAggregator;
    Pipeline* iPipeline;
    Brh iUrl;
    TUint iSeconds;
    TUint iTrackDurationSeconds;
    TUint iStreamId;
};

class PipelineSongcast
{
public:
    PipelineSongcast(TestProtocol* aTestProtocol, Environment& aEnv, Net::DvStack& aDvStack, TIpAddress aAdapter, const Brx& aSenderUdn, const TChar* aSenderFriendlyName, TUint aChannel);
    int Run();
    ~PipelineSongcast();
private:
    TestProtocol* iTestProtocol;
    DriverSongcastSender* iDriver;
};


} // namespace Media
} // namespace OpenHome

