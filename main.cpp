#include "mainidyll.h"

#include <QApplication>
#include <QIcon>
#include <QStyleHints>

#ifdef IDYLLWILD_MAC_UPDATES
void setupMacUpdates(QMainWindow &window);
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    QApplication::setOrganizationName(QStringLiteral("Idyllwild"));
    // Keep the stable application name for QSettings and other stored data.
    QApplication::setApplicationName(QStringLiteral("IdyllwildApp"));
    QApplication::setApplicationDisplayName(QStringLiteral("Business Management & Inquiry App"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/app-icon.png")));
    MainIdyll w;
#ifdef IDYLLWILD_MAC_UPDATES
    setupMacUpdates(w);
#endif
    w.show();
    return a.exec();
}
