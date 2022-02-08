#ifndef THREADS_H
#define THREADS_H

#include <QThread>
#include <QWidget>
#include <QMutex>

class Thread : public QThread
{
        Q_OBJECT
    private:
        QWidget* parent;
        QMutex mutex;
        int timer_ms;
        int loop_ms;
    public:
        Thread(int timer = 20, int loop = 20) : QThread()
        {
            timer_ms = timer;
            loop_ms = loop;
        }
        void run()
        {
            lastPollTime = QDateTime::currentDateTimeUtc();
            while(!isInterruptionRequested())
            {
                if(lock() && timer_ms > fabs(lastPollTime.msecsTo(QDateTime::currentDateTimeUtc())))
                {
                    lastPollTime = QDateTime::currentDateTimeUtc();
                    emit threadLoop(this);
                }
                timer_ms = loop_ms;
                QThread::msleep(1);
            }
            disconnect(this, 0, 0, 0);
        }
        bool lock()
        {
            return mutex.tryLock();
        }
        void unlock()
        {
            mutex.unlock();
        }
        void setTimer(int timer)
        {
            timer_ms = timer;
        }
    private:
        QDateTime lastPollTime;
    signals:
        void threadLoop(Thread *);
};

#endif // THREADS_H
