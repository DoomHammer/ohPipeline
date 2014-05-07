#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Media/Filler.h>
#include <OpenHome/Av/InfoProvider.h>
#include "AllocatorInfoLogger.h"
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Private/Thread.h>

#include <limits.h>
#include <vector>
#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {
namespace TestFiller {

class DummyUriProvider : public UriProvider
{
public:
    DummyUriProvider(TrackFactory& aTrackFactory);
    ~DummyUriProvider();
    const Brx& TrackUriByIndex(TUint aIndex) const;
    TUint IdByIndex(TUint aIndex) const;
private: // from UriProvider
    void Begin(TUint aTrackId);
    void BeginLater(TUint aTrackId);
    EStreamPlay GetNext(Track*& aTrack);
    TUint CurrentTrackId() const;
    TBool MoveNext();
    TBool MovePrevious();
private:
    static const TInt kNumEntries = 3;
    TrackFactory& iTrackFactory;
    Track* iTracks[kNumEntries];
    TInt iIndex;
    TInt iPendingIndex;
};

class DummyUriStreamer : public IUriStreamer, private IStreamHandler, private INonCopyable
{
public:
    DummyUriStreamer(ISupply& aSupply, Semaphore& aTrackAddedSem, Semaphore& aTrackCompleteSem);
    virtual ~DummyUriStreamer();
    TUint TrackId() const;
    TUint StreamId() const;
private: // from IUriStreamer
    TBool DoStream(Track& aTrack);
    void Interrupt(TBool aInterrupt);
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
    TUint TrySeek(TUint aTrackId, TUint aStreamId, TUint64 aOffset);
    TUint TryStop(TUint aTrackId, TUint aStreamId);
    TBool TryGet(IWriter& aWriter, TUint aTrackId, TUint aStreamId, TUint64 aOffset, TUint aBytes);
    void NotifyStarving(const Brx& aMode, TUint aTrackId, TUint aStreamId);
private:
    ISupply& iSupply;
    Semaphore& iTrackAddedSem;
    Semaphore& iTrackCompleteSem;
    TUint iPipelineTrackId;
    TUint iStreamId;
};

class DummySupply : public ISupply
{
public:
    DummySupply();
    ~DummySupply();
    const Brx& LastTrackUri() const;
    TUint LastTrackId() const;
    TUint LastStreamId() const;
    const Brx& LastMode() const;
    TBool LastSupportsLatency() const;
    TBool LastIsRealTime() const;
    TUint LastDelayJiffies() const;
private: // from ISupply
    void OutputMode(const Brx& aMode, TBool aSupportsLatency, TBool aRealTime);
    void OutputTrack(Track& aTrack, TUint aTrackId);
    void OutputDelay(TUint aJiffies);
    void OutputStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, IStreamHandler& aStreamHandler, TUint aStreamId);
    void OutputData(const Brx& aData);
    void OutputMetadata(const Brx& aMetadata);
    void OutputFlush(TUint aFlushId);
    void OutputWait();
    void OutputHalt(TUint aHaltId);
    void OutputQuit();
private:
    BwsMode iLastMode;
    TBool iLastSupportsLatency;
    TBool iLastRealTime;
    BwsTrackUri iLastTrackUri;
    TUint iLastTrackId;
    TUint iLastStreamId;
    TUint iLastDelayJiffies;
};

class SuiteFiller : public Suite, private IPipelineIdTracker, private IStreamPlayObserver
{
    static const TUint kDefaultLatency = Jiffies::kJiffiesPerMs * 150;
public:
    SuiteFiller();
    ~SuiteFiller();
private: // from Suite
    void Test();
private: // from IPipelineIdTracker
    void AddStream(TUint aId, TUint aPipelineTrackId, TUint aStreamId, TBool aPlayNow);
private: // from IStreamPlayObserver
    void NotifyTrackFailed(TUint aTrackId);
    void NotifyStreamPlayStatus(TUint aTrackId, TUint aStreamId, EStreamPlay aStatus);
private:
    Semaphore iTrackAddedSem;
    Semaphore iTrackCompleteSem;
    AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    Filler* iFiller;
    DummyUriProvider* iUriProvider;
    DummyUriStreamer* iUriStreamer;
    DummySupply* iDummySupply;
    TUint iTrackId;
    TUint iPipelineTrackId;
    TUint iStreamId;
    TBool iPlayNow;
};

} // namespace TestFiller
} // namespace Media
} // namespace OpenHome

using namespace OpenHome::Media::TestFiller;

// DummyUriProvider

DummyUriProvider::DummyUriProvider(TrackFactory& aTrackFactory)
    : UriProvider("Dummy", false, false)
    , iTrackFactory(aTrackFactory)
    , iIndex(-1)
    , iPendingIndex(-1)
{
    iTracks[0] = iTrackFactory.CreateTrack(Brn("http://addr:port/path/file1"), Brx::Empty(), NULL, false);
    iTracks[1] = iTrackFactory.CreateTrack(Brn("http://addr:port/path/file2"), Brx::Empty(), NULL, false);
    iTracks[2] = iTrackFactory.CreateTrack(Brn("http://addr:port/path/file3"), Brx::Empty(), NULL, false);
}

DummyUriProvider::~DummyUriProvider()
{
    iTracks[0]->RemoveRef();
    iTracks[1]->RemoveRef();
    iTracks[2]->RemoveRef();
}

const Brx& DummyUriProvider::TrackUriByIndex(TUint aIndex) const
{
    return iTracks[aIndex]->Uri();
}

TUint DummyUriProvider::IdByIndex(TUint aIndex) const
{
    return iTracks[aIndex]->Id();
}

void DummyUriProvider::Begin(TUint aTrackId)
{
    TInt index = 0;
    while (index < kNumEntries && iTracks[index]->Id() != aTrackId) {
        index++;
    }
    if (index == kNumEntries) {
        THROW(UriProviderInvalidId);
    }
    iIndex = index-1;
}

void DummyUriProvider::BeginLater(TUint /*aTrackId*/)
{
    ASSERTS(); // don't expect this to ever be called
}

EStreamPlay DummyUriProvider::GetNext(Track*& aTrack)
{
    const TBool firstCall = (iIndex<0);
    TUint index;
    if (iPendingIndex != -1) {
        index = iPendingIndex;
        iPendingIndex = -1;
    }
    else {
        if (++iIndex == kNumEntries) {
            iIndex = 0;
        }
        index = iIndex;
    }
    aTrack = iTracks[index];
    aTrack->AddRef();
    return (!firstCall && iIndex == 0? ePlayLater : ePlayYes);
}

TUint DummyUriProvider::CurrentTrackId() const
{
    return iTracks[iIndex]->Id();
}

TBool DummyUriProvider::MoveNext()
{
    iPendingIndex = ++iIndex;
    if (iPendingIndex == kNumEntries) {
        iPendingIndex = 0;
    }
    return true;
}

TBool DummyUriProvider::MovePrevious()
{
    iPendingIndex = --iIndex;
    if (iPendingIndex < 0) {
        iPendingIndex = kNumEntries-1;
    }
    return true;
}


// DummyUriStreamer

DummyUriStreamer::DummyUriStreamer(ISupply& aSupply, Semaphore& aTrackAddedSem, Semaphore& aTrackCompleteSem)
    : iSupply(aSupply)
    , iTrackAddedSem(aTrackAddedSem)
    , iTrackCompleteSem(aTrackCompleteSem)
    , iPipelineTrackId(0)
    , iStreamId(0)
{
}

DummyUriStreamer::~DummyUriStreamer()
{
}

TUint DummyUriStreamer::TrackId() const
{
    return iPipelineTrackId;
}

TUint DummyUriStreamer::StreamId() const
{
    return iStreamId;
}

TBool DummyUriStreamer::DoStream(Track& aTrack)
{
    iPipelineTrackId++;
    iStreamId++;
    iSupply.OutputTrack(aTrack, iPipelineTrackId);
    iSupply.OutputStream(aTrack.Uri(), 1LL, false, false, *this, iStreamId);
    iTrackAddedSem.Signal();
    iTrackCompleteSem.Wait();
    return true;
}

void DummyUriStreamer::Interrupt(TBool /*aInterrupt*/)
{
}

EStreamPlay DummyUriStreamer::OkToPlay(TUint /*aTrackId*/, TUint /*aStreamId*/)
{
    ASSERTS();
    return ePlayNo;
}

TUint DummyUriStreamer::TrySeek(TUint /*aTrackId*/, TUint /*aStreamId*/, TUint64 /*aOffset*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint DummyUriStreamer::TryStop(TUint /*aTrackId*/, TUint /*aStreamId*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TBool DummyUriStreamer::TryGet(IWriter& /*aWriter*/, TUint /*aTrackId*/, TUint /*aStreamId*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    ASSERTS();
    return false;
}

void DummyUriStreamer::NotifyStarving(const Brx& /*aMode*/, TUint /*aTrackId*/, TUint /*aStreamId*/)
{
}


// DummySupply

DummySupply::DummySupply()
{
}

DummySupply::~DummySupply()
{
}

const Brx& DummySupply::LastTrackUri() const
{
    return iLastTrackUri;
}

TUint DummySupply::LastTrackId() const
{
    return iLastTrackId;
}

TUint DummySupply::LastStreamId() const
{
    return iLastStreamId;
}

const Brx& DummySupply::LastMode() const
{
    return iLastMode;
}

TBool DummySupply::LastSupportsLatency() const
{
    return iLastSupportsLatency;
}

TBool DummySupply::LastIsRealTime() const
{
    return iLastRealTime;
}

TUint DummySupply::LastDelayJiffies() const
{
    return iLastDelayJiffies;
}

void DummySupply::OutputMode(const Brx& aMode, TBool aSupportsLatency, TBool aRealTime)
{
    iLastMode.Replace(aMode);
    iLastSupportsLatency = aSupportsLatency;
    iLastRealTime = aRealTime;
}

void DummySupply::OutputTrack(Track& aTrack, TUint aTrackId)
{
    iLastTrackUri.Replace(aTrack.Uri());
    iLastTrackId = aTrackId;
}

void DummySupply::OutputDelay(TUint aJiffies)
{
    iLastDelayJiffies = aJiffies;
}

void DummySupply::OutputStream(const Brx& /*aUri*/, TUint64 /*aTotalBytes*/, TBool /*aSeekable*/, TBool /*aLive*/, IStreamHandler& /*aStreamHandler*/, TUint aStreamId)
{
    iLastStreamId = aStreamId;
}

void DummySupply::OutputData(const Brx& /*aData*/)
{
    ASSERTS();
}

void DummySupply::OutputMetadata(const Brx& /*aMetadata*/)
{
    ASSERTS();
}

void DummySupply::OutputFlush(TUint /*aFlushId*/)
{
    ASSERTS();
}

void DummySupply::OutputWait()
{
    ASSERTS();
}

void DummySupply::OutputHalt(TUint /*aHaltId*/)
{
}

void DummySupply::OutputQuit()
{
}


// SuiteFiller

SuiteFiller::SuiteFiller()
    : Suite("Trivial Filler tests")
    , iTrackAddedSem("TASM", 0)
    , iTrackCompleteSem("TCSM", 0)
{
    iTrackFactory = new TrackFactory(iInfoAggregator, 4);
    iDummySupply = new DummySupply();
    iFiller = new Filler(*iDummySupply, *this, *iTrackFactory, *this, kDefaultLatency);
    iUriProvider = new DummyUriProvider(*iTrackFactory);
    iUriStreamer = new DummyUriStreamer(*iFiller, iTrackAddedSem, iTrackCompleteSem);
    iFiller->Add(*iUriProvider);
    iFiller->Start(*iUriStreamer);
}

SuiteFiller::~SuiteFiller()
{
    iFiller->Quit();
    delete iUriStreamer;
    delete iFiller;
    delete iUriProvider;
    delete iDummySupply;
    delete iTrackFactory;
}

void SuiteFiller::Test()
{
    // Play for invalid mode should throw
    TEST_THROWS(iFiller->Play(Brn("NotARealMode"), 1), FillerInvalidMode);

    // Play for valid mode but invalid trackId should throw
    TEST_THROWS(iFiller->Play(iUriProvider->Mode(), UINT_MAX), UriProviderInvalidId);

    // Play for valid mode/trackId should succeed and Begin should be called
    // IUriStreamer should be passed uri for first track
    iFiller->Play(iUriProvider->Mode(), iUriProvider->IdByIndex(0));
    iTrackAddedSem.Wait();
    TEST(iDummySupply->LastMode() == iUriProvider->Mode());
    TEST(iDummySupply->LastSupportsLatency() == iUriProvider->SupportsLatency());
    TEST(iDummySupply->LastIsRealTime() == iUriProvider->IsRealTime());
    TEST(iDummySupply->LastTrackUri() == iUriProvider->TrackUriByIndex(0));
    TEST(iDummySupply->LastTrackId() == iUriStreamer->TrackId());
    TEST(iDummySupply->LastStreamId() == iUriStreamer->StreamId());
    TEST(iDummySupply->LastDelayJiffies() == 0/*kDefaultLatency*/); // FIXME - see #1808
    TEST(iTrackId == iUriProvider->IdByIndex(0));
    TEST(iPipelineTrackId == iDummySupply->LastTrackId());
    TEST(iStreamId == iDummySupply->LastStreamId());
    TEST(iPlayNow);
    TUint pipelineTrackId = iPipelineTrackId;
    TUint trackId = iTrackId;

    // When first track completes, IUriStreamer should be passed uri for second track
    iTrackCompleteSem.Signal();
    iTrackAddedSem.Wait();
    TEST(iDummySupply->LastTrackUri() == iUriProvider->TrackUriByIndex(1));
    TEST(iDummySupply->LastTrackId() == iUriStreamer->TrackId());
    TEST(iDummySupply->LastStreamId() == iUriStreamer->StreamId());
    TEST(iTrackId == iUriProvider->IdByIndex(1));
    TEST(iTrackId != trackId);
    TEST(iPipelineTrackId == iDummySupply->LastTrackId());
    TEST(iPipelineTrackId != pipelineTrackId);
    TEST(iStreamId == iDummySupply->LastStreamId());
    TEST(iPlayNow);
    pipelineTrackId = iPipelineTrackId;
    trackId = iTrackId;

    // Stop/Next during second track.  Once track completes IUriStreamer should be passed uri for third track
    (void)iFiller->Stop();
    iTrackCompleteSem.Signal();
    // test for invalid Next() arg
    TEST(!iFiller->Next(Brn("InvalidMode")));
    //
    TEST(iFiller->Next(iUriProvider->Mode()));
    iTrackAddedSem.Wait();
    TEST(iDummySupply->LastTrackUri() == iUriProvider->TrackUriByIndex(2));
    TEST(iDummySupply->LastTrackId() == iUriStreamer->TrackId());
    TEST(iDummySupply->LastStreamId() == iUriStreamer->StreamId());
    TEST(iTrackId == iUriProvider->IdByIndex(2));
    TEST(iTrackId != trackId);
    TEST(iPipelineTrackId == iDummySupply->LastTrackId());
    TEST(iPipelineTrackId != pipelineTrackId);
    TEST(iStreamId == iDummySupply->LastStreamId());
    TEST(iPlayNow);
    pipelineTrackId = iPipelineTrackId;
    trackId = iTrackId;

    // Stop/Prev during third track.  Once track completes IUriStreamer should be passed uri for second track
    (void)iFiller->Stop();
    iTrackCompleteSem.Signal();
    TEST(iFiller->Prev(iUriProvider->Mode()));
    iTrackAddedSem.Wait();
    TEST(iDummySupply->LastTrackUri() == iUriProvider->TrackUriByIndex(1));
    TEST(iDummySupply->LastTrackId() == iUriStreamer->TrackId());
    TEST(iDummySupply->LastStreamId() == iUriStreamer->StreamId());
    TEST(iTrackId == iUriProvider->IdByIndex(1));
    TEST(iTrackId != trackId);
    TEST(iPipelineTrackId == iDummySupply->LastTrackId());
    TEST(iPipelineTrackId != pipelineTrackId);
    TEST(iStreamId == iDummySupply->LastStreamId());
    TEST(iPlayNow);
    pipelineTrackId = iPipelineTrackId;
    trackId = iTrackId;

    // Once track completes, IUriStreamer should (again) be passed uri for third track
    iTrackCompleteSem.Signal();
    iTrackAddedSem.Wait();
    TEST(iDummySupply->LastTrackUri() == iUriProvider->TrackUriByIndex(2));
    TEST(iDummySupply->LastTrackId() == iUriStreamer->TrackId());
    TEST(iDummySupply->LastStreamId() == iUriStreamer->StreamId());
    TEST(iTrackId == iUriProvider->IdByIndex(2));
    TEST(iTrackId != trackId);
    TEST(iPipelineTrackId == iDummySupply->LastTrackId());
    TEST(iPipelineTrackId != pipelineTrackId);
    TEST(iStreamId == iDummySupply->LastStreamId());
    TEST(iPlayNow);
    pipelineTrackId = iPipelineTrackId;
    trackId = iTrackId;

    // Once track completes, dummy UriProvider will return first track to be played later.  IUriStreamer should not be passed anything
    iTrackCompleteSem.Signal();
    iTrackAddedSem.Wait();
    TEST(iTrackId == iUriProvider->IdByIndex(0));
    TEST(iTrackId != trackId);
    TEST(iPipelineTrackId == iDummySupply->LastTrackId());
    TEST(iPipelineTrackId != pipelineTrackId);
    TEST(iStreamId == iDummySupply->LastStreamId());
    TEST(!iPlayNow);
    (void)iFiller->Stop();
    iTrackCompleteSem.Signal();
}

void SuiteFiller::AddStream(TUint aId, TUint aPipelineTrackId, TUint aStreamId, TBool aPlayNow)
{
    iTrackId = aId;
    iPipelineTrackId = aPipelineTrackId;
    iStreamId = aStreamId;
    iPlayNow = aPlayNow;
}

void SuiteFiller::NotifyTrackFailed(TUint /*aTrackId*/)
{
}

void SuiteFiller::NotifyStreamPlayStatus(TUint /*aTrackId*/, TUint /*aStreamId*/, EStreamPlay /*aStatus*/)
{
}



void TestFiller()
{
    Runner runner("Basic Filler tests\n");
    runner.Add(new SuiteFiller());
    runner.Run();
}
