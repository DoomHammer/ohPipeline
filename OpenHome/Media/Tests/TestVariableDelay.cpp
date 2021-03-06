#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Pipeline/VariableDelay.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/InfoProvider.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>
#include <OpenHome/Media/Pipeline/RampValidator.h>

#include <string.h>
#include <limits.h>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuiteVariableDelay : public SuiteUnitTest, private IPipelineElementUpstream, private IStreamHandler, private IMsgProcessor
{
    static const TUint kDecodedAudioCount = 2;
    static const TUint kMsgAudioPcmCount  = 2;
    static const TUint kMsgSilenceCount   = 1;

    static const TUint kRampDuration = Jiffies::kPerMs * 20;
    static const TUint kDownstreamDelay = 30 * Jiffies::kPerMs;
    static const TUint kMsgSilenceSize = Jiffies::kPerMs;

    static const TUint kSampleRate  = 44100;
    static const TUint kNumChannels = 2;

    static const Brn kMode;
public:
    SuiteVariableDelay();
    ~SuiteVariableDelay();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryStop(TUint aStreamId) override;
    void NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving) override;
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgBitRate* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    enum EMsgType
    {
        ENone
       ,EMsgAudioPcm
       ,EMsgSilence
       ,EMsgPlayable
       ,EMsgDecodedStream
       ,EMsgMode
       ,EMsgTrack
       ,EMsgDrain
       ,EMsgDelay
       ,EMsgEncodedStream
       ,EMsgMetaText
       ,EMsgStreamInterrupted
       ,EMsgBitRate
       ,EMsgHalt
       ,EMsgFlush
       ,EMsgWait
       ,EMsgQuit
    };
private:
    void PullNext();
    void PullNext(EMsgType aExpectedMsg);
    MsgAudio* CreateAudio();
    void TestAllMsgsPass();
    void TestDelayShorterThanDownstreamIgnored();
    void TestDelayFromRunning();
    void TestDelayFromStarting();
    void TestReduceDelayFromRunning();
    void TestChangeDelayWhileRampingDown();
    void TestChangeDelayWhileRampingUp();
    void TestNewStreamCancelsRamp();
    void TestNotifyStarvingFromStarting();
    void TestNotifyStarvingFromRunning();
    void TestNotifyStarvingFromRampingDown();
    void TestNotifyStarvingFromRampingUp();
    void TestNoSilenceInjectedBeforeDecodedStream();
    void TestDelayAppliedAfterDrain();
private:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
    VariableDelay* iVariableDelay;
    RampValidator* iRampValidator;
    EMsgType iNextGeneratedMsg;
    EMsgType iLastMsg;
    TUint iJiffies;
    TUint iNumMsgsGenerated;
    TUint iAudioMsgSizeJiffies;
    TUint64 iTrackOffset;
    TBool iNextModeSupportsLatency;
    TUint iNextDelayAbsoluteJiffies;
    TUint iStreamId;
    TUint iNextStreamId;
};

} // namespace Media
} // namespace OpenHome


// SuiteVariableDelay

const Brn SuiteVariableDelay::kMode("VariableDelayMode");

SuiteVariableDelay::SuiteVariableDelay()
    : SuiteUnitTest("VariableDelay")
{
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestAllMsgsPass), "TestAllMsgsPass");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestDelayShorterThanDownstreamIgnored), "TestDelayShorterThanDownstreamIgnored");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestDelayFromRunning), "TestDelayFromRunning");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestDelayFromStarting), "TestDelayFromStarting");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestReduceDelayFromRunning), "TestReduceDelayFromRunning");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestChangeDelayWhileRampingDown), "TestChangeDelayWhileRampingDown");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestChangeDelayWhileRampingUp), "TestChangeDelayWhileRampingUp");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestNewStreamCancelsRamp), "TestNewStreamCancelsRamp");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestNotifyStarvingFromStarting), "TestNotifyStarvingFromStarting");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestNotifyStarvingFromRunning), "TestNotifyStarvingFromRunning");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestNotifyStarvingFromRampingDown), "TestNotifyStarvingFromRampingDown");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestNotifyStarvingFromRampingUp), "TestNotifyStarvingFromRampingUp");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestNoSilenceInjectedBeforeDecodedStream), "TestNoSilenceInjectedBeforeDecodedStream");
    AddTest(MakeFunctor(*this, &SuiteVariableDelay::TestDelayAppliedAfterDrain), "TestDelayAppliedAfterDrain");
}

SuiteVariableDelay::~SuiteVariableDelay()
{
}

void SuiteVariableDelay::Setup()
{
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(kMsgAudioPcmCount, kDecodedAudioCount);
    init.SetMsgSilenceCount(kMsgSilenceCount);
    init.SetMsgEncodedStreamCount(2);
    init.SetMsgDecodedStreamCount(2);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 1);
    iVariableDelay = new VariableDelay("Variable Delay", *iMsgFactory, *this, kDownstreamDelay, kRampDuration);
    iRampValidator = new RampValidator(*iVariableDelay, "RampValidator");
    iLastMsg = ENone;
    iJiffies = 0;
    iNumMsgsGenerated = 0;
    iAudioMsgSizeJiffies = 0;
    iTrackOffset = 0;
    iNextModeSupportsLatency = true;
    iNextDelayAbsoluteJiffies = 0;
    iStreamId = UINT_MAX;
    iNextStreamId = 0;
}

void SuiteVariableDelay::TearDown()
{
    delete iRampValidator;
    delete iVariableDelay;
    delete iMsgFactory;
    delete iTrackFactory;
}

Msg* SuiteVariableDelay::Pull()
{
    iNumMsgsGenerated++;
    switch (iNextGeneratedMsg)
    {
    case EMsgAudioPcm:
        return CreateAudio();
    case EMsgSilence:
        return iMsgFactory->CreateMsgSilence(kMsgSilenceSize);
    case EMsgDecodedStream:
        return iMsgFactory->CreateMsgDecodedStream(iNextStreamId++, 0, 0, 0, 0, Brx::Empty(), 0, 0, false, false, false, nullptr);
    case EMsgMode:
        return iMsgFactory->CreateMsgMode(kMode, iNextModeSupportsLatency, true, ModeClockPullers(), false, false);
    case EMsgTrack:
    {
        Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
        Msg* msg = iMsgFactory->CreateMsgTrack(*track);
        track->RemoveRef();
        return msg;
    }
    case EMsgDrain:
        return iMsgFactory->CreateMsgDrain(Functor());
    case EMsgDelay:
        return iMsgFactory->CreateMsgDelay(iNextDelayAbsoluteJiffies);
    case EMsgEncodedStream:
        return iMsgFactory->CreateMsgEncodedStream(Brn("http://1.2.3.4:5"), Brn("metatext"), 0, 0, 0, false, false, nullptr);
    case EMsgMetaText:
        return iMsgFactory->CreateMsgMetaText(Brn("metatext"));
    case EMsgStreamInterrupted:
        return iMsgFactory->CreateMsgStreamInterrupted();
    case EMsgBitRate:
        return iMsgFactory->CreateMsgBitRate(100);
    case EMsgHalt:
        return iMsgFactory->CreateMsgHalt();
    case EMsgFlush:
        return iMsgFactory->CreateMsgFlush(1);
    case EMsgWait:
        return iMsgFactory->CreateMsgWait();
    case EMsgQuit:
        return iMsgFactory->CreateMsgQuit();
    default:
        ASSERTS();
        return nullptr;
    }
}

MsgAudio* SuiteVariableDelay::CreateAudio()
{
    static const TUint kDataBytes = 3 * 1024;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0x7f, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, kNumChannels, kSampleRate, 16, EMediaDataEndianLittle, iTrackOffset);
    iAudioMsgSizeJiffies = audio->Jiffies();
    iTrackOffset += iAudioMsgSizeJiffies;
    return audio;
}

EStreamPlay SuiteVariableDelay::OkToPlay(TUint /*aStreamId*/)
{
    ASSERTS();
    return ePlayNo;
}

TUint SuiteVariableDelay::TrySeek(TUint /*aStreamId*/, TUint64 /*aOffset*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint SuiteVariableDelay::TryStop(TUint /*aStreamId*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

void SuiteVariableDelay::NotifyStarving(const Brx& /*aMode*/, TUint /*aStreamId*/, TBool /*aStarving*/)
{
}

Msg* SuiteVariableDelay::ProcessMsg(MsgMode* aMsg)
{
    iLastMsg = EMsgMode;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgTrack* aMsg)
{
    iLastMsg = EMsgTrack;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgDrain* aMsg)
{
    iLastMsg = EMsgDrain;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgDelay* aMsg)
{
    iLastMsg = EMsgDelay;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastMsg = EMsgEncodedStream;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS(); /* only expect to deal with decoded audio at this stage of the pipeline */
    return nullptr;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgMetaText* aMsg)
{
    iLastMsg = EMsgMetaText;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iLastMsg = EMsgStreamInterrupted;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgHalt* aMsg)
{
    iLastMsg = EMsgHalt;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgFlush* aMsg)
{
    iLastMsg = EMsgFlush;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgWait* aMsg)
{
    iLastMsg = EMsgWait;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastMsg = EMsgDecodedStream;
    iStreamId = aMsg->StreamInfo().StreamId();
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgBitRate* aMsg)
{
    iLastMsg = EMsgBitRate;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastMsg = EMsgAudioPcm;
    TUint jiffies = aMsg->Jiffies();

    MsgPlayable* playable = aMsg->CreatePlayable();
    ProcessorPcmBufTest pcmProcessor;
    playable->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());
    playable->RemoveRef();
    const TByte* ptr = buf.Ptr();
    const TInt firstSubsample = (ptr[0]<<8) | ptr[1];
    const TUint bytes = buf.Bytes();
    const TInt lastSubsample = (ptr[bytes-2]<<8) | ptr[bytes-1];

    switch (iVariableDelay->iStatus)
    {
    case VariableDelay::ERampingDown:
        TEST(firstSubsample > lastSubsample);
        break;
    case VariableDelay::ERampingUp:
        TEST(firstSubsample < lastSubsample);
        break;
    case VariableDelay::ERampedDown:
        break;
    case VariableDelay::ERunning:
        if (iJiffies >= kRampDuration) {
            TEST(firstSubsample == lastSubsample);
        }
        break;
    case VariableDelay::EStarting:
        TEST(firstSubsample == lastSubsample);
        break;
    }
    iJiffies += jiffies;
    return nullptr;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgSilence* aMsg)
{
    iLastMsg = EMsgSilence;
    iJiffies += aMsg->Jiffies();
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS(); // MsgPlayable not expected at this stage of the pipeline
    return nullptr;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgQuit* aMsg)
{
    iLastMsg = EMsgQuit;
    return aMsg;
}

void SuiteVariableDelay::PullNext()
{
    Msg* msg = iRampValidator->Pull();
    msg = msg->Process(*this);
    if (msg != nullptr) {
        msg->RemoveRef();
    }
}

void SuiteVariableDelay::PullNext(EMsgType aExpectedMsg)
{
    iNextGeneratedMsg = aExpectedMsg;
    PullNext();
    TEST(iLastMsg == aExpectedMsg);
}

void SuiteVariableDelay::TestAllMsgsPass()
{
    /* 'AllMsgs' excludes encoded & playable audio - VariableDelay is assumed only
       useful to the portion of the pipeline that deals in decoded audio */
    static const EMsgType msgs[] = { EMsgMode, EMsgTrack, EMsgDrain, EMsgDelay,
                                     EMsgEncodedStream, EMsgMetaText, EMsgStreamInterrupted,
                                     EMsgDecodedStream, EMsgBitRate, EMsgAudioPcm, EMsgSilence,
                                     EMsgHalt, EMsgFlush, EMsgWait, EMsgQuit };
    for (TUint i=0; i<sizeof(msgs)/sizeof(msgs[0]); i++) {
        PullNext(msgs[i]);
    }
}

void SuiteVariableDelay::TestDelayShorterThanDownstreamIgnored()
{
    PullNext(EMsgMode);
    TEST(iVariableDelay->iDelayJiffies == 0);
    TEST(iVariableDelay->iDelayAdjustment == 0);
    static const TUint kDelay = kDownstreamDelay - 1;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iDelayJiffies == 0);
    TEST(iVariableDelay->iDelayAdjustment == 0);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
}

void SuiteVariableDelay::TestDelayFromRunning()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);

    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampedDown);

    iJiffies = 0;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelay - kDownstreamDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingUp);

    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelay::ERampingUp);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelay::ERunning);
}

void SuiteVariableDelay::TestDelayFromStarting()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);

    iJiffies = 0;
    iNextGeneratedMsg = EMsgAudioPcm;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext();
    }
    TEST(iJiffies == kDelay - kDownstreamDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERunning);
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelay::ERunning);
}

void SuiteVariableDelay::TestReduceDelayFromRunning()
{
    TestDelayFromStarting();
    static const TUint kDelay = 40 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    while (!iVariableDelay->IsEmpty()) {
        PullNext();
    }
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);

    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampedDown);

    iJiffies = 0;
    TUint64 prevOffset = iTrackOffset;
    const TUint queuedAudio = iVariableDelay->Jiffies();
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingUp);

    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelay::ERampingUp);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelay::ERunning);
    TUint audioGenerated = (TUint)(iTrackOffset - prevOffset);
    TEST(audioGenerated - iJiffies + queuedAudio - iVariableDelay->Jiffies() == 20 * Jiffies::kPerMs);
}

void SuiteVariableDelay::TestChangeDelayWhileRampingDown()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);

    iJiffies = 0;
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);
    static const TUint kDelay2 = 50 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay2;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampedDown);
}

void SuiteVariableDelay::TestChangeDelayWhileRampingUp()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);

    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampedDown);

    iJiffies = 0;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelay - kDownstreamDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingUp);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay2 = 70 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay2;
    const TUint remainingRamp = iVariableDelay->iRemainingRampSize;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iVariableDelay->iRemainingRampSize == iVariableDelay->iRampDuration - remainingRamp);
}

void SuiteVariableDelay::TestNewStreamCancelsRamp()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);
    PullNext(EMsgDecodedStream);
    while (iVariableDelay->iStatus == VariableDelay::EStarting) {
        iNextGeneratedMsg = ENone;
        PullNext();
        TEST(iLastMsg == EMsgSilence);
    }
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelay::ERunning);
}

void SuiteVariableDelay::TestNotifyStarvingFromStarting()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
    iVariableDelay->NotifyStarving(kMode, iStreamId, true);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
    TEST(iVariableDelay->iDelayAdjustment > 0);
    iNextGeneratedMsg = EMsgAudioPcm;
    while (iVariableDelay->iDelayAdjustment > 0) {
        PullNext();
        TEST(iLastMsg == EMsgSilence);
        iNextGeneratedMsg = ENone;
    }
    TEST(iJiffies == kDelay - kDownstreamDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERunning);
    PullNext();
    TEST(iLastMsg == EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelay::ERunning);
}

void SuiteVariableDelay::TestNotifyStarvingFromRunning()
{
    TestDelayFromRunning();
    TEST(iVariableDelay->iStatus == VariableDelay::ERunning);
    iVariableDelay->NotifyStarving(kMode, iStreamId, true);

    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);
    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampedDown);
}

void SuiteVariableDelay::TestNotifyStarvingFromRampingDown()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);

    iJiffies = 0;
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);
    const TUint remainingRamp = iVariableDelay->iRemainingRampSize;
    iVariableDelay->NotifyStarving(kMode, iStreamId, true);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iVariableDelay->iRemainingRampSize == remainingRamp);
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampedDown);
}

void SuiteVariableDelay::TestNotifyStarvingFromRampingUp()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);

    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampedDown);
    iJiffies = 0;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext(EMsgSilence);
    }
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingUp);
    PullNext(EMsgAudioPcm);

    const TUint completedRamp = iVariableDelay->iRampDuration - iVariableDelay->iRemainingRampSize;
    iVariableDelay->NotifyStarving(kMode, iStreamId, true);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iVariableDelay->iRemainingRampSize == completedRamp);
    const TUint remainingRamp = iVariableDelay->iRemainingRampSize;
    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelay::ERampingDown);
    TEST(iJiffies == remainingRamp);
    TEST(remainingRamp < kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampedDown);
}

void SuiteVariableDelay::TestNoSilenceInjectedBeforeDecodedStream()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    static const TUint kDelay = 150 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    PullNext(EMsgTrack);
}

void SuiteVariableDelay::TestDelayAppliedAfterDrain()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);
    static const TUint kDelay = 40 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::EStarting);

    iJiffies = 0;
    iNextGeneratedMsg = EMsgAudioPcm;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext();
    }
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelay::ERunning);
    PullNext(EMsgDrain);
    iNextGeneratedMsg = EMsgSilence;
    PullNext();
    TEST(iLastMsg == EMsgSilence);
    iNextGeneratedMsg = EMsgAudioPcm;
    iJiffies = 0;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext();
        TEST(iLastMsg == EMsgSilence);
    }
    TEST(iJiffies == kDelay - kDownstreamDelay);
    TEST(iVariableDelay->iStatus == VariableDelay::ERampingUp);
    PullNext(EMsgAudioPcm);
}



void TestVariableDelay()
{
    Runner runner("Variable delay tests\n");
    runner.Add(new SuiteVariableDelay());
    runner.Run();
}
