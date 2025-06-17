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

    private:
        void Connect(bool clicked = false);
        void Disconnect(bool clicked = false);
        int mountversion;
        int num_axes;
        int axis_index { 0 };
        double Altitude, Latitude, Longitude, Elevation;
        double Ra {0.0};
        double Dec {0.0};

        double* toDms(double d);
        QString toHMS(double hms);
        QString toDMS(double dms);
        double fromHMSorDMS(QString dms);

        bool axis_lospeed { false };
        bool axisdirection { false };
        double Speed;
        double currentSteps;
        SkywatcherAxisStatus status;
        double lastPollTime;
        double lastSteps;
        double lastSpeeds[60] { { 0 } };
        bool oldTracking { false };
        bool isTracking { false };
        int axisstatus;
        int motionmode;
        bool correcting_tracking { false };
        int stop_correction { true };
        Thread *PositionThread;
        Thread *UIThread;
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
        void disconnectDecControls(bool block);
        void ReadValues();
        Ui::MainWindow *ui;
        static void WriteValues(MainWindow *wnd);
        QMutex mutex;
};
#endif // MAINWINDOW_H
