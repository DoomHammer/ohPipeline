#include "OhmSocket.h"

using namespace OpenHome;
using namespace OpenHome::Av;

// OhmSocket

// Sends on same socket in Unicast mode, but different socket in Multicast mode

OhmSocket::OhmSocket(Environment& aEnv)
    : iEnv(aEnv)
    , iRxSocket(0)
    , iTxSocket(0)
    , iReader(0)
{
}

OhmSocket::~OhmSocket()
{
    if (iRxSocket != 0) {
        Close();
    }
}

void OhmSocket::OpenUnicast(TIpAddress aInterface, TUint aTtl)
{
    ASSERT(!iRxSocket);
    ASSERT(!iTxSocket);
    ASSERT(!iReader);
    iRxSocket = new SocketUdp(iEnv, 0, aInterface);
    iRxSocket->SetTtl(aTtl);
    iRxSocket->SetRecvBufBytes(kReceiveBufBytes);
    iRxSocket->SetSendBufBytes(kSendBufBytes);
    iReader = new UdpReader(*iRxSocket);
    iThis.Replace(Endpoint(iRxSocket->Port(), aInterface));
}

void OhmSocket::OpenMulticast(TIpAddress aInterface, TUint aTtl, const Endpoint& aEndpoint)
{
    ASSERT(!iRxSocket);
    ASSERT(!iTxSocket);
    ASSERT(!iReader);
    iRxSocket = new SocketUdpMulticast(iEnv, aInterface, aEndpoint);
    iRxSocket->SetRecvBufBytes(kReceiveBufBytes);
    iTxSocket = new SocketUdp(iEnv, 0, aInterface);
    iTxSocket->SetTtl(aTtl);
    iTxSocket->SetSendBufBytes(kSendBufBytes);
    iReader = new UdpReader(*iRxSocket);
    iThis.Replace(aEndpoint);
}

void OhmSocket::Send(const Brx& aBuffer, const Endpoint& aEndpoint)
{
    if (iTxSocket != NULL) {
        iTxSocket->Send(aBuffer, aEndpoint);
    }
    else if (iRxSocket != NULL) {
        iRxSocket->Send(aBuffer, aEndpoint);
    }
}

Endpoint OhmSocket::This() const
{
    return iThis;
}

Endpoint OhmSocket::Sender() const
{
    ASSERT(iReader);
    return iReader->Sender();
}

void OhmSocket::Close()
{
    ASSERT(iReader != NULL);
    delete iReader;
    iReader = NULL;
    ASSERT(iRxSocket != NULL);
    delete iRxSocket;
    iRxSocket = NULL;
    if (iTxSocket != NULL) {
        delete iTxSocket;
        iTxSocket = NULL;
    }
}

void OhmSocket::Interrupt(TBool aInterrupt)
{
    // FIXME - thread-safety
    if (iRxSocket != NULL) {
        iRxSocket->Interrupt(aInterrupt);
    }
    if (iTxSocket != NULL) {
        iTxSocket->Interrupt(aInterrupt);
    }
}

void OhmSocket::Read(Bwx& aBuffer)
{
    ASSERT(iReader);
    iReader->Read(aBuffer);
}

void OhmSocket::ReadFlush()
{
    ASSERT(iReader);
    iReader->ReadFlush();
}

void OhmSocket::ReadInterrupt()
{
    ASSERT(iReader);
    iReader->ReadInterrupt();
}


// OhzSocket

OhzSocket::OhzSocket(Environment& aEnv)
    : iEnv(aEnv)
    , iRxSocket(0)
    , iTxSocket(0)
    , iEndpoint(51972, Brn("239.255.255.250"))
{
}

OhzSocket::~OhzSocket()
{
    if (iRxSocket != 0) {
        Close();
    }
}

const Endpoint& OhzSocket::This() const
{
    return iEndpoint;
}

void OhzSocket::Open(TIpAddress aInterface, TUint aTtl)
{
    ASSERT(!iRxSocket);
    iRxSocket = new SocketUdpMulticast(iEnv, aInterface, iEndpoint);
    iTxSocket = new SocketUdp(iEnv, 0, aInterface);
    iTxSocket->SetTtl(aTtl);
    iReader = new UdpReader(*iRxSocket);
}

void OhzSocket::Send(const Brx& aBuffer)
{
    ASSERT(iTxSocket);
    iTxSocket->Send(aBuffer, iEndpoint);
}

void OhzSocket::Close()
{
    ASSERT(iRxSocket);
    delete iRxSocket;
    delete iTxSocket;
    delete iReader;
    iRxSocket = 0;
    iTxSocket = 0;
    iReader = 0;
}
    
void OhzSocket::Read(Bwx& aBuffer)
{
    ASSERT(iRxSocket);
    iReader->Read(aBuffer);
}

void OhzSocket::ReadFlush()
{
    ASSERT(iRxSocket != 0);
    iReader->ReadFlush();
}

void OhzSocket::ReadInterrupt()
{
    ASSERT(iRxSocket != 0);
    iReader->ReadInterrupt();
}
