#include "mainidyll.h"

#include <QApplication>

#ifdef IDYLLWILD_MAC_UPDATES
void checkForMacUpdates(QApplication &app);
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Idyllwild"));
    QApplication::setApplicationName(QStringLiteral("IdyllwildApp"));
    MainIdyll w;
    w.show();
#ifdef IDYLLWILD_MAC_UPDATES
    checkForMacUpdates(a);
#endif
    return a.exec();
}
