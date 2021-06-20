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
    int percent, finished;
    bool threadsRunning;
private:
    int axisstatus[2];
    int motionmode[2];
    bool initial;
    static void Progress(MainWindow *wnd);
    void UpdateValues(int axis);
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
