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

        int num_axes;
        int *axis_version;
        int axis_index { 0 };
        double Altitude, Latitude, Longitude, Elevation;
        double Ra {0.0};
        double Dec {0.0};

        double* toDms(double d);
        QString toHMS(double hms);
        QString toDMS(double dms);
        double fromHMSorDMS(QString dms);

        bool axis_lospeed[2] { false, false };
        bool axisdirection[2] { false, false };
        double Speed[2];
        double currentSteps[2];
        SkywatcherAxisStatus status[2];
        double lastPollTime[2];
        double lastSteps[2];
        double lastSpeeds[2][60] { { 0 }, { 0 }};
        bool oldTracking[2] { false, false };
        bool isTracking[2] { false, false };
        int axisstatus[2];
        int motionmode[2];
        bool correcting_tracking[2] { false, false };
        int stop_correction[2] { true, true };
        Thread *RaThread;
        Thread *DecThread;
        Thread *IndicationThread;
        Thread *ProgressThread;
        Thread *WriteThread;
        Thread *ServerThread;
        QSettings * settings;
        QString ini;
        QString firmwareFilename;
        QUdpSocket socket;
        int percent { 0 };
        int finished { 1 };
        int threadsStopped;
        bool isConnected;
        bool initial;
        int timer { 1000 };
        QStringList CheckFirmware(QString url, int timeout_ms = 30000);
        bool DownloadFirmware(QString url, QString jsonfile, QString filename, QSettings *settings, int timeout_ms = 30000);
        void disconnectControls(bool block);
        void UpdateValues(int axis);
        Ui::MainWindow *ui;
        static void WriteValues(MainWindow *wnd);
        QMutex RAmutex, DEmutex;
};
#endif // MAINWINDOW_H
