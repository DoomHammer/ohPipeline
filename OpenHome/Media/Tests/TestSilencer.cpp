#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Utils/Silencer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>

#include <list>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuiteSilencer : public SuiteUnitTest, private IPipelineElementUpstream, private IMsgProcessor
{
    static const TUint kSilenceJiffies = Jiffies::kPerMs * 5;
public:
    SuiteSilencer();
    ~SuiteSilencer();
private: // from SuiteUnitTest
    void Setup();
    void TearDown();
private:
    void TestMsgsPassedOn();
    void TestSilenceGeneratedWhenNoMsgAvailable();
    void TestHaltIsConsumed();
    void TestSilenceDurationIsCorrect();
    void TestPassesMsgsAfterSilenceGeneration();
private: // from IPipelineElementUpstream
    Msg* Pull();
private:
    enum EMsgType
    {
        ENone
       ,EMsgPlayable
       ,EMsgDecodedStream
       ,EMsgMode
       ,EMsgHalt
       ,EMsgQuit
    };
private:
    void QueuePendingMsg(Msg* aMsg);
    MsgPlayable* CreateAudio();
    Msg* CreateDecodedStream();
    void PullNext(EMsgType aExpectedMsg);
    void PullNextNoWait(EMsgType aExpectedMsg);
    TBool TimesEqual(TUint aJiffy1, TUint aJiffy2);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg);
    Msg* ProcessMsg(MsgSession* aMsg);
    Msg* ProcessMsg(MsgTrack* aMsg);
    Msg* ProcessMsg(MsgDelay* aMsg);
    Msg* ProcessMsg(MsgEncodedStream* aMsg);
    Msg* ProcessMsg(MsgAudioEncoded* aMsg);
    Msg* ProcessMsg(MsgMetaText* aMsg);
    Msg* ProcessMsg(MsgHalt* aMsg);
    Msg* ProcessMsg(MsgFlush* aMsg);
    Msg* ProcessMsg(MsgWait* aMsg);
    Msg* ProcessMsg(MsgDecodedStream* aMsg);
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
    Msg* ProcessMsg(MsgSilence* aMsg);
    Msg* ProcessMsg(MsgPlayable* aMsg);
    Msg* ProcessMsg(MsgQuit* aMsg);
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
    Silencer* iSilencer;
    std::list<Msg*> iPendingMsgs;
    Mutex iLock;
    Semaphore iMsgSem;
    EMsgType iLastMsg;
    TUint64 iTrackOffset;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
    TUint iLastPulledBytes;
    TUint iLastPulledJiffies;
    TBool iLastPlayableWasSilence;
};

} // namespace Media
} // namespace OpenHome


// SuiteSilencer

SuiteSilencer::SuiteSilencer()
    : SuiteUnitTest("Silencer tests")
    , iLock("TSIL")
    , iMsgSem("TSIL", 0)
    , iLastMsg(ENone)
    , iTrackOffset(0)
    , iNumChannels(2)
{
    AddTest(MakeFunctor(*this, &SuiteSilencer::TestMsgsPassedOn), "TestMsgsPassedOn");
    AddTest(MakeFunctor(*this, &SuiteSilencer::TestSilenceGeneratedWhenNoMsgAvailable), "TestSilenceGeneratedWhenNoMsgAvailable");
    AddTest(MakeFunctor(*this, &SuiteSilencer::TestHaltIsConsumed), "TestHaltIsConsumed");
    AddTest(MakeFunctor(*this, &SuiteSilencer::TestSilenceDurationIsCorrect), "TestSilenceDurationIsCorrect");
    AddTest(MakeFunctor(*this, &SuiteSilencer::TestPassesMsgsAfterSilenceGeneration), "TestPassesMsgsAfterSilenceGeneration");
}

SuiteSilencer::~SuiteSilencer()
{
}

void SuiteSilencer::Setup()
{
    iSampleRate = 44100;
    iBitDepth = 16;
    iNumChannels = 2;
    iTrackOffset = 0;
    iMsgFactory = new MsgFactory(iInfoAggregator, 1, 1, 10, 10, 10, 10, 10, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1);
    iSilencer = new Silencer(*iMsgFactory, *this, kPriorityNormal, kSilenceJiffies, 2);
    iLastPulledBytes = iLastPulledJiffies = 0;
    iLastPlayableWasSilence = false;
}

void SuiteSilencer::TearDown()
{
    while (iPendingMsgs.size() > 0) {
        iPendingMsgs.front()->RemoveRef();
        iPendingMsgs.pop_front();
    }
    if (!iSilencer->iQuit) {
        QueuePendingMsg(iMsgFactory->CreateMsgQuit());
        PullNext(EMsgQuit);
    }
    delete iSilencer;
    delete iMsgFactory;
}

void SuiteSilencer::TestMsgsPassedOn()
{
    QueuePendingMsg(iMsgFactory->CreateMsgMode(Brn("dummyMode"), true, false, NULL));
    QueuePendingMsg(CreateDecodedStream());
    QueuePendingMsg(CreateAudio());
    QueuePendingMsg(iMsgFactory->CreateMsgQuit());

    PullNext(EMsgMode);
    PullNext(EMsgDecodedStream);
    PullNext(EMsgPlayable);
    TEST(!iLastPlayableWasSilence);
    PullNext(EMsgQuit);
}

void SuiteSilencer::TestSilenceGeneratedWhenNoMsgAvailable()
{
    QueuePendingMsg(CreateDecodedStream());
    PullNext(EMsgDecodedStream);
    PullNextNoWait(EMsgPlayable);
    TEST(iLastPlayableWasSilence);
}

void SuiteSilencer::TestHaltIsConsumed()
{
    QueuePendingMsg(iMsgFactory->CreateMsgHalt());
    QueuePendingMsg(CreateDecodedStream());
    PullNext(EMsgDecodedStream);
}

void SuiteSilencer::TestSilenceDurationIsCorrect()
{
    QueuePendingMsg(CreateDecodedStream());
    PullNext(EMsgDecodedStream);
    PullNextNoWait(EMsgPlayable);
    TEST(TimesEqual(iLastPulledJiffies, kSilenceJiffies));
    TEST(iLastPlayableWasSilence);

    iSampleRate = 48000;
    QueuePendingMsg(CreateDecodedStream());
    PullNext(EMsgDecodedStream);
    PullNextNoWait(EMsgPlayable);
    TEST(TimesEqual(iLastPulledJiffies, kSilenceJiffies));
    TEST(iLastPlayableWasSilence);

    iBitDepth = 24;
    QueuePendingMsg(CreateDecodedStream());
    PullNext(EMsgDecodedStream);
    PullNextNoWait(EMsgPlayable);
    TEST(TimesEqual(iLastPulledJiffies, kSilenceJiffies));
    TEST(iLastPlayableWasSilence);

    iNumChannels = 1;
    QueuePendingMsg(CreateDecodedStream());
    PullNext(EMsgDecodedStream);
    PullNextNoWait(EMsgPlayable);
    TEST(TimesEqual(iLastPulledJiffies, kSilenceJiffies));
    TEST(iLastPlayableWasSilence);
}

void SuiteSilencer::TestPassesMsgsAfterSilenceGeneration()
{
    QueuePendingMsg(CreateDecodedStream());
    PullNext(EMsgDecodedStream);
    PullNextNoWait(EMsgPlayable);

    QueuePendingMsg(iMsgFactory->CreateMsgMode(Brn("dummyMode"), true, false, NULL));
    QueuePendingMsg(CreateDecodedStream());
    QueuePendingMsg(CreateAudio());

    PullNext(EMsgMode);
    PullNext(EMsgDecodedStream);
    PullNext(EMsgPlayable);
    TEST(!iLastPlayableWasSilence);
}

Msg* SuiteSilencer::Pull()
{
    iMsgSem.Wait();
    iLock.Wait();
    ASSERT(iPendingMsgs.size() > 0);
    Msg* msg = iPendingMsgs.front();
    iPendingMsgs.pop_front();
    iLock.Signal();
    return msg;
}

void SuiteSilencer::QueuePendingMsg(Msg* aMsg)
{
    iLock.Wait();
    iPendingMsgs.push_back(aMsg);
    iMsgSem.Signal();
    iLock.Signal();
}

MsgPlayable* SuiteSilencer::CreateAudio()
{
    static const TUint kDataBytes = 3 * 1024;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0x7f, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, iNumChannels, iSampleRate, iBitDepth, EMediaDataLittleEndian, iTrackOffset);
    iTrackOffset += audio->Jiffies();
    MsgPlayable* playable = audio->CreatePlayable();
    return playable;
}

Msg* SuiteSilencer::CreateDecodedStream()
{
    return iMsgFactory->CreateMsgDecodedStream(0, 128000, iBitDepth, iSampleRate, iNumChannels, Brn("dummy codec"), (TUint64)1<<31, 0, false, false, false, NULL);
}

void SuiteSilencer::PullNext(EMsgType aExpectedMsg)
{
    while (iSilencer->iFifo.SlotsUsed() == 0) {
        Thread::Sleep(1); /* sop to platforms without thread priorities; ensure any recently
                             queued msg has been grabbed by the Silencer thread. */
    }
    PullNextNoWait(aExpectedMsg);
}

void SuiteSilencer::PullNextNoWait(EMsgType aExpectedMsg)
{
    Msg* msg = iSilencer->Pull();
    ASSERT(msg != NULL);
    msg = msg->Process(*this);
    msg->RemoveRef();
    TEST(iLastMsg == aExpectedMsg);
}

TBool SuiteSilencer::TimesEqual(TUint aJiffy1, TUint aJiffy2)
{
    /* Not all sample rates can represent a precise number of milliseconds.
       So we might end up pulling slightly more or less audio than we expect.
       This nasty bodge checks that times are roughly equivalent, all we need for this test. */
    static const TUint kJiffyEpsilon = Jiffies::kPerMs / 50;
    if (aJiffy1 > aJiffy2) {
        return (aJiffy1 - aJiffy2 < kJiffyEpsilon);
    }
    return (aJiffy2 - aJiffy1 < kJiffyEpsilon);
}

Msg* SuiteSilencer::ProcessMsg(MsgMode* aMsg)
{
    iLastMsg = EMsgMode;
    return aMsg;
}

Msg* SuiteSilencer::ProcessMsg(MsgSession* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSilencer::ProcessMsg(MsgTrack* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSilencer::ProcessMsg(MsgDelay* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSilencer::ProcessMsg(MsgEncodedStream* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSilencer::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSilencer::ProcessMsg(MsgMetaText* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSilencer::ProcessMsg(MsgHalt* aMsg)
{
    iLastMsg = EMsgHalt;
    return aMsg;
}

Msg* SuiteSilencer::ProcessMsg(MsgFlush* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSilencer::ProcessMsg(MsgWait* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSilencer::ProcessMsg(MsgDecodedStream* aMsg)
{
    TEST(aMsg->StreamInfo().BitDepth() == iBitDepth);
    TEST(aMsg->StreamInfo().SampleRate() == iSampleRate);
    TEST(aMsg->StreamInfo().NumChannels() == iNumChannels);
    iLastMsg = EMsgDecodedStream;
    return aMsg;
}

Msg* SuiteSilencer::ProcessMsg(MsgAudioPcm* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSilencer::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSilencer::ProcessMsg(MsgPlayable* aMsg)
{
    iLastMsg = EMsgPlayable;
    iLastPulledBytes = aMsg->Bytes();
    const TUint jiffiesPerSample = Jiffies::JiffiesPerSample(iSampleRate);
    const TUint bytesPerSample = iNumChannels * iBitDepth/8;
    iLastPulledJiffies = (iLastPulledBytes / bytesPerSample) * jiffiesPerSample;

    ProcessorPcmBufPacked pcmProcessor;
    aMsg->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());
    const TByte* ptr = buf.Ptr();
    const TUint bytes = buf.Bytes();
    TUint firstSubsample, lastSubsample;
    switch (iBitDepth)
    {
    default:
        ASSERTS();
    case 8:
        firstSubsample = ptr[0];
        lastSubsample = ptr[bytes-1];
        break;
    case 16:
        firstSubsample = (ptr[0]<<8) | ptr[1];
        lastSubsample = (ptr[bytes-2]<<8) | ptr[bytes-1];
        break;
    case 24:
        firstSubsample = (ptr[0]<<16) | (ptr[1]<<8) | ptr[2];
        lastSubsample = (ptr[bytes-3]<<16) | (ptr[bytes-2]<<8) | ptr[bytes-1];
        break;
    }
    iLastPlayableWasSilence = (firstSubsample == 0 && lastSubsample == 0);

    return aMsg;
}

Msg* SuiteSilencer::ProcessMsg(MsgQuit* aMsg)
{
    iLastMsg = EMsgQuit;
    return aMsg;
}



void TestSilencer()
{
    Runner runner("Silencer tests\n");
    runner.Add(new SuiteSilencer());
    runner.Run();
}
