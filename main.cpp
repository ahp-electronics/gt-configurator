#include "mainwindow.h"

#include <QApplication>

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow)
{
    int argc = 0;
    char **argv = {NULL};
    QApplication a(argc, argv);
#else
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
#endif
    MainWindow w;
    w.show();
    a.setWindowIcon(QIcon(":/icons/icon.ico"));
    return a.exec();
}
