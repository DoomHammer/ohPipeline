#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Media/Codec/Id3v2.h>

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

// Container

Container::Container(IPipelineElementUpstream& aUpstreamElement)
    : iUpstreamElement(aUpstreamElement)
    , iCheckForContainer(false)
    , iAudioEncoded(NULL)
{
}

Msg* Container::Pull()
{
    Msg* msg;
    do {
        msg = iUpstreamElement.Pull();
        msg = msg->Process(*this);
    } while (msg == NULL);
    return msg;
}

void Container::Read(Bwx& aBuf, TUint aOffset, TUint aBytes)
{
    // we don't expect to find (and wouldn't cope with) chained msgs...
    ASSERT(iAudioEncoded->Bytes() <= sizeof(iReadBuf));

    iAudioEncoded->CopyTo(iReadBuf);
    Brn buf(&iReadBuf[aOffset], aBytes);
    aBuf.Append(buf);
}

Msg* Container::ProcessMsg(MsgAudioEncoded* aMsg)
{
    MsgAudioEncoded* msg = aMsg;
    if (iCheckForContainer) {
        iAudioEncoded = msg;

        try {
            //Attempt to construct an id3 tag -- this will throw if not present
            Id3v2 id3(*this);
            LOG(kMedia, "Selector::DoRecognise found id3 tag of %d bytes -- skipping\n", id3.ContainerSize());
            msg = iAudioEncoded->Split(id3.ContainerSize());
            iAudioEncoded->RemoveRef();
            iAudioEncoded = msg;
        }
        catch(MediaCodecId3v2NotFound) { //thrown from Id3v2 constructor
        }

        iCheckForContainer = false;
        iAudioEncoded = NULL;
    }

    return msg;
}

Msg* Container::ProcessMsg(MsgAudioPcm* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* Container::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* Container::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* Container::ProcessMsg(MsgAudioFormat* /*aMsg*/)
{
    ASSERTS(); // expect this Msg to be generated by a downstream decoder element
    return NULL;
}

Msg* Container::ProcessMsg(MsgTrack* aMsg)
{
    iCheckForContainer = true;
    return aMsg;
}

Msg* Container::ProcessMsg(MsgAudioStream* aMsg)
{
    iCheckForContainer = true;
    return aMsg;
}

Msg* Container::ProcessMsg(MsgMetaText* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* Container::ProcessMsg(MsgHalt* aMsg)
{
    return aMsg;
}

Msg* Container::ProcessMsg(MsgFlush* aMsg)
{
    return aMsg;
}

Msg* Container::ProcessMsg(MsgQuit* aMsg)
{
    return aMsg;
}