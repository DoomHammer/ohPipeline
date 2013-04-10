#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Media/IdProvider.h>
#include <OpenHome/Functor.h>

#include <vector>
#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuiteUnitTest : public Suite
{
protected:
    SuiteUnitTest(const TChar* aName);
    void AddTest(Functor aTest);
private: // from Suite
    void Test();
private:
    virtual void Setup() = 0;
    virtual void TearDown() = 0;
private:
    std::vector<Functor> iTests;
};
    
class SuiteIdsAreUnique : public Suite, private IStopper
{
public:
    SuiteIdsAreUnique();
    ~SuiteIdsAreUnique();
    void Test();
private: // from IStopper
    void RemoveStream(TUint aTrackId, TUint aStreamId);
private:
    PipelineIdProvider* iPipelineIdProvider;
};

class SuiteSingleStream : public SuiteUnitTest, private IStopper
{
    static const Brn kStyle;
    static const Brn kProviderId;
    static const TUint kTrackId = 0;
    static const TUint kStreamId = 0;
public:
    SuiteSingleStream();
private: // from IStopper
    void RemoveStream(TUint aTrackId, TUint aStreamId);
private: // from SuiteUnitTest
    void Setup();
    void TearDown();
private:
    void OkToPlayOnceForValidIds();
    void OkToPlayForInvalidIds();
    void InvalidateAtMatch();
    void InvalidateAtNoMatch();
    void InvalidateAfterMatch();
    void InvalidateAfterNoMatch();
private:
    PipelineIdProvider* iPipelineIdProvider;
    IPipelineIdProvider* iIdProvider;
};

class SuiteMaxStreams : public Suite, private IStopper
{
    static const Brn kStyle;
    static const Brn kProviderId;
    static const TUint kTrackId = 0;
    static const TUint kStreamId = 0;
public:
    SuiteMaxStreams();
    ~SuiteMaxStreams();
    void Test();
private: // from IStopper
    void RemoveStream(TUint aTrackId, TUint aStreamId);
private:
    PipelineIdProvider* iPipelineIdProvider;
};

class SuiteMultiStreams : public SuiteUnitTest, private IStopper
{
    static const Brn kStyle;
    static const Brn kProviderId1;
    static const Brn kProviderId2;
    static const Brn kProviderId3;
    static const TUint kTrackId1 = 1;
    static const TUint kTrackId2 = 2;
    static const TUint kTrackId3 = 3;
    static const TUint kStreamId1 = 1;
    static const TUint kStreamId2 = 2;
    static const TUint kIdInvalid = 999;
public:
    SuiteMultiStreams();
private: // from IStopper
    void RemoveStream(TUint aTrackId, TUint aStreamId);
private: // from SuiteUnitTest
    void Setup();
    void TearDown();
private:
    void AllCanBePlayed();
    void FirstPlaysIncorrectCallsThenPlayAll();
    void FirstPlaysThenInvalidated();
    void FirstPlaysThenInvalidateAfter();
    void FirstPlaysThenInvalidateThird();
    void FirstPlaysThenInvalidateAfterThird();
    void SecondPlaysThenInvalidated();
    void SecondPlaysThenInvalidateAfter();
    void InvalidateAtWithInvalidStyle();
    void InvalidateAtWithInvalidProviderId();
private:
    PipelineIdProvider* iPipelineIdProvider;
    IPipelineIdProvider* iIdProvider;
    TUint iRemoveTrackId;
    TUint iRemoveStreamId;
};

} // namespace Media
} // namespace OpenHome

/* Test cases are
Create Id provider
    check that NextTrackId returns vary
    check that NextStreamId returns vary
Create Id provider, add 1 element
    check it can be OkToPlay'd; check another OkToPlay fails
    call InvalidateAt for element, check OkToPlay fails
    call InvalidateAt for no element, check OkToPlay succeeds
    call InvalidateAfter for element, check OkToPlay succeeds
    call InvalidateAfter for no element, check OkToPlay succeeds
Create Id provider, add max elements
    check each can be OkToPlay'd, check second attempt at initial element fails
Create Id provider, add 3 elements (2 streams in first track, 1 in second track)
    check all can be OkToPlay'd
    check first can be OkToPlay'd, correct track and incorrect stream fails
                                   incorrect track and correct stream fails
                                   incorrect track and incorrect stream fails
                                   second can be OkToPlay'd, third can be OkToPlay'd, fourth OkToPlay fails
    check first can be OkToPlay'd, call InvalidateAt for first, check Stopper is called and OkToPlay for second fails, third succeeds
    check first can be OkToPlay'd, call InvalidateAfter for first, check OkToPlay for second, third fails
    check first can be OkToPlay'd, call InvalidateAt for second, check OkToPlay for third succeeds
    check first can be OkToPlay'd, call InvalidateAfter for second, check OkToPlay for third fails
    play first then second, call InvalidateAt for second, check Stopper is called and OkToPlay for third, fourth succeeds
    play first then second, call InvalidateAfter for second, check OkToPlay for third, fourth fails
    check first can be OkToPlay'd, call InvalidateAt for first with incorrect style, check OkToPlay for second succeeds
    check first can be OkToPlay'd, call InvalidateAt for first with incorrect providerId, check OkToPlay for second succeeds
*/

// SuiteUnitTest

SuiteUnitTest::SuiteUnitTest(const TChar* aName)
    : Suite(aName)
{
}

void SuiteUnitTest::AddTest(Functor aTest)
{
    iTests.push_back(aTest);
}

void SuiteUnitTest::Test()
{
    for (TUint i=0; i<iTests.size(); i++) {
        Setup();
        iTests[i]();
        TearDown();
    }
}

    
// SuiteIdsAreUnique

SuiteIdsAreUnique::SuiteIdsAreUnique()
    : Suite("Ids are unique")
{
    iPipelineIdProvider = new PipelineIdProvider(*this);
}

SuiteIdsAreUnique::~SuiteIdsAreUnique()
{
    delete iPipelineIdProvider;
}

void SuiteIdsAreUnique::Test()
{
    static const TUint kTestIterations = 10;
    std::vector<TUint> ids;
    IPipelineIdProvider* idProvider = static_cast<IPipelineIdProvider*>(iPipelineIdProvider);
    for (TUint i=0; i<kTestIterations; i++) {
        TUint trackId = idProvider->NextTrackId();
        TEST(std::find(ids.begin(), ids.end(), trackId) == ids.end());
        ids.push_back(trackId);
    }
    ids.clear();
    for (TUint i=0; i<kTestIterations; i++) {
        TUint streamId = idProvider->NextTrackId();
        TEST(std::find(ids.begin(), ids.end(), streamId) == ids.end());
        ids.push_back(streamId);
    }
}

void SuiteIdsAreUnique::RemoveStream(TUint /*aTrackId*/, TUint /*aStreamId*/)
{
    ASSERTS();
}


// SuiteSingleStream

const Brn SuiteSingleStream::kStyle("TestStyle");
const Brn SuiteSingleStream::kProviderId("ProviderId");

SuiteSingleStream::SuiteSingleStream()
    : SuiteUnitTest("Single active stream")
{
    AddTest(MakeFunctor(*this, &SuiteSingleStream::OkToPlayOnceForValidIds));
    AddTest(MakeFunctor(*this, &SuiteSingleStream::OkToPlayForInvalidIds));
    AddTest(MakeFunctor(*this, &SuiteSingleStream::InvalidateAtMatch));
    AddTest(MakeFunctor(*this, &SuiteSingleStream::InvalidateAtNoMatch));
    AddTest(MakeFunctor(*this, &SuiteSingleStream::InvalidateAfterMatch));
    AddTest(MakeFunctor(*this, &SuiteSingleStream::InvalidateAfterNoMatch));
}

void SuiteSingleStream::RemoveStream(TUint /*aTrackId*/, TUint /*aStreamId*/)
{
    ASSERTS();
}

void SuiteSingleStream::Setup()
{
    iPipelineIdProvider = new PipelineIdProvider(*this);
    iPipelineIdProvider->AddStream(kStyle, kProviderId, kTrackId, kStreamId);
    iIdProvider = static_cast<IPipelineIdProvider*>(iPipelineIdProvider);
}

void SuiteSingleStream::TearDown()
{
    delete iPipelineIdProvider;
    iPipelineIdProvider = NULL;
    iIdProvider = NULL;
}

void SuiteSingleStream::OkToPlayOnceForValidIds()
{
    TEST(iIdProvider->OkToPlay(kTrackId, kStreamId) == ePlayYes);
    // check we don't get permission to play the same track twice
    TEST(iIdProvider->OkToPlay(kTrackId, kStreamId) == ePlayNo);
}

void SuiteSingleStream::OkToPlayForInvalidIds()
{
    TEST(iIdProvider->OkToPlay(kTrackId+1, kStreamId) == ePlayNo);
    TEST(iIdProvider->OkToPlay(kTrackId, kStreamId+1) == ePlayNo);
    TEST(iIdProvider->OkToPlay(kTrackId+1, kStreamId+1) == ePlayNo);
    // check that previous failures don't prevent us from playing the next track
    TEST(iIdProvider->OkToPlay(kTrackId, kStreamId) == ePlayYes);
}

void SuiteSingleStream::InvalidateAtMatch()
{
    iIdProvider->InvalidateAt(kStyle, kProviderId);
    TEST(iIdProvider->OkToPlay(kTrackId, kStreamId) == ePlayNo);
}

void SuiteSingleStream::InvalidateAtNoMatch()
{
    iIdProvider->InvalidateAt(kStyle, Brn("nomatch"));
    iIdProvider->InvalidateAt(Brn("nomatch"), kProviderId);
    TEST(iIdProvider->OkToPlay(kTrackId, kStreamId) == ePlayYes);
}

void SuiteSingleStream::InvalidateAfterMatch()
{
    iIdProvider->InvalidateAfter(kStyle, kProviderId);
    TEST(iIdProvider->OkToPlay(kTrackId, kStreamId) == ePlayYes);
}

void SuiteSingleStream::InvalidateAfterNoMatch()
{
    iIdProvider->InvalidateAfter(kStyle, Brn("nomatch"));
    iIdProvider->InvalidateAfter(Brn("nomatch"), kProviderId);
    TEST(iIdProvider->OkToPlay(kTrackId, kStreamId) == ePlayYes);
}


// SuiteMaxStreams

const Brn SuiteMaxStreams::kStyle("TestStyle");
const Brn SuiteMaxStreams::kProviderId("ProviderId");

SuiteMaxStreams::SuiteMaxStreams()
    : Suite("Max active streams")
{
}

SuiteMaxStreams::~SuiteMaxStreams()
{
}

void SuiteMaxStreams::Test()
{
    // fill idProvider
    // check each can be OkToPlay'd, check second attempt at initial element fails
    // add another stream, check OkToPlay for it succeeds (so the buffer indices can wrap)

    PipelineIdProvider* pipelineIdProvider = new PipelineIdProvider(*this);
    const TUint max = pipelineIdProvider->MaxStreams()-1;
    for (TUint streamId = kStreamId; streamId<max; streamId++) {
        pipelineIdProvider->AddStream(kStyle, kProviderId, kTrackId, streamId);
    }
    IPipelineIdProvider* idProvider = static_cast<IPipelineIdProvider*>(pipelineIdProvider);
    for (TUint streamId = kStreamId; streamId<max; streamId++) {
        TEST(idProvider->OkToPlay(kTrackId, streamId) == ePlayYes);
    }
    TEST(idProvider->OkToPlay(kTrackId, kStreamId) == ePlayNo);

    pipelineIdProvider->AddStream(kStyle, kProviderId, kTrackId, kStreamId);
    pipelineIdProvider->AddStream(kStyle, kProviderId, kTrackId, kStreamId+1);
    pipelineIdProvider->AddStream(kStyle, kProviderId, kTrackId, kStreamId+2);
    TEST(idProvider->OkToPlay(kTrackId, kStreamId) == ePlayYes);
    TEST(idProvider->OkToPlay(kTrackId, kStreamId+1) == ePlayYes);
    TEST(idProvider->OkToPlay(kTrackId, kStreamId+2) == ePlayYes);

    delete pipelineIdProvider;
}

void SuiteMaxStreams::RemoveStream(TUint /*aTrackId*/, TUint /*aStreamId*/)
{
    ASSERTS();
}


// SuiteMultiStreams

const Brn SuiteMultiStreams::kStyle("TestStyle");
const Brn SuiteMultiStreams::kProviderId1("1");
const Brn SuiteMultiStreams::kProviderId2("2");
const Brn SuiteMultiStreams::kProviderId3("3");

SuiteMultiStreams::SuiteMultiStreams()
    : SuiteUnitTest("Multiple active streams")
{
    AddTest(MakeFunctor(*this, &SuiteMultiStreams::AllCanBePlayed));
    AddTest(MakeFunctor(*this, &SuiteMultiStreams::FirstPlaysIncorrectCallsThenPlayAll));
    AddTest(MakeFunctor(*this, &SuiteMultiStreams::FirstPlaysThenInvalidated));
    AddTest(MakeFunctor(*this, &SuiteMultiStreams::FirstPlaysThenInvalidateAfter));
    AddTest(MakeFunctor(*this, &SuiteMultiStreams::FirstPlaysThenInvalidateThird));
    AddTest(MakeFunctor(*this, &SuiteMultiStreams::FirstPlaysThenInvalidateAfterThird));
    AddTest(MakeFunctor(*this, &SuiteMultiStreams::SecondPlaysThenInvalidated));
    AddTest(MakeFunctor(*this, &SuiteMultiStreams::SecondPlaysThenInvalidateAfter));
    AddTest(MakeFunctor(*this, &SuiteMultiStreams::InvalidateAtWithInvalidStyle));
    AddTest(MakeFunctor(*this, &SuiteMultiStreams::InvalidateAtWithInvalidProviderId));
}

void SuiteMultiStreams::RemoveStream(TUint aTrackId, TUint aStreamId)
{
    iRemoveTrackId = aTrackId;
    iRemoveStreamId = aStreamId;
}

void SuiteMultiStreams::Setup()
{
    iPipelineIdProvider = new PipelineIdProvider(*this);
    iPipelineIdProvider->AddStream(kStyle, kProviderId1, kTrackId1, kStreamId1);
    iPipelineIdProvider->AddStream(kStyle, kProviderId1, kTrackId1, kStreamId2);
    iPipelineIdProvider->AddStream(kStyle, kProviderId2, kTrackId2, kStreamId1);
    iPipelineIdProvider->AddStream(kStyle, kProviderId3, kTrackId3, kStreamId1);
    iIdProvider = static_cast<IPipelineIdProvider*>(iPipelineIdProvider);
    iRemoveTrackId = kIdInvalid;
    iRemoveStreamId = kIdInvalid;
}

void SuiteMultiStreams::TearDown()
{
    delete iPipelineIdProvider;
    iPipelineIdProvider = NULL;
    iIdProvider = NULL;
}

void SuiteMultiStreams::AllCanBePlayed()
{
    // check all can be OkToPlay'd
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId2) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId2, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId3, kStreamId1) == ePlayYes);
}

void SuiteMultiStreams::FirstPlaysIncorrectCallsThenPlayAll()
{
    /* check first can be OkToPlay'd, correct track and incorrect stream fails
            incorrect track and correct stream fails
            incorrect track and incorrect stream fails
            second can be OkToPlay'd, third can be OkToPlay'd, fourth OkToPlay fails */
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId1, kIdInvalid) == ePlayNo);
    TEST(iIdProvider->OkToPlay(kIdInvalid, kStreamId2) == ePlayNo);
    TEST(iIdProvider->OkToPlay(kIdInvalid, kIdInvalid) == ePlayNo);
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId2) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId2, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId3, kStreamId1) == ePlayYes);
}

void SuiteMultiStreams::FirstPlaysThenInvalidated()
{
    // check first can be OkToPlay'd, call InvalidateAt for first, check Stopper is called and OkToPlay for second fails, third + fourth succeed
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId1) == ePlayYes);
    iIdProvider->InvalidateAt(kStyle, kProviderId1);
    TEST(iRemoveTrackId == kTrackId1);
    TEST(iRemoveStreamId == kStreamId1);
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId2) == ePlayNo);
    TEST(iIdProvider->OkToPlay(kTrackId2, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId3, kStreamId1) == ePlayYes);
}

void SuiteMultiStreams::FirstPlaysThenInvalidateAfter()
{
    // check first can be OkToPlay'd, call InvalidateAfter for first, check OkToPlay for second succeeds, third/fourth fails
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId1) == ePlayYes);
    iIdProvider->InvalidateAfter(kStyle, kProviderId1);
    TEST(iRemoveTrackId == kIdInvalid);
    TEST(iRemoveStreamId == kIdInvalid);
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId2) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId2, kStreamId1) == ePlayNo);
    TEST(iIdProvider->OkToPlay(kTrackId3, kStreamId1) == ePlayNo);
}

void SuiteMultiStreams::FirstPlaysThenInvalidateThird()
{
    // check first can be OkToPlay'd, call InvalidateAt for third, check OkToPlay for second succeeds, third fails, fourth succeeds
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId1) == ePlayYes);
    iIdProvider->InvalidateAt(kStyle, kProviderId2);
    TEST(iRemoveTrackId == kIdInvalid);
    TEST(iRemoveStreamId == kIdInvalid);
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId2) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId2, kStreamId1) == ePlayNo);
    TEST(iIdProvider->OkToPlay(kTrackId3, kStreamId1) == ePlayYes);
}

void SuiteMultiStreams::FirstPlaysThenInvalidateAfterThird()
{
    // check first can be OkToPlay'd, call InvalidateAfter for third, check OkToPlay for second/third succeeds, fourth fails
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId1) == ePlayYes);
    iIdProvider->InvalidateAfter(kStyle, kProviderId2);
    TEST(iRemoveTrackId == kIdInvalid);
    TEST(iRemoveStreamId == kIdInvalid);
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId2) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId2, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId3, kStreamId1) == ePlayNo);
}

void SuiteMultiStreams::SecondPlaysThenInvalidated()
{
    // play first then second, call InvalidateAt for second, check Stopper is called and OkToPlay for third, fourth succeeds
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId2) == ePlayYes);
    iIdProvider->InvalidateAt(kStyle, kProviderId1);
    TEST(iRemoveTrackId == kTrackId1);
    TEST(iRemoveStreamId == kStreamId2);
    TEST(iIdProvider->OkToPlay(kTrackId2, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId3, kStreamId1) == ePlayYes);
}

void SuiteMultiStreams::SecondPlaysThenInvalidateAfter()
{
    // play first then second, call InvalidateAfter for second, check OkToPlay for third, fourth fails
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId2) == ePlayYes);
    iIdProvider->InvalidateAfter(kStyle, kProviderId1);
    TEST(iRemoveTrackId == kIdInvalid);
    TEST(iRemoveStreamId == kIdInvalid);
    TEST(iIdProvider->OkToPlay(kTrackId2, kStreamId1) == ePlayNo);
    TEST(iIdProvider->OkToPlay(kTrackId3, kStreamId1) == ePlayNo);
}

void SuiteMultiStreams::InvalidateAtWithInvalidStyle()
{
    // check first can be OkToPlay'd, call InvalidateAt for first with incorrect style, check OkToPlay for second etc succeeds
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId1) == ePlayYes);
    iIdProvider->InvalidateAt(Brn("incorrect"), kProviderId1);
    TEST(iRemoveTrackId == kIdInvalid);
    TEST(iRemoveStreamId == kIdInvalid);
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId2) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId2, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId3, kStreamId1) == ePlayYes);
}

void SuiteMultiStreams::InvalidateAtWithInvalidProviderId()
{
    // check first can be OkToPlay'd, call InvalidateAt for first with incorrect providerId, check OkToPlay for second etc succeeds
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId1) == ePlayYes);
    iIdProvider->InvalidateAfter(kStyle, Brn("incorrect"));
    TEST(iRemoveTrackId == kIdInvalid);
    TEST(iRemoveStreamId == kIdInvalid);
    TEST(iIdProvider->OkToPlay(kTrackId1, kStreamId2) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId2, kStreamId1) == ePlayYes);
    TEST(iIdProvider->OkToPlay(kTrackId3, kStreamId1) == ePlayYes);
}



void TestIdProvider()
{
    Runner runner("Basic IdProvider tests\n");
    runner.Add(new SuiteIdsAreUnique());
    runner.Add(new SuiteSingleStream());
    runner.Add(new SuiteMaxStreams());
    runner.Add(new SuiteMultiStreams());
    runner.Run();
}