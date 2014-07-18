#include <OpenHome/Media/Pipeline/AudioReservoir.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// AudioReservoir

AudioReservoir::AudioReservoir(TUint aMaxSize)
    : iMaxSize(aMaxSize)
    , iLock("ARES")
    , iSem("ARES", 0)
{
}

AudioReservoir::~AudioReservoir()
{
}

Msg* AudioReservoir::Pull()
{
    Msg* msg;
    do {
        msg = DoDequeue();
        iLock.Wait();
        if (Size() < iMaxSize) {
            iSem.Signal();
        }
        iLock.Signal();
    } while (msg == NULL);
    return msg;
}

void AudioReservoir::Push(Msg* aMsg)
{
    DoEnqueue(aMsg);
}

void AudioReservoir::BlockIfFull()
{
    iLock.Wait();
    const TBool full = (Size() >= iMaxSize);
    (void)iSem.Clear();
    iLock.Signal();
    if (full) {
        iSem.Wait();
    }
}
