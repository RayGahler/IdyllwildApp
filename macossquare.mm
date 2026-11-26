#include "squarepayments.h"

#include <QJsonDocument>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace Square {
namespace {
NSDictionary *squareKeychainQuery()
{
    return @{
        (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: @"com.idyllwild.IdyllwildApp.square",
        (__bridge id)kSecAttrAccount: @"merchant"
    };
}
}

Credentials savedCredentials()
{
    NSMutableDictionary *query = [squareKeychainQuery() mutableCopy];
    query[(__bridge id)kSecReturnData] = @YES;
    CFTypeRef result = nullptr;
    if (SecItemCopyMatching((__bridge CFDictionaryRef)query, &result) != errSecSuccess)
        return {};
    NSData *data = CFBridgingRelease(result);
    const auto value = QJsonDocument::fromJson(
        QByteArray(static_cast<const char *>(data.bytes), data.length)).object();
    return {value.value("accessToken").toString(), value.value("locationId").toString()};
}

bool saveCredentials(const Credentials &credentials)
{
    const auto serialized = QJsonDocument(QJsonObject{
        {"accessToken", credentials.accessToken}, {"locationId", credentials.locationId}
    }).toJson(QJsonDocument::Compact);
    NSData *bytes = [NSData dataWithBytes:serialized.constData() length:serialized.size()];
    NSDictionary *attributes = @{(__bridge id)kSecValueData: bytes};
    auto *query = squareKeychainQuery();
    const auto status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)attributes);
    if (status == errSecSuccess)
        return true;
    if (status != errSecItemNotFound)
        return false;
    NSMutableDictionary *entry = [query mutableCopy];
    [entry addEntriesFromDictionary:attributes];
    return SecItemAdd((__bridge CFDictionaryRef)entry, nullptr) == errSecSuccess;
}

}
