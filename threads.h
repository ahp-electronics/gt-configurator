#ifndef THREADS_H
#define THREADS_H

#include <QThread>
#include <QWidget>
#include <QMutex>

class Thread : public QThread {
    Q_OBJECT
private:
    QWidget* parent;
    QMutex mutex;
    int timer_ms;
    int loop_ms;
public:
    Thread(int timer = 20, int loop = 20) : QThread() { timer_ms = 20; loop_ms = loop; }
    void run() {
        while(!isInterruptionRequested()) {
            lastPollTime = QDateTime::currentDateTimeUtc();
            if(lock()) {
                emit threadLoop(this);
            }
            QThread::msleep(fmax(1, timer_ms-fabs(lastPollTime.msecsTo(QDateTime::currentDateTimeUtc()))));
            QThread::msleep(loop_ms);
        }
        disconnect(this, 0, 0, 0);
    }
    bool lock() { return mutex.tryLock(); }
    void unlock() { mutex.unlock(); }
    void setTimer(int timer) { timer_ms = timer; }
private:
    QDateTime lastPollTime;
signals:
    void threadLoop(Thread *);
};

#endif // THREADS_H
