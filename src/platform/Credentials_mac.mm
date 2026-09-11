/* macOS Keychain backend (Security.framework). Objective-C++. */
#include "Credentials.hpp"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <QByteArray>

namespace creds {

const char *kServiceName = "KalturaLiveControl";

namespace {

NSString *serviceString()
{
	return [NSString stringWithUTF8String:kServiceName];
}

NSString *accountString(const QString &account)
{
	return [NSString stringWithUTF8String:account.toUtf8().constData()];
}

/* The lookup half of the query, shared by all three operations. */
NSMutableDictionary *baseQuery(const QString &account)
{
	NSMutableDictionary *q = [NSMutableDictionary dictionary];
	q[(__bridge id)kSecClass] = (__bridge id)kSecClassGenericPassword;
	q[(__bridge id)kSecAttrService] = serviceString();
	q[(__bridge id)kSecAttrAccount] = accountString(account);
	return q;
}

} // namespace

bool isAvailable()
{
	return true;
}

bool store(const QString &account, const QString &secret)
{
	/* SecItemAdd fails with errSecDuplicateItem rather than overwriting, so clear
	 * any existing entry first. */
	remove(account);

	const QByteArray utf8 = secret.toUtf8();
	NSData *data = [NSData dataWithBytes:utf8.constData() length:(NSUInteger)utf8.size()];

	NSMutableDictionary *q = baseQuery(account);
	q[(__bridge id)kSecValueData] = data;
	/* Readable only while the machine is unlocked, and never backed up off-device. */
	q[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly;

	OSStatus status = SecItemAdd((__bridge CFDictionaryRef)q, NULL);
	return status == errSecSuccess;
}

bool retrieve(const QString &account, QString *out)
{
	if (!out)
		return false;

	NSMutableDictionary *q = baseQuery(account);
	q[(__bridge id)kSecReturnData] = @YES;
	q[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;

	CFTypeRef result = NULL;
	OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)q, &result);
	if (status != errSecSuccess || result == NULL)
		return false;

	NSData *data = (__bridge_transfer NSData *)result;
	*out = QString::fromUtf8(static_cast<const char *>([data bytes]),
				 static_cast<int>([data length]));
	return true;
}

bool remove(const QString &account)
{
	NSMutableDictionary *q = baseQuery(account);
	OSStatus status = SecItemDelete((__bridge CFDictionaryRef)q);
	return status == errSecSuccess || status == errSecItemNotFound;
}

} // namespace creds
