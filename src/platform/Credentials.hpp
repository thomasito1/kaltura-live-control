/*
 * OS-keychain credential storage (spec section 4).
 *
 * An admin secret is a full-account credential. In a browser app it dies with the tab;
 * in a desktop plugin we are asked to persist it, so it goes in the OS keychain -
 * never a plaintext config file, and never the OBS scene collection JSON (users share
 * those).
 *
 * Backends: Windows Credential Manager (CredWriteW), macOS Keychain (SecItemAdd).
 */
#pragma once

#include <QString>

namespace creds {

/* Service name the entries are filed under in the OS credential store. */
extern const char *kServiceName;

/* `account` identifies which credential, e.g. "adminSecret:<partnerId>". */
bool store(const QString &account, const QString &secret);
bool retrieve(const QString &account, QString *out);
bool remove(const QString &account);

/* False on platforms with no real backend, so the UI can hide "remember me". */
bool isAvailable();

} // namespace creds
