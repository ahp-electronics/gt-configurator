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
    Thread(int timer = 20, int loop = 10) : QThread() { timer_ms = 20; loop_ms = loop; }
    void run() {
        while(!isInterruptionRequested()) {
            if(lock())
                emit threadLoop(this);
            else
                QThread::msleep(timer_ms);
            QThread::msleep(loop_ms);
        }
        disconnect(this, 0, 0, 0);
    }
    bool lock() { return mutex.tryLock(); }
    void unlock() { mutex.unlock(); }
    void setTimer(int timer) { timer_ms = timer; }
signals:
    void threadLoop(Thread *);
};

#endif // THREADS_H
