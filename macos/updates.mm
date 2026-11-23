#import <Sparkle/Sparkle.h>

#include <QAction>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace {
QString updateText(const char *text)
{
    return QCoreApplication::translate("MacUpdates", text);
}
}

@interface IdyllwildUpdateFeed : NSObject <SPUUpdaterDelegate> {
@public
    QPointer<QAction> statusAction;
}
@property(copy) NSString *url;
@end

@implementation IdyllwildUpdateFeed

- (NSString *)feedURLStringForUpdater:(SPUUpdater *)updater
{
    (void)updater;
    return self.url;
}

- (void)updater:(SPUUpdater *)updater didFindValidUpdate:(SUAppcastItem *)item
{
    if (statusAction)
        statusAction->setText(updateText("Update available"));
}

- (void)updater:(SPUUpdater *)updater willDownloadUpdate:(SUAppcastItem *)item
    withRequest:(NSMutableURLRequest *)request
{
    if (statusAction)
        statusAction->setText(updateText("Downloading update…"));
}

- (void)updater:(SPUUpdater *)updater didDownloadUpdate:(SUAppcastItem *)item
{
    if (statusAction)
        statusAction->setText(updateText("Update downloaded. Preparing to install…"));
}

- (BOOL)updater:(SPUUpdater *)updater willInstallUpdateOnQuit:(SUAppcastItem *)item
    immediateInstallationBlock:(void (^)(void))immediateInstall
{
    if (statusAction)
        statusAction->setText(updateText("Update ready. Quit to install."));
    return NO;
}

- (void)updater:(SPUUpdater *)updater didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)check
          error:(NSError *)error
{
    if (!statusAction || !error)
        return;
    if (error.code == SUNoUpdateError)
        statusAction->setText(updateText("No new update found"));
    else if (error.code == SUInstallationCanceledError)
        statusAction->setText(updateText("Update installation canceled"));
    else
        statusAction->setText(updateText("Could not complete the update"));
}

@end

namespace {
void checkForMacUpdates(QMainWindow &window, QAction *status, QAction *check, bool manual)
{
    static IdyllwildUpdateFeed *feed = [[IdyllwildUpdateFeed alloc] init];
    static SPUStandardUpdaterController *controller = nil;
    if (controller && !controller.updater.canCheckForUpdates)
        return;

    if (!controller)
        status->setText(updateText("Checking for updates…"));
    check->setEnabled(false);
    auto *network = new QNetworkAccessManager(&window);
    const QString repository = QStringLiteral(IDYLLWILD_UPDATE_REPOSITORY);
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=100")
                                    .arg(repository)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "IdyllwildApp");
    request.setTransferTimeout(10000);
    auto *reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::finished, &window,
                     [reply, network, repository, status, check, manual] {
        network->deleteLater();
        check->setEnabled(true);
        const auto response = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError || !response.isArray()) {
            status->setText(updateText("Could not check for updates"));
            return;
        }

        const auto releases = response.array();
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
        if (feedUrl.isEmpty()) {
            status->setText(updateText("No new update found"));
            return;
        }

        feed->statusAction = status;
        feed.url = [NSString stringWithUTF8String:feedUrl.toUtf8().constData()];
        if (!controller) {
            controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
                                                                      updaterDelegate:feed
                                                                   userDriverDelegate:nil];
            [controller startUpdater];
        }
        if (manual)
            [controller checkForUpdates:nil];
        else if (controller.updater.automaticallyChecksForUpdates)
            [controller.updater checkForUpdatesInBackground];
    });
}
}

void setupMacUpdates(QMainWindow &window)
{
    auto *menu = window.menuBar()->addMenu(updateText("Updates"));
    menu->addAction(updateText("Installed build #%1").arg(IDYLLWILD_UPDATE_BUILD))->setEnabled(false);
    auto *status = menu->addAction(updateText("Checking for updates…"));
    status->setEnabled(false);
    menu->addSeparator();
    auto *check = menu->addAction(updateText("Check for Updates…"));
    QObject::connect(check, &QAction::triggered, &window, [&window, status, check] {
        checkForMacUpdates(window, status, check, true);
    });
    QTimer::singleShot(0, &window, [&window, status, check] {
        if (check->isEnabled())
            checkForMacUpdates(window, status, check, false);
    });
}
