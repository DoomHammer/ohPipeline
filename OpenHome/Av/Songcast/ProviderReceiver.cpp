#include <OpenHome/Av/Songcast/ProviderReceiver.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>
#include <Generated/DvAvOpenhomeOrgReceiver1.h>
#include <OpenHome/Net/Core/DvInvocationResponse.h>
#include <OpenHome/Private/Uri.h>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Av;

static const TUint kBadSenderUri = 800;
static const Brn kBadSenderUriMsg("Invalid sender uri");

ProviderReceiver::ProviderReceiver(Net::DvDevice& aDevice, ISourceReceiver& aSource, const TChar* aProtocolInfo)
    : DvProviderAvOpenhomeOrgReceiver1(aDevice)
    , iLock("PRCV")
    , iSource(aSource)
    , iProtocolInfo(aProtocolInfo)
{
    EnablePropertyUri();
    EnablePropertyMetadata();
    EnablePropertyTransportState();
    EnablePropertyProtocolInfo();

    EnableActionPlay();
    EnableActionStop();
    EnableActionSetSender();
    EnableActionSender();
    EnableActionProtocolInfo();
    EnableActionTransportState();

    SetPropertyUri(Brx::Empty());
    SetPropertyMetadata(Brx::Empty());
    NotifyPipelineState(Media::EPipelineStopped);
    SetPropertyProtocolInfo(iProtocolInfo);
}

void ProviderReceiver::NotifyPipelineState(Media::EPipelineState aState)
{
    const TChar* state = NULL;
    switch (aState)
    {
    case Media::EPipelinePlaying:
        state = "Playing";
        break;
    case Media::EPipelinePaused:
        state = "Waiting";
        break;
    case Media::EPipelineStopped:
        state = "Stopped";
        break;
    case Media::EPipelineBuffering:
        state = "Buffering";
        break;
    default:
        ASSERTS();
    }
    iTransportState.Set(state);
    SetPropertyTransportState(iTransportState);
}

void ProviderReceiver::Play(IDvInvocation& aInvocation)
{
    iSource.Play();
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderReceiver::Stop(IDvInvocation& aInvocation)
{
    iSource.Stop();
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderReceiver::SetSender(IDvInvocation& aInvocation, const Brx& aUri, const Brx& aMetadata)
{
    try {
        iSource.SetSender(aUri, aMetadata);
    }
    catch (UriError&) {
        aInvocation.Error(kBadSenderUri, kBadSenderUriMsg);
    }
    iLock.Wait();
    iSenderUri.Replace(aUri);
    iSenderMetadata.Replace(aMetadata);
    SetPropertyUri(aUri);
    SetPropertyMetadata(aMetadata);
    iLock.Signal();
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderReceiver::Sender(IDvInvocation& aInvocation, IDvInvocationResponseString& aUri, IDvInvocationResponseString& aMetadata)
{
    aInvocation.StartResponse();
    {
        AutoMutex a(iLock);
        aUri.Write(iSenderUri);
        aUri.WriteFlush();
        aMetadata.Write(iSenderMetadata);
    }
    aMetadata.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderReceiver::ProtocolInfo(IDvInvocation& aInvocation, IDvInvocationResponseString& aValue)
{
    aInvocation.StartResponse();
    aValue.Write(iProtocolInfo);
    aValue.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderReceiver::TransportState(IDvInvocation& aInvocation, IDvInvocationResponseString& aValue)
{
    aInvocation.StartResponse();
    {
        AutoMutex a(iLock);
        aValue.Write(iTransportState);
    }
    aValue.WriteFlush();
    aInvocation.EndResponse();
}
