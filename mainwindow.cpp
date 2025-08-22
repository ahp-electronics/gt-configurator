#include "mainwindow.h"
#include <unistd.h>
#include <ctime>
#include <cmath>
#include <cstring>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QIODevice>
#include <functional>
#include <QTemporaryFile>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QFileDialog>
#include <QTimer>
#include <QMutex>
#include <errno.h>
#include <libdfu.h>
#include "./ui_mainwindow.h"
static const double SIDEREAL_DAY = 86164.0916000;
static const double SIDEREAL_NOON = (SIDEREAL_DAY / 2);
static MountType mounttype[] =
{
    isEQ6,
    isHEQ5,
    isEQ5,
    isEQ3,
    isEQ8,
    isAZEQ6,
    isAZEQ5,
    isGT,
    isMF,
    is114GT,
    isDOB,
    isCustom,
};

static QList<int> mounttypes({
    ///Sky-Watcher EQ6
    0x00,
    ///Sky-Watcher HEQ5
    0x01,
    ///Sky-Watcher EQ5
    0x02,
    ///Sky-Watcher EQ3
    0x03,
    ///Sky-Watcher EQ8
    0x04,
    ///Sky-Watcher AZEQ6
    0x05,
    ///Sky-Watcher AZEQ5
    0x06,
    ///Sky-Watcher GT
    0x80,
    ///Fork Mount
    0x81,
    ///114GT
    0x82,
    ///Dobsonian mount
    0x90,
    ///Custom mount
    0xF0,
});

char *strrand(int len)
{
    int i;
    char* ret = (char*)malloc(len+1);
    for(i = 0; i < len; i++)
        ret[i] = 'a' + (rand() % 21);
    ret[i] = 0;
    return ret;
}

const int base_timing = 1500000;
const int offset_timing = 1500000>>4;

QStringList MainWindow::CheckFirmware(QString url, int timeout_ms)
{
    QByteArray list;
    QJsonDocument doc;
    QNetworkAccessManager* manager = new QNetworkAccessManager();
    QNetworkReply *response = manager->get(QNetworkRequest(QUrl(url)));
    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    connect(response, SIGNAL(finished()), &loop, SLOT(quit()));
    timer.start(timeout_ms);
    loop.exec();
    QString base64 = "";
    if(response->error() == QNetworkReply::NetworkError::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(response->readAll());
        QJsonObject obj = doc.object();
        base64 = obj["data"].toString();
    }
    if(base64.isNull() || base64.isEmpty()) {
        goto ck_end;
    }
    list = QByteArray::fromBase64(base64.toUtf8());
    doc = QJsonDocument::fromJson(list.toStdString().c_str());
    response->deleteLater();
    response->manager()->deleteLater();
    return doc.toVariant().toStringList();
ck_end:
    response->deleteLater();
    response->manager()->deleteLater();
    return QStringList();
}

bool MainWindow::DownloadFirmware(QString url, QString filename, QSettings *settings, int timeout_ms)
{
    QByteArray bin;
    QFile file(filename);
    QNetworkAccessManager* manager = new QNetworkAccessManager();
    QNetworkReply *response = manager->get(QNetworkRequest(QUrl(url)));
    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    connect(response, SIGNAL(finished()), &loop, SLOT(quit()));
    timer.start(timeout_ms);
    loop.exec();
    QString base64 = settings->value("firmware", "").toString();
    if(response->error() == QNetworkReply::NetworkError::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(response->readAll());
        QJsonObject obj = doc.object();
        base64 = obj["data"].toString();
    }
    if(base64.isNull() || base64.isEmpty()) {
        goto dl_end;
    }
    bin = QByteArray::fromBase64(base64.toUtf8());
    file.open(QIODevice::WriteOnly);
    file.write(bin, bin.length());
    file.close();
dl_end:
    response->deleteLater();
    response->manager()->deleteLater();
    if(!QFile::exists(filename)) return false;
    return true;
}

void MainWindow::readIni(QString ini)
{
    QString dir = QDir(ini).dirName();
    if(!QDir(dir).exists())
    {
        QDir().mkdir(dir);
    }
    if(!QFile(ini).exists())
    {
        QFile *f = new QFile(ini);
        f->open(QIODevice::WriteOnly);
        f->close();
        f->~QFile();
    }
    QSettings *settings = new QSettings(ini, QSettings::Format::IniFormat);
    ui->Notes->setText(QByteArray::fromBase64(settings->value("Notes").toString().toUtf8()));

    ui->PWMFreq->setValue(settings->value("PWMFreq", ahp_gt_get_pwm_frequency(axis_number)).toInt());
    ui->MountType->setCurrentIndex(settings->value("MountType", 0).toInt());
    ui->MountStyle->setCurrentIndex(settings->value("MountStyle", 0).toInt());
    //ui->HighBauds->setChecked(settings->value("HighBauds", false).toBool());
    ui->LimitIntensity->setChecked(settings->value("LimitIntensity", false).toBool());
    ui->Intensity->setValue(settings->value("Intensity", 0).toInt());   int flags = ahp_gt_get_mount_flags();

    int features = ahp_gt_get_features(axis_number);
    features &= ~(isAZEQ | hasHalfCurrentTracking);
    features |= hasCommonSlewStart;
    features |= (settings->value("HalfCurrent", false).toBool() ? hasHalfCurrentTracking : 0);
    flags &= ~isForkMount;
    flags &= ~bauds_115200;
    flags |= ((ui->MountStyle->currentIndex() == 1) ? isForkMount : 0);
    //flags |= (ui->HighBauds->isChecked() ? bauds_115200 : 0);
    flags |= halfCurrentRA;
    flags |= halfCurrentDec;
    ahp_gt_set_mount_flags((GTFlags)flags);
    ahp_gt_set_mount_type((MountType)mounttypes[ui->MountType->currentIndex()]);
    ahp_gt_set_features(axis_number, (SkywatcherFeature)(features | ((ui->MountStyle->currentIndex() == 2) ? isAZEQ : 0)));
    ahp_gt_set_features(axis_number, (SkywatcherFeature)(features | ((ui->MountStyle->currentIndex() == 2) ? isAZEQ : 0)));
    ahp_gt_set_pwm_frequency(axis_number, ui->PWMFreq->value());


    ui->MotorSteps->setValue(settings->value("MotorSteps", ahp_gt_get_motor_steps(axis_number)).toInt());
    ui->Motor->setValue(settings->value("Motor", ahp_gt_get_motor_teeth(axis_number)).toInt());
    ui->Worm->setValue(settings->value("Worm", ahp_gt_get_worm_teeth(axis_number)).toInt());
    ui->Crown->setValue(settings->value("Crown", ahp_gt_get_crown_teeth(axis_number)).toInt());
    ui->MaxSpeed->setValue(settings->value("MaxSpeed", ahp_gt_get_max_speed(axis_number) * SIDEREAL_NOON / M_PI).toInt());
    ui->Acceleration->setValue(settings->value("Acceleration",
                                 ui->Acceleration->maximum() - ahp_gt_get_acceleration_angle(axis_number) * 1800.0 / M_PI).toInt());
    ui->Invert->setChecked(settings->value("Invert", ahp_gt_get_direction_invert(axis_number) == 1).toBool());
    ui->Inductance->setValue(settings->value("Inductance", 10).toInt());
    ui->Resistance->setValue(settings->value("Resistance", 20000).toInt());
    ui->Current->setValue(settings->value("Current", 1000).toInt());
    ui->Voltage->setValue(settings->value("Voltage", 12).toInt());
    ui->GPIO->setCurrentIndex(settings->value("GPIO", ahp_gt_get_feature(axis_number)).toInt());
    ui->Coil->setCurrentIndex(settings->value("Coil", ahp_gt_get_stepping_conf(axis_number)).toInt());
    ui->SteppingMode->setCurrentIndex(settings->value("SteppingMode", ahp_gt_get_stepping_mode(axis_number)).toInt());
    ui->Mean->setValue(settings->value("Mean", 1).toInt());

    ahp_gt_set_timing(axis_number, settings->value("TimingValue", 1500000).toInt());
    ahp_gt_set_motor_steps(axis_number, ui->MotorSteps->value());
    ahp_gt_set_motor_teeth(axis_number, ui->Motor->value());
    ahp_gt_set_worm_teeth(axis_number, ui->Worm->value());
    ahp_gt_set_crown_teeth(axis_number, ui->Crown->value());
    ahp_gt_set_direction_invert(axis_number, ui->Invert->isChecked());
    ahp_gt_set_stepping_conf(axis_number, (GTSteppingConfiguration)ui->Coil->currentIndex());
    ahp_gt_set_stepping_mode(axis_number, (GTSteppingMode)ui->SteppingMode->currentIndex());
    switch(ui->GPIO->currentIndex())
    {
        case 0:
            ahp_gt_set_feature(axis_number, GpioUnused);
            break;
        case 1:
            ahp_gt_set_feature(axis_number, GpioAsST4);
            break;
        case 2:
            ahp_gt_set_feature(axis_number, GpioAsPulseDrive);
            break;
        default:
            break;
    }
    double target = settings->value("Target", 0).toDouble();
}

void MainWindow::saveIni(QString ini)
{
    QString dir = QDir(ini).dirName();
    if(!QDir(dir).exists())
    {
        QDir().mkdir(dir);
    }
    if(!QFile(ini).exists())
    {
        QFile *f = new QFile(ini);
        f->open(QIODevice::WriteOnly);
        f->close();
        f->~QFile();
    }
    QSettings *oldsettings = settings;
    QSettings *s = new QSettings(ini, QSettings::Format::IniFormat);
    settings = s;

    settings->setValue("Invert", ui->Invert->isChecked());
    settings->setValue("SteppingMode", ui->SteppingMode->currentIndex());
    settings->setValue("MotorSteps", ui->MotorSteps->value());
    settings->setValue("Worm", ui->Worm->value());
    settings->setValue("Motor", ui->Motor->value());
    settings->setValue("Crown", ui->Crown->value());
    settings->setValue("Acceleration", ui->Acceleration->value());
    settings->setValue("MaxSpeed", ui->MaxSpeed->value());
    settings->setValue("Coil", ui->Coil->currentIndex());
    settings->setValue("GPIO", ui->GPIO->currentIndex());
    settings->setValue("Inductance", ui->Inductance->value());
    settings->setValue("Resistance", ui->Resistance->value());
    settings->setValue("Current", ui->Current->value());
    settings->setValue("Voltage", ui->Voltage->value());
    settings->setValue("TimingValue", ahp_gt_get_timing(axis_number));
    settings->setValue("Mean", ui->Mean->value());

    settings->setValue("LimitIntensity", ui->LimitIntensity->isChecked());
    settings->setValue("Intensity", ui->Intensity->value());
    settings->setValue("MountType", ui->MountType->currentIndex());
    settings->setValue("Address", ui->Address->value());
    settings->setValue("PWMFreq", ui->PWMFreq->value());
    settings->setValue("MountStyle", ui->MountStyle->currentIndex());
    //settings->setValue("HighBauds", ui->HighBauds->isChecked());
    settings->setValue("Timing", ui->Timing->value());
    settings->setValue("Notes", QString(ui->Notes->text().toUtf8().toBase64()));
    s->~QSettings();
    settings = oldsettings;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ahp_set_app_name("GT Configurator");
    ahp_set_debug_level(AHP_DEBUG_DEBUG);
    IndicationThread = new Thread(this, 100, 500);
    ProgressThread = new Thread(this, 100, 10);
    PositionThread = new Thread(this, 1000, 1000);
    setAccessibleName("GT Configurator");
    firmwareFilename = QStandardPaths::standardLocations(QStandardPaths::TempLocation).at(0) + "/" + strrand(32);
    QString homedir = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).at(0);
    ini = homedir + "/settings.ini";
    if(!QDir(homedir).exists())
    {
        QDir().mkdir(homedir);
    }
    if(!QFile(ini).exists())
    {
        QFile *f = new QFile(ini);
        f->open(QIODevice::WriteOnly);
        f->close();
        f->~QFile();
    }
    stop_correction = true;
    settings = new QSettings(ini, QSettings::Format::IniFormat);
    isConnected = false;
    this->setFixedSize(600, 660);
    ui->setupUi(this);
    QString lastPort = settings->value("LastPort", "").toString();
    if(lastPort != "")
        ui->ComPort->addItem(lastPort);
    ui->ComPort->addItem("localhost:11880");
    ahp_gt_set_axes_limit(NumAxes);
    for(int a = 0; a < ahp_gt_get_axes_limit(); a++) {
        ui->Axis->addItem(ahp_gt_get_axis_name(a));
    }
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    if(ports.length() > 0)
    {
        for (int i = 0; i < ports.length(); i++)
        {
            if(ports[i].portName() != lastPort)
                ui->ComPort->addItem(ports[i].portName());
        }
    }
    else
        ui->ComPort->addItem("No serial ports available");
    ui->MountType->setCurrentIndex(0);
    WriteThread = new Thread(this);
    connect(WriteThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), [ = ] (Thread * thread) {
        saveIni(getDefaultIni());
        percent = 0;
        finished = 0;
        ui->WorkArea->setEnabled(false);
        ui->Connection->setEnabled(false);
        finished = 0;
        if(ui->Write->text() == "Flash")
        {
            if(QFile::exists(firmwareFilename)) {
                while(mutex.tryLock()) QThread::msleep(10);
                QFile f(firmwareFilename);
                f.open(QIODevice::ReadOnly);
                if(!dfu_flash(f.handle(), &percent, &finished))
                {
                    settings->setValue("firmware", f.readAll().toBase64());
                }
                f.close();
                mutex.unlock();
            }
        }
        else
        {
            ui->Write->setEnabled(false);
            ahp_gt_write_values(axis_number, &percent, &finished);
            ahp_gt_reload(axis_number);
            ui->Write->setEnabled(true);
            ui->WorkArea->setEnabled(true);
        }
        ui->Connection->setEnabled(true);
        percent = 0;
        thread->requestInterruption();
        thread->unlock();
    });
    connect(ui->LoadFW, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool triggered)
    {
        QStringList versions = CheckFirmware("https://www.iliaplatone.com/firmware.php?product=gt*", 3000);
        if(versions.count() > 0) {
            ui->FW_List->clear();
            for (QString version : versions)
                ui->FW_List->addItem(version.replace("firmware/", "").replace("-firmware.bin", ""));
        }
    });
    connect(ui->FW_List, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            [ = ](int value)
    {
        QString url = "https://www.iliaplatone.com/firmware.php?download=yes&product="+ui->FW_List->currentText();
        if(DownloadFirmware(url, firmwareFilename, settings))
            ui->Write->setText("Flash");
        else {
            QFile f(firmwareFilename);
            QFile s("qrc:/data/"+ui->FW_List->currentText()+".json");
            QJsonDocument doc = QJsonDocument::fromJson(s.readAll());
            s.close();
            QJsonObject obj = doc.object();
            QString base64 = obj["data"].toString();
            if(base64.isNull() || base64.isEmpty()) {
                f.close();
                goto dl_end;
            }
            f.write(QByteArray::fromBase64(base64.toUtf8()));
            f.close();
        }
        ui->Connection->setEnabled(false);
        ui->Configuration->setEnabled(false);
        ui->Control->setEnabled(false);
        ui->commonSettings->setEnabled(false);
        ui->Advanced->setEnabled(false);
        dl_end:
        return;
    });
    connect(ui->Connect, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool checked)
    {
        QString portname;
        int port = 9600;
        QString address = "localhost";
        int failure = 1;
        if(ui->ComPort->currentText().contains(':'))
        {
            address = ui->ComPort->currentText().split(":")[0];
            port = ui->ComPort->currentText().split(":")[1].toInt();
            if(!ahp_gt_connect_udp(address.toStdString().c_str(), port)) {
                failure = ahp_gt_detect_device();
            }
        }
        else
        {
            portname.append(ui->ComPort->currentText());
            failure = ahp_gt_connect(portname.toUtf8());
            if(failure)
                ahp_gt_disconnect();
        }
        if(!failure)
        {
            ahp_gt_select_device(0);
            int a = 0;
            axis_number = 0;
            version[0] = 0;
            for (a= 0; a < NumAxes && version[a] == 0; a++){
                version[a] = ahp_gt_get_mc_version(a);
                if((version[a] & 0xf) == 5) {
                    GT[a] = GT5;
                    axis_number = a;
                } else if((version[a] & 0xf) == 4) {
                    GT[a] = GT5_BRAKE;
                    axis_number = a;
                }
                ahp_gt_read_values(a);
            }
            GT[0] = 0;
            GT[1] = 0;
            if((version[0] & 0xf) == 1 && (version[1] & 0xf) == 1){
                    GT[0] = GT1;
                    GT[1] = GT1;
                }
                else if((version[0] & 0xf) == 2 && (version[1] & 0xf) == 3){
                    GT[0] = GT2;
                    GT[1] = GT2;
                } else if((version[0] & 0xf) == 6 && (version[1] & 0xf) == 7){
                    GT[0] = GT2_BRAKE;
                    GT[1] = GT2_BRAKE;
                }

            ui->Axis->setCurrentIndex(axis_number);
            settings->setValue("LastPort", ui->ComPort->currentText());
            ui->Write->setText("Write");
            ui->Write->setEnabled(true);
            ahp_gt_read_values(axis_number);
            int flags = ahp_gt_get_mount_flags();
            ahp_gt_set_mount_flags((GTFlags)flags);
            ui->LoadFW->setEnabled(false);
            ui->Connect->setEnabled(false);
            ui->Disconnect->setEnabled(true);
            ui->labelNotes->setEnabled(true);
            ui->Notes->setEnabled(true);
            ui->Configuration->setEnabled(true);
            ui->Control->setEnabled(true);
            ui->commonSettings->setEnabled(true);
            ui->Advanced->setEnabled(true);
            ui->loadConfig->setEnabled(true);
            ui->saveConfig->setEnabled(true);
            ui->WorkArea->setEnabled(true);
            isConnected = true;
            finished = true;
            ui->ComPort->setEnabled(false);
            IndicationThread->start();
        }
    });
    connect(ui->Disconnect, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool checked)
    {
        IndicationThread->stop();
        ui->Write->setText("Flash");
        ui->Write->setEnabled(true);
        ui->ComPort->setEnabled(true);
        isConnected = false;
        finished = false;
        //ui->HighBauds->setChecked(false);
        ui->Timing->setEnabled(true);
        ui->LoadFW->setEnabled(true);
        ui->Connect->setEnabled(true);
        ui->Disconnect->setEnabled(false);
        ui->labelNotes->setEnabled(false);
        ui->Notes->setEnabled(false);
        ui->Configuration->setEnabled(false);
        ui->Control->setEnabled(false);
        ui->commonSettings->setEnabled(false);
        ui->Advanced->setEnabled(false);
        ui->loadConfig->setEnabled(false);
        ui->saveConfig->setEnabled(false);
        ahp_gt_stop_motion(axis_number, 0);
        ahp_gt_disconnect();
    });
    connect(ui->loadConfig, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool triggered)
    {
        QString ini = QFileDialog::getOpenFileName(this, "Open configuration file",
                      QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0), "Configuration files (*.ini)");
        if(ini.endsWith(".ini"))
        {
            readIni(ini);
        }
    });
    connect(ui->saveConfig, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool triggered)
    {
        QString ini = QFileDialog::getSaveFileName(this, "Save configuration file",
                      QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0), "Configuration files (*.ini)");
        if(!ini.endsWith(".ini"))
            ini = ini.append(".ini");
        saveIni(ini);
    });

    connect(ui->MountType, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            [ = ](int index)
    {
        ahp_gt_set_mount_type(mounttype[index]);
        saveIni(ini);
    });
    connect(ui->Invert, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked),
            [ = ](bool checked)
    {
        ahp_gt_set_direction_invert(axis_number, checked);
        saveIni(ini);
    });
    connect(ui->MotorSteps, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_steps(axis_number, value);
        saveIni(ini);
    });
    connect(ui->Worm, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_worm_teeth(axis_number, value);
        saveIni(ini);
    });
    connect(ui->Motor, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_teeth(axis_number, value);
        saveIni(ini);
    });
    connect(ui->Crown, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_crown_teeth(axis_number, value);
        saveIni(ini);
    });
    connect(ui->Acceleration, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_acceleration_angle(axis_number, (ui->Acceleration->maximum() - ui->Acceleration->value()) * M_PI / 1800.0);
        ui->Acceleration_label->setText("Acceleration: " + QString::number((double)ui->Acceleration->maximum() / 10.0 - (double)value / 10.0) + "°");
        saveIni(ini);
    });
    connect(ui->MaxSpeed, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_max_speed(axis_number, ui->MaxSpeed->value() * M_PI / SIDEREAL_NOON);
        saveIni(ini);
    });
    connect(ui->SteppingMode, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_mode(axis_number, (GTSteppingMode)index);
        saveIni(ini);
    });
    connect(ui->Coil, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_conf(axis_number, (GTSteppingConfiguration)index);
        saveIni(ini);
    });
    connect(ui->GPIO, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        switch(index)
        {
            case 0:
                ahp_gt_set_feature(axis_number, GpioUnused);
                break;
            case 1:
                ahp_gt_set_feature(axis_number, GpioAsST4);
                break;
            case 2:
                ahp_gt_set_feature(axis_number, GpioAsPulseDrive);
                break;
            default:
                break;
        }
        saveIni(ini);
    });
    connect(ui->Address, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        if(isConnected) {
            ahp_gt_copy_device(ahp_gt_get_current_device(), value);
            ahp_gt_write_values(axis_number, nullptr, nullptr);
            if(ahp_gt_get_current_device() > 0)
                ahp_gt_delete_device(ahp_gt_get_current_device());
        }
        saveIni(ini);
    });/*
    connect(ui->HighBauds, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), [ = ] (bool checked)
    {
        int flags = (int)ahp_gt_get_mount_flags();
        flags &= ~bauds_115200;
        if(checked)
            flags |= bauds_115200;
        ahp_gt_set_mount_flags((GTFlags)flags);
        saveIni(ini);
    });*/
    connect(ui->PWMFreq, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_pwm_frequency(axis_number, value);
        ui->PWMFreq_label->setText("PWM: " + QString::number(366 + 366 * value) + " Hz");
        saveIni(ini);
    });
    connect(ui->MountStyle, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            [ = ](int index)
    {
        int flags = (int)ahp_gt_get_mount_flags();
        if(index == 2) {
            ahp_gt_set_features(axis_number, (SkywatcherFeature)(ahp_gt_get_features(axis_number) | isAZEQ));
            ahp_gt_set_features(axis_number, (SkywatcherFeature)(ahp_gt_get_features(axis_number) | isAZEQ));
        } else {
            ahp_gt_set_features(axis_number, (SkywatcherFeature)(ahp_gt_get_features(axis_number) & ~isAZEQ));
            ahp_gt_set_features(axis_number, (SkywatcherFeature)(ahp_gt_get_features(axis_number) & ~isAZEQ));
        }
        flags &= ~isForkMount;
        ahp_gt_set_mount_flags((GTFlags)(flags | (index == 1 ? isForkMount : 0)));
        saveIni(ini);
    });
    connect(ui->Axis, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            [ = ](int value)
    {
        switch (GT[value]) {
        case GT1:
        case GT2:
        case GT2_BRAKE:
            if(value > 1) {
                value = 1;
                ui->Axis->setCurrentIndex(value);
            }
            break;
        case GT5:
        case GT5_BRAKE:
            if(isConnected) {
                ahp_gt_copy_axis(axis_number, value);
                ahp_gt_write_values(axis_number, nullptr, nullptr);
                ahp_gt_delete_axis(axis_number);
            }
            break;
        default:
            break;
        }
        axis_number = value;
        ahp_gt_read_values(axis_number);
    });
    connect(ui->Speed, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ui->Speed_label->setText("Ra speed: " + QString::number(value) + "x");
    });
    connect(ui->Minus, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        ahp_gt_stop_motion(axis_number, axisdirection != true || axis_lospeed != (fabs(ui->Speed->value()) < 128.0));
        ahp_gt_start_motion(axis_number, ui->Speed->value() * M_PI / SIDEREAL_NOON);
        axisdirection= true;
        axis_lospeed = (fabs(ui->Speed->value()) < 128.0);
    });
    connect(ui->Plus, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        ahp_gt_stop_motion(axis_number, axisdirection != false || axis_lospeed != (fabs(ui->Speed->value()) < 128.0));
        ahp_gt_start_motion(axis_number, -ui->Speed->value() * M_PI / SIDEREAL_NOON);
        axisdirection = false;
        axis_lospeed = (fabs(ui->Speed->value()) < 128.0);
    });
    connect(ui->Stop, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        ahp_gt_stop_motion(axis_number, 0);
    });
    connect(ui->Minus, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(axis_number, 0);
    });
    connect(ui->Plus, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(axis_number, 0);
    });
    connect(ui->LimitIntensity, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), this,
            [ = ](bool checked)
    {
        ahp_gt_limit_intensity(axis_number, checked);
        saveIni(ini);
    });
    connect(ui->Intensity, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged), this,
            [ = ](int value)
    {
        ahp_gt_set_intensity_limit(axis_number, value);
        saveIni(ini);
    });
    connect(ui->Write, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool checked = false)
    {
        if(ui->Write->text() == "Write")
        {
        }

        WriteThread->start();
    });
    connect(ui->Inductance, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Resistance, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Current, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Voltage, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Target, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
    {
        phi = M_PI * 2 / ui->Target->value();
        saveIni(ini);
    });
    connect(ui->Goto, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked), [ = ](bool checked)
    {
        ahp_gt_goto_absolute(axis_number, phi, (double)ui->Speed->value() * M_PI / SIDEREAL_NOON);
    });
    connect(ui->Timing, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged), [ = ](int value)
    {
        double offset = ((double)value/ui->Timing->maximum())*offset_timing;
        ahp_gt_set_timing(axis_number, base_timing-offset);
        ui->Timing_label->setText("Timing: "+QString::number(offset * 100.0 / base_timing)+"%");
        saveIni(ini);
    });
    connect(ProgressThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), this, [ = ] (Thread * parent)
    {
        ui->progress->setValue(fmax(ui->progress->minimum(), fmin(ui->progress->maximum(), percent)));
        parent->unlock();
    });
    connect(IndicationThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), this, [ = ] (Thread * parent)
    {
        if(isConnected && finished)
        {
                ui->CurrentSteps->setText(QString::number((int)currentSteps));
                ui->Rate->setText("deg/sec: " + QString::number(Speed/3600.0));
                UpdateValues(axis_number);
        }

        parent->unlock();
    });
    connect(PositionThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), [ = ] (Thread * parent)
    {
        if(isConnected && finished)
        {
            currentSteps = ahp_gt_get_position(axis_number, &status.timestamp) * ahp_gt_get_totalsteps(axis_number) / M_PI / 2.0;
            double diffTime = (double)status.timestamp-lastPollTime;
            lastPollTime = status.timestamp;
            double diffSteps = currentSteps - lastSteps;
            lastSteps = currentSteps;
            diffSteps *= 360.0 * 60.0 * 60.0 / ahp_gt_get_totalsteps(axis_number);
            Speed = 0.0;
            int _n_speeds = 1;
            _n_speeds = ui->Mean->value();
            for(int s = 0; s < _n_speeds; s++)
            {
                if(s < _n_speeds - 1)
                    lastSpeeds[s] = lastSpeeds[s + 1];
                else
                    lastSpeeds[s] = diffSteps;
                Speed += lastSpeeds[s];
            }
            Speed /= _n_speeds * diffTime;/*
            if(!stop_correction) {
                ahp_gt_correct_tracking(axis_number, SIDEREAL_DAY * ahp_gt_get_wormsteps(axis_number) / ahp_gt_get_totalsteps(axis_number), &stop_correction);
                    if(ui->Tune->isChecked())
                        ui->Tune->click();
            }*/
        }
        parent->unlock();
    });
    PositionThread->start();
    ProgressThread->start();
}

MainWindow::~MainWindow()
{
    if(isConnected)
        ui->Disconnect->click();
    PositionThread->stop();
    IndicationThread->stop();
    ProgressThread->stop();
    WriteThread->stop();
    PositionThread->wait();
    IndicationThread->wait();
    ProgressThread->wait();
    WriteThread->wait();
    if(QFile(firmwareFilename).exists())
        unlink(firmwareFilename.toUtf8());
    threadsStopped = true;
    delete ui;
}

void MainWindow::disconnectControls(bool block)
{
    ui->MountType->blockSignals(block);
    ui->Address->blockSignals(block);
    ui->MountStyle->blockSignals(block);
    ui->PWMFreq->blockSignals(block);

    ui->Invert->blockSignals(block);
    ui->MotorSteps->blockSignals(block);
    ui->Worm->blockSignals(block);
    ui->Motor->blockSignals(block);
    ui->Crown->blockSignals(block);
    ui->Acceleration->blockSignals(block);
    ui->MaxSpeed->blockSignals(block);
    ui->SteppingMode->blockSignals(block);
    ui->Coil->blockSignals(block);
    ui->GPIO->blockSignals(block);
}

void MainWindow::UpdateValues(int axis)
{
    double totalsteps = ahp_gt_get_totalsteps(axis) * ahp_gt_get_divider(axis) / ahp_gt_get_multiplier(axis);
    ui->Divider->setText(QString::number(ahp_gt_get_divider(axis)));
    ui->Multiplier->setText(QString::number(ahp_gt_get_multiplier(axis)));
    ui->WormSteps->setText(QString::number(ahp_gt_get_wormsteps(axis)));
    ui->TotalSteps->setText(QString::number(ahp_gt_get_totalsteps(axis)));
    ui->TrackingFrequency->setText("Steps/s: " + QString::number(totalsteps / SIDEREAL_DAY));
    ui->SPT->setText("sec/turn: " + QString::number(SIDEREAL_DAY / (ahp_gt_get_crown_teeth(axis)*ahp_gt_get_worm_teeth(
                           0) / ahp_gt_get_motor_teeth(axis))));
    double L = (double)ui->Inductance->value() / 1000000.0;
    double R = (double)ui->Resistance->value() / 1000.0;
    double mI = (double)ui->Current->value() / 1000.0;
    double mV = (double)ui->Voltage->value();
    double Z = sqrt(fmax(0, pow(mV / mI, 2.0) - pow(R, 2.0)));
    double f = (2.0 * M_PI * Z / L);
    ui->PWMFrequency->setText("PWM Hz: " + QString::number(f));
    ui->GotoFrequency->setText("Goto Hz: " + QString::number(totalsteps * ahp_gt_get_max_speed(axis) / M_PI / 2));
    ui->MotorSteps->setValue(ahp_gt_get_motor_steps(axis));
    ui->Motor->setValue(ahp_gt_get_motor_teeth(axis));
    ui->Worm->setValue(ahp_gt_get_worm_teeth(axis));
    ui->Crown->setValue(ahp_gt_get_crown_teeth(axis));
    ui->Acceleration->setValue(ui->Acceleration->maximum() - ahp_gt_get_acceleration_angle(axis) * 1800.0 / M_PI);
    ui->MaxSpeed->setMaximum(ahp_gt_get_speed_limit(axis) * SIDEREAL_NOON / M_PI);
    ui->Speed->setMaximum(ahp_gt_get_speed_limit(axis) * SIDEREAL_NOON / M_PI);
    ui->MaxSpeed->setValue(ahp_gt_get_max_speed(axis) * SIDEREAL_NOON / M_PI);
    ui->MaxSpeed_label->setText("Maximum speed: " + QString::number(ahp_gt_get_max_speed(axis) * SIDEREAL_NOON / M_PI) + "x");
    ui->Coil->setCurrentIndex(ahp_gt_get_stepping_conf(axis));
    ui->SteppingMode->setCurrentIndex(ahp_gt_get_stepping_mode(axis));
    ui->Invert->setChecked(ahp_gt_get_direction_invert(axis));
    switch(ahp_gt_get_feature(axis))
    {
        case GpioUnused:
            ui->GPIO->setCurrentIndex(0);
            break;
        case GpioAsST4:
            ui->GPIO->setCurrentIndex(1);
            break;
        case GpioAsPulseDrive:
            ui->GPIO->setCurrentIndex(2);
            break;
        default:
            break;
    }
    ui->PWMFreq->setValue(ahp_gt_get_pwm_frequency(axis));
    ui->PWMFreq_label->setText("PWM: " + QString::number(366 + 366 * ui->PWMFreq->value()) + " Hz");
    if(ahp_gt_get_current_device() > 0)
        ui->Address->setValue(ahp_gt_get_current_device());
    ui->MountType->setCurrentIndex(mounttypes.indexOf(ahp_gt_get_mount_type()));
    int index = 0;
    index |= (((ahp_gt_get_features(axis) & isAZEQ) != 0) ? 2 : 0);
    index |= (((ahp_gt_get_features(axis) & isAZEQ) != 0) ? 2 : 0);
    if(!index) {
        index |= (((ahp_gt_get_mount_flags() & isForkMount) != 0) ? 1 : 0);
    }
    ui->MountStyle->setCurrentIndex(index);
    //ui->HighBauds->setChecked((ahp_gt_get_mount_flags() & bauds_115200) != 0);
    ui->Timing->setValue((base_timing-ahp_gt_get_timing(axis))*ui->Timing->maximum()/offset_timing);
    ui->LimitIntensity->setChecked(ahp_gt_is_intensity_limited(axis));
    ui->Intensity->setValue(ahp_gt_get_intensity_limit(axis));
}
