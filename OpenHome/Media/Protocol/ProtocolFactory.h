#pragma once

#include <OpenHome/Types.h>

namespace OpenHome {
    class Environment;
    class Brx;
namespace Configuration {
    class IStoreReadOnly;
    class IConfigInitialiser;
}
namespace Av {
    class Credentials;
}
namespace Media {

class Protocol;

class ProtocolFactory
{
public:
    static Protocol* NewHls(Environment& aEnv, const Brx& aUserAgent);
    static Protocol* NewHttp(Environment& aEnv, const Brx& aUserAgent); // UA is optional so can be empty
    static Protocol* NewHttps(Environment& aEnv);
    static Protocol* NewFile(Environment& aEnv);
    static Protocol* NewTone(Environment& aEnv);
    static Protocol* NewRtsp(Environment& aEnv, const Brx& aGuid);
    static Protocol* NewTidal(Environment& aEnv, const Brx& aToken, Av::Credentials& aCredentialsManager, Configuration::IConfigInitialiser& aConfigInitialiser);
    static Protocol* NewQobuz(Environment& aEnv, const Brx& aAppId, const Brx& aAppSecret, Av::Credentials& aCredentialsManager, Configuration::IConfigInitialiser& aConfigInitialiser);
};

} // namespace Media
} // namespace OpenHome

