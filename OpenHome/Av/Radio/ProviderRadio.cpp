#include <OpenHome/Av/Radio/ProviderRadio.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <Generated/DvAvOpenhomeOrgRadio1.h>
#include <OpenHome/Av/Radio/PresetDatabase.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Arch.h>
#include <OpenHome/Net/Core/DvInvocationResponse.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Converter.h>

#include <array>

namespace OpenHome {
namespace Av {

class WriterInvocationResponseString : public IWriter, private INonCopyable
{
public:
    WriterInvocationResponseString(IDvInvocationResponseString& aIrs);
    ~WriterInvocationResponseString();
public: // from IWriter
    void Write(TByte aValue);
    void Write(const Brx& aBuffer);
    void WriteFlush();
private:
    IDvInvocationResponseString& iIrs;
};

} // namespace Av
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Av;

static const TUint kIdNotFoundCode = 800;
static const Brn kIdNotFoundMsg("Id not found");
static const TUint kInvalidRequestCode = 802;
static const Brn kInvalidRequestMsg("Comma separated id request list invalid");
static const TUint kInvalidChannelCode = 803;
static const Brn kInvalidChannelMsg("Selected channel is invalid");

ProviderRadio::ProviderRadio(Net::DvDevice& aDevice, ISourceRadio& aSource, IPresetDatabaseReader& aDbReader, const TChar* aProtocolInfo)
    : DvProviderAvOpenhomeOrgRadio1(aDevice)
    , iLock("PRAD")
    , iSource(aSource)
    , iDbReader(aDbReader)
    , iProtocolInfo(aProtocolInfo)
    , iDbSeq(0)
{
    iDbReader.SetObserver(*this);

    EnablePropertyUri();
    EnablePropertyMetadata();
    EnablePropertyTransportState();
    EnablePropertyId();
    EnablePropertyIdArray();
    EnablePropertyChannelsMax();
    EnablePropertyProtocolInfo();

    EnableActionPlay();
    EnableActionPause();
    EnableActionStop();
    EnableActionSeekSecondAbsolute();
    EnableActionSeekSecondRelative();
    EnableActionChannel();
    EnableActionSetChannel();
    EnableActionTransportState();
    EnableActionId();
    EnableActionSetId();
    EnableActionRead();
    EnableActionReadList();
    EnableActionIdArray();
    EnableActionIdArrayChanged();
    EnableActionChannelsMax();
    EnableActionProtocolInfo();

    (void)SetPropertyUri(Brx::Empty());
    (void)SetPropertyMetadata(Brx::Empty());
    SetTransportState(Media::EPipelineStopped);
    (void)SetPropertyId(IPresetDatabaseReader::kPresetIdNone);
    UpdateIdArrayProperty();
    (void)SetPropertyChannelsMax(IPresetDatabaseReader::kMaxPresets);
    (void)SetPropertyProtocolInfo(iProtocolInfo);
}

ProviderRadio::~ProviderRadio()
{
}

void ProviderRadio::SetTransportState(Media::EPipelineState aState)
{
    Brn state(Media::TransportState::FromPipelineState(aState));
    iLock.Wait();
    (void)SetPropertyTransportState(state);
    iLock.Signal();
}

void ProviderRadio::PresetDatabaseChanged()
{
    UpdateIdArrayProperty();
}

void ProviderRadio::Play(IDvInvocation& aInvocation)
{
    iSource.Play();
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderRadio::Pause(IDvInvocation& aInvocation)
{
    iSource.Pause();
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderRadio::Stop(IDvInvocation& aInvocation)
{
    iSource.Stop();
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderRadio::SeekSecondAbsolute(IDvInvocation& aInvocation, TUint aValue)
{
    iSource.SeekAbsolute(aValue);
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderRadio::SeekSecondRelative(IDvInvocation& aInvocation, TInt aValue)
{
    iSource.SeekRelative(aValue);
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderRadio::Channel(IDvInvocation& aInvocation, IDvInvocationResponseString& aUri, IDvInvocationResponseString& aMetadata)
{
    aInvocation.StartResponse();
    {
        AutoMutex a(iLock);
        aUri.Write(iUri);
        aUri.WriteFlush();
        aMetadata.Write(iMetaData);
        aMetadata.WriteFlush();
    }
    aInvocation.EndResponse();
}

void ProviderRadio::SetChannel(IDvInvocation& aInvocation, const Brx& aUri, const Brx& aMetadata)
{
    SetChannel(IPresetDatabaseReader::kPresetIdNone, aUri, aMetadata);
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderRadio::TransportState(IDvInvocation& aInvocation, IDvInvocationResponseString& aValue)
{
    aInvocation.StartResponse();
    {
        AutoMutex a(iLock);
        Brhz state;
        GetPropertyTransportState(state);
        aValue.Write(state);
    }
    aValue.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderRadio::Id(IDvInvocation& aInvocation, IDvInvocationResponseUint& aValue)
{
    aInvocation.StartResponse();
    {
        AutoMutex a(iLock);
        TUint id;
        GetPropertyId(id);
        aValue.Write(id);
    }
    aInvocation.EndResponse();
}

void ProviderRadio::SetId(IDvInvocation& aInvocation, TUint aValue, const Brx& aUri)
{
    // FIXME - far too much data on the stack here
    Media::BwsTrackUri uri;
    Media::BwsTrackMetaData metadata;
    if (aValue == IPresetDatabaseReader::kPresetIdNone || !iDbReader.TryGetPresetById(aValue, uri, metadata)) {
        iUri.Replace(Brx::Empty());
        iMetaData.Replace(Brx::Empty());
        aInvocation.Error(kInvalidChannelCode, kInvalidChannelMsg);
    }
    SetChannel(aValue, aUri, metadata);
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderRadio::Read(IDvInvocation& aInvocation, TUint aId, IDvInvocationResponseString& aMetadata)
{
    Media::BwsTrackMetaData metadata; // FIXME - far too much data on the stack here
    if (aId == IPresetDatabaseReader::kPresetIdNone || !iDbReader.TryGetPresetById(aId, metadata)) {
        aInvocation.Error(kIdNotFoundCode, kIdNotFoundMsg);
    }
    aInvocation.StartResponse();
    aMetadata.Write(metadata);
    aMetadata.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderRadio::ReadList(IDvInvocation& aInvocation, const Brx& aIdList, IDvInvocationResponseString& aChannelList)
{
    iLock.Wait();
    const TUint seq = iDbSeq;
    iLock.Signal();
    Parser parser(aIdList);
    TUint index = 0;
    Media::BwsTrackMetaData metadata; // FIXME - heavy stack requirements
    const Brn entryStart("<Entry>");
    const Brn entryEnd("</Entry>");
    const Brn idStart("<Id>");
    const Brn idEnd("</Id>");
    const Brn metaStart("<Metadata>");
    const Brn metaEnd("</Metadata>");
    Brn idBuf;
    idBuf.Set(parser.Next(' '));

    aInvocation.StartResponse();
    aChannelList.Write(Brn("<ChannelList>"));
    do {
        try {
            TUint id = Ascii::Uint(idBuf);
            if (iDbReader.TryGetPresetById(id, seq, metadata, index)) {
                aChannelList.Write(entryStart);
                aChannelList.Write(idStart);
                aChannelList.Write(idBuf);
                aChannelList.Write(idEnd);
                aChannelList.Write(metaStart);
                WriterInvocationResponseString writer(aChannelList);
                Converter::ToXmlEscaped(writer, metadata);
                aChannelList.Write(metaEnd);
                aChannelList.Write(entryEnd);
            }
        }
        catch (AsciiError&) {
            // FIXME - volkano approach requires much more dynamic allocation but allows for better error reporting
        }
        idBuf.Set(parser.Next(' '));
    } while (idBuf != Brx::Empty());
    aChannelList.Write(Brn("</ChannelList>"));
    aChannelList.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderRadio::IdArray(IDvInvocation& aInvocation, IDvInvocationResponseUint& aToken, IDvInvocationResponseBinary& aArray)
{
    AutoMutex a(iLock);
    UpdateIdArray();
    aInvocation.StartResponse();
    aToken.Write(iDbSeq);
    Bws<4> idBuf;
    aArray.Write(iIdArrayBuf);
    aArray.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderRadio::IdArrayChanged(IDvInvocation& aInvocation, TUint aToken, IDvInvocationResponseBool& aValue)
{
    iLock.Wait();
    const bool changed = (aToken!=iDbSeq);
    iLock.Signal();
    aInvocation.StartResponse();
    aValue.Write(changed);
    aInvocation.EndResponse();
}

void ProviderRadio::ChannelsMax(IDvInvocation& aInvocation, IDvInvocationResponseUint& aValue)
{
    aInvocation.StartResponse();
    aValue.Write(IPresetDatabaseReader::kMaxPresets);
    aInvocation.EndResponse();
}

void ProviderRadio::ProtocolInfo(IDvInvocation& aInvocation, IDvInvocationResponseString& aValue)
{
    aInvocation.StartResponse();
    aValue.Write(iProtocolInfo);
    aValue.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderRadio::SetChannel(TUint aPresetId, const Brx& aUri, const Brx& aMetadata)
{
    iLock.Wait();
    iUri.Replace(aUri);
    iMetaData.Replace(aMetadata);
    iLock.Signal();
    (void)SetPropertyId(aPresetId);
    (void)SetPropertyMetadata(iMetaData);
    iSource.Fetch(aUri, aMetadata);
}

void ProviderRadio::UpdateIdArray()
{
    iDbReader.GetIdArray(iIdArray, iDbSeq);
    iIdArrayBuf.SetBytes(0);
    for (TUint i=0; i<IPresetDatabaseReader::kMaxPresets; i++) {
        TUint32 bigEndianId = Arch::BigEndian4(iIdArray[i]);
        Brn idBuf(reinterpret_cast<const TByte*>(&bigEndianId), sizeof(bigEndianId));
        iIdArrayBuf.Append(idBuf);
    }
}

void ProviderRadio::UpdateIdArrayProperty()
{
    AutoMutex a(iLock);
    UpdateIdArray();
    (void)SetPropertyIdArray(iIdArrayBuf);
}


// WriterInvocationResponseString

WriterInvocationResponseString::WriterInvocationResponseString(IDvInvocationResponseString& aIrs)
    : iIrs(aIrs)
{
}

WriterInvocationResponseString::~WriterInvocationResponseString()
{
}

void WriterInvocationResponseString::Write(TByte aValue)
{
    Brn buf(&aValue, sizeof(TByte));
    Write(buf);
}

void WriterInvocationResponseString::Write(const Brx& aBuffer)
{
    iIrs.Write(aBuffer);
}

void WriterInvocationResponseString::WriteFlush()
{
    iIrs.WriteFlush();
}
