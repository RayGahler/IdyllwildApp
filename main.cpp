#include "mainidyll.h"

#include <QApplication>

#ifdef IDYLLWILD_MAC_UPDATES
void setupMacUpdates(QMainWindow &window);
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Idyllwild"));
    QApplication::setApplicationName(QStringLiteral("IdyllwildApp"));
    MainIdyll w;
#ifdef IDYLLWILD_MAC_UPDATES
    setupMacUpdates(w);
#endif
    w.show();
    return a.exec();
}
