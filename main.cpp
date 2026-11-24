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
    QApplication::setApplicationName(QStringLiteral("IdyllwildApp"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/app-icon.png")));
    MainIdyll w;
#ifdef IDYLLWILD_MAC_UPDATES
    setupMacUpdates(w);
#endif
    w.show();
    return a.exec();
}
