#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QThread>
#include <QSettings>
#include <QMainWindow>
#include <QUdpSocket>
#include <QDateTime>
#include <ahp_gt.h>
#include "threads.h"

#define _n_speeds 1

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

        int flashFirmware();
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
        double currentSteps[2];
        SkywatcherAxisStatus status[2];
        QDateTime lastPollTime[2];
        double lastSteps[2];
        double lastSpeeds[2][_n_speeds];
        Thread *IndicationThread;
        Thread *ProgressThread;
        Thread *StatusThread;
        Thread *WriteThread;
        QSettings * settings;
        QString ini;
        QString firmwareFilename;
        QUdpSocket socket;
        int percent { 0 };
        int finished { 1 };
        bool threadsRunning;
        bool isConnected;
        int axisstatus[2];
        int motionmode[2];
        bool initial;
        int timer { 1000 };
        void UpdateValues(int axis);
        Ui::MainWindow *ui;
        bool oldTracking;
        static void WriteValues(MainWindow *wnd);
        QMutex RAmutex, DEmutex;
};
#endif // MAINWINDOW_H
