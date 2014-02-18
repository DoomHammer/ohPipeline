#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Skipper.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Av/InfoProvider.h>
#include "AllocatorInfoLogger.h"
#include <OpenHome/Media/ProcessorPcmUtils.h>

#include <list>
#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuiteSkipper : public SuiteUnitTest, private IPipelineElementUpstream, private IStreamHandler, private IMsgProcessor
{
    static const TUint kRampDuration = Jiffies::kJiffiesPerMs * 50; // shorter than production code but this is assumed to not matter
    static const TUint kExpectedFlushId = 5;
    static const TUint kSampleRate = 44100;
    static const TUint kNumChannels = 2;
public:
    SuiteSkipper();
    ~SuiteSkipper();
private: // from SuiteUnitTest
    void Setup();
    void TearDown();
private: // from IPipelineElementUpstream
    Msg* Pull();
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
    TUint TrySeek(TUint aTrackId, TUint aStreamId, TUint64 aOffset);
    TUint TryStop(TUint aTrackId, TUint aStreamId);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgTrack* aMsg);
    Msg* ProcessMsg(MsgEncodedStream* aMsg);
    Msg* ProcessMsg(MsgAudioEncoded* aMsg);
    Msg* ProcessMsg(MsgMetaText* aMsg);
    Msg* ProcessMsg(MsgHalt* aMsg);
    Msg* ProcessMsg(MsgFlush* aMsg);
    Msg* ProcessMsg(MsgDecodedStream* aMsg);
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
    Msg* ProcessMsg(MsgSilence* aMsg);
    Msg* ProcessMsg(MsgPlayable* aMsg);
    Msg* ProcessMsg(MsgQuit* aMsg);
private:
    enum EMsgType
    {
        ENone
       ,EMsgTrack
       ,EMsgEncodedStream
       ,EMsgMetaText
       ,EMsgDecodedStream
       ,EMsgAudioPcm
       ,EMsgSilence
       ,EMsgHalt
       ,EMsgFlush
       ,EMsgQuit
    };
private:
    void PullNext();
    void PullNext(EMsgType aExpectedMsg);
    Msg* CreateTrack();
    Msg* CreateEncodedStream();
    Msg* CreateDecodedStream();
    Msg* CreateAudio();
    void TestAllMsgsPassWhileNotSkipping();
    void TestRemoveStreamRampAudioRampsDown();
    void TestRemoveStreamRampHaltDeliveredOnRampDown();
    void TestRemoveStreamRampAllMsgsPassDuringRamp();
    void TestRemoveStreamRampFewMsgsPassAfterRamp();
    void TestRemoveStreamRampNewTrackResets();
    void TestRemoveStreamRampNewStreamResets();
    void TestRemoveStreamNoRampFewMsgsPass();
    void TestTryRemoveInvalidTrack();
    void TestTryRemoveInvalidStream();
    void TestTryRemoveRampValidTrackAndStream();
    void TestTryRemoveNoRampValidTrackAndStream();
private:
    AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    MsgFactory* iMsgFactory;
    Skipper* iSkipper;
    EMsgType iLastPulledMsg;
    TBool iRamping;
    TUint iTrackId;
    TUint iStreamId;
    TUint64 iTrackOffset;
    TUint64 iJiffies;
    std::list<Msg*> iPendingMsgs;
    TUint iLastSubsample;
    TUint iNextTrackId;
    TUint iNextStreamId;
};

} // namespace Media
} // namespace OpenHome


SuiteSkipper::SuiteSkipper()
    : SuiteUnitTest("Skipper")
{
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestAllMsgsPassWhileNotSkipping), "TestAllMsgsPassWhileNotSkipping");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampAudioRampsDown), "TestRemoveStreamRampAudioRampsDown");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampHaltDeliveredOnRampDown), "TestRemoveStreamRampHaltDeliveredOnRampDown");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampAllMsgsPassDuringRamp), "TestRemoveStreamRampAllMsgsPassDuringRamp");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampFewMsgsPassAfterRamp), "TestRemoveStreamRampFewMsgsPassAfterRamp");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampNewTrackResets), "TestRemoveStreamRampNewTrackResets");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampNewStreamResets), "TestRemoveStreamRampNewStreamResets");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamNoRampFewMsgsPass), "TestRemoveStreamNoRampFewMsgsPass");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestTryRemoveInvalidTrack), "TestTryRemoveInvalidTrack");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestTryRemoveInvalidStream), "TestTryRemoveInvalidStream");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestTryRemoveRampValidTrackAndStream), "TestTryRemoveRampValidTrackAndStream");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestTryRemoveNoRampValidTrackAndStream), "TestTryRemoveNoRampValidTrackAndStream");
}

SuiteSkipper::~SuiteSkipper()
{
}

void SuiteSkipper::Setup()
{
    iTrackFactory = new TrackFactory(iInfoAggregator, 5);
    iMsgFactory = new MsgFactory(iInfoAggregator, 0, 0, 50, 52, 10, 1, 0, 2, 2, 2, 2, 2, 2, 1);
    iSkipper = new Skipper(*iMsgFactory, *this, kRampDuration);
    iTrackId = iStreamId = UINT_MAX;
    iTrackOffset = 0;
    iJiffies = 0;
    iRamping = false;
    iLastSubsample = 0xffffff;
    iNextTrackId = 1;
    iNextStreamId = 1;
}

void SuiteSkipper::TearDown()
{
    while (iPendingMsgs.size() > 0) {
        iPendingMsgs.front()->RemoveRef();
        iPendingMsgs.pop_front();
    }
    delete iSkipper;
    delete iMsgFactory;
    delete iTrackFactory;
}

Msg* SuiteSkipper::Pull()
{
    ASSERT(iPendingMsgs.size() > 0);
    Msg* msg = iPendingMsgs.front();
    iPendingMsgs.pop_front();
    return msg;
}

EStreamPlay SuiteSkipper::OkToPlay(TUint /*aTrackId*/, TUint /*aStreamId*/)
{
    ASSERTS();
    return ePlayNo;
}

TUint SuiteSkipper::TrySeek(TUint /*aTrackId*/, TUint /*aStreamId*/, TUint64 /*aOffset*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint SuiteSkipper::TryStop(TUint aTrackId, TUint aStreamId)
{
    if (aTrackId == iTrackId && aStreamId == iStreamId) {
        return kExpectedFlushId;
    }
    return MsgFlush::kIdInvalid;
}

Msg* SuiteSkipper::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSkipper::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastPulledMsg = EMsgAudioPcm;
    iJiffies += aMsg->Jiffies();
    MsgPlayable* playable = aMsg->CreatePlayable();
    ProcessorPcmBufPacked pcmProcessor;
    playable->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());
    ASSERT(buf.Bytes() >= 6);
    const TByte* ptr = buf.Ptr();
    const TUint bytes = buf.Bytes();
    const TUint firstSubsample = (ptr[0]<<16) | (ptr[1]<<8) | ptr[2];

    if (iRamping) {
        TEST(firstSubsample <= iLastSubsample);
    }
    else {
        TEST(firstSubsample == 0x7f7f7f);
    }
        iLastSubsample = (ptr[bytes-3]<<16) | (ptr[bytes-2]<<8) | ptr[bytes-1];
    if (iRamping) {
        TEST(iLastSubsample < firstSubsample);
        iRamping = (iLastSubsample > 0);
    }
    else {
        TEST(firstSubsample == 0x7f7f7f);
    }

    return playable;
}

Msg* SuiteSkipper::ProcessMsg(MsgSilence* aMsg)
{
    iLastPulledMsg = EMsgSilence;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteSkipper::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastPulledMsg = EMsgDecodedStream;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgTrack* aMsg)
{
    iLastPulledMsg = EMsgTrack;
    iTrackId = aMsg->IdPipeline();
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastPulledMsg = EMsgEncodedStream;
    iStreamId = aMsg->StreamId();
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgMetaText* aMsg)
{
    iLastPulledMsg = EMsgMetaText;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgHalt* aMsg)
{
    iLastPulledMsg = EMsgHalt;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgFlush* aMsg)
{
    iLastPulledMsg = EMsgFlush;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgQuit* aMsg)
{
    iLastPulledMsg = EMsgQuit;
    return aMsg;
}

void SuiteSkipper::PullNext()
{
    Msg* msg = iSkipper->Pull();
    msg = msg->Process(*this);
    msg->RemoveRef();
}

void SuiteSkipper::PullNext(EMsgType aExpectedMsg)
{
    Msg* msg = iSkipper->Pull();
    msg = msg->Process(*this);
    msg->RemoveRef();
    TEST(iLastPulledMsg == aExpectedMsg);
}

Msg* SuiteSkipper::CreateTrack()
{
    Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty(), NULL, false);
    Msg* msg = iMsgFactory->CreateMsgTrack(*track, iNextTrackId++, Brn("testmode"));
    track->RemoveRef();
    return msg;
}

Msg* SuiteSkipper::CreateEncodedStream()
{
    return iMsgFactory->CreateMsgEncodedStream(Brx::Empty(), Brx::Empty(), 1<<21, ++iNextStreamId, true, false, this);
}

Msg* SuiteSkipper::CreateDecodedStream()
{
    return iMsgFactory->CreateMsgDecodedStream(iNextStreamId, 100, 24, kSampleRate, kNumChannels, Brn("notARealCodec"), 1LL<<38, 0, true, true, false);
}

Msg* SuiteSkipper::CreateAudio()
{
    static const TUint kDataBytes = 3 * 1024;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0x7f, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, kNumChannels, kSampleRate, 24, EMediaDataLittleEndian, iTrackOffset);
    iTrackOffset += audio->Jiffies();
    return audio;
}

void SuiteSkipper::TestAllMsgsPassWhileNotSkipping()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(Jiffies::kJiffiesPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(2));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    iPendingMsgs.push_back(CreateTrack());

    PullNext(EMsgTrack);
    PullNext(EMsgEncodedStream);
    PullNext(EMsgMetaText);
    PullNext(EMsgDecodedStream);
    PullNext(EMsgAudioPcm);
    PullNext(EMsgSilence);
    PullNext(EMsgHalt);
    PullNext(EMsgFlush);
    PullNext(EMsgQuit);
    PullNext(EMsgTrack);
}

void SuiteSkipper::TestRemoveStreamRampAudioRampsDown()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    iSkipper->RemoveCurrentStream(true);
    iRamping = true;
    iJiffies = 0;
    while (iRamping) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    TEST(iJiffies == kRampDuration);
}

void SuiteSkipper::TestRemoveStreamRampHaltDeliveredOnRampDown()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    for (TUint i=0; i<3; i++) {
        PullNext();
    }

    iSkipper->RemoveCurrentStream(true);
    iRamping = true;
    while (iRamping) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    PullNext(EMsgHalt);
}

void SuiteSkipper::TestRemoveStreamRampAllMsgsPassDuringRamp()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());

    for (TUint i=0; i<3; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgDecodedStream);

    iSkipper->RemoveCurrentStream(true);
    iRamping = true;
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    PullNext(EMsgMetaText);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(Jiffies::kJiffiesPerMs * 3));
    PullNext(EMsgSilence);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    PullNext(EMsgHalt);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(2));
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
}

void SuiteSkipper::TestRemoveStreamRampFewMsgsPassAfterRamp()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());

    for (TUint i=0; i<3; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgDecodedStream);

    iSkipper->RemoveCurrentStream(true);
    iRamping = true;
    while (iRamping) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    PullNext(EMsgHalt);
    while (!iSkipper->iQueue.IsEmpty()) {
        PullNext(EMsgAudioPcm);
    }
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(Jiffies::kJiffiesPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    PullNext(EMsgHalt);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId)); // should be consumed by Skipper
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId+1));
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
}

void SuiteSkipper::TestRemoveStreamRampNewTrackResets()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    iSkipper->RemoveCurrentStream(true);
    iRamping = true;
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
    iRamping = false;
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
}

void SuiteSkipper::TestRemoveStreamRampNewStreamResets()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    iSkipper->RemoveCurrentStream(true);
    iRamping = true;
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    iPendingMsgs.push_back(CreateEncodedStream());
    PullNext(EMsgEncodedStream);
    iRamping = false;
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
}

void SuiteSkipper::TestRemoveStreamNoRampFewMsgsPass()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());

    for (TUint i=0; i<3; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgDecodedStream);

    iSkipper->RemoveCurrentStream(false);
    // don't expect a Halt - not ramping implies that the pipeline is already halted (or buffering)
    TEST(iSkipper->iQueue.IsEmpty());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(Jiffies::kJiffiesPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    PullNext(EMsgHalt);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId)); // should be consumed by Skipper
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId+1));
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
}

void SuiteSkipper::TestTryRemoveInvalidTrack()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    for (TUint i=0; i<3; i++) {
        PullNext();
    }

    TEST(!iSkipper->TryRemoveStream(iTrackId+1, iStreamId, true));
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
}

void SuiteSkipper::TestTryRemoveInvalidStream()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    for (TUint i=0; i<3; i++) {
        PullNext();
    }

    TEST(!iSkipper->TryRemoveStream(iTrackId, iStreamId+1, true));
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
}

void SuiteSkipper::TestTryRemoveRampValidTrackAndStream()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());

    for (TUint i=0; i<3; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgDecodedStream);

    TEST(iSkipper->TryRemoveStream(iTrackId, iStreamId, true));
    iRamping = true;
    
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    PullNext(EMsgMetaText);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(Jiffies::kJiffiesPerMs * 3));
    PullNext(EMsgSilence);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    PullNext(EMsgHalt);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(2));
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);

    while (iRamping) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    PullNext(EMsgHalt);
    while (!iSkipper->iQueue.IsEmpty()) {
        PullNext(EMsgAudioPcm);
    }
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(Jiffies::kJiffiesPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    PullNext(EMsgHalt);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId)); // should be consumed by Skipper
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId+1));
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
}

void SuiteSkipper::TestTryRemoveNoRampValidTrackAndStream()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());

    for (TUint i=0; i<3; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgDecodedStream);

    TEST(iSkipper->TryRemoveStream(iTrackId, iStreamId, false));
    iRamping = false;
    
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(Jiffies::kJiffiesPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    PullNext(EMsgHalt);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId)); // should be consumed by Skipper
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId+1));
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
}



void TestSkipper()
{
    Runner runner("Skipper tests\n");
    runner.Add(new SuiteSkipper());
    runner.Run();
}
