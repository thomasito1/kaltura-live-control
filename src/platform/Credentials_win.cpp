/* Windows Credential Manager backend. */
#include "Credentials.hpp"

#include <QByteArray>
#include <QVector>

#include <windows.h>
#include <wincred.h>

namespace creds {

const char *kServiceName = "KalturaLiveControl";

namespace {

/* CREDENTIALW wants a mutable LPWSTR, so build an owned buffer rather than
 * const_cast-ing QString's internal storage. */
QVector<wchar_t> targetFor(const QString &account)
{
	const QString target = QString::fromLatin1(kServiceName) + QLatin1Char(':') + account;
	QVector<wchar_t> buf(target.size() + 1, L'\0');
	target.toWCharArray(buf.data());
	return buf;
}

} // namespace

bool isAvailable()
{
	return true;
}

bool store(const QString &account, const QString &secret)
{
	QVector<wchar_t> target = targetFor(account);
	const QByteArray blob = secret.toUtf8();

	CREDENTIALW cred = {};
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = target.data();
	cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
	cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
	/* LOCAL_MACHINE rather than ENTERPRISE: never roam a full-account secret. */
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

	return CredWriteW(&cred, 0) != FALSE;
}

bool retrieve(const QString &account, QString *out)
{
	if (!out)
		return false;

	QVector<wchar_t> target = targetFor(account);
	PCREDENTIALW cred = nullptr;

	if (!CredReadW(target.data(), CRED_TYPE_GENERIC, 0, &cred))
		return false;

	*out = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob),
				 static_cast<int>(cred->CredentialBlobSize));
	CredFree(cred);
	return true;
}

bool remove(const QString &account)
{
	QVector<wchar_t> target = targetFor(account);
	return CredDeleteW(target.data(), CRED_TYPE_GENERIC, 0) != FALSE;
}

} // namespace creds
