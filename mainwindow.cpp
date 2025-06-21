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

#define POSITION_THREAD_LOOP 250

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

QStringList MainWindow::CheckFirmware(QString url, int timeout_ms)
{
    QStringList firmware = QStringList();
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
    firmware = doc.toVariant().toStringList();
    for(int i = 0; i < firmware.length(); i++)
        firmware[i] = firmware[i].replace("firmware/", "").replace("-firmware.bin", "");
    return firmware;
ck_end:
    response->deleteLater();
    response->manager()->deleteLater();
    return firmware;
}

bool MainWindow::DownloadFirmware(QString url, QString jsonfile, QString filename, QSettings *settings, int timeout_ms)
{
    QByteArray bin;
    QFile file(filename);
    QFile f(jsonfile);
    QNetworkAccessManager* manager = new QNetworkAccessManager();
    QNetworkReply *response = manager->get(QNetworkRequest(QUrl(url)));
    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    connect(response, SIGNAL(finished()), &loop, SLOT(quit()));
    timer.start(timeout_ms);
    loop.exec();
    if(response->error() == QNetworkReply::NetworkError::NoError) {
        QByteArray json = response->readAll();
        if(json.length() == 0) goto dl_end;
        QJsonDocument doc = QJsonDocument::fromJson(json);
        if(doc.isEmpty()) goto dl_end;
        QJsonObject obj = doc.object();
        QString base64 = obj["data"].toString();
        if(base64.isNull() || base64.isEmpty()) {
            goto dl_end;
        }
        bin = QByteArray::fromBase64(base64.toUtf8());
        file.open(QIODevice::WriteOnly);
        file.write(bin, bin.length());
        file.close();
        f.open(QIODevice::WriteOnly);
        f.write(json, json.length());
        f.close();
    }
dl_end:
    response->deleteLater();
    response->manager()->deleteLater();
    if(!QFile::exists(filename)) return false;
    return true;
}

void MainWindow::readIni(QString ini)
{
    disconnectControls(true);
    finished = 0;
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

    ui->SetIndex->setValue(settings->value("Index", ahp_gt_get_address()).toInt());
    ui->PWMFreq->setValue(settings->value("PWMFreq", ahp_gt_get_pwm_frequency(axis_index)).toInt());
    ui->MountType->setCurrentIndex(settings->value("MountType", 0).toInt());
    ui->MountStyle->setCurrentIndex(settings->value("MountStyle", 0).toInt());
    ui->HighBauds->setChecked(settings->value("HighBauds", false).toBool());
    int flags = ahp_gt_get_mount_flags();
    int features = ahp_gt_get_features(axis_index);
    features &= ~(isAZEQ | hasHalfCurrentTracking);
    features |= hasCommonSlewStart;
    features |= (settings->value("HalfCurrentTracking", false).toBool() ? hasHalfCurrentTracking : 0);
    flags &= ~isForkMount;
    flags &= ~bauds_115200;
    flags |= ((ui->MountStyle->currentIndex() == 1) ? isForkMount : 0);
    flags |= (ui->HighBauds->isChecked() ? bauds_115200 : 0);
    ahp_gt_set_mount_flags((GTFlags)flags);
    ahp_gt_set_mount_type((MountType)mounttypes[ui->MountType->currentIndex()]);
    ahp_gt_set_features(axis_index, (SkywatcherFeature)(features | ((ui->MountStyle->currentIndex() == 2) ? isAZEQ : 0)));
    ahp_gt_set_pwm_frequency(axis_index, ui->PWMFreq->value());

    ui->MotorSteps->setValue(settings->value("MotorSteps", ahp_gt_get_motor_steps(axis_index)).toInt());
    ui->Motor->setValue(settings->value("Motor", ahp_gt_get_motor_teeth(axis_index)).toInt());
    ui->Worm->setValue(settings->value("Worm", ahp_gt_get_worm_teeth(axis_index)).toInt());
    ui->Crown->setValue(settings->value("Crown", ahp_gt_get_crown_teeth(axis_index)).toInt());
    ui->MaxSpeed->setValue(settings->value("MaxSpeed", ahp_gt_get_max_speed(axis_index)).toDouble() * SIDEREAL_DAY / M_PI / 2);
    ui->Acceleration->setValue(settings->value("Acceleration", ui->Acceleration->maximum() - ahp_gt_get_acceleration_angle(axis_index) * 1800.0 / M_PI).toInt());
    ui->Invert->setChecked(settings->value("Invert", ahp_gt_get_direction_invert(axis_index) == 1).toBool());
    ui->Inductance->setValue(settings->value("Inductance", 10).toInt());
    ui->Resistance->setValue(settings->value("Resistance", 20000).toInt());
    ui->Current->setValue(settings->value("Current", 1000).toInt());
    ui->Voltage->setValue(settings->value("Voltage", 12).toInt());
    ui->GPIO->setCurrentIndex(settings->value("GPIO", ahp_gt_get_feature(axis_index)).toInt());
    ui->Coil->setCurrentIndex(settings->value("Coil", ahp_gt_get_stepping_conf(axis_index)).toInt());
    ui->TrackRate->setValue(settings->value("TimingValue", 0).toInt());
    ui->SteppingMode->setCurrentIndex(settings->value("SteppingMode", ahp_gt_get_stepping_mode(axis_index)).toInt());
    ui->Mean->setValue(settings->value("Mean", 1).toInt());

    ahp_gt_set_motor_steps(axis_index, ui->MotorSteps->value());
    ahp_gt_set_motor_teeth(axis_index, ui->Motor->value());
    ahp_gt_set_worm_teeth(axis_index, ui->Worm->value());
    ahp_gt_set_crown_teeth(axis_index, ui->Crown->value());
    ahp_gt_set_direction_invert(axis_index, ui->Invert->isChecked());
    ahp_gt_set_stepping_conf(axis_index, (GTSteppingConfiguration)ui->Coil->currentIndex());
    ahp_gt_set_timing(axis_index, AHP_GT_ONE_SECOND-(double)ui->TrackRate->value()*AHP_GT_ONE_SECOND/(double)ui->TrackRate->maximum()/10.0);
    ahp_gt_set_stepping_mode(axis_index, (GTSteppingMode)ui->SteppingMode->currentIndex());
    switch(ui->GPIO->currentIndex())
    {
        case 0:
            ahp_gt_set_feature(axis_index, GpioUnused);
            break;
        case 1:
            ahp_gt_set_feature(axis_index, GpioAsST4);
            break;
        case 2:
            ahp_gt_set_feature(axis_index, GpioAsEncoder);
            break;
        case 3:
            ahp_gt_set_feature(axis_index, GpioAsPulseDrive);
            break;
        default:
            break;
    }
    disconnectControls(false);
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

    settings->setValue("MountType", ui->MountType->currentIndex());
    settings->setValue("Axis", axis_index);
    settings->setValue("Invert", ui->Invert->isChecked());
    settings->setValue("MotorSteps", ui->MotorSteps->value());
    settings->setValue("SetIndex", ui->SetIndex->value());
    settings->setValue("Worm", ui->Worm->value());
    settings->setValue("Motor", ui->Motor->value());
    settings->setValue("Crown", ui->Crown->value());
    settings->setValue("Acceleration", ui->Acceleration->value());
    settings->setValue("MaxSpeed", ui->MaxSpeed->value());
    settings->setValue("SteppingMode", ui->SteppingMode->currentIndex());
    settings->setValue("Coil", ui->Coil->currentIndex());
    settings->setValue("TrackRate", ui->TrackRate->value());
    settings->setValue("GPIO", ui->GPIO->currentIndex());
    settings->setValue("HighBauds", ui->HighBauds->isChecked());
    settings->setValue("IntensityLimiter", ui->IntensityLimiter->isChecked());
    settings->setValue("Intensity", ui->Intensity->value());
    settings->setValue("PWMFreq", ui->PWMFreq->value());
    settings->setValue("MountStyle", ui->MountStyle->currentIndex());
    settings->setValue("TotalSteps", ui->TotalSteps->value());
    settings->setValue("WormSteps", ui->WormSteps->value());
    settings->setValue("Multiplier", ui->Multiplier->value());
    settings->setValue("Divider", ui->Divider->value());
    settings->setValue("Inductance", ui->Inductance->value());
    settings->setValue("Resistance", ui->Resistance->value());
    settings->setValue("Current", ui->Current->value());
    settings->setValue("Voltage", ui->Voltage->value());

    s->~QSettings();
    settings = oldsettings;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ahp_set_app_name((char*)"GT Configurator");
    ahp_set_debug_level(AHP_DEBUG_DEBUG);
    UIThread = new Thread(this, 100, 50);
    ProgressThread = new Thread(this, 100, 150);
    PositionThread = new Thread(this, 1000, POSITION_THREAD_LOOP);
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
    stop_correction = true;
    settings = new QSettings(ini, QSettings::Format::IniFormat);
    isConnected = false;
    this->setFixedSize(650, 660);
    ui->setupUi(this);

    ui->SetAxis->addItem("Ra");
    ui->SetAxis->addItem("Dec");
    ui->SetAxis->addItem("Focus");
    ui->SetAxis->addItem("Filter");
    ui->SetAxis->addItem("Rotator");
    ui->SetAxis->addItem("Iris");
    ui->SetAxis->addItem("Shutter");
    ui->SetAxis->addItem("Dome");
    ui->SetAxis->addItem("Instrument");
    ui->SetAxis->addItem("TipX");
    ui->SetAxis->addItem("TipY");
    ui->SetAxis->addItem("TipZ");
    ui->SetAxis->addItem("TiltX");
    ui->SetAxis->addItem("TiltY");
    ui->SetAxis->addItem("TiltZ");
    ui->SetAxis->addItem("InstrumentX");
    ui->SetAxis->addItem("InstrumentY");
    ui->SetAxis->addItem("InstrumentZ");
    ui->SetAxis->addItem("InstrumentRotationX");
    ui->SetAxis->addItem("InstrumentRotationY");
    ui->SetAxis->addItem("InstrumentRotationZ");
    ui->SetAxis->addItem("PhasePrimaryX");
    ui->SetAxis->addItem("PhasePrimaryY");
    ui->SetAxis->addItem("PhasePrimaryZ");
    ui->SetAxis->addItem("PhaseSecondaryX");
    ui->SetAxis->addItem("PhaseSecondaryY");
    ui->SetAxis->addItem("PhaseSecondaryZ");
    ui->SetAxis->addItem("PhaseTertiaryX");
    ui->SetAxis->addItem("PhaseTertiaryY");
    ui->SetAxis->addItem("PhaseTertiaryZ");
    ui->SetAxis->addItem("FrequencyPrimaryX");
    ui->SetAxis->addItem("FrequencyPrimaryY");
    ui->SetAxis->addItem("FrequencyPrimaryZ");
    ui->SetAxis->addItem("FrequencySecondaryX");
    ui->SetAxis->addItem("FrequencySecondaryY");
    ui->SetAxis->addItem("FrequencySecondaryZ");
    ui->SetAxis->addItem("FrequencyTertiaryX");
    ui->SetAxis->addItem("FrequencyTertiaryY");
    ui->SetAxis->addItem("FrequencyTertiaryZ");
    ui->SetAxis->addItem("PCMPrimaryX");
    ui->SetAxis->addItem("PCMPrimaryY");
    ui->SetAxis->addItem("PCMPrimaryZ");
    ui->SetAxis->addItem("PCMSecondaryX");
    ui->SetAxis->addItem("PCMSecondaryY");
    ui->SetAxis->addItem("PCMSecondaryZ");
    ui->SetAxis->addItem("PCMTertiaryX");
    ui->SetAxis->addItem("PCMTertiaryY");
    ui->SetAxis->addItem("PCMTertiaryZ");
    ui->SetAxis->addItem("PlaneX");
    ui->SetAxis->addItem("PlaneY");
    ui->SetAxis->addItem("PlaneZ");
    ui->SetAxis->addItem("RailX");
    ui->SetAxis->addItem("RailY");
    ui->SetAxis->addItem("RailZ");

    ui->Axis->addItem("Ra");
    ui->Axis->addItem("Dec");
    ui->Axis->addItem("Focus");
    ui->Axis->addItem("Filter");
    ui->Axis->addItem("Rotator");
    ui->Axis->addItem("Iris");
    ui->Axis->addItem("Shutter");
    ui->Axis->addItem("Dome");
    ui->Axis->addItem("Instrument");
    ui->Axis->addItem("TipX");
    ui->Axis->addItem("TipY");
    ui->Axis->addItem("TipZ");
    ui->Axis->addItem("TiltX");
    ui->Axis->addItem("TiltY");
    ui->Axis->addItem("TiltZ");
    ui->Axis->addItem("InstrumentX");
    ui->Axis->addItem("InstrumentY");
    ui->Axis->addItem("InstrumentZ");
    ui->Axis->addItem("InstrumentRotationX");
    ui->Axis->addItem("InstrumentRotationY");
    ui->Axis->addItem("InstrumentRotationZ");
    ui->Axis->addItem("PhasePrimaryX");
    ui->Axis->addItem("PhasePrimaryY");
    ui->Axis->addItem("PhasePrimaryZ");
    ui->Axis->addItem("PhaseSecondaryX");
    ui->Axis->addItem("PhaseSecondaryY");
    ui->Axis->addItem("PhaseSecondaryZ");
    ui->Axis->addItem("PhaseTertiaryX");
    ui->Axis->addItem("PhaseTertiaryY");
    ui->Axis->addItem("PhaseTertiaryZ");
    ui->Axis->addItem("FrequencyPrimaryX");
    ui->Axis->addItem("FrequencyPrimaryY");
    ui->Axis->addItem("FrequencyPrimaryZ");
    ui->Axis->addItem("FrequencySecondaryX");
    ui->Axis->addItem("FrequencySecondaryY");
    ui->Axis->addItem("FrequencySecondaryZ");
    ui->Axis->addItem("FrequencyTertiaryX");
    ui->Axis->addItem("FrequencyTertiaryY");
    ui->Axis->addItem("FrequencyTertiaryZ");
    ui->Axis->addItem("PCMPrimaryX");
    ui->Axis->addItem("PCMPrimaryY");
    ui->Axis->addItem("PCMPrimaryZ");
    ui->Axis->addItem("PCMSecondaryX");
    ui->Axis->addItem("PCMSecondaryY");
    ui->Axis->addItem("PCMSecondaryZ");
    ui->Axis->addItem("PCMTertiaryX");
    ui->Axis->addItem("PCMTertiaryY");
    ui->Axis->addItem("PCMTertiaryZ");
    ui->Axis->addItem("PlaneX");
    ui->Axis->addItem("PlaneY");
    ui->Axis->addItem("PlaneZ");
    ui->Axis->addItem("RailX");
    ui->Axis->addItem("RailY");
    ui->Axis->addItem("RailZ");
    ui->Axis->setCurrentIndex(-1);
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
        QStringList firmwarelist = CheckFirmware("https://www.iliaplatone.com/firmware.php?product=gt*");
        ui->Firmware->clear();
        if(firmwarelist.length() > 0)
            ui->Firmware->addItems(firmwarelist);
        else {
            ui->Firmware->addItem("Firmware");
        }
    });
    connect(ui->Firmware, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
    [ = ](bool checked)
    {
        QString selectedfirmware = ui->Firmware->currentText();
        QString url = "https://www.iliaplatone.com/firmware.php?download=on&product="+selectedfirmware;
        QString jsonfile = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0)+"/"+selectedfirmware+".json";
        if(DownloadFirmware(url, jsonfile, firmwareFilename, settings)) {
            ui->Write->setText("Flash");
            ui->Write->setEnabled(true);
        } else if(DownloadFirmware(jsonfile, jsonfile, firmwareFilename, settings)) {
            ui->Write->setText("Flash");
            ui->Write->setEnabled(true);
        } else if(DownloadFirmware("qrc:///data/"+selectedfirmware+".json", jsonfile, firmwareFilename, settings)) {
            ui->Write->setText("Flash");
            ui->Write->setEnabled(true);
        }
        ui->Connection->setEnabled(false);
        ui->WorkArea->setEnabled(false);

    });
    connect(ui->Disconnect, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked), this, &MainWindow::Disconnect);
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
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(180);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isHEQ5:
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(180);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isEQ5:
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(144);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isEQ3:
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(144);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isEQ8:
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(360);
                ui->MountStyle->setCurrentIndex(0);
                break;
            default: break;
        }
        switch(mounttype[index+isAZEQ6]) {
            case isAZEQ6:
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(180);
                ui->MountStyle->setCurrentIndex(2);
                break;
            case isAZEQ5:
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(144);
                ui->MountStyle->setCurrentIndex(2);
                break;
        default:
            break;
        }
        switch(mounttype[index+isGT]) {
            case isGT:
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(144);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isMF:
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(144);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case is114GT:
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(144);
                ui->MountStyle->setCurrentIndex(0);
                break;
            case isDOB:
                ui->Motor->setValue(10);
                ui->Worm->setValue(40);
                ui->Crown->setValue(100);
                ui->MountStyle->setCurrentIndex(1);
                break;
            default:
                break;
        }*/
        ahp_gt_set_mount_type(mounttype[index]);
        saveIni(ini);
    });
    connect(ui->Axis, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
    [ = ](int value)
    {
        Disconnect();
        ahp_gt_set_axes_limit(NumAxes);
        num_axes = ahp_gt_get_axes_limit();
        axis_index = value;
        Connect();
    });
    connect(ui->SetAxis, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
    [ = ](int value)
    {
        if(mountversion == 0x538)
            ahp_gt_move_axis(axis_index, value);
    });
    connect(ui->Invert, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked),
            [ = ](bool checked)
    {
        ahp_gt_set_direction_invert(axis_index, checked);
        saveIni(ini);
    });
    connect(ui->MotorSteps, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_motor_steps(axis_index, value);
        saveIni(ini);
    });
    connect(ui->SetIndex, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int index)
    {
        int address = ui->SetIndex->value();
        ahp_gt_set_address(address);
        saveIni(ini);
    });
    connect(ui->Worm, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_worm_teeth(axis_index, value);
        saveIni(ini);
    });
    connect(ui->Motor, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_teeth(axis_index, value);
        saveIni(ini);
    });
    connect(ui->Crown, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_crown_teeth(axis_index, value);
        saveIni(ini);
    });
    connect(ui->Acceleration, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_acceleration_angle(axis_index, (ui->Acceleration->maximum() - ui->Acceleration->value()) * M_PI / 1800.0);
        ui->Acceleration_label->setText("Acceleration: " + QString::number((double)(ui->Acceleration->maximum() - ui->Acceleration->value()) / 10) + "°");
        saveIni(ini);
    });
    connect(ui->MaxSpeed, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_max_speed(axis_index, ui->MaxSpeed->value() * M_PI * 2 / SIDEREAL_DAY);
        ui->MaxSpeed_label->setText("Maximum speed: " + QString::number(ahp_gt_get_max_speed(axis_index) * SIDEREAL_DAY / M_PI / 2) + "x");
        saveIni(ini);
    });
    connect(ui->SteppingMode, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_mode(axis_index, (GTSteppingMode)index);
        saveIni(ini);
    });
    connect(ui->Coil, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_conf(axis_index, (GTSteppingConfiguration)index);
        saveIni(ini);
    });
    connect(ui->TrackRate, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged), [ = ] (int value)
    {
        ahp_gt_set_timing(axis_index, AHP_GT_ONE_SECOND-(double)value*AHP_GT_ONE_SECOND/(double)ui->TrackRate->maximum()/10.0);
        ui->TrackRate_label->setText("Track Rate offset: " + QString::number((double)ui->TrackRate->value()/(double)ui->TrackRate->maximum()/10.0) + "%");
        saveIni(ini);
    });
    connect(ui->GPIO, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        switch(index)
        {
            case 0:
                ahp_gt_set_feature(axis_index, GpioUnused);
                break;
            case 1:
                ahp_gt_set_feature(axis_index, GpioAsST4);
                break;
            case 2:
                ahp_gt_set_feature(axis_index, GpioAsPulseDrive);
                break;
            default:
                break;
        }
        saveIni(ini);
    });
    connect(ui->HighBauds, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), [ = ] (bool checked)
    {
        int flags = (int)ahp_gt_get_mount_flags();
        flags &= ~bauds_115200;
        if(checked)
            flags |= bauds_115200;
        ahp_gt_set_mount_flags((GTFlags)flags);
        saveIni(ini);
    });
    connect(ui->IntensityLimiter, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), [ = ](bool checked)
    {
        ahp_gt_limit_intensity(axis_index, ui->IntensityLimiter->isChecked());
        saveIni(ini);
    });
    connect(ui->Intensity, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged), [ = ](int value)
    {
        ahp_gt_set_intensity_limit(axis_index, ui->Intensity->value());
        ui->IntensityLimiter_label->setText("Current offset: " + QString::number(ui->Intensity->value() * 100 / ui->Intensity->maximum()) + "%");
        saveIni(ini);
    });
    connect(ui->PWMFreq, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged), [ = ](int value)
    {
        ahp_gt_set_pwm_frequency(axis_index, ui->PWMFreq->value());
        ui->PWMFreq_label->setText("PWM: " + QString::number(round(366.2109375 * (ahp_gt_get_pwm_frequency(axis_index) + 1))) + " Hz");
        saveIni(ini);
    });
    connect(ui->MountStyle, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            [ = ](int index)
    {
        int flags = (int)ahp_gt_get_mount_flags();
        if(index == 2) {
            ahp_gt_set_features(axis_index, (SkywatcherFeature)(ahp_gt_get_features(axis_index) | isAZEQ));
        } else {
            ahp_gt_set_features(axis_index, (SkywatcherFeature)(ahp_gt_get_features(axis_index) & ~isAZEQ));
        }
        flags &= ~isForkMount;
        ahp_gt_set_mount_flags((GTFlags)(flags | (index == 1 ? isForkMount : 0)));
        saveIni(ini);
    });
    connect(ui->Speed, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ui->Speed_label->setText("speed: " + QString::number(value) + "x");
    });
    connect(ui->Plus, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        isTracking = false;
        ahp_gt_stop_motion(axis_index, axisdirection != true || axis_lospeed != (fabs(ui->Speed->value()) < 128.0));
        ahp_gt_start_motion(axis_index, ui->Speed->value() * M_PI * 2 / SIDEREAL_DAY);
        axisdirection = true;
        axis_lospeed = (fabs(ui->Speed->value()) < 128.0);
    });
    connect(ui->Minus, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        isTracking = false;
        ahp_gt_stop_motion(axis_index, axisdirection != true || axis_lospeed != (fabs(ui->Speed->value()) < 128.0));
        ahp_gt_start_motion(axis_index, -ui->Speed->value() * M_PI * 2 / SIDEREAL_DAY);
        axisdirection = true;
        axis_lospeed = (fabs(ui->Speed->value()) < 128.0);
    });
    connect(ui->Stop, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        oldTracking = false;
        ahp_gt_stop_motion(axis_index, 0);
    });
    connect(ui->Minus, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(axis_index, 0);
    });
    connect(ui->Plus, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(axis_index, 0);
    });
    connect(ui->Tracking, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked),
    [ = ](bool checked)
    {
        oldTracking = checked;
    });
    connect(ui->TotalSteps, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_totalsteps(axis_index, value);
    });
    connect(ui->WormSteps, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_wormsteps(axis_index, value);
    });
    connect(ui->Multiplier, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_multiplier(axis_index, value);
    });
    connect(ui->Divider, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [ = ](int value)
    {
        ahp_gt_set_divider(axis_index, value);
    });
    connect(ui->FixTracking, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked),
            [ = ](bool checked)
    {
        stop_correction = checked;
    });
    connect(ui->Write, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool checked = false)
    {
        if(finished)
            percent = finished = 0;
        if(percent == 0 && finished == 0) {
            saveIni(getDefaultIni());
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
            else if(ui->Write->text() == "Write")
            {
                if(isConnected) {
                    ahp_gt_write_values(axis_index, &percent, &finished);
                    axis_index = ui->SetAxis->currentIndex();
                    ahp_gt_read_values(axis_index);
                    emit reload_values();
                }
            }
        }
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
    connect(ui->Server, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked), [ = ] (bool checked)
    {
        oldTracking = false;
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
    connect(this, static_cast<void (MainWindow::*)()>(&MainWindow::reload_values), this, &MainWindow::ReadValues);
    connect(UIThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), [ = ](Thread *parent) { emit update_ui(); parent->unlock(); });
    connect(this, static_cast<void (MainWindow::*)()>(&MainWindow::update_ui), this, &MainWindow::UiThread);
    connect(PositionThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), [ = ] (Thread * parent)
    {
        int a = axis_index;
        if(isConnected && finished)
        {
            currentSteps = ahp_gt_get_position(a, &status.timestamp) * ahp_gt_get_totalsteps(a) / M_PI / 2.0;
            if(oldTracking && !isTracking) {
                status = ahp_gt_get_status(a);
                if(status.Running == 0) {
                    parent->setLoop(POSITION_THREAD_LOOP);
                    ahp_gt_start_tracking(a);
                    axis_lospeed = true;
                    isTracking = true;
                }
            }
            if(!oldTracking && isTracking) {
                ahp_gt_stop_motion(a, 0);
                isTracking = false;
            }
            double diffTime = (double)status.timestamp-lastPollTime;
            lastPollTime = status.timestamp;
            double diffSteps = currentSteps - lastSteps;
            lastSteps = currentSteps;
            diffSteps *= 360.0 * 60.0 * 60.0 / ahp_gt_get_totalsteps(a);
            Speed = 0.0;
            int _n_speeds = 1;
            if(a == axis_index)
                _n_speeds = ui->Mean->value();
            for(int s = 0; s < _n_speeds; s++)
            {
                if(s < _n_speeds - 1)
                    lastSpeeds[s] = lastSpeeds[s + 1];
                else
                    lastSpeeds[s] = diffSteps;
                Speed += lastSpeeds[s];
            }
            Speed /= _n_speeds * diffTime;
            if(!stop_correction) {
                bool oldtracking = oldTracking;
                oldTracking = false;
                isTracking = false;
                ahp_gt_correct_tracking(a, SIDEREAL_DAY * ahp_gt_get_wormsteps(a) / ahp_gt_get_totalsteps(a), &stop_correction);
                oldTracking = oldtracking;
            }
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
    ProgressThread->stop();
    UIThread->stop();
    ServerThread->stop();
    PositionThread->wait();
    ProgressThread->stop();
    UIThread->wait();
    ServerThread->wait();
    if(QFile(firmwareFilename).exists())
        unlink(firmwareFilename.toUtf8());
    threadsStopped = true;
    delete ui;
}

void MainWindow::disconnectControls(bool block)
{
    ui->Acceleration->blockSignals(block);
    ui->Axis->blockSignals(block);
    ui->Coil->blockSignals(block);
    ui->Crown->blockSignals(block);
    ui->Current->blockSignals(block);
    ui->GPIO->blockSignals(block);
    ui->HighBauds->blockSignals(block);
    ui->Intensity->blockSignals(block);
    ui->IntensityLimiter->blockSignals(block);
    ui->Invert->blockSignals(block);
    ui->MaxSpeed->blockSignals(block);
    ui->MaxSpeed_label->blockSignals(block);
    ui->Motor->blockSignals(block);
    ui->MotorSteps->blockSignals(block);
    ui->MountStyle->blockSignals(block);
    ui->MountType->blockSignals(block);
    ui->PWMFreq->blockSignals(block);
    ui->SetIndex->blockSignals(block);
    ui->Speed->blockSignals(block);
    ui->SteppingMode->blockSignals(block);
    ui->TrackRate->blockSignals(block);
    ui->TrackRate_label->blockSignals(block);
    ui->Worm->blockSignals(block);
}

void MainWindow::ReadValues()
{
    disconnectControls(true);
    ui->MotorSteps->setValue(ahp_gt_get_motor_steps(axis_index));
    ui->Motor->setValue(ahp_gt_get_motor_teeth(axis_index));
    ui->Worm->setValue(ahp_gt_get_worm_teeth(axis_index));
    ui->Crown->setValue(ahp_gt_get_crown_teeth(axis_index));
    ui->Acceleration->setValue(ui->Acceleration->maximum() - ahp_gt_get_acceleration_angle(axis_index) * 1800.0 / M_PI);
    ui->PWMFreq->setValue(ahp_gt_get_pwm_frequency(axis_index));
    double speedlimit = 800;
    ui->MaxSpeed->setValue(ahp_gt_get_max_speed(axis_index) * SIDEREAL_DAY / M_PI / 2);
    ui->Coil->setCurrentIndex(ahp_gt_get_stepping_conf(axis_index));
    ui->SteppingMode->setCurrentIndex(ahp_gt_get_stepping_mode(axis_index));
    ui->Invert->setChecked(ahp_gt_get_direction_invert(axis_index));
    ui->TrackRate->setValue(-((double)ahp_gt_get_timing(axis_index)-AHP_GT_ONE_SECOND)*(double)ui->TrackRate->maximum()*10.0/AHP_GT_ONE_SECOND);
    switch(ahp_gt_get_feature(axis_index))
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
    if((mountversion & 0xff) != 0x37) {
        ui->IntensityLimiter_label->setEnabled(true);
        ui->IntensityLimiter->setEnabled(true);
        ui->Intensity->setEnabled(true);
        ui->IntensityLimiter->setChecked(ahp_gt_is_intensity_limited(axis_index));
        ui->Intensity->setValue(ahp_gt_get_intensity_limit(axis_index));
    } else {
        ui->IntensityLimiter_label->setEnabled(false);
        ui->IntensityLimiter->setEnabled(false);
        ui->Intensity->setEnabled(false);
    }
    if(mountversion == 0x538) {
        ui->GPIO->setEnabled(false);
        ui->SetAxis_label->setEnabled(true);
        ui->SetAxis->setEnabled(true);
    } else {
        ui->GPIO->setEnabled(true);
        ui->SetAxis_label->setEnabled(false);
        ui->SetAxis->setEnabled(false);
    }
    ui->SetIndex->setValue(ahp_gt_get_address());
    ui->MountType->setCurrentIndex(mounttypes.indexOf(ahp_gt_get_mount_type()));
    ui->HighBauds->setChecked((ahp_gt_get_mount_flags() & bauds_115200) != 0);
    int index = 0;
    if(mountversion != 0x538) {
        index |= (((ahp_gt_get_features(0) & isAZEQ) != 0) ? 1 : 0);
        index |= (((ahp_gt_get_features(1) & isAZEQ) != 0) ? 1 : 0);
        index |= (((ahp_gt_get_mount_flags() & isForkMount) != 0) ? 2 : 0);
    }
    ui->MountStyle->setCurrentIndex(index);
    disconnectControls(false);
}

void MainWindow::UiThread() {
    ///////////
    bool oldstate = false;
    oldstate = stop_correction;
    if(oldstate != ui->FixTracking->isChecked()) {
        if(!oldstate) {
            ui->Minus->setEnabled(true);
            ui->Plus->setEnabled(true);
            ui->Stop->setEnabled(true);
        } else if(oldstate){
            ui->Minus->setEnabled(oldstate);
            ui->Plus->setEnabled(oldstate);
            ui->Stop->setEnabled(oldstate);
        }
    }

    ///////////
    oldstate = finished;
    if(isConnected)
    {
        if(!oldstate) {
            ui->Write->setEnabled(false);
            ui->Connection->setEnabled(false);
            ui->WorkArea->setEnabled(false);
        } else if(oldstate){
            ui->Write->setEnabled(oldstate);
            ui->Connection->setEnabled(oldstate);
            ui->WorkArea->setEnabled(oldstate);
        }
    }
    ///////////
    if(isConnected && finished)
    {
        ui->CurrentSteps->setText(QString::number((int)currentSteps));
        ui->Rate->setText("as/sec: " + QString::number(Speed));
        double totalsteps = ahp_gt_get_totalsteps(axis_index) * ahp_gt_get_divider(axis_index);
        double microsteps = ahp_gt_get_multiplier(axis_index);
        ui->Divider->setValue(ahp_gt_get_divider(axis_index));
        ui->Multiplier->setValue(microsteps);
        ui->WormSteps->setValue(ahp_gt_get_wormsteps(axis_index));
        ui->TotalSteps->setValue(ahp_gt_get_totalsteps(axis_index));
        if(ahp_gt_get_tracking_mode() == HalfStep)
            ui->TrackingFrequency->setText("Steps/s: " + QString::number(totalsteps / SIDEREAL_DAY));
        else
            ui->TrackingFrequency->setText("Steps/s: " + QString::number(totalsteps / microsteps / SIDEREAL_DAY));
        ui->SPT->setText("sec/turn: " + QString::number(SIDEREAL_DAY / (ahp_gt_get_crown_teeth(axis_index)*ahp_gt_get_worm_teeth(axis_index) / ahp_gt_get_motor_teeth(axis_index))));
        double L = (double)ui->Inductance->value() / 1000000.0;
        double R = (double)ui->Resistance->value() / 1000.0;
        double I = (double)ui->Current->value() / 1000.0;
        double V = (double)ui->Voltage->value();
        double omega_current = fabs(M_PI*2*Speed/360/60/60);
        double omega_kilo = fabs(M_PI*2*1000/(totalsteps/microsteps));
        double L_step = I/omega_kilo;
        ui->omega->setText("ω: " + QString::number(currentSteps*M_PI*2/ahp_gt_get_totalsteps(axis_index)));
        ui->Maxtorque->setText("DC torque: " + QString::number(V/R));
        ui->Mintorque->setText("Reference torque: " + QString::number(V/pow((pow(L_step*omega_kilo, 2)+pow(R, 2)), 0.5)));
        ui->Currenttorque->setText("Current torque: " + QString::number(V/pow((pow(L_step*omega_current, 2.0)+pow(R, 2)), 0.5)));
        ui->GotoFrequency->setText("Goto Hz: " + QString::number(ahp_gt_get_totalsteps(axis_index) * ahp_gt_get_divider(axis_index) * ahp_gt_get_max_speed(axis_index) / microsteps / M_PI / 2.0));
    }
    ///////////
    ui->MaxSpeed_label->setText("Maximum speed: " + QString::number(ahp_gt_get_max_speed(axis_index) * SIDEREAL_DAY / M_PI / 2) + "x");
    ui->PWMFreq_label->setText("PWM: " + QString::number(round(366.2109375 * (ahp_gt_get_pwm_frequency(axis_index) + 1))) + " Hz");
    ui->Acceleration_label->setText("Acceleration: " + QString::number((double)(ui->Acceleration->maximum() - ui->Acceleration->value()) / 10) + "°");
    ui->TrackRate_label->setText("Track Rate offset: " + QString::number((double)ui->TrackRate->value()/(double)ui->TrackRate->maximum()/10.0) + "%");

}

void MainWindow::Connect(bool clicked)
{
    QString portname;
    int port = 9600;
    QString address = "localhost";
    int success = 0;
    isConnected = false;
    ahp_gt_set_axes_limit(NumAxes);
    ahp_gt_select_device(0);
    if(ui->ComPort->currentText().contains(':'))
    {
        address = ui->ComPort->currentText().split(":")[0];
        port = ui->ComPort->currentText().split(":")[1].toInt();
        success = !ahp_gt_connect_udp(address.toStdString().c_str(), port);
    }
    else
    {
        portname.append(ui->ComPort->currentText());
        success = !ahp_gt_connect(portname.toUtf8());
    }
    mountversion = ahp_gt_get_mc_version(axis_index) & 0xfff;
    success = mountversion != 0;
    if(success)
    {
        settings->setValue("LastPort", ui->ComPort->currentText());
        ui->ComPort->setEnabled(false);
        ui->Disconnect->setEnabled(true);
        ui->WorkArea->setEnabled(true);
        ui->Write->setText("Write");
        ui->Write->setEnabled(true);
        ui->LoadFW->setEnabled(true);
        ahp_gt_read_values(axis_index);
        oldTracking = false;
        isTracking = false;
        isConnected = true;
        finished = true;
        if(mountversion != 0x538)
            ui->SetAxis->setCurrentIndex(axis_index);
        emit reload_values();
        UIThread->start();
    } else
        emit ui->Disconnect->clicked(false);
}

void MainWindow::Disconnect(bool clicked)
{
    UIThread->stop();

    ui->ComPort->setEnabled(true);
    ui->Disconnect->setEnabled(false);
    ui->WorkArea->setEnabled(false);
    ui->Write->setText("Write");
    ui->Write->setEnabled(true);
    ui->LoadFW->setEnabled(true);
    ui->GPIO->setEnabled(true);
    ui->SetAxis_label->setEnabled(false);
    ui->SetAxis->setEnabled(false);
    ui->IntensityLimiter_label->setEnabled(false);
    ui->IntensityLimiter->setEnabled(false);
    ui->Intensity->setEnabled(false);
    ahp_gt_stop_motion(axis_index, 0);
    ahp_gt_disconnect();
    isConnected = false;
    finished = false;
    axis_index = -1;
}
