/*
 * No-op credential backend for platforms without a shipping keychain integration
 * (Linux is not a target for this plugin). Deliberately stores nothing: failing to
 * remember a secret is fine, writing one to disk in the clear is not.
 */
#include "Credentials.hpp"

namespace creds {

const char *kServiceName = "KalturaLiveControl";

bool isAvailable()
{
	return false;
}

bool store(const QString &, const QString &)
{
	return false;
}

bool retrieve(const QString &, QString *)
{
	return false;
}

bool remove(const QString &)
{
	return true;
}

} // namespace creds
