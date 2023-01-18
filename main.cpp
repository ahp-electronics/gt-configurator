#include "mainwindow.h"
#include <config.h>

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
    w.setWindowTitle("GT Configurator - Version " GT_CONFIGURATOR_VERSION " Engine " + QString::number(ahp_gt_get_version(), 16));
    QFont font = w.font();
    font.setPixelSize(12);
    w.setFont(font);
    w.show();
    a.setWindowIcon(QIcon(":/icons/icon.ico"));
    return a.exec();
}
