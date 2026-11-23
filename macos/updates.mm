#import <Sparkle/Sparkle.h>

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

@interface IdyllwildUpdateFeed : NSObject <SPUUpdaterDelegate>
@property(copy) NSString *url;
@end

@implementation IdyllwildUpdateFeed

- (NSString *)feedURLStringForUpdater:(SPUUpdater *)updater
{
    (void)updater;
    return self.url;
}

@end

void checkForMacUpdates(QApplication &app)
{
    auto *network = new QNetworkAccessManager(&app);
    const QString repository = QStringLiteral(IDYLLWILD_UPDATE_REPOSITORY);
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=100")
                                    .arg(repository)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "IdyllwildApp");
    request.setTransferTimeout(10000);
    auto *reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::finished, &app, [reply, network, repository] {
        network->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;

        const auto releases = QJsonDocument::fromJson(reply->readAll()).array();
        const QRegularExpression tagPattern(QStringLiteral("^macos-build-[0-9]+$"));
        const QRegularExpression titlePattern(QStringLiteral("^macOS build #([0-9]+)$"));
        qlonglong newest = IDYLLWILD_UPDATE_BUILD;
        QString feedUrl;
        for (const auto &entry : releases) {
            const auto release = entry.toObject();
            const auto tag = release.value("tag_name").toString();
            const auto title = titlePattern.match(release.value("name").toString());
            if (release.value("draft").toBool() || !tagPattern.match(tag).hasMatch() || !title.hasMatch())
                continue;
            const auto build = title.captured(1).toLongLong();
            if (build <= newest)
                continue;

            const QString expectedUrl = QStringLiteral("https://github.com/%1/releases/download/%2/appcast.xml")
                                            .arg(repository, tag);
            for (const auto &assetEntry : release.value("assets").toArray()) {
                const auto asset = assetEntry.toObject();
                if (asset.value("browser_download_url").toString() == expectedUrl) {
                    newest = build;
                    feedUrl = expectedUrl;
                    break;
                }
            }
        }
        if (feedUrl.isEmpty())
            return;

        static IdyllwildUpdateFeed *feed = [[IdyllwildUpdateFeed alloc] init];
        feed.url = [NSString stringWithUTF8String:feedUrl.toUtf8().constData()];
        static SPUStandardUpdaterController *controller =
            [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
                                                        updaterDelegate:feed
                                                     userDriverDelegate:nil];
        [controller startUpdater];
        if (controller.updater.automaticallyChecksForUpdates)
            [controller.updater checkForUpdatesInBackground];
    });
}
