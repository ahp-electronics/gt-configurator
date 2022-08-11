#include "mainwindow.h"
#include <unistd.h>
#include <ctime>
#include <cmath>
#include <cstring>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QIODevice>
#include <functional>
#include <QTemporaryFile>
#include <QSerialPort>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSerialPortInfo>
#include <QFileDialog>
#include <QTimer>
#include <errno.h>
#include <libusb.h>
#include "dfu/dfu.h"
#include "./ui_mainwindow.h"
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

char *strrand(int len)
{
    int i;
    char* ret = (char*)malloc(len);
    for(i = 0; i < len; i++)
        ret[i] = 'a' + (rand() % 21);
    return ret;
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

    ui->Address->setValue(settings->value("Address", ahp_gt_get_address()).toInt());
    ui->PWMFreq->setValue(settings->value("PWMFreq", ahp_gt_get_pwm_frequency()).toInt());
    ui->MountType->setCurrentIndex(settings->value("MountType", 0).toInt());
    ui->MountStyle->setCurrentIndex(settings->value("MountStyle", 0).toInt());
    ui->TuneTrack->setChecked(settings->value("TuneTrack", false).toBool());
    ahp_gt_set_mount_type((MountType)ui->MountType->currentIndex());
    ahp_gt_set_mount_flags((GT1Flags)(ui->MountStyle->currentIndex() == 1));
    ahp_gt_set_features(0, (SkywatcherFeature)((ui->MountStyle->currentIndex() == 2)));
    ahp_gt_set_features(1, (SkywatcherFeature)((ui->MountStyle->currentIndex() == 2)));
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
    ui->SteppingMode_0->setCurrentIndex(settings->value("SteppingMode_0", ahp_gt_get_stepping_mode(0)).toInt());

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
    ui->SteppingMode_1->setCurrentIndex(settings->value("SteppingMode_1", ahp_gt_get_stepping_mode(1)).toInt());

    ahp_gt_set_motor_steps(0, ui->MotorSteps_0->value());
    ahp_gt_set_motor_teeth(0, ui->Motor_0->value());
    ahp_gt_set_worm_teeth(0, ui->Worm_0->value());
    ahp_gt_set_crown_teeth(0, ui->Crown_0->value());
    ahp_gt_set_direction_invert(0, ui->Invert_0->isChecked());
    ahp_gt_set_stepping_conf(0, (GT1SteppingConfiguration)ui->Coil_0->currentIndex());
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
    UpdateValues(0);

    ahp_gt_set_motor_steps(1, ui->MotorSteps_1->value());
    ahp_gt_set_motor_teeth(1, ui->Motor_1->value());
    ahp_gt_set_worm_teeth(1, ui->Worm_1->value());
    ahp_gt_set_crown_teeth(1, ui->Crown_1->value());
    ahp_gt_set_direction_invert(1, ui->Invert_1->isChecked());
    ahp_gt_set_stepping_conf(1, (GT1SteppingConfiguration)ui->Coil_1->currentIndex());
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
    UpdateValues(1);
    ahp_gt_set_position(0, M_PI / 2.0);
    ahp_gt_set_position(1, M_PI / 2.0);
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

    settings->setValue("MountType", ui->MountType->currentIndex());
    settings->setValue("Address", ui->Address->value());
    settings->setValue("PWMFreq", ui->PWMFreq->value());
    settings->setValue("MountStyle", ui->MountStyle->currentIndex());
    settings->setValue("Notes", QString(ui->Notes->text().toUtf8().toBase64()));
    s->~QSettings();
    settings = oldsettings;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    IndicationThread = new Thread(this, 100, 500);
    ProgressThread = new Thread(this, 10, 10);
    StatusThread = new Thread(this, 10, 100);
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
    settings = new QSettings(ini, QSettings::Format::IniFormat);
    isConnected = false;
    this->setFixedSize(1100, 570);
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
        if(ui->Write->text() == "Flash")
        {
            if(!dfu_flash(firmwareFilename.toStdString().c_str(), &percent, &finished))
            {
                QFile *f = new QFile(firmwareFilename);
                f->open(QIODevice::ReadOnly);
                settings->setValue("firmware", f->readAll().toBase64());
                f->close();
                f->~QFile();
            }
        }
        else
        {
            ahp_gt_write_values(0, &percent, &finished);
            ahp_gt_write_values(1, &percent, &finished);
            ahp_gt_set_position(0, M_PI / 2.0);
            ahp_gt_set_position(1, M_PI / 2.0);
        }
        ui->WorkArea->setEnabled(true);
        finished = 1;
        percent = 0;
        thread->requestInterruption();
        thread->unlock();
    });
    connect(ServerThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), [ = ] (Thread * thread) {
        ahp_gt_set_aligned(1);
        ahp_gt_start_synscan_server(11882, &finished);
        finished = true;
        threadsRunning = true;
        thread->requestInterruption();
        thread->unlock();
    });
    connect(ui->LoadFW, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool checked)
    {
        QString url = "https://www.iliaplatone.com/firmware.php?product=gt1";
        QNetworkAccessManager* manager = new QNetworkAccessManager();
        QNetworkReply* response = manager->get(QNetworkRequest(QUrl(url)));

        QEventLoop loop;
        connect(response, SIGNAL(finished()), &loop, SLOT(quit()));
        connect(response, SIGNAL(error(QNetworkReply::NetworkError)), &loop, SLOT(quit()));
        loop.exec();

        QJsonDocument doc = QJsonDocument::fromJson(response->readAll());
        QJsonObject obj = doc.object();
        QString base64 = obj["data"].toString();
        if(base64 == settings->value("firmware", "").toString()) return;
        QByteArray data = QByteArray::fromBase64(base64.toUtf8());
        QFile *f = new QFile(firmwareFilename);
        f->open(QIODevice::WriteOnly);
        f->write(data.data(), data.length());
        f->close();
        f->~QFile();
        response->deleteLater();
        response->manager()->deleteLater();
        ui->Write->setText("Flash");
        ui->Write->setEnabled(true);
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
#ifndef _WIN32
        portname.append("/dev/");
#endif
        int port = 9600;
        QString address = "localhost";
        int failure = 1;
        if(ui->ComPort->currentText().contains(':'))
        {
            address = ui->ComPort->currentText().split(":")[0];
            port = ui->ComPort->currentText().split(":")[1].toInt();
            ahp_gt_select_device(0);
            if(!ahp_gt_connect_udp(address.toStdString().c_str(), port)) {
                failure = ahp_gt_detect_device();
            }
        }
        else
        {
            portname.append(ui->ComPort->currentText());
            ahp_gt_select_device(0);
            if(!ahp_gt_connect(portname.toUtf8())) {
                failure = ahp_gt_detect_device();
            }
        }
        if(!failure)
        {
            settings->setValue("LastPort", ui->ComPort->currentText());
            ui->Write->setText("Write");
            ui->Write->setEnabled(true);
            ahp_gt_read_values(0);
            ahp_gt_read_values(1);
            ui->LoadFW->setEnabled(false);
            ui->Connect->setEnabled(false);
            ui->Disconnect->setEnabled(true);
            ui->labelNotes->setEnabled(true);
            ui->Notes->setEnabled(true);
            ui->RA->setEnabled(true);
            ui->DEC->setEnabled(true);
            ui->WorkArea->setEnabled(true);
            ui->Control->setEnabled(true);
            ui->commonSettings->setEnabled(true);
            ui->AdvancedRA->setEnabled(true);
            ui->AdvancedDec->setEnabled(true);
            ui->loadConfig->setEnabled(true);
            ui->saveConfig->setEnabled(true);
            readIni(ini);
            isConnected = true;
            finished = true;
        }
    });
    connect(ui->Disconnect, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool checked)
    {
        ui->Write->setText("Write");
        ui->Write->setEnabled(false);
        isConnected = false;
        finished = false;
        ui->Server->setChecked(false);
        ui->LoadFW->setEnabled(true);
        ui->Connect->setEnabled(true);
        ui->Disconnect->setEnabled(false);
        ui->labelNotes->setEnabled(false);
        ui->Notes->setEnabled(false);
        ui->RA->setEnabled(false);
        ui->DEC->setEnabled(false);
        ui->WorkArea->setEnabled(true);
        ui->Control->setEnabled(false);
        ui->commonSettings->setEnabled(false);
        ui->AdvancedRA->setEnabled(false);
        ui->AdvancedDec->setEnabled(false);
        ui->loadConfig->setEnabled(false);
        ui->saveConfig->setEnabled(false);
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
        switch(mounttype[index])
        {
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
        }
        UpdateValues(0);
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->Invert_0, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
            [ = ](int state)
    {
        ahp_gt_set_direction_invert(0, ui->Invert_0->isChecked());
        saveIni(ini);
    });
    connect(ui->Invert_1, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
            [ = ](int state)
    {
        ahp_gt_set_direction_invert(1, ui->Invert_1->isChecked());
        saveIni(ini);
    });
    connect(ui->MotorSteps_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_steps(0, ui->MotorSteps_0->value());
        UpdateValues(0);
        saveIni(ini);
    });
    connect(ui->Worm_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_worm_teeth(0, ui->Worm_0->value());
        UpdateValues(0);
        saveIni(ini);
    });
    connect(ui->Motor_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_teeth(0, ui->Motor_0->value());
        UpdateValues(0);
        saveIni(ini);
    });
    connect(ui->Crown_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_crown_teeth(0, ui->Crown_0->value());
        UpdateValues(0);
        saveIni(ini);
    });
    connect(ui->MotorSteps_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_steps(1, ui->MotorSteps_1->value());
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->Worm_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_worm_teeth(1, ui->Worm_1->value());
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->Motor_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_motor_teeth(1, ui->Motor_1->value());
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->Crown_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_crown_teeth(1, ui->Crown_1->value());
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->Ra_P, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        oldTracking = ui->Tracking->isChecked();
        ui->Tracking->setChecked(false);
        ahp_gt_stop_motion(0, 1);
        ahp_gt_start_motion(0, ui->Ra_Speed->value());
    });
    connect(ui->Ra_N, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        oldTracking = ui->Tracking->isChecked();
        ui->Tracking->setChecked(false);
        ahp_gt_stop_motion(0, 1);
        ahp_gt_start_motion(0, -ui->Ra_Speed->value());
    });
    connect(ui->Dec_P, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        ahp_gt_stop_motion(1, 1);
        ahp_gt_start_motion(1, ui->Dec_Speed->value());
    });
    connect(ui->Dec_N, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        ahp_gt_stop_motion(1, 1);
        ahp_gt_start_motion(1, -ui->Dec_Speed->value());
    });
    connect(ui->Stop, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
            [ = ]()
    {
        ahp_gt_stop_motion(0, 1);
        ahp_gt_stop_motion(1, 1);
    });
    connect(ui->Ra_P, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(0, 1);
        ui->Tracking->setChecked(oldTracking);
    });
    connect(ui->Ra_N, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(0, 1);
        ui->Tracking->setChecked(oldTracking);
    });
    connect(ui->Dec_P, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(1, 1);
    });
    connect(ui->Dec_N, static_cast<void (QPushButton::*)()>(&QPushButton::released),
            [ = ]()
    {
        ahp_gt_stop_motion(1, 1);
    });
    connect(ui->Tracking, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
            [ = ](int state)
    {
        if(ui->Tracking->isChecked())
            ahp_gt_start_tracking(0);
        else
            ahp_gt_stop_motion(0, 1);
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
        ui->MaxSpeed_label_0->setText("Maximum speed: " + QString::number(value) + "x");
        UpdateValues(0);
        saveIni(ini);
    });
    connect(ui->MaxSpeed_1, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_max_speed(1, ui->MaxSpeed_1->value());
        ui->MaxSpeed_label_1->setText("Maximum speed: " + QString::number(value) + "x");
        UpdateValues(1);
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
    connect(ui->Address, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_address(ui->Address->value());
        saveIni(ini);
    });
    connect(ui->PWMFreq, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
            [ = ](int value)
    {
        ahp_gt_set_pwm_frequency(ui->PWMFreq->value());
        ahp_gt_set_mount_type(mounttype[ui->MountType->currentIndex()]);
        ui->PWMFreq_label->setText("PWM: " + QString::number(1500 + 700 * value) + " Hz");
        saveIni(ini);
    });
    connect(ui->TuneTrack, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked),
            [ = ](bool checked)
    {
        stop_correction[0] = true;
        if(checked) {
            correct_tracking[0] = true;
        } else {
            correct_tracking[0] = false;
        }
    });
    connect(ui->MountStyle, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            [ = ](int index)
    {
        ahp_gt_set_features(0, (SkywatcherFeature)((ui->MountStyle->currentIndex() == 2)));
        ahp_gt_set_features(1, (SkywatcherFeature)((ui->MountStyle->currentIndex() == 2)));
        ahp_gt_set_mount_flags((GT1Flags)(ui->MountStyle->currentIndex() == 1));
        saveIni(ini);
    });
    connect(ui->Write, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [ = ](bool checked = false)
    {
        if(ui->Write->text() == "Write")
        {
            //UpdateValues(0);
            //UpdateValues(1);
        }
        else
        {
            ui->ComPort->setEnabled(false);
        }

        WriteThread->start();
    });
    connect(ui->Inductance_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        UpdateValues(0);
        saveIni(ini);
    });
    connect(ui->Resistance_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        UpdateValues(0);
        saveIni(ini);
    });
    connect(ui->Current_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        UpdateValues(0);
        saveIni(ini);
    });
    connect(ui->Voltage_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        UpdateValues(0);
        saveIni(ini);
    });
    connect(ui->Inductance_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->Resistance_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->Current_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->Voltage_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            [ = ](int value)
    {
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->SteppingMode_0, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_mode(0, (GT1SteppingMode)ui->SteppingMode_0->currentIndex());
        UpdateValues(0);
        saveIni(ini);
    });
    connect(ui->SteppingMode_1, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_mode(1, (GT1SteppingMode)ui->SteppingMode_1->currentIndex());
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->Coil_0, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_conf(0, (GT1SteppingConfiguration)ui->Coil_0->currentIndex());
        saveIni(ini);
    });
    connect(ui->Coil_1, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
        ahp_gt_set_stepping_conf(1, (GT1SteppingConfiguration)ui->Coil_1->currentIndex());
        saveIni(ini);
    });
    connect(ui->GPIO_0, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ] (int index)
    {
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
        saveIni(ini);
    });
    connect(ui->GPIO_1, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [ = ]  (int index)
    {
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
        saveIni(ini);
    });
    connect(ui->Goto, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked), [ = ]  (bool checked)
    {
        ahp_gt_goto_absolute(0, (double)ui->TargetSteps_0->value()*M_PI * 2.0 / (double)ahp_gt_get_totalsteps(0),
                             ui->Ra_Speed->value());
        ahp_gt_goto_absolute(1, (double)ui->TargetSteps_1->value()*M_PI * 2.0 / (double)ahp_gt_get_totalsteps(1),
                             ui->Dec_Speed->value());
    });
    connect(ProgressThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), this, [ = ] (Thread * parent)
    {
        ui->progress->setValue(percent);
        parent->unlock();
    });
    connect(IndicationThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), this, [ = ] (Thread * parent)
    {
        if(isConnected && finished)
        {
            for(int a = 0; a < 2; a++)
            {
                int totalsteps = ahp_gt_get_totalsteps(a);
                QDateTime now = QDateTime::currentDateTimeUtc();
                double diffTime = (double)lastPollTime[a].msecsTo(now);
                lastPollTime[a] = now;
                double Steps = currentSteps[a];
                double diffSteps = Steps - lastSteps[a];
                lastSteps[a] = Steps;
                Steps *= totalsteps / M_PI / 2.0;
                diffSteps *= 180.0 * 60.0 * 60.0 / M_PI;
                double Speed = 0.0;
                for(int s = 0; s < _n_speeds; s++)
                {
                    if(s < _n_speeds - 1)
                        lastSpeeds[a][s] = lastSpeeds[a][s + 1];
                    else
                        lastSpeeds[a][s] = diffSteps * 1000.0 / diffTime;
                    Speed += lastSpeeds[a][s];
                }
                Speed /= _n_speeds;
                if(a == 0)
                {
                    ui->CurrentSteps_0->setText(QString::number((int)Steps));
                    ui->Rate_0->setText("as/sec: " + QString::number(Speed));
                }
                if(a == 1)
                {
                    ui->CurrentSteps_1->setText(QString::number(Steps));
                    ui->Rate_1->setText("as/sec: " + QString::number(Speed));
                }
            }
            ui->progress->setValue(percent);
            ui->Control->setEnabled(true);
            ui->WriteArea->setEnabled(true);
            ui->AdvancedRA->setEnabled(true);
            ui->AdvancedDec->setEnabled(true);
            if(ui->Server->isChecked()) {
                finished = false;
                threadsRunning = false;
                ServerThread->start();
            }
        } else {
            if(isConnected && !ui->Server->isChecked()) {
                finished = true;
            }
            ui->Control->setEnabled(false);
            ui->WriteArea->setEnabled(!isConnected);
            ui->AdvancedRA->setEnabled(false);
            ui->AdvancedDec->setEnabled(false);
        }

        parent->unlock();
    });
    connect(StatusThread, static_cast<void (Thread::*)(Thread *)>(&Thread::threadLoop), [ = ] (Thread * parent)
    {
        if(isConnected && finished)
        {
            for(int a = 0; a < 2; a++)
            {
                status[a] = ahp_gt_get_status(a);
                currentSteps[a] = ahp_gt_get_position(a);
                if(correct_tracking[a] && stop_correction[a]) {
                    correcting_tracking[a] = true;
                    stop_correction[a] = false;
                    ahp_gt_correct_tracking(a, SIDEREAL_DAY * ahp_gt_get_wormsteps(a) / ahp_gt_get_totalsteps(a), (int*)&stop_correction[a]);
                }
                if(correcting_tracking[a] && stop_correction[a]) {
                    correcting_tracking[a] = false;
                    ui->TuneTrack->setChecked(false);
                }
            }
        }
        parent->unlock();
    });
    IndicationThread->start();
    ProgressThread->start();
    StatusThread->start();
}

MainWindow::~MainWindow()
{
    if(QFile(firmwareFilename).exists())
        unlink(firmwareFilename.toUtf8());
    threadsRunning = false;
    delete ui;
}

void MainWindow::UpdateValues(int axis)
{
    if(axis == 0)
    {
        double totalsteps = ahp_gt_get_totalsteps(0) * ahp_gt_get_divider(0) / ahp_gt_get_multiplier(0);
        ui->Divider0->setText(QString::number(ahp_gt_get_divider(0)));
        ui->Multiplier0->setText(QString::number(ahp_gt_get_multiplier(0)));
        ui->WormSteps0->setText(QString::number(ahp_gt_get_wormsteps(0)));
        ui->TotalSteps0->setText(QString::number(ahp_gt_get_totalsteps(0)));
        ui->TrackingFrequency_0->setText("Steps/s: " + QString::number(totalsteps / SIDEREAL_DAY));
        ui->SPT_0->setText("sec/turn: " + QString::number(SIDEREAL_DAY / (ahp_gt_get_crown_teeth(0)*ahp_gt_get_worm_teeth(
                               0) / ahp_gt_get_motor_teeth(0))));
        double L = (double)ui->Inductance_0->value() / 1000000.0;
        double R = (double)ui->Resistance_0->value() / 1000.0;
        double mI = (double)ui->Current_0->value() / 1000.0;
        double mV = (double)ui->Voltage_0->value();
        double Z = sqrt(fmax(0, pow(mV / mI, 2.0) - pow(R, 2.0)));
        double f = (2.0 * M_PI * Z / L);
        ui->MinFrequency_0->setText("PWM Hz: " + QString::number(f));
        ui->MaxFrequency_0->setText("Goto Hz: " + QString::number(totalsteps * ahp_gt_get_max_speed(0) / SIDEREAL_DAY));
    }
    else if (axis == 1)
    {
        double totalsteps = ahp_gt_get_totalsteps(1) * ahp_gt_get_divider(1) / ahp_gt_get_multiplier(1);
        ui->Divider1->setText(QString::number(ahp_gt_get_divider(1)));
        ui->Multiplier1->setText(QString::number(ahp_gt_get_multiplier(1)));
        ui->WormSteps1->setText(QString::number(ahp_gt_get_wormsteps(1)));
        ui->TotalSteps1->setText(QString::number(ahp_gt_get_totalsteps(1)));
        ui->TrackingFrequency_1->setText("Steps/s: " + QString::number(totalsteps / SIDEREAL_DAY));
        ui->SPT_1->setText("sec/turn: " + QString::number(SIDEREAL_DAY / (ahp_gt_get_crown_teeth(1)*ahp_gt_get_worm_teeth(
                               1) / ahp_gt_get_motor_teeth(1))));
        double L = (double)ui->Inductance_1->value() / 1000000.0;
        double R = (double)ui->Resistance_1->value() / 1000.0;
        double mI = (double)ui->Current_1->value() / 1000.0;
        double mV = (double)ui->Voltage_1->value();
        double Z = sqrt(fmax(0, pow(mV / mI, 2.0) - pow(R, 2.0)));
        double f = (2.0 * M_PI * Z / L);
        ui->MinFrequency_1->setText("PWM Hz: " + QString::number(f));
        ui->MaxFrequency_1->setText("Goto Hz: " + QString::number(totalsteps * ahp_gt_get_max_speed(1) / SIDEREAL_DAY));
    }
}
