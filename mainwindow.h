#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <thread>
#include <QThread>
#include <QMainWindow>
#include <ahp_gt.h>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    int flashFirmware();
private:
    QString firmwareFilename;
    int percent { 0 };
    int finished { 1 };
    bool threadsRunning;
    bool isConnected;
    int axisstatus[2];
    int motionmode[2];
    bool initial;
    void UpdateValues(int axis);
    Ui::MainWindow *ui;
    bool oldTracking;
    static void Progress(MainWindow *wnd);
    static void WriteValues(MainWindow *wnd);
signals:
    void onUpdateProgress();
};
#endif // MAINWINDOW_H
