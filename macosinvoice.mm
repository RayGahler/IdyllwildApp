#import <Foundation/Foundation.h>

#include "invoice.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QThread>
#include <QUuid>

ICloudInvoiceLink publishMacInvoice(const QString &code, const QString &number,
                                    const QByteArray &pdfBytes)
{
    @autoreleasepool {
        const auto translate = [](const char *text) { return QCoreApplication::translate("Invoice", text); };
        const auto failed = [](const QString &error) { return ICloudInvoiceLink{{}, {}, error}; };
        NSFileManager *manager = NSFileManager.defaultManager;
        const QDir drive(QDir::home().filePath(QStringLiteral("Library/Mobile Documents/com~apple~CloudDocs")));
        if (!manager.ubiquityIdentityToken || !drive.exists())
            return failed(translate("Turn on iCloud Drive in System Settings and try again."));

        const QString directory = drive.filePath(QStringLiteral("Idyllwild Invoices/%1/%2")
            .arg(code, QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!QDir().mkpath(directory))
            return failed(translate("Could not create the invoice folder in iCloud Drive."));

        const QString path = QDir(directory).filePath(number + QStringLiteral(".pdf"));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(pdfBytes) != pdfBytes.size() || !file.commit())
            return failed(translate("Could not save the PDF to iCloud Drive. %1").arg(file.errorString()));

        NSURL *fileUrl = [NSURL fileURLWithPath:path.toNSString()];
        QElapsedTimer uploadTime;
        uploadTime.start();
        while (uploadTime.elapsed() < 90000) {
            @autoreleasepool {
                NSNumber *uploaded = nil;
                [fileUrl getResourceValue:&uploaded forKey:NSURLUbiquitousItemIsUploadedKey error:nil];
                if (uploaded.boolValue) {
                    NSError *error = nil;
                    NSDate *expiration = nil;
                    NSURL *link = [manager URLForPublishingUbiquitousItemAtURL:fileUrl
                                                               expirationDate:&expiration error:&error];
                    if (!link || !expiration) {
                        return failed(translate("iCloud could not create the download link. %1")
                            .arg(QString::fromNSString(error.localizedDescription)));
                    }
                    const auto expiresAt = QDateTime::fromMSecsSinceEpoch(
                        qRound64(expiration.timeIntervalSince1970 * 1000)).toUTC();
                    return {QUrl(QString::fromNSString(link.absoluteString)), expiresAt, {}};
                }
                NSError *uploadError = nil;
                [fileUrl getResourceValue:&uploadError forKey:NSURLUbiquitousItemUploadingErrorKey error:nil];
                if (uploadError)
                    return failed(QString::fromNSString(uploadError.localizedDescription));
                [fileUrl removeAllCachedResourceValues];
            }
            QThread::sleep(1);
        }
        return failed(translate("iCloud Drive is still syncing the PDF. Try publishing it again once sync finishes."));
    }
}
