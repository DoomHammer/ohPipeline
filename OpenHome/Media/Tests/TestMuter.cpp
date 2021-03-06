#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Pipeline/Muter.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/InfoProvider.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>

#include <list>
#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuiteMuter : public SuiteUnitTest, private IPipelineElementUpstream, private IMsgProcessor
{
    static const TUint kRampDuration = Jiffies::kPerMs * 50; // shorter than production code but this is assumed to not matter
    static const TUint kExpectedFlushId = 5;
    static const TUint kSampleRate = 44100;
    static const TUint kNumChannels = 2;
public:
    SuiteMuter();
    ~SuiteMuter();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
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
       ,EMsgMode
       ,EMsgTrack
       ,EMsgDrain
       ,EMsgDelay
       ,EMsgEncodedStream
       ,EMsgMetaText
       ,EMsgStreamInterrupted
       ,EMsgDecodedStream
       ,EMsgAudioPcm
       ,EMsgSilence
       ,EMsgHalt
       ,EMsgFlush
       ,EMsgWait
       ,EMsgQuit
    };
private:
    void PullNext();
    void PullNext(EMsgType aExpectedMsg);
    Msg* CreateTrack();
    Msg* CreateDecodedStream();
    Msg* CreateAudio();
private:
    void TestMsgsPassWhenRunning();
    void TestMuteImmediateWhenHalted();
    void TestMuteRampsDownWhenNotHalted();
    void TestUnmuteImmediateWhenHalted();
    void TestUnmuteRampsUpWhenNotHalted();
    void TestUnmuteWhileRampingDown();
    void TestMuteWhileRampingUp();
private:
    AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    MsgFactory* iMsgFactory;
    Muter* iMuter;
    EMsgType iLastPulledMsg;
    TBool iRampingUp;
    TBool iRampingDown;
    TUint iStreamId;
    TUint64 iTrackOffset;
    TUint64 iJiffies;
    std::list<Msg*> iPendingMsgs;
    TUint iLastSubsample;
    TUint iNextStreamId;
    TUint iLastHaltId;
};

} // namespace Media
} // namespace OpenHome


SuiteMuter::SuiteMuter()
    : SuiteUnitTest("Muter")
{
    AddTest(MakeFunctor(*this, &SuiteMuter::TestMsgsPassWhenRunning), "TestMsgsPassWhenRunning");
    AddTest(MakeFunctor(*this, &SuiteMuter::TestMuteImmediateWhenHalted), "TestMuteImmediateWhenHalted");
    AddTest(MakeFunctor(*this, &SuiteMuter::TestMuteRampsDownWhenNotHalted), "TestMuteRampsDownWhenNotHalted");
    AddTest(MakeFunctor(*this, &SuiteMuter::TestUnmuteImmediateWhenHalted), "TestUnmuteImmediateWhenHalted");
    AddTest(MakeFunctor(*this, &SuiteMuter::TestUnmuteRampsUpWhenNotHalted), "TestUnmuteRampsUpWhenNotHalted");
    AddTest(MakeFunctor(*this, &SuiteMuter::TestUnmuteWhileRampingDown), "TestUnmuteWhileRampingDown");
    AddTest(MakeFunctor(*this, &SuiteMuter::TestMuteWhileRampingUp), "TestMuteWhileRampingUp");
}

SuiteMuter::~SuiteMuter()
{
}

void SuiteMuter::Setup()
{
    iTrackFactory = new TrackFactory(iInfoAggregator, 5);
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(52, 50);
    init.SetMsgSilenceCount(10);
    init.SetMsgDecodedStreamCount(3);
    init.SetMsgTrackCount(3);
    init.SetMsgEncodedStreamCount(3);
    init.SetMsgMetaTextCount(3);
    init.SetMsgHaltCount(2);
    init.SetMsgFlushCount(2);
    init.SetMsgModeCount(2);
    init.SetMsgWaitCount(2);
    init.SetMsgDelayCount(2);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iMuter = new Muter(*iMsgFactory, *this, kRampDuration);
    iStreamId = UINT_MAX;
    iTrackOffset = 0;
    iJiffies = 0;
    iRampingUp = iRampingDown = false;
    iLastSubsample = 0xffffff;
    iNextStreamId = 1;
    iLastHaltId = MsgHalt::kIdInvalid;
}

void SuiteMuter::TearDown()
{
    while (iPendingMsgs.size() > 0) {
        iPendingMsgs.front()->RemoveRef();
        iPendingMsgs.pop_front();
    }
    delete iMuter;
    delete iMsgFactory;
    delete iTrackFactory;
}

Msg* SuiteMuter::Pull()
{
    ASSERT(iPendingMsgs.size() > 0);
    Msg* msg = iPendingMsgs.front();
    iPendingMsgs.pop_front();
    return msg;
}

Msg* SuiteMuter::ProcessMsg(MsgMode* aMsg)
{
    iLastPulledMsg = EMsgMode;
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgTrack* aMsg)
{
    iLastPulledMsg = EMsgTrack;
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgDrain* aMsg)
{
    iLastPulledMsg = EMsgDrain;
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgDelay* aMsg)
{
    iLastPulledMsg = EMsgDelay;
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastPulledMsg = EMsgEncodedStream;
    iStreamId = aMsg->StreamId();
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteMuter::ProcessMsg(MsgMetaText* aMsg)
{
    iLastPulledMsg = EMsgMetaText;
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iLastPulledMsg = EMsgStreamInterrupted;
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgHalt* aMsg)
{
    iLastPulledMsg = EMsgHalt;
    iLastHaltId = aMsg->Id();
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgFlush* aMsg)
{
    iLastPulledMsg = EMsgFlush;
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgWait* aMsg)
{
    iLastPulledMsg = EMsgWait;
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastPulledMsg = EMsgDecodedStream;
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgBitRate* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteMuter::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastPulledMsg = EMsgAudioPcm;
    iJiffies += aMsg->Jiffies();
    MsgPlayable* playable = aMsg->CreatePlayable();
    ProcessorPcmBufTest pcmProcessor;
    playable->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());
    ASSERT(buf.Bytes() >= 6);
    const TByte* ptr = buf.Ptr();
    const TUint bytes = buf.Bytes();
    const TUint firstSubsample = (ptr[0]<<16) | (ptr[1]<<8) | ptr[2];
 
    if (iRampingDown) {
        ASSERT(!iRampingUp);
    }
    
    if (iRampingDown) {
        TEST(firstSubsample <= iLastSubsample);
    }
    else if (iRampingUp) {
        TEST(firstSubsample >= iLastSubsample);
    }
    else {
        TEST(firstSubsample == 0x7f7f7f);
    }
    iLastSubsample = (ptr[bytes-3]<<16) | (ptr[bytes-2]<<8) | ptr[bytes-1];
    if (iRampingDown) {
        TEST(iLastSubsample < firstSubsample);
        iRampingDown = (iLastSubsample > 0);
    }
    else if (iRampingUp) {
        TEST(iLastSubsample > firstSubsample);
        iRampingUp = (iLastSubsample < 0x7f7f7e); // FIXME - see #830
    }
    else {
        TEST(firstSubsample == 0x7f7f7f);
    }

    return playable;
}

Msg* SuiteMuter::ProcessMsg(MsgSilence* aMsg)
{
    iLastPulledMsg = EMsgSilence;
    return aMsg;
}

Msg* SuiteMuter::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteMuter::ProcessMsg(MsgQuit* aMsg)
{
    iLastPulledMsg = EMsgQuit;
    return aMsg;
}

void SuiteMuter::PullNext()
{
    Msg* msg = iMuter->Pull();
    msg = msg->Process(*this);
    msg->RemoveRef();
}

void SuiteMuter::PullNext(EMsgType aExpectedMsg)
{
    Msg* msg = iMuter->Pull();
    msg = msg->Process(*this);
    msg->RemoveRef();
    TEST(iLastPulledMsg == aExpectedMsg);
}

Msg* SuiteMuter::CreateTrack()
{
    Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
    Msg* msg = iMsgFactory->CreateMsgTrack(*track);
    track->RemoveRef();
    return msg;
}

Msg* SuiteMuter::CreateDecodedStream()
{
    return iMsgFactory->CreateMsgDecodedStream(iNextStreamId, 100, 24, kSampleRate, kNumChannels, Brn("notARealCodec"), 1LL<<38, 0, true, true, false, nullptr);
}

Msg* SuiteMuter::CreateAudio()
{
    static const TUint kDataBytes = 3 * 1024;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0x7f, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, kNumChannels, kSampleRate, 24, EMediaDataEndianLittle, iTrackOffset);
    iTrackOffset += audio->Jiffies();
    return audio;
}

void SuiteMuter::TestMsgsPassWhenRunning()
{
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMode(Brx::Empty(), false, true, ModeClockPullers(), false, false));
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgDrain(Functor()));
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(Jiffies::kPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgStreamInterrupted());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());

    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDrain);
    PullNext(EMsgDecodedStream);
    PullNext(EMsgAudioPcm);
    PullNext(EMsgSilence);
    PullNext(EMsgHalt);
    PullNext(EMsgStreamInterrupted);
    PullNext(EMsgQuit);
}

void SuiteMuter::TestMuteImmediateWhenHalted()
{
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    PullNext(EMsgHalt);
    iMuter->Mute();
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgSilence);
}

void SuiteMuter::TestMuteRampsDownWhenNotHalted()
{
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    iMuter->Mute();
    iRampingDown = true;
    while (iRampingDown) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgSilence);
}

void SuiteMuter::TestUnmuteImmediateWhenHalted()
{
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    PullNext(EMsgHalt);
    iMuter->Mute();
    TEST(iMuter->iState == Muter::eMuted);
    iMuter->Unmute();
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    TEST(iMuter->iState == Muter::eRunning);
}

void SuiteMuter::TestUnmuteRampsUpWhenNotHalted()
{
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    PullNext(EMsgHalt);
    iMuter->Mute();
    TEST(iMuter->iState == Muter::eMuted);
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgSilence);
    iMuter->Unmute();
    iRampingUp = true;
    iLastSubsample = 0;
    while (iRampingUp) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
}

void SuiteMuter::TestUnmuteWhileRampingDown()
{
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    iRampingDown = true;
    iMuter->Mute();
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    iMuter->Unmute();
    iRampingDown = false;
    iRampingUp = true;
    TEST(iMuter->iState != Muter::eRunning);
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    TEST(iMuter->iState == Muter::eRunning);
    TEST(!iRampingUp);
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
}

void SuiteMuter::TestMuteWhileRampingUp()
{
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    PullNext(EMsgHalt);
    iMuter->Mute();
    TEST(iMuter->iState == Muter::eMuted);
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgSilence);
    iMuter->Unmute();
    iRampingUp = true;
    iLastSubsample = 0;
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);

    iMuter->Mute();
    TEST(iMuter->iState != Muter::eMuted);
    iRampingDown = true;
    iRampingUp = false;
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    TEST(iMuter->iState == Muter::eMuted);
    TEST(!iRampingDown);
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgSilence);
}



void TestMuter()
{
    Runner runner("Muter tests\n");
    runner.Add(new SuiteMuter());
    runner.Run();
}
