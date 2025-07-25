#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QThread>
#include <QSettings>
#include <QMainWindow>
#include <QUdpSocket>
#include <QDateTime>
#include <QStandardPaths>
#include <ahp_gt.h>
#include "threads.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
        Q_OBJECT

    enum {
        GT1 = 1,
        GT2 = 2,
        GT2_BRAKE = 3,
        GT5 = 5
    } GT_Version;

    public:
        MainWindow(QWidget *parent = nullptr);
        ~MainWindow();

        int flashFirmware(const char *filename, int *progress, int *finished);
        void saveIni(QString ini);
        void readIni(QString ini);
        inline QString getDefaultIni()
        {
            return ini;
        }
        void lockRA()
        {
            while(!RAmutex.tryLock());
        }
        void unlockRA()
        {
            RAmutex.unlock();
        }

    private:
        int GT { 0 };
        int version[NumAxes] { 0 };
        int axis_number { 0  };
        double phi {0.0};

        bool axis_lospeed { false};
        bool axisdirection { false };
        double Speed;
        double currentSteps;
        SkywatcherAxisStatus status;
        double lastPollTime;
        double lastSteps;
        double lastSpeeds[60] { 0 };
        Thread *PositionThread;
        Thread *IndicationThread;
        Thread *ProgressThread;
        Thread *WriteThread;
        QSettings * settings;
        QString ini;
        QString firmwareFilename;
        QUdpSocket socket;
        int percent { 0 };
        int finished { 1 };
        int threadsStopped;
        bool isConnected;
        int axisstatus;
        int motionmode;
        bool correcting_tracking { false };
        int stop_correction { true };
        bool initial;
        int timer { 1000 };
        QStringList CheckFirmware(QString url, int timeout_ms = 1000);
        bool DownloadFirmware(QString url, QString filename, QSettings *settings, int timeout_ms = 30000);
        void disconnectControls(bool block);
        void UpdateValues(int axis);
        Ui::MainWindow *ui;
        static void WriteValues(MainWindow *wnd);
        QMutex RAmutex, DEmutex;
};
#endif // MAINWINDOW_H
