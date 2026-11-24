#include "mainidyll.h"

#include <QApplication>
#include <QIcon>

#ifdef IDYLLWILD_MAC_UPDATES
void setupMacUpdates(QMainWindow &window);
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Idyllwild"));
    // Keep the stable application name for QSettings and other stored data.
    QApplication::setApplicationName(QStringLiteral("IdyllwildApp"));
    QApplication::setApplicationDisplayName(QStringLiteral("Idyllwild A.R.I.F"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/app-icon.png")));
    MainIdyll w;
#ifdef IDYLLWILD_MAC_UPDATES
    setupMacUpdates(w);
#endif
    w.show();
    return a.exec();
}
