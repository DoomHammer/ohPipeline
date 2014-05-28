#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Av/UpnpAv/UriProviderRepeater.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Media/Tests/AllocatorInfoLogger.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Functor.h>

#include <vector>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Av {

class SuiteUriProviderRepeater : public SuiteUnitTest
{
public:
    SuiteUriProviderRepeater();
private: // from SuiteUnitTest
    void Setup();
    void TearDown();
private:
    void TestPlayNow();
    void TestPlayLater();
    void TestGetNextTwiceAfterBegin();
    void TestGetNextTwiceAfterBeginLater();
    void TestGetNextThenBegin();
    void TestCurrentTrackId();
    void TestNullTrack();
private:
    static const TUint kTrackCount = 2;
    static const Brn kUri;
    static const Brn kMetaData;
    AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    UriProviderRepeater* iUriProviderRepeater;
    UriProvider* iUriProvider;  // UriProvider methods should only be accessed
                                // through the UriProvider base class.
};

} // namespace Media
} // namespace OpenHome


// SuiteUriProviderRepeater

const Brn SuiteUriProviderRepeater::kUri("http://a.test.uri");
const Brn SuiteUriProviderRepeater::kMetaData("");

SuiteUriProviderRepeater::SuiteUriProviderRepeater()
    : SuiteUnitTest("SuiteUriProviderRepeater")
{
    AddTest(MakeFunctor(*this, &SuiteUriProviderRepeater::TestPlayNow), "TestPlayNow");
    AddTest(MakeFunctor(*this, &SuiteUriProviderRepeater::TestPlayLater), "TestPlayLater");
    AddTest(MakeFunctor(*this, &SuiteUriProviderRepeater::TestGetNextTwiceAfterBegin), "TestGetNextTwiceAfterBegin");
    AddTest(MakeFunctor(*this, &SuiteUriProviderRepeater::TestGetNextTwiceAfterBeginLater), "TestGetNextTwiceAfterBeginLater");
    AddTest(MakeFunctor(*this, &SuiteUriProviderRepeater::TestGetNextThenBegin), "TestGetNextThenBegin");
    AddTest(MakeFunctor(*this, &SuiteUriProviderRepeater::TestCurrentTrackId), "TestCurrentTrackId");
    AddTest(MakeFunctor(*this, &SuiteUriProviderRepeater::TestNullTrack), "TestNullTrack");
}

void SuiteUriProviderRepeater::Setup()
{
    iTrackFactory = new TrackFactory(iInfoAggregator, kTrackCount);
    iUriProviderRepeater = new UriProviderRepeater("SuiteUriProviderRepeater", *iTrackFactory);
    iUriProvider = iUriProviderRepeater;
}

void SuiteUriProviderRepeater::TearDown()
{
    delete iUriProvider;
    delete iTrackFactory;
}

void SuiteUriProviderRepeater::TestPlayNow()
{
    Track* trackOut = NULL;
    Track* track = iUriProviderRepeater->SetTrack(kUri, kMetaData);
    iUriProvider->Begin(track->Id());
    EStreamPlay play = iUriProvider->GetNext(trackOut);
    TEST(play == ePlayYes);
    TEST(trackOut->Uri() == track->Uri());
    TEST(trackOut->Id() == track->Id());
    trackOut->RemoveRef();
    track->RemoveRef();
}

void SuiteUriProviderRepeater::TestPlayLater()
{
    Track* trackOut = NULL;
    Track* track = iUriProviderRepeater->SetTrack(kUri, kMetaData);
    iUriProvider->BeginLater(track->Id());
    EStreamPlay play = iUriProvider->GetNext(trackOut);
    TEST(play == ePlayLater);
    TEST(trackOut->Uri() == track->Uri());
    TEST(trackOut->Id() == track->Id());
    trackOut->RemoveRef();
    track->RemoveRef();
}

void SuiteUriProviderRepeater::TestGetNextTwiceAfterBegin()
{
    Track* trackOut = NULL;
    Track* track = iUriProviderRepeater->SetTrack(kUri, kMetaData);
    iUriProvider->Begin(track->Id());

    // Calling GetNext() first time should return ePlayYes
    EStreamPlay play = iUriProvider->GetNext(trackOut);
    TEST(play == ePlayYes);
    TEST(trackOut->Uri() == track->Uri());
    TEST(trackOut->Id() == track->Id());
    trackOut->RemoveRef();

    // Calling GetNext() second time should return ePlayLater
    play = iUriProvider->GetNext(trackOut);
    TEST(play == ePlayLater);
    TEST(trackOut->Uri() == track->Uri());
    TEST(trackOut->Id() == track->Id());
    trackOut->RemoveRef();

    track->RemoveRef();
}

void SuiteUriProviderRepeater::TestGetNextTwiceAfterBeginLater()
{
    Track* trackOut = NULL;
    Track* track = iUriProviderRepeater->SetTrack(kUri, kMetaData);
    iUriProvider->BeginLater(track->Id());

    // Calling GetNext() first time should return ePlayYes
    EStreamPlay play = iUriProvider->GetNext(trackOut);
    TEST(play == ePlayLater);
    TEST(trackOut->Uri() == track->Uri());
    TEST(trackOut->Id() == track->Id());
    trackOut->RemoveRef();

    // Calling GetNext() second time should return ePlayLater
    play = iUriProvider->GetNext(trackOut);
    TEST(play == ePlayLater);
    TEST(trackOut->Uri() == track->Uri());
    TEST(trackOut->Id() == track->Id());
    trackOut->RemoveRef();

    track->RemoveRef();
}

void SuiteUriProviderRepeater::TestGetNextThenBegin()
{
    // This should reset the state returned by GetNext()
    Track* trackOut = NULL;
    Track* track = iUriProviderRepeater->SetTrack(kUri, kMetaData);
    iUriProvider->Begin(track->Id());

    // Calling GetNext() first time should return ePlayYes
    EStreamPlay play = iUriProvider->GetNext(trackOut);
    TEST(play == ePlayYes);
    TEST(trackOut->Uri() == track->Uri());
    TEST(trackOut->Id() == track->Id());
    trackOut->RemoveRef();

    // Calling GetNext() second time should return ePlayLater
    play = iUriProvider->GetNext(trackOut);
    TEST(play == ePlayLater);
    TEST(trackOut->Uri() == track->Uri());
    TEST(trackOut->Id() == track->Id());

    track->RemoveRef();

    // Call Begin() again. GetNext() should now return ePlayYes.
    iUriProvider->Begin(track->Id());
    play = iUriProvider->GetNext(trackOut);
    TEST(play == ePlayYes);
    TEST(trackOut->Uri() == track->Uri());
    TEST(trackOut->Id() == track->Id());
    trackOut->RemoveRef();

    track->RemoveRef();
}

void SuiteUriProviderRepeater::TestCurrentTrackId()
{
    Track* track = iUriProviderRepeater->SetTrack(kUri, kMetaData);
    iUriProvider->Begin(track->Id());
    TUint id = iUriProvider->CurrentTrackId();
    TEST(id == track->Id());
    track->RemoveRef();
}

void SuiteUriProviderRepeater::TestNullTrack()
{
    Track* track = NULL;
    Track* trackOut = NULL;
    iUriProviderRepeater->SetTrack(track);
    iUriProvider->Begin(Track::kIdNone);
    EStreamPlay play = iUriProvider->GetNext(trackOut);
    TEST(trackOut == NULL);
    TEST(play == ePlayNo);
}



void TestUriProviderRepeater()
{
    Runner runner("UriProviderRepeater tests\n");
    runner.Add(new SuiteUriProviderRepeater());
    runner.Run();
}
