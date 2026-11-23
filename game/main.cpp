#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    pig4_detail::MainWindowLayoutGuard<unsigned(sizeof(MainWindow))>::check();
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
