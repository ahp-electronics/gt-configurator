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
#include <errno.h>
#include <libdfu.h>
#include "./ui_mainwindow.h"

#define POSITION_THREAD_LOOP 1000

static const double SIDEREAL_DAY = 86164.0916000;
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

bool MainWindow::DownloadFirmware(QString url, QString filename, QSettings *settings, int timeout_ms)
{
    QByteArray bin;
    QFile file(filename);
    QNetworkAccessManager* manager = new QNetworkAccessManager();
    QNetworkReply *response = manager->get(QNetworkRequest(QUrl(url+"&download=on")));
    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    connect(response, SIGNAL(finished()), &loop, SLOT(quit()));
    timer.start(timeout_ms);
    loop.exec();
    QString base64 = settings->value("data", "").toString();
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
    while(!IndicationThread->lock()) Thread::msleep(100);
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

    ui->Address->setValue(settings->value("Address", ahp_gt_get_address()).toInt());
    ui->PWMFreq->setValue(settings->value("PWMFreq", ahp_gt_get_pwm_frequency()).toInt());
    ui->MountType->setCurrentIndex(settings->value("MountType", 0).toInt());
    ui->MountStyle->setCurrentIndex(settings->value("MountStyle", 0).toInt());
    ui->HighBauds->setChecked(settings->value("HighBauds", false).toBool());
    ui->HalfCurrent_0->setChecked(settings->value("HalfCurrent_0", false).toBool());
    ui->HalfCurrent_1->setChecked(settings->value("HalfCurrent_1", false).toBool());
    int flags = ahp_gt_get_mount_flags();
    int features = ahp_gt_get_features(0);
    features &= ~(isAZEQ | hasHalfCurrentTracking);
    features |= hasCommonSlewStart;
    features |= (settings->value("HalfCurrent", false).toBool() ? hasHalfCurrentTracking : 0);
    ui->HalfCurrent->setChecked(settings->value("HalfCurrent", false).toBool());
    flags &= ~isForkMount;
    flags &= ~bauds_115200;
    flags &= ~halfCurrentRA;
    flags &= ~halfCurrentDec;
    flags |= ((ui->MountStyle->currentIndex() == 1) ? isForkMount : 0);
    flags |= (ui->HighBauds->isChecked() ? bauds_115200 : 0);
    flags |= (ui->HalfCurrent_0->isChecked() ? halfCurrentRA : 0);
    flags |= (ui->HalfCurrent_1->isChecked() ? halfCurrentDec : 0);
    ahp_gt_set_mount_flags((GT1Flags)flags);
    ahp_gt_set_mount_type((MountType)mounttypes[ui->MountType->currentIndex()]);
    ahp_gt_set_features(0, (SkywatcherFeature)(features | ((ui->MountStyle->currentIndex() == 2) ? isAZEQ : 0) | (ui->HalfCurrent->isChecked() ? hasHalfCurrentTracking : 0)));
    ahp_gt_set_features(1, (SkywatcherFeature)(features | ((ui->MountStyle->currentIndex() == 2) ? isAZEQ : 0) | (ui->HalfCurrent->isChecked() ? hasHalfCurrentTracking : 0)));
    ahp_gt_set_pwm_frequency(ui->PWMFreq->value());
    ahp_gt_set_address(ui->Address->value());


    ui->MotorSteps_0->setValue(settings->value("MotorSteps_0", ahp_gt_get_motor_steps(0)).toInt());
    ui->Motor_0->setValue(settings->value("Motor_0", ahp_gt_get_motor_teeth(0)).toInt());
    ui->Worm_0->setValue(settings->value("Worm_0", ahp_gt_get_worm_teeth(0)).toInt());
    ui->Crown_0->setValue(settings->value("Crown_0", ahp_gt_get_crown_teeth(0)).toInt());
    ui->MaxSpeed_0->setValue(settings->value("MaxSpeed_0", ahp_gt_get_max_speed(0)).toInt());
    ui->Acceleration_0->setValue(settings->value("Acceleration_0",
                                 ui->Acceleration_0->maximum() - ahp_gt_get_acceleration_angle(0) * 1800.0 / M_PI).toInt());
    ui->Invert_0->setChecked(settings->value("Invert_0", ahp_gt_get_direction_invert(0) == 1).toBool());
    ui->Inductance_0->setValue(settings->value("Inductance_0", 10).toInt());
    ui->Resistance_0->setValue(settings->value("Resistance_0", 20000).toInt());
    ui->Current_0->setValue(settings->value("Current_0", 1000).toInt());
    ui->Voltage_0->setValue(settings->value("Voltage_0", 12).toInt());
    ui->GPIO_0->setCurrentIndex(settings->value("GPIO_0", ahp_gt_get_feature(0)).toInt());
    ui->Coil_0->setCurrentIndex(settings->value("Coil_0", ahp_gt_get_stepping_conf(0)).toInt());
    ui->TrackRate_0->setValue(-(settings->value("TimingValue_0", ahp_gt_get_timing(0)).toInt()-AHP_GT_ONE_SECOND)/AHP_GT_ONE_SECOND*10000.0);
    ui->SteppingMode_0->setCurrentIndex(settings->value("SteppingMode_0", ahp_gt_get_stepping_mode(0)).toInt());
    ui->Mean_0->setValue(settings->value("Mean_0", 1).toInt());

    ui->MotorSteps_1->setValue(settings->value("MotorSteps_1", ahp_gt_get_motor_steps(1)).toInt());
    ui->Motor_1->setValue(settings->value("Motor_1", ahp_gt_get_motor_teeth(1)).toInt());
    ui->Worm_1->setValue(settings->value("Worm_1", ahp_gt_get_worm_teeth(1)).toInt());
    ui->Crown_1->setValue(settings->value("Crown_1", ahp_gt_get_crown_teeth(1)).toInt());
    ui->MaxSpeed_1->setValue(settings->value("MaxSpeed_1", ahp_gt_get_max_speed(1)).toInt());
    ui->Acceleration_1->setValue(settings->value("Acceleration_1",
                                 ui->Acceleration_1->maximum() - ahp_gt_get_acceleration_angle(1) * 1800.0 / M_PI).toInt());
    ui->Invert_1->setChecked(settings->value("Invert_1", ahp_gt_get_direction_invert(1) == 1).toBool());
    ui->Inductance_1->setValue(settings->value("Inductance_1", 10).toInt());
    ui->Resistance_1->setValue(settings->value("Resistance_1", 20000).toInt());
    ui->Current_1->setValue(settings->value("Current_1", 1000).toInt());
    ui->Voltage_1->setValue(settings->value("Voltage_1", 12).toInt());
    ui->GPIO_1->setCurrentIndex(settings->value("GPIO_1", ahp_gt_get_feature(1)).toInt());
    ui->Coil_1->setCurrentIndex(settings->value("Coil_1", ahp_gt_get_stepping_conf(1)).toInt());
    ui->TrackRate_1->setValue(-(settings->value("TimingValue_1", ahp_gt_get_timing(1)).toInt()-AHP_GT_ONE_SECOND)/AHP_GT_ONE_SECOND*10000.0);
    ui->SteppingMode_1->setCurrentIndex(settings->value("SteppingMode_1", ahp_gt_get_stepping_mode(1)).toInt());
    ui->Mean_1->setValue(settings->value("Mean_1", 1).toInt());

    ahp_gt_set_motor_steps(0, ui->MotorSteps_0->value());
    ahp_gt_set_motor_teeth(0, ui->Motor_0->value());
    ahp_gt_set_worm_teeth(0, ui->Worm_0->value());
    ahp_gt_set_crown_teeth(0, ui->Crown_0->value());
    ahp_gt_set_direction_invert(0, ui->Invert_0->isChecked());
    ahp_gt_set_stepping_conf(0, (GT1SteppingConfiguration)ui->Coil_0->currentIndex());
    ahp_gt_set_timing(0, (GT1SteppingConfiguration)-ui->TrackRate_0->value()*AHP_GT_ONE_SECOND/10000+AHP_GT_ONE_SECOND);
    ahp_gt_set_stepping_mode(0, (GT1SteppingMode)ui->SteppingMode_0->currentIndex());
    switch(ui->GPIO_0->currentIndex())
    {
        case 0:
            ahp_gt_set_feature(0, GpioUnused);
            break;
        case 1:
            ahp_gt_set_feature(0, GpioAsST4);
            break;
        case 2:
            ahp_gt_set_feature(0, GpioAsPulseDrive);
            break;
        default:
            break;
    }

    ahp_gt_set_motor_steps(1, ui->MotorSteps_1->value());
    ahp_gt_set_motor_teeth(1, ui->Motor_1->value());
    ahp_gt_set_worm_teeth(1, ui->Worm_1->value());
    ahp_gt_set_crown_teeth(1, ui->Crown_1->value());
    ahp_gt_set_direction_invert(1, ui->Invert_1->isChecked());
    ahp_gt_set_stepping_conf(1, (GT1SteppingConfiguration)ui->Coil_1->currentIndex());
    ahp_gt_set_timing(1, (GT1SteppingConfiguration)-ui->TrackRate_1->value()*AHP_GT_ONE_SECOND/10000+AHP_GT_ONE_SECOND);
    ahp_gt_set_stepping_mode(1, (GT1SteppingMode)ui->SteppingMode_1->currentIndex());
    switch(ui->GPIO_1->currentIndex())
    {
        case 0:
            ahp_gt_set_feature(1, GpioUnused);
            break;
        case 1:
            ahp_gt_set_feature(1, GpioAsST4);
            break;
        case 2:
            ahp_gt_set_feature(1, GpioAsPulseDrive);
            break;
        default:
            break;
    }
    Ra = settings->value("Ra", 0).toDouble();
    Dec = settings->value("Dec", 0).toDouble();
    Latitude = settings->value("Latitude", 0).toDouble();
    Longitude = settings->value("Longitude", 0).toDouble();
    Altitude = settings->value("Altitude", 0).toDouble();
    Elevation = settings->value("Elevation", 0).toDouble();
    double* ra = toDms(Ra);
    double* dec = toDms(Dec);
    double* lat = toDms(Latitude);
    double* lon = toDms(Longitude);
    double* alt = toDms(Altitude);

    ui->Ra_0->setValue(ra[0]);
    ui->Dec_0->setValue(dec[0]);
    ui->Lat_0->setValue(lat[0]);
    ui->Lon_0->setValue(lon[0]);
    ui->Alt_0->setValue(alt[0]);
    ui->Ra_1->setValue(ra[1]);
    ui->Dec_1->setValue(dec[1]);
    ui->Lat_1->setValue(lat[1]);
    ui->Lon_1->setValue(lon[1]);
    ui->Alt_1->setValue(alt[1]);
    ui->Ra_2->setValue(ra[2]);
    ui->Dec_2->setValue(dec[2]);
    ui->Lat_2->setValue(lat[2]);
    ui->Lon_2->setValue(lon[2]);
    ui->Alt_2->setValue(alt[2]);
    ui->Elevation->setValue(Elevation);
    IndicationThread->unlock();
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

    settings->setValue("HalfCurrent_0", ui->HalfCurrent_0->isChecked());
    settings->setValue("Invert_0", ui->Invert_0->isChecked());
    settings->setValue("SteppingMode_0", ui->SteppingMode_0->currentIndex());
    settings->setValue("MotorSteps_0", ui->MotorSteps_0->value());
    settings->setValue("Worm_0", ui->Worm_0->value());
    settings->setValue("Motor_0", ui->Motor_0->value());
    settings->setValue("Crown_0", ui->Crown_0->value());
    settings->setValue("Acceleration_0", ui->Acceleration_0->value());
    settings->setValue("MaxSpeed_0", ui->MaxSpeed_0->value());
    settings->setValue("Coil_0", ui->Coil_0->currentIndex());
    settings->setValue("GPIO_0", ui->GPIO_0->currentIndex());
    settings->setValue("Inductance_0", ui->Inductance_0->value());
    settings->setValue("Resistance_0", ui->Resistance_0->value());
    settings->setValue("Current_0", ui->Current_0->value());
    settings->setValue("Voltage_0", ui->Voltage_0->value());
    settings->setValue("TimingValue_0", ui->TrackRate_0->value()*AHP_GT_ONE_SECOND/10000+AHP_GT_ONE_SECOND);
    settings->setValue("Mean_0", ui->Mean_0->value());

    settings->setValue("HalfCurrent_1", ui->HalfCurrent_1->isChecked());
    settings->setValue("Invert_1", ui->Invert_1->isChecked());
    settings->setValue("SteppingMode_1", ui->SteppingMode_1->currentIndex());
    settings->setValue("MotorSteps_1", ui->MotorSteps_1->value());
    settings->setValue("Worm_1", ui->Worm_1->value());
    settings->setValue("Motor_1", ui->Motor_1->value());
    settings->setValue("Crown_1", ui->Crown_1->value());
    settings->setValue("Acceleration_1", ui->Acceleration_1->value());
    settings->setValue("MaxSpeed_1", ui->MaxSpeed_1->value());
    settings->setValue("Coil_1", ui->Coil_1->currentIndex());
    settings->setValue("GPIO_1", ui->GPIO_1->currentIndex());
    settings->setValue("Inductance_1", ui->Inductance_1->value());
    settings->setValue("Resistance_1", ui->Resistance_1->value());
    settings->setValue("Current_1", ui->Current_1->value());
    settings->setValue("Voltage_1", ui->Voltage_1->value());
    settings->setValue("TimingValue_1", ui->TrackRate_1->value()*AHP_GT_ONE_SECOND/10000+AHP_GT_ONE_SECOND);
    settings->setValue("Mean_1", ui->Mean_1->value());

    settings->setValue("MountType", ui->MountType->currentIndex());
    settings->setValue("Address", ui->Address->value());
    settings->setValue("PWMFreq", ui->PWMFreq->value());
    settings->setValue("MountStyle", ui->MountStyle->currentIndex());
    settings->setValue("HalfCurrent", ui->HalfCurrent->isChecked());
    settings->setValue("HighBauds", ui->HighBauds->isChecked());
    settings->setValue("Notes", QString(ui->Notes->text().toUtf8().toBase64()));

    settings->setValue("Ra", Ra);
    settings->setValue("Dec", Dec);
    settings->setValue("Latitude", Latitude);
    settings->setValue("Longitude", Longitude);
    settings->setValue("Altitude", Altitude);
    settings->setValue("Elevation", Elevation);
    s->~QSettings();
    settings = oldsettings;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ahp_set_app_name((char*)"GT Configurator");
    ahp_set_debug_level(AHP_DEBUG_DEBUG);
    IndicationThread = new Thread(this, 100, 500);
    ProgressThread = new Thread(this, 100, 10);
    RaThread = new Thread(this, 1000, POSITION_THREAD_LOOP);
    DecThread = new Thread(this, 1000, POSITION_THREAD_LOOP);
    ServerThread = new Thread(this);
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
    stop_correction[0] = true;
    stop_correction[1] = true;
    settings = new QSettings(ini, QSettings::Format::IniFormat);
    isConnected = false;
    this->setFixedSize(1100, 700);
    ui->setupUi(this);
    QString lastPort = settings->value("LastPort", "").toString();
    if(lastPort != "")
        ui->ComPort->addItem(lastPort);
    ui->ComPort->addItem("localhost:11880");
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
        ui->Write->setEnabled(false);
        ui->Connection->setEnabled(false);
        finished = 0;
        if(ui->Write->text() == "Flash")
        {
            if(QFile::exists(firmwareFilename)) {
                QFile f(firmwareFilename);
                f.open(QIODevice::ReadOnly);
                if(!dfu_flash(f.handle(), &percent, &finished))
                {
                    settings->setValue("firmware", f.readAll().toBase64());
                }
                f.close();
            }
        }
        else
        {
            ahp_gt_write_values(0, &percent, &finished);
            ahp_gt_write_values(1, &percent, &finished);
            ui->Write->setEnabled(true);
            ui->WorkArea->setEnabled(true);
        }
        ui->Connection->setEnabled(true);
        percent = 0;
        thread->requestInterruption();
        thread->unlock();
    });
    connect(ServerThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), [ = ] (Thread * thread) {
        ahp_gt_set_aligned(1);
        threadsStopped = false;
        ahp_gt_start_synscan_server(11882, &threadsStopped);
        threadsStopped = true;
        thread->requestInterruption();
        thread->unlock();
    });
    connect(ui->LoadFW, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool checked)
    {
        QString url = "https://www.iliaplatone.com/firmware.php?product=gt1";
        if(DownloadFirmware(url, firmwareFilename, settings))
            ui->Write->setText("Flash");
        ui->Write->setEnabled(true);
        ui->Connection->setEnabled(false);
        ui->RA->setEnabled(false);
        ui->DEC->setEnabled(false);
        ui->Control->setEnabled(false);
        ui->commonSettings->setEnabled(false);
        ui->AdvancedRA->setEnabled(false);
        ui->AdvancedDec->setEnabled(false);

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
            if(!ahp_gt_connect(portname.toUtf8())) {
                failure = ahp_gt_detect_device();
            } else {
                ahp_gt_disconnect();
            }
        }
        if(!failure)
        {
            settings->setValue("LastPort", ui->ComPort->currentText());
            ui->Write->setText("Write");
            ui->Write->setEnabled(true);
            ahp_gt_read_values(0);
            ahp_gt_read_values(1);
            int flags = ahp_gt_get_mount_flags();
            ahp_gt_set_mount_flags((GT1Flags)flags);
            ui->LoadFW->setEnabled(false);
            ui->Connect->setEnabled(false);
            ui->Disconnect->setEnabled(true);
            ui->labelNotes->setEnabled(true);
            ui->Notes->setEnabled(true);
            ui->RA->setEnabled(true);
            ui->DEC->setEnabled(true);
            ui->Control->setEnabled(true);
            ui->commonSettings->setEnabled(true);
            ui->AdvancedRA->setEnabled(true);
            ui->AdvancedDec->setEnabled(true);
            ui->loadConfig->setEnabled(true);
            ui->saveConfig->setEnabled(true);
            ui->WorkArea->setEnabled(true);
            oldTracking[0] = false;
            isTracking[0] = false;
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
        ui->Write->setText("Write");
        ui->Write->setEnabled(false);
        ui->ComPort->setEnabled(true);
        isConnected = false;
        finished = false;
        ui->HighBauds->setChecked(false);
        ui->Server->setChecked(false);
        ui->LoadFW->setEnabled(true);
        ui->Connect->setEnabled(true);
        ui->Disconnect->setEnabled(false);
        ui->labelNotes->setEnabled(false);
        ui->Notes->setEnabled(false);
        ui->RA->setEnabled(false);
        ui->DEC->setEnabled(false);
        ui->Control->setEnabled(false);
        ui->commonSettings->setEnabled(false);
        ui->AdvancedRA->setEnabled(false);
        ui->AdvancedDec->setEnabled(false);
        ui->loadConfig->setEnabled(false);
        ui->saveConfig->setEnabled(false);
        ahp_gt_stop_motion(0, 0);
        ahp_gt_stop_motion(1, 0);
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
        /*
        switch(mounttype[index]) {
            case isEQ6:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(180);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(180);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isHEQ5:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(180);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(180);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isEQ5:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(144);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(144);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isEQ3:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(144);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(144);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isEQ8:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(360);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(360);
                ui->MountStyle->setCurrentIndex(0);
                break;
            default: break;
        }
        switch(mounttype[index+isAZEQ6]) {
            case isAZEQ6:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(180);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(180);
                ui->MountStyle->setCurrentIndex(2);
                break;
            case isAZEQ5:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(144);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(144);
                ui->MountStyle->setCurrentIndex(2);
                break;
        default:
            break;
        }
        switch(mounttype[index+isGT]) {
            case isGT:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(144);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(144);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isMF:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(144);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(144);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case is114GT:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(144);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(144);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isDOB:
                ui->Motor_0->setValue(10);
                ui->Worm_0->setValue(40);
                ui->Crown_0->setValue(100);
                ui->Motor_1->setValue(10);
                ui->Worm_1->setValue(40);
                ui->Crown_1->setValue(100);
                ui->MountStyle->setCurrentIndex(1);
                break;
            default:
                break;
        }*/
        ahp_gt_set_mount_type(mounttype[index]);
        saveIni(ini);
    });
    connect(ui->Invert_0, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked),
            [ = ](bool checked)
    {
        ahp_gt_set_direction_invert(0, checked);
        saveIni(ini);
    });
    connect(ui->Invert_1, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked),
            [ = ](bool checked)
    {
        ahp_gt_set_direction_invert(1, checked);
        saveIni(ini);
    });
    connect(ui->MotorSteps_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_steps(0, value);
        saveIni(ini);
    });
    connect(ui->Worm_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_worm_teeth(0, value);
        saveIni(ini);
    });
    connect(ui->Motor_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_teeth(0, value);
        saveIni(ini);
    });
    connect(ui->Crown_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_crown_teeth(0, value);
        saveIni(ini);
    });
    connect(ui->MotorSteps_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_steps(1, value);
        saveIni(ini);
    });
    connect(ui->Worm_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_worm_teeth(1, value);
        saveIni(ini);
    });
    connect(ui->Motor_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_teeth(1, value);
        saveIni(ini);
    });
    connect(ui->Crown_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_crown_teeth(1, value);
        saveIni(ini);
    });
    connect(ui->Acceleration_0, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_acceleration_angle(0, (ui->Acceleration_0->maximum() - ui->Acceleration_0->value()) * M_PI / 1800.0);
        ui->Acceleration_label_0->setText("Acceleration: " + QString::number((double)ui->Acceleration_0->maximum() / 10.0 - (double)value / 10.0) + "°");
        saveIni(ini);
    });
    connect(ui->Acceleration_1, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_acceleration_angle(1,  (ui->Acceleration_1->maximum() - ui->Acceleration_1->value()) * M_PI / 1800.0);
        ui->Acceleration_label_1->setText("Acceleration: " + QString::number((double)ui->Acceleration_1->maximum() / 10.0 - (double)value / 10.0) + "°");
        saveIni(ini);
    });
    connect(ui->MaxSpeed_0, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_max_speed(0, ui->MaxSpeed_0->value());
        saveIni(ini);
    });
    connect(ui->MaxSpeed_1, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_max_speed(1, ui->MaxSpeed_1->value());
        saveIni(ini);
    });
    connect(ui->SteppingMode_0, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_mode(0, (GT1SteppingMode)index);
        saveIni(ini);
    });
    connect(ui->SteppingMode_1, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_mode(1, (GT1SteppingMode)index);
        saveIni(ini);
    });
    connect(ui->Coil_0, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_conf(0, (GT1SteppingConfiguration)index);
        saveIni(ini);
    });
    connect(ui->Coil_1, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
            {
                ahp_gt_set_stepping_conf(1, (GT1SteppingConfiguration)index);
                saveIni(ini);
            });
    connect(ui->TrackRate_0, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged), [ = ] (int value)
    {
        ahp_gt_set_timing(0, AHP_GT_ONE_SECOND + AHP_GT_ONE_SECOND * -value / 10000.0);
        saveIni(ini);
    });
    connect(ui->TrackRate_1, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged), [ = ] (int value)
    {
        ahp_gt_set_timing(1, AHP_GT_ONE_SECOND + AHP_GT_ONE_SECOND * -value / 10000.0);
        saveIni(ini);
    });
    connect(ui->GPIO_0, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        switch(index)
        {
            case 0:
                ahp_gt_set_feature(0, GpioUnused);
                break;
            case 1:
                ahp_gt_set_feature(0, GpioAsST4);
                break;
            case 2:
                ahp_gt_set_feature(0, GpioAsPulseDrive);
                break;
            default:
                break;
        }
        saveIni(ini);
    });
    connect(ui->GPIO_1, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ]  (int index)
    {
        switch(index)
        {
            case 0:
                ahp_gt_set_feature(1, GpioUnused);
                break;
            case 1:
                ahp_gt_set_feature(1, GpioAsST4);
                break;
            case 2:
                ahp_gt_set_feature(1, GpioAsPulseDrive);
                break;
            default:
                break;
        }
        saveIni(ini);
    });
    connect(ui->Address, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_address(value);
        saveIni(ini);
    });
    connect(ui->HighBauds, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), [ = ] (bool checked)
    {
        int flags = (int)ahp_gt_get_mount_flags();
        flags &= ~bauds_115200;
        if(checked)
            flags |= bauds_115200;
        ahp_gt_set_mount_flags((GT1Flags)flags);
        saveIni(ini);
    });
    connect(ui->PWMFreq, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_pwm_frequency(value);
        ui->PWMFreq_label->setText("PWM: " + QString::number(1500 + 700 * value) + " Hz");
        saveIni(ini);
    });
    connect(ui->MountStyle, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            [ = ](int index)
    {
        int flags = (int)ahp_gt_get_mount_flags();
        if(index == 2) {
            ahp_gt_set_features(0, (SkywatcherFeature)(ahp_gt_get_features(0) | isAZEQ));
            ahp_gt_set_features(1, (SkywatcherFeature)(ahp_gt_get_features(1) | isAZEQ));
        } else {
            ahp_gt_set_features(0, (SkywatcherFeature)(ahp_gt_get_features(0) & ~isAZEQ));
            ahp_gt_set_features(1, (SkywatcherFeature)(ahp_gt_get_features(1) & ~isAZEQ));
        }
        flags &= ~isForkMount;
        ahp_gt_set_mount_flags((GT1Flags)(flags | (index == 1 ? isForkMount : 0)));
        saveIni(ini);
    });
    connect(ui->HalfCurrent, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), [ = ] (bool checked)
    {
        ahp_gt_set_features(0, (SkywatcherFeature)((ahp_gt_get_features(0) & ahp_gt_get_features(1) & ~hasHalfCurrentTracking) | (checked ? hasHalfCurrentTracking : 0)));
        ahp_gt_set_features(1, (SkywatcherFeature)((ahp_gt_get_features(0) & ahp_gt_get_features(1) & ~hasHalfCurrentTracking) | (checked ? hasHalfCurrentTracking : 0)));
        saveIni(ini);
    });
    connect(ui->HalfCurrent_0, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), [ = ] (bool checked)
    {
        int flags = (int)ahp_gt_get_mount_flags();
        flags &= ~halfCurrentRA;
        ahp_gt_set_mount_flags((GT1Flags)(flags | (checked ? halfCurrentRA : 0)));
        saveIni(ini);
    });
    connect(ui->HalfCurrent_1, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), [ = ] (bool checked)
    {
        int flags = (int)ahp_gt_get_mount_flags();
        flags &= ~halfCurrentDec;
        ahp_gt_set_mount_flags((GT1Flags)(flags | (checked ? halfCurrentDec : 0)));
        saveIni(ini);
    });
    connect(ui->Ra_Speed, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ui->Ra_Speed_label->setText("Ra speed: " + QString::number(value) + "x");
    });
    connect(ui->Dec_Speed, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ui->Dec_Speed_label->setText("Dec speed: " + QString::number(value) + "x");
    });
    connect(ui->W, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        isTracking[0] = false;
        ahp_gt_stop_motion(0, axisdirection[0] != true || axis_lospeed[0] != (fabs(ui->Ra_Speed->value()) < 128.0));
        ahp_gt_start_motion(0, ui->Ra_Speed->value());
        RaThread->setLoop(abs(POSITION_THREAD_LOOP/ui->Ra_Speed->value()));
        axisdirection[0] = true;
        axis_lospeed[0] = (fabs(ui->Ra_Speed->value()) < 128.0);
    });
    connect(ui->E, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        isTracking[0] = false;
        ahp_gt_stop_motion(0, axisdirection[0] != false || axis_lospeed[0] != (fabs(ui->Ra_Speed->value()) < 128.0));
        ahp_gt_start_motion(0, -ui->Ra_Speed->value());
        RaThread->setLoop(abs(POSITION_THREAD_LOOP/ui->Ra_Speed->value()));
        axisdirection[0] = false;
        axis_lospeed[0] = (fabs(ui->Ra_Speed->value()) < 128.0);
    });
    connect(ui->N, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        isTracking[1] = false;
        ahp_gt_stop_motion(1, axisdirection[1] != true || axis_lospeed[1] != (fabs(ui->Dec_Speed->value()) < 128.0));
        ahp_gt_start_motion(1, ui->Dec_Speed->value());
        DecThread->setLoop(abs(POSITION_THREAD_LOOP/ui->Dec_Speed->value()));
        axisdirection[1] = true;
        axis_lospeed[1] = (fabs(ui->Dec_Speed->value()) < 128.0);
    });
    connect(ui->S, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        isTracking[1] = false;
        ahp_gt_stop_motion(1, axisdirection[1] != false || axis_lospeed[1] != (fabs(ui->Dec_Speed->value()) < 128.0));
        ahp_gt_start_motion(1, -ui->Dec_Speed->value());
        DecThread->setLoop(abs(POSITION_THREAD_LOOP/ui->Dec_Speed->value()));
        axisdirection[1] = false;
        axis_lospeed[1] = (fabs(ui->Dec_Speed->value()) < 128.0);
    });
    connect(ui->NW, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        isTracking[0] = false;
        isTracking[1] = false;
        ahp_gt_stop_motion(0, axisdirection[0] != true || axis_lospeed[0] != (fabs(ui->Ra_Speed->value()) < 128.0));
        ahp_gt_start_motion(0, ui->Ra_Speed->value());
        ahp_gt_stop_motion(1, axisdirection[1] != true || axis_lospeed[1] != (fabs(ui->Dec_Speed->value()) < 128.0));
        ahp_gt_start_motion(1, ui->Dec_Speed->value());
        axisdirection[0] = true;
        axis_lospeed[0] = (fabs(ui->Ra_Speed->value()) < 128.0);
        axisdirection[1] = true;
        axis_lospeed[1] = (fabs(ui->Dec_Speed->value()) < 128.0);
    });
    connect(ui->NE, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        isTracking[0] = false;
        isTracking[1] = false;
        ahp_gt_stop_motion(0, axisdirection[0] != false || axis_lospeed[0] != (fabs(ui->Ra_Speed->value()) < 128.0));
        ahp_gt_start_motion(0, -ui->Ra_Speed->value());
        ahp_gt_stop_motion(1, axisdirection[1] != true || axis_lospeed[1] != (fabs(ui->Dec_Speed->value()) < 128.0));
        ahp_gt_start_motion(1, ui->Dec_Speed->value());
        axisdirection[0] = false;
        axis_lospeed[0] = (fabs(ui->Ra_Speed->value()) < 128.0);
        axisdirection[1] = true;
        axis_lospeed[1] = (fabs(ui->Dec_Speed->value()) < 128.0);
    });
    connect(ui->SW, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        isTracking[0] = false;
        isTracking[1] = false;
        ahp_gt_stop_motion(0, axisdirection[0] != true || axis_lospeed[0] != (fabs(ui->Ra_Speed->value()) < 128.0));
        ahp_gt_start_motion(0, ui->Ra_Speed->value());
        ahp_gt_stop_motion(1, axisdirection[1] != false || axis_lospeed[1] != (fabs(ui->Dec_Speed->value()) < 128.0));
        ahp_gt_start_motion(1, -ui->Dec_Speed->value());
        axisdirection[0] = true;
        axis_lospeed[0] = (fabs(ui->Ra_Speed->value()) < 128.0);
        axisdirection[1] = false;
        axis_lospeed[1] = (fabs(ui->Dec_Speed->value()) < 128.0);
    });
    connect(ui->SE, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        isTracking[0] = false;
        isTracking[1] = false;
        ahp_gt_stop_motion(0, axisdirection[0] != false || axis_lospeed[0] != (fabs(ui->Ra_Speed->value()) < 128.0));
        ahp_gt_start_motion(0, -ui->Ra_Speed->value());
        ahp_gt_stop_motion(1, axisdirection[1] != false || axis_lospeed[1] != (fabs(ui->Dec_Speed->value()) < 128.0));
        ahp_gt_start_motion(1, -ui->Dec_Speed->value());
        axisdirection[0] = false;
        axis_lospeed[0] = (fabs(ui->Ra_Speed->value()) < 128.0);
        axisdirection[1] = false;
        axis_lospeed[1] = (fabs(ui->Dec_Speed->value()) < 128.0);
    });
    connect(ui->Stop, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        oldTracking[0] = false;
        oldTracking[1] = false;
        ahp_gt_stop_motion(0, 0);
        ahp_gt_stop_motion(1, 0);
    });
    connect(ui->W, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(0, 0);
    });
    connect(ui->E, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(0, 0);
    });
    connect(ui->N, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(1, 0);
    });
    connect(ui->S, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(1, 0);
    });
    connect(ui->NW, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(0, 0);
        ahp_gt_stop_motion(1, 0);
    });
    connect(ui->NE, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(0, 0);
        ahp_gt_stop_motion(1, 0);
    });
    connect(ui->SW, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(0, 0);
        ahp_gt_stop_motion(1, 0);
    });
    connect(ui->SE, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(0, 0);
        ahp_gt_stop_motion(1, 0);
    });
    connect(ui->Tracking, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked),
    [ = ](bool checked)
    {
        oldTracking[0] = checked;
    });
    connect(ui->TotalSteps0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_totalsteps(0, value);
    });
    connect(ui->WormSteps0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_wormsteps(0, value);
    });
    connect(ui->Multiplier0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_multiplier(0, value);
    });
    connect(ui->Divider0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_divider(0, value);
    });
    connect(ui->TotalSteps1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_totalsteps(1, value);
    });
    connect(ui->WormSteps1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_wormsteps(1, value);
    });
    connect(ui->Multiplier1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_multiplier(1, value);
    });
    connect(ui->Divider1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_divider(1, value);
    });
    connect(ui->TuneRa, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), this,
            [ = ](bool checked)
    {
        stop_correction[0] = !checked;
        if(stop_correction[0]) {
            ui->TuneDec->setEnabled(true);
            ui->E->setEnabled(true);
            ui->W->setEnabled(true);
            ui->NE->setEnabled(true);
            ui->NW->setEnabled(true);
            ui->SE->setEnabled(true);
            ui->SW->setEnabled(true);
            ui->Stop->setEnabled(true);
            ui->Goto->setEnabled(true);
            ui->Tracking->setEnabled(true);
        } else {
            ui->TuneDec->setEnabled(false);
            ui->E->setEnabled(false);
            ui->W->setEnabled(false);
            ui->NE->setEnabled(false);
            ui->NW->setEnabled(false);
            ui->SE->setEnabled(false);
            ui->SW->setEnabled(false);
            ui->Stop->setEnabled(false);
            ui->Goto->setEnabled(false);
            ui->Tracking->setEnabled(false);
        }
    });
    connect(ui->TuneDec, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), this,
            [ = ](bool checked)
    {
        stop_correction[1] = !checked;
        if(stop_correction[1]) {
            ui->TuneRa->setEnabled(true);
            ui->S->setEnabled(true);
            ui->N->setEnabled(true);
            ui->NE->setEnabled(true);
            ui->NW->setEnabled(true);
            ui->SE->setEnabled(true);
            ui->SW->setEnabled(true);
            ui->Stop->setEnabled(true);
            ui->Goto->setEnabled(true);
        } else {
            ui->TuneRa->setEnabled(false);
            ui->S->setEnabled(false);
            ui->N->setEnabled(false);
            ui->NE->setEnabled(false);
            ui->NW->setEnabled(false);
            ui->SE->setEnabled(false);
            ui->SW->setEnabled(false);
            ui->Stop->setEnabled(false);
            ui->Goto->setEnabled(false);
        }
    });
    connect(ui->Write, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool checked = false)
    {
        oldTracking[0] = false;
        oldTracking[1] = false;
        if(ui->Write->text() == "Write")
        {
        }

        WriteThread->start();
    });
    connect(ui->Inductance_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Resistance_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Current_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Voltage_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Inductance_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Resistance_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Current_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Voltage_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        saveIni(ini);
    });
    connect(ui->Ra_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
    {
        if(value < 0)
            ui->Ra_0->setValue(23);
        if(value > 23)
            ui->Ra_0->setValue(0);
        Ra = fromHMSorDMS(QString::number(ui->Ra_0->value()) + ":" + QString::number(ui->Ra_1->value()) + ":" +
                          QString::number(ui->Ra_2->value()));
        saveIni(ini);
    });
    connect(ui->Ra_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
    {
        if(value < 0)
            ui->Ra_1->setValue(59);
        if(value > 59)
            ui->Ra_1->setValue(0);
        Ra = fromHMSorDMS(QString::number(ui->Ra_0->value()) + ":" + QString::number(ui->Ra_1->value()) + ":" +
                          QString::number(ui->Ra_2->value()));
        saveIni(ini);
    });
    connect(ui->Ra_2, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
    {
        if(value < 0)
            ui->Ra_2->setValue(59);
        if(value > 59)
            ui->Ra_2->setValue(0);
        Ra = fromHMSorDMS(QString::number(ui->Ra_0->value()) + ":" + QString::number(ui->Ra_1->value()) + ":" +
                          QString::number(ui->Ra_2->value()));
        saveIni(ini);
    });
    connect(ui->Dec_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
    {
        Dec = fromHMSorDMS(QString::number(ui->Dec_0->value()) + ":" + QString::number(ui->Dec_1->value()) + ":" +
                           QString::number(ui->Dec_2->value()));
        saveIni(ini);
    });
    connect(ui->Dec_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
    {
        if(value < 0)
            ui->Dec_1->setValue(59);
        if(value > 59)
            ui->Dec_1->setValue(0);
        Dec = fromHMSorDMS(QString::number(ui->Dec_0->value()) + ":" + QString::number(ui->Dec_1->value()) + ":" +
                           QString::number(ui->Dec_2->value()));
        saveIni(ini);
    });
    connect(ui->Dec_2, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
    {
        if(value < 0)
            ui->Dec_2->setValue(59);
        if(value > 59)
            ui->Dec_2->setValue(0);
        Dec = fromHMSorDMS(QString::number(ui->Dec_0->value()) + ":" + QString::number(ui->Dec_1->value()) + ":" +
                           QString::number(ui->Dec_2->value()));
        saveIni(ini);
    });
    connect(ui->Lat_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
    {
        Latitude = fromHMSorDMS(QString::number(ui->Lat_0->value()) + ":" + QString::number(
                                    ui->Lat_1->value()) + ":" + QString::number(ui->Lat_2->value()));
        saveIni(ini);
    });
    connect(ui->Lat_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
    {
        if(value < 0)
            ui->Lat_1->setValue(59);
        if(value > 59)
            ui->Lat_1->setValue(0);
        Latitude = fromHMSorDMS(QString::number(ui->Lat_0->value()) + ":" + QString::number(
                                    ui->Lat_1->value()) + ":" + QString::number(ui->Lat_2->value()));
        saveIni(ini);
    });
    connect(ui->Lat_2, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
    {
        if(value < 0)
            ui->Lat_2->setValue(59);
        if(value > 59)
            ui->Lat_2->setValue(0);
        Latitude = fromHMSorDMS(QString::number(ui->Lat_0->value()) + ":" + QString::number(
                                    ui->Lat_1->value()) + ":" + QString::number(ui->Lat_2->value()));
        saveIni(ini);
    });
    connect(ui->Lon_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
            {
                if(value < 0)
                    ui->Lon_0->setValue(359);
                if(value > 359)
                    ui->Lon_0->setValue(0);
                Longitude = fromHMSorDMS(QString::number(ui->Lon_0->value()) + ":" + QString::number(
                                             ui->Lon_1->value()) + ":" + QString::number(ui->Lon_2->value()));
                saveIni(ini);
            });
    connect(ui->Lon_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
            {
                if(value < 0)
                    ui->Lon_1->setValue(59);
                if(value > 59)
                    ui->Lon_1->setValue(0);
                Longitude = fromHMSorDMS(QString::number(ui->Lon_0->value()) + ":" + QString::number(
                                             ui->Lon_1->value()) + ":" + QString::number(ui->Lon_2->value()));
                saveIni(ini);
            });
    connect(ui->Lon_2, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
            {
                if(value < 0)
                    ui->Lon_2->setValue(59);
                if(value > 59)
                    ui->Lon_2->setValue(0);
                Longitude = fromHMSorDMS(QString::number(ui->Lon_0->value()) + ":" + QString::number(
                                             ui->Lon_1->value()) + ":" + QString::number(ui->Lon_2->value()));
                saveIni(ini);
            });
    connect(ui->Alt_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
            {
                if(value < 0)
                    ui->Alt_0->setValue(359);
                if(value > 359)
                    ui->Alt_0->setValue(0);
                Altitude = fromHMSorDMS(QString::number(ui->Alt_0->value()) + ":" + QString::number(
                                             ui->Alt_1->value()) + ":" + QString::number(ui->Alt_2->value()));
                saveIni(ini);
            });
    connect(ui->Alt_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
            {
                if(value < 0)
                    ui->Alt_1->setValue(59);
                if(value > 59)
                    ui->Alt_1->setValue(0);
                Altitude = fromHMSorDMS(QString::number(ui->Alt_0->value()) + ":" + QString::number(
                                             ui->Alt_1->value()) + ":" + QString::number(ui->Alt_2->value()));
                saveIni(ini);
            });
    connect(ui->Alt_2, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
            {
                if(value < 0)
                    ui->Alt_2->setValue(59);
                if(value > 59)
                    ui->Alt_2->setValue(0);
                Altitude = fromHMSorDMS(QString::number(ui->Alt_0->value()) + ":" + QString::number(
                                            ui->Alt_1->value()) + ":" + QString::number(ui->Alt_2->value()));
                saveIni(ini);
            });
    connect(ui->Elevation, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [ = ](int value)
            {
                Elevation = value;
                saveIni(ini);
            });
    connect(ui->Goto, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked), [ = ](bool checked)
    {
        isTracking[0] = false;
        isTracking[1] = false;
        ahp_gt_set_location(Latitude, Longitude, 0);
        double alt, az;
        ahp_gt_get_alt_az_coordinates(Ra, Dec, &alt, &az);
        if(alt > (Altitude * M_PI / 180) - acos(1/((Elevation / 6378137.0 + 1.0))))
            ahp_gt_goto_radec(Ra, Dec);
    });
    connect(ui->Halt, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked), [ = ](bool checked)
    {
        ahp_gt_stop_motion(0, 0);
        ahp_gt_stop_motion(1, 0);
    });
    connect(ui->Server, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), [ = ] (bool checked)
    {
        oldTracking[0] = false;
        oldTracking[1] = false;
        if(checked) {
            threadsStopped = false;
            ServerThread->start();
        }
        if(!threadsStopped && !checked) {
            threadsStopped = true;
        }
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
            for(int a = 0; a < 2; a++)
            {
                if(a == 0)
                {
                    ui->CurrentSteps_0->setText(QString::number((int)currentSteps[a]));
                    ui->Rate_0->setText("as/sec: " + QString::number(Speed[a]));
                }
                if(a == 1)
                {
                    ui->CurrentSteps_1->setText(QString::number(currentSteps[a]));
                    ui->Rate_1->setText("as/sec: " + QString::number(Speed[a]));
                }
                UpdateValues(a);
            }
        }

        parent->unlock();
    });
    connect(RaThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), [ = ] (Thread * parent)
    {
        int a = 0;
        if(isConnected && finished)
        {
            currentSteps[a] = ahp_gt_get_position(a, &status[a].timestamp) * ahp_gt_get_totalsteps(a) / M_PI / 2.0;
            if(oldTracking[a] && !isTracking[a]) {
                status[a] = ahp_gt_get_status(a);
                if(status[a].Running == 0) {
                    RaThread->setLoop(POSITION_THREAD_LOOP);
                    ahp_gt_start_tracking(a);
                    axis_lospeed[a] = true;
                    isTracking[a] = true;
                }
            }
            if(!oldTracking[a] && isTracking[a]) {
                ahp_gt_stop_motion(a, 0);
                isTracking[a] = false;
            }
            double diffTime = (double)status[a].timestamp-lastPollTime[a];
            lastPollTime[a] = status[a].timestamp;
            double diffSteps = currentSteps[a] - lastSteps[a];
            lastSteps[a] = currentSteps[a];
            diffSteps *= 360.0 * 60.0 * 60.0 / ahp_gt_get_totalsteps(a);
            Speed[a] = 0.0;
            int _n_speeds = 1;
            if(a == 0)
                _n_speeds = ui->Mean_0->value();
            else
                _n_speeds = ui->Mean_1->value();
            for(int s = 0; s < _n_speeds; s++)
            {
                if(s < _n_speeds - 1)
                    lastSpeeds[a][s] = lastSpeeds[a][s + 1];
                else
                    lastSpeeds[a][s] = diffSteps;
                Speed[a] += lastSpeeds[a][s];
            }
            Speed[a] /= _n_speeds * diffTime;
            if(!stop_correction[a]) {
                bool oldtracking = oldTracking[a];
                oldTracking[a] = false;
                isTracking[a] = false;
                ahp_gt_correct_tracking(a, SIDEREAL_DAY * ahp_gt_get_wormsteps(a) / ahp_gt_get_totalsteps(a), &stop_correction[a]);
                if(a == 0) {
                    if(ui->TuneRa->isChecked())
                        ui->TuneRa->click();
                } else {
                    if(ui->TuneDec->isChecked())
                        ui->TuneDec->click();
                }
                oldTracking[a] = oldtracking;
            }
        }
        parent->unlock();
    });
    connect(DecThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), [ = ] (Thread * parent)
    {
        int a = 1;
        if(isConnected && finished)
        {
            currentSteps[a] = ahp_gt_get_position(a, &status[a].timestamp) * ahp_gt_get_totalsteps(a) / M_PI / 2.0;
            if(oldTracking[a] && !isTracking[a]) {
                status[a] = ahp_gt_get_status(a);
                if(status[a].Running == 0) {
                    ahp_gt_start_tracking(a);
                    axis_lospeed[a] = true;
                    isTracking[a] = true;
                }
            }
            if(!oldTracking[a] && isTracking[a]) {
                ahp_gt_stop_motion(a, 0);
                isTracking[a] = false;
            }
            double diffTime = (double)status[a].timestamp-lastPollTime[a];
            lastPollTime[a] = status[a].timestamp;
            double diffSteps = currentSteps[a] - lastSteps[a];
            lastSteps[a] = currentSteps[a];
            diffSteps *= 360.0 * 60.0 * 60.0 / ahp_gt_get_totalsteps(a);
            Speed[a] = 0.0;
            int _n_speeds = 1;
            if(a == 0)
                _n_speeds = ui->Mean_0->value();
            else
                _n_speeds = ui->Mean_1->value();
            for(int s = 0; s < _n_speeds; s++)
            {
                if(s < _n_speeds - 1)
                    lastSpeeds[a][s] = lastSpeeds[a][s + 1];
                else
                    lastSpeeds[a][s] = diffSteps;
                Speed[a] += lastSpeeds[a][s];
            }
            Speed[a] /= _n_speeds * diffTime;
            if(!stop_correction[a]) {
                bool oldtracking = oldTracking[a];
                oldTracking[a] = false;
                isTracking[a] = false;
                ahp_gt_correct_tracking(a, SIDEREAL_DAY * ahp_gt_get_wormsteps(a) / ahp_gt_get_totalsteps(a), &stop_correction[a]);
                if(a == 0) {
                    if(ui->TuneRa->isChecked())
                        ui->TuneRa->click();
                } else {
                    if(ui->TuneDec->isChecked())
                        ui->TuneDec->click();
                }
                oldTracking[a] = oldtracking;
            }
        }
        parent->unlock();
    });
    RaThread->start();
    DecThread->start();
    ProgressThread->start();
}

MainWindow::~MainWindow()
{
    if(isConnected)
        ui->Disconnect->click();
    RaThread->stop();
    DecThread->stop();
    IndicationThread->stop();
    ProgressThread->stop();
    WriteThread->stop();
    ServerThread->stop();
    RaThread->wait();
    DecThread->wait();
    IndicationThread->wait();
    ProgressThread->wait();
    WriteThread->wait();
    ServerThread->wait();
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
    ui->HalfCurrent->blockSignals(block);
    ui->HighBauds->blockSignals(block);

    ui->HalfCurrent_0->blockSignals(block);
    ui->Invert_0->blockSignals(block);
    ui->MotorSteps_0->blockSignals(block);
    ui->Worm_0->blockSignals(block);
    ui->Motor_0->blockSignals(block);
    ui->Crown_0->blockSignals(block);
    ui->Acceleration_0->blockSignals(block);
    ui->MaxSpeed_0->blockSignals(block);
    ui->SteppingMode_0->blockSignals(block);
    ui->Coil_0->blockSignals(block);
    ui->GPIO_0->blockSignals(block);

    ui->HalfCurrent_1->blockSignals(block);
    ui->Invert_1->blockSignals(block);
    ui->MotorSteps_1->blockSignals(block);
    ui->Worm_1->blockSignals(block);
    ui->Motor_1->blockSignals(block);
    ui->Crown_1->blockSignals(block);
    ui->Acceleration_1->blockSignals(block);
    ui->MaxSpeed_1->blockSignals(block);
    ui->SteppingMode_1->blockSignals(block);
    ui->Coil_1->blockSignals(block);
    ui->GPIO_1->blockSignals(block);
}

void MainWindow::UpdateValues(int axis)
{
    disconnectControls(true);
    if(axis == 0)
    {
        double totalsteps = ahp_gt_get_totalsteps(0) * ahp_gt_get_divider(0) / ahp_gt_get_multiplier(0);
        ui->Divider0->setValue(ahp_gt_get_divider(0));
        ui->Multiplier0->setValue(ahp_gt_get_multiplier(0));
        ui->WormSteps0->setValue(ahp_gt_get_wormsteps(0));
        ui->TotalSteps0->setValue(ahp_gt_get_totalsteps(0));
        ui->TrackingFrequency_0->setText("Steps/s: " + QString::number(totalsteps / SIDEREAL_DAY));
        ui->SPT_0->setText("sec/turn: " + QString::number(SIDEREAL_DAY / (ahp_gt_get_crown_teeth(0)*ahp_gt_get_worm_teeth(
                               0) / ahp_gt_get_motor_teeth(0))));
        double L = (double)ui->Inductance_0->value() / 1000000.0;
        double R = (double)ui->Resistance_0->value() / 1000.0;
        double mI = (double)ui->Current_0->value() / 1000.0;
        double mV = (double)ui->Voltage_0->value();
        double Z = sqrt(fmax(0, pow(mV / mI, 2.0) - pow(R, 2.0)));
        double f = (2.0 * M_PI * Z / L);
        ui->PWMFrequency_0->setText("PWM Hz: " + QString::number(f));
        ui->GotoFrequency_0->setText("Goto Hz: " + QString::number(totalsteps * ahp_gt_get_max_speed(0) / SIDEREAL_DAY));
        ui->MotorSteps_0->setValue(ahp_gt_get_motor_steps(0));
        ui->Motor_0->setValue(ahp_gt_get_motor_teeth(0));
        ui->Worm_0->setValue(ahp_gt_get_worm_teeth(0));
        ui->Crown_0->setValue(ahp_gt_get_crown_teeth(0));
        ui->Acceleration_0->setValue(ui->Acceleration_0->maximum() - ahp_gt_get_acceleration_angle(0) * 1800.0 / M_PI);
        ui->MaxSpeed_0->setMaximum(2000);
        ui->Ra_Speed->setMaximum(2000);
        ui->MaxSpeed_0->setValue(ahp_gt_get_max_speed(0));
        ui->MaxSpeed_label_0->setText("Maximum speed: " + QString::number(ahp_gt_get_max_speed(0)) + "x");
        ui->Coil_0->setCurrentIndex(ahp_gt_get_stepping_conf(0));
        ui->SteppingMode_0->setCurrentIndex(ahp_gt_get_stepping_mode(0));
        ui->Invert_0->setChecked(ahp_gt_get_direction_invert(0));
        ui->HalfCurrent_0->setChecked(ahp_gt_get_mount_flags() & halfCurrentRA);
        ui->TrackRate_0->setValue(-(ahp_gt_get_timing(0)-AHP_GT_ONE_SECOND)*10000/AHP_GT_ONE_SECOND);
        ui->TrackRate_label_0->setText("Track Rate offset: " + QString::number((double)ui->TrackRate_0->value()/100.0) + "%");
    }
    else if (axis == 1)
    {
        double totalsteps = ahp_gt_get_totalsteps(1) * ahp_gt_get_divider(1) / ahp_gt_get_multiplier(1);
        ui->Divider1->setValue(ahp_gt_get_divider(1));
        ui->Multiplier1->setValue(ahp_gt_get_multiplier(1));
        ui->WormSteps1->setValue(ahp_gt_get_wormsteps(1));
        ui->TotalSteps1->setValue(ahp_gt_get_totalsteps(1));
        ui->TrackingFrequency_1->setText("Steps/s: " + QString::number(totalsteps / SIDEREAL_DAY));
        ui->SPT_1->setText("sec/turn: " + QString::number(SIDEREAL_DAY / (ahp_gt_get_crown_teeth(1)*ahp_gt_get_worm_teeth(
                               1) / ahp_gt_get_motor_teeth(1))));
        double L = (double)ui->Inductance_1->value() / 1000000.0;
        double R = (double)ui->Resistance_1->value() / 1000.0;
        double mI = (double)ui->Current_1->value() / 1000.0;
        double mV = (double)ui->Voltage_1->value();
        double Z = sqrt(fmax(0, pow(mV / mI, 2.0) - pow(R, 2.0)));
        double f = (2.0 * M_PI * Z / L);
        ui->PWMFrequency_1->setText("PWM Hz: " + QString::number(f));
        ui->GotoFrequency_1->setText("Goto Hz: " + QString::number(totalsteps * ahp_gt_get_max_speed(1) / SIDEREAL_DAY));
        ui->MotorSteps_1->setValue(ahp_gt_get_motor_steps(1));
        ui->Motor_1->setValue(ahp_gt_get_motor_teeth(1));
        ui->Worm_1->setValue(ahp_gt_get_worm_teeth(1));
        ui->Crown_1->setValue(ahp_gt_get_crown_teeth(1));
        ui->Acceleration_1->setValue(ui->Acceleration_1->maximum() - ahp_gt_get_acceleration_angle(1) * 1800.0 / M_PI);
        ui->MaxSpeed_1->setMaximum(2000);
        ui->Dec_Speed->setMaximum(2000);
        ui->MaxSpeed_1->setValue(ahp_gt_get_max_speed(1));
        ui->MaxSpeed_label_1->setText("Maximum speed: " + QString::number(ahp_gt_get_max_speed(1)) + "x");
        ui->Coil_1->setCurrentIndex(ahp_gt_get_stepping_conf(1));
        ui->SteppingMode_1->setCurrentIndex(ahp_gt_get_stepping_mode(1));
        ui->Invert_1->setChecked(ahp_gt_get_direction_invert(1));
        ui->HalfCurrent_1->setChecked(ahp_gt_get_mount_flags() & halfCurrentDec);
        ui->TrackRate_1->setValue(-(ahp_gt_get_timing(1)-AHP_GT_ONE_SECOND)*10000/AHP_GT_ONE_SECOND);
        ui->TrackRate_label_1->setText("Track Rate offset: " + QString::number((double)ui->TrackRate_1->value()/100.0) + "%");
    }
    switch(ahp_gt_get_feature(0))
    {
        case GpioUnused:
            ui->GPIO_0->setCurrentIndex(0);
            break;
        case GpioAsST4:
            ui->GPIO_0->setCurrentIndex(1);
            break;
        case GpioAsPulseDrive:
            ui->GPIO_0->setCurrentIndex(2);
            break;
        default:
            break;
    }
    switch(ahp_gt_get_feature(1))
    {
        case GpioUnused:
            ui->GPIO_1->setCurrentIndex(0);
            break;
        case GpioAsST4:
            ui->GPIO_1->setCurrentIndex(1);
            break;
        case GpioAsPulseDrive:
            ui->GPIO_1->setCurrentIndex(2);
            break;
        default:
            break;
    }
    ui->PWMFreq->setValue(ahp_gt_get_pwm_frequency());
    ui->PWMFreq_label->setText("PWM: " + QString::number(1500 + 700 * ui->PWMFreq->value()) + " Hz");
    ui->Address->setValue(ahp_gt_get_address());
    ui->MountType->setCurrentIndex(mounttypes.indexOf(ahp_gt_get_mount_type()));
    ui->HalfCurrent->setChecked((ahp_gt_get_features(0) & ahp_gt_get_features(1) & hasHalfCurrentTracking) == hasHalfCurrentTracking);
    int index = 0;
    index |= (((ahp_gt_get_features(0) & isAZEQ) != 0) ? 2 : 0);
    index |= (((ahp_gt_get_features(1) & isAZEQ) != 0) ? 2 : 0);
    if(!index) {
        index |= (((ahp_gt_get_mount_flags() & isForkMount) != 0) ? 1 : 0);
    }
    ui->MountStyle->setCurrentIndex(index);
    ui->HighBauds->setChecked((ahp_gt_get_mount_flags() & bauds_115200) != 0);
    disconnectControls(false);
}

QString MainWindow::toDMS(double dms)
{
    double d, m, s;
    dms = fabs(dms);
    d = floor(dms);
    dms -= d;
    dms *= 60.0;
    m = floor(dms);
    dms -= m;
    dms *= 60000.0;
    s = floor(dms) / 1000.0;
    return QString::number(d) + QString(":") + QString::number(m) + QString(":") + QString::number(s);
}

double* MainWindow::toDms(double d)
{
    double* dms = (double*)malloc(sizeof(double) * 3);
    dms[0] = floor(d);
    d -= dms[0];
    d *= 60.0;
    dms[1] = floor(d);
    d -= dms[1];
    d *= 60.0;
    dms[2] = d;
    return dms;
}

QString MainWindow::toHMS(double hms)
{
    double h, m, s;
    hms = fabs(hms);
    h = floor(hms);
    hms -= h;
    hms *= 60.0;
    m = floor(hms);
    hms -= m;
    hms *= 60000.0;
    s = floor(hms) / 1000.0;
    return QString::number(h) + QString(":") + QString::number(m) + QString(":") + QString::number(s);
}

double MainWindow::fromHMSorDMS(QString dms)
{
    double d;
    double m;
    double s;
    QStringList deg = dms.split(":");
    d = deg[0].toDouble();
    m = deg[1].toDouble() / 60.0 * (d < 0 ? -1 : 1);
    s = deg[2].toDouble() / 3600.0 * (d < 0 ? -1 : 1);
    return d + m + s;
}
