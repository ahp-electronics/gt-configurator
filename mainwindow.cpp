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
static MountType mounttype[] = {
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

void MainWindow::Progress(MainWindow *wnd)
{
    wnd->threadsRunning = true;
    while(wnd->threadsRunning) {
        emit wnd->onUpdateProgress();
        QThread::msleep(100);
    }
}

int MainWindow::flashFirmware()
{
    dfu_status status;
    libusb_context *ctx;
    dfu_file file;
    memset(&file, 0, sizeof(file));
    int ret = libusb_init(&ctx);
    int transfer_size = 0;
    int func_dfu_transfer_size;
    if(dfu_root != NULL)
        free(dfu_root);
    dfu_root = NULL;
    finished = 0;
    if (ret) {
        fprintf(stderr, "unable to initialize libusb: %s", libusb_error_name(ret));
        return EIO;
    }
    strcpy(file.name, firmwareFilename.toStdString().c_str());
    dfu_load_file(&file, MAYBE_SUFFIX, MAYBE_PREFIX);

    if (match_vendor < 0 && file.idVendor != 0xffff) {
        match_vendor = file.idVendor;
    }
    if (match_product < 0 && file.idProduct != 0xffff) {
        match_product = file.idProduct;
    }
    probe_devices(ctx);

    if (dfu_root == NULL) {
        ret = ENODEV;
        goto out;
    } else if (dfu_root->next != NULL) {
        /* We cannot safely support more than one DFU capable device
         * with same vendor/product ID, since during DFU we need to do
         * a USB bus reset, after which the target device will get a
         * new address */
        fprintf(stderr, "More than one DFU capable USB device found! "
               "Try `--list' and specify the serial number "
               "or disconnect all but one device\n");
        ret = ENODEV;
        goto out;
    }

    if (((file.idVendor  != 0xffff && file.idVendor  != dfu_root->vendor) ||
         (file.idProduct != 0xffff && file.idProduct != dfu_root->product))) {
        fprintf(stderr, "Error: File ID %04x:%04x does "
            "not match device (%04x:%04x)",
            file.idVendor, file.idProduct,
            dfu_root->vendor, dfu_root->product);
        ret = EINVAL;
        goto out;
    }

    ret = libusb_open(dfu_root->dev, &dfu_root->dev_handle);
    if (ret || !dfu_root->dev_handle)
        errx(EX_IOERR, "Cannot open device: %s", libusb_error_name(ret));

    ret = libusb_claim_interface(dfu_root->dev_handle, dfu_root->interface);
    if (ret < 0) {
        errx(EX_IOERR, "Cannot claim interface - %s", libusb_error_name(ret));
    }

    ret = libusb_set_interface_alt_setting(dfu_root->dev_handle, dfu_root->interface, dfu_root->altsetting);
    if (ret < 0) {
        errx(EX_IOERR, "Cannot set alternate interface: %s", libusb_error_name(ret));
    }

status_again:
    ret = dfu_get_status(dfu_root, &status );
    if (ret < 0) {
        fprintf(stderr, "error get_status: %s", libusb_error_name(ret));
    }

    QThread::msleep(status.bwPollTimeout);

    switch (status.bState) {
    case DFU_STATE_appIDLE:
    case DFU_STATE_appDETACH:
        fprintf(stderr, "Device still in Runtime Mode!");
        break;
    case DFU_STATE_dfuERROR:
        if (dfu_clear_status(dfu_root->dev_handle, dfu_root->interface) < 0) {
            fprintf(stderr, "error clear_status");
        }
        goto status_again;
        break;
    case DFU_STATE_dfuDNLOAD_IDLE:
    case DFU_STATE_dfuUPLOAD_IDLE:
        if (dfu_abort(dfu_root->dev_handle, dfu_root->interface) < 0) {
            fprintf(stderr, "can't send DFU_ABORT");
        }
        goto status_again;
        break;
    case DFU_STATE_dfuIDLE:
        break;
    default:
        break;
    }

    if (DFU_STATUS_OK != status.bStatus ) {
        /* Clear our status & try again. */
        if (dfu_clear_status(dfu_root->dev_handle, dfu_root->interface) < 0)
            fprintf(stderr, "USB communication error");
        if (dfu_get_status(dfu_root, &status) < 0)
            fprintf(stderr, "USB communication error");
        if (DFU_STATUS_OK != status.bStatus)
            fprintf(stderr, "Status is not OK: %d", status.bStatus);

        QThread::msleep(status.bwPollTimeout);
    }

    func_dfu_transfer_size = libusb_le16_to_cpu(dfu_root->func_dfu.wTransferSize);
    if (func_dfu_transfer_size) {
        if (!transfer_size)
            transfer_size = func_dfu_transfer_size;
    } else {
        if (!transfer_size)
            fprintf(stderr, "Transfer size must be specified");
    }

    if (transfer_size < dfu_root->bMaxPacketSize0) {
        transfer_size = dfu_root->bMaxPacketSize0;
    }

    if (dfuload_do_dnload(dfu_root, transfer_size, &file, &percent) < 0) {
         ret = EFAULT;
    }

    libusb_close(dfu_root->dev_handle);
    dfu_root->dev_handle = NULL;
out:
    libusb_exit(ctx);
    return ret;
}

void MainWindow::WriteValues(MainWindow *wnd)
{
    wnd->percent = 0;
    wnd->finished = 0;
    if(wnd->ui->Write->text() == "Flash") {
        if(!wnd->flashFirmware()) {
            QFile *f = new QFile(wnd->firmwareFilename);
            f->open(QIODevice::ReadOnly);
            wnd->settings->setValue("firmware", f->readAll().toBase64());
            f->close();
            f->~QFile();
        }
        wnd->finished = 1;
    } else {
        ahp_gt_write_values(0, &wnd->percent, &wnd->finished);
        ahp_gt_write_values(1, &wnd->percent, &wnd->finished);
    }
    wnd->percent = 0;
}

void MainWindow::readIni(QString ini)
{
    QString dir = QDir(ini).dirName();
    if(!QDir(dir).exists()){
        QDir().mkdir(dir);
    }
    if(!QFile(ini).exists()){
        QFile *f = new QFile(ini);
        f->open(QIODevice::WriteOnly);
        f->close();
        f->~QFile();
    }
    QSettings *oldsettings = settings;
    QSettings *s = new QSettings(ini, QSettings::Format::IniFormat);
    settings = s;
    emit ui->MountType->currentIndexChanged(settings->value("MountType", 0).toInt());

    ui->MotorSteps_0->setValue(settings->value("MotorSteps_0", ahp_gt_get_motor_steps(0)).toInt());
    ui->Motor_0->setValue(settings->value("Motor_0", ahp_gt_get_motor_teeth(0)).toInt());
    ui->Worm_0->setValue(settings->value("Worm_0", ahp_gt_get_worm_teeth(0)).toInt());
    ui->Crown_0->setValue(settings->value("Crown_0", ahp_gt_get_crown_teeth(0)).toInt());
    ui->MaxSpeed_0->setValue(settings->value("MaxSpeed_0", ahp_gt_get_max_speed(0)).toInt());
    ui->Acceleration_0->setValue(settings->value("Acceleration_0", ui->Acceleration_0->maximum()-ahp_gt_get_acceleration_angle(0)*3600.0/M_PI).toInt());
    emit ui->GPIO_0->currentIndexChanged(settings->value("GPIO_0", ahp_gt_get_feature(0)).toInt());
    emit ui->Coil_0->currentIndexChanged(settings->value("Coil_0", ahp_gt_get_stepping_conf(0)).toInt());
    ui->Invert_0->setChecked(settings->value("Invert_0", ahp_gt_get_direction_invert(0) == 1).toBool());
    emit ui->SteppingMode_0->currentIndexChanged(settings->value("SteppingMode_0", ahp_gt_get_stepping_mode(0)).toInt());
    ui->Inductance_0->setValue(settings->value("Inductance_0", 10).toInt());
    ui->Resistance_0->setValue(settings->value("Resistance_0", 20000).toInt());
    ui->Current_0->setValue(settings->value("Current_0", 1000).toInt());
    ui->Voltage_0->setValue(settings->value("Voltage_0", 12).toInt());

    ui->MotorSteps_1->setValue(settings->value("MotorSteps_1", ahp_gt_get_motor_steps(1)).toInt());
    ui->Motor_1->setValue(settings->value("Motor_1", ahp_gt_get_motor_teeth(1)).toInt());
    ui->Worm_1->setValue(settings->value("Worm_1", ahp_gt_get_worm_teeth(1)).toInt());
    ui->Crown_1->setValue(settings->value("Crown_1", ahp_gt_get_crown_teeth(1)).toInt());
    ui->MaxSpeed_1->setValue(settings->value("MaxSpeed_1", ahp_gt_get_max_speed(1)).toInt());
    ui->Acceleration_1->setValue(settings->value("Acceleration_1", ui->Acceleration_1->maximum()-ahp_gt_get_acceleration_angle(1)*3600.0/M_PI).toInt());
    emit ui->GPIO_1->currentIndexChanged(settings->value("GPIO_1", ahp_gt_get_feature(1)).toInt());
    emit ui->Coil_1->currentIndexChanged(settings->value("Coil_1", ahp_gt_get_stepping_conf(1)).toInt());
    ui->Invert_1->setChecked(settings->value("Invert_1", ahp_gt_get_direction_invert(1) == 1).toBool());
    emit ui->SteppingMode_1->currentIndexChanged(settings->value("SteppingMode_1", ahp_gt_get_stepping_mode(1)).toInt());
    ui->Inductance_1->setValue(settings->value("Inductance_1", 10).toInt());
    ui->Resistance_1->setValue(settings->value("Resistance_1", 20000).toInt());
    ui->Current_1->setValue(settings->value("Current_1", 1000).toInt());
    ui->Voltage_1->setValue(settings->value("Voltage_1", 12).toInt());

    ui->PWMFreq->setValue(settings->value("PWMFreq", ahp_gt_get_pwm_frequency()).toInt());
    ui->isAZEQ->setChecked(settings->value("isAZEQ", false).toBool());
    s->~QSettings();
    settings = oldsettings;
}

void MainWindow::saveIni(QString ini)
{
    QString dir = QDir(ini).dirName();
    if(!QDir(dir).exists()){
        QDir().mkdir(dir);
    }
    if(!QFile(ini).exists()){
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
    settings->setValue("PWMFreq", ui->PWMFreq->value());
    settings->setValue("isAZEQ", ui->isAZEQ->isChecked());
    s->~QSettings();
    settings = oldsettings;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    setAccessibleName("GT Configurator");
    firmwareFilename = QStandardPaths::standardLocations(QStandardPaths::TempLocation).at(0)+tmpnam(NULL);
    QString homedir = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).at(0);
    ini = homedir+"/settings.ini";
    if(!QDir(homedir).exists()){
        QDir().mkdir(homedir);
    }
    if(!QFile(ini).exists()){
        QFile *f = new QFile(ini);
        f->open(QIODevice::WriteOnly);
        f->close();
        f->~QFile();
    }
    settings = new QSettings(ini, QSettings::Format::IniFormat);
    isConnected = false;
    this->setFixedSize(770, 745);
    ui->setupUi(this);
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    if(ports.length() == 0)
        ui->ComPort->addItem("No ports available");
    else {
        ui->ComPort->addItem(settings->value("LastPort", ports[0].portName()).toString());
        for (int i = 1; i < ports.length(); i++)
            ui->ComPort->addItem(ports[i].portName());
        ui->MountType->setCurrentIndex(0);
    }
    connect(ui->LoadFW, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [=](bool checked) {
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
        f->open(QIODevice::WriteOnly|QIODevice::NewOnly);
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
            [=](bool checked) {
        if(ui->ComPort->currentText() == "No ports available")
            return;
        char port[150] = {0};
        #ifndef _WIN32
            strcat(port, "/dev/");
        #endif
        strcat(port, ports[ui->ComPort->currentIndex()].portName().toStdString().c_str());
        if(!ahp_gt_connect(port)) {
            settings->setValue("LastPort", port);
            ui->Write->setText("Write");
            ui->Write->setEnabled(true);
            isConnected = true;
            ahp_gt_read_values(0);
            ahp_gt_read_values(1);
            ui->LoadFW->setEnabled(false);
            ui->Connect->setEnabled(false);
            ui->RA->setEnabled(true);
            ui->DEC->setEnabled(true);
            ui->WorkArea->setEnabled(true);
            ui->Control->setEnabled(true);
            ui->commonSettings->setEnabled(true);
            ui->AdvancedRA->setEnabled(true);
            ui->AdvancedDec->setEnabled(true);
            readIni(ini);
        }
    });
    connect(ui->loadConfig, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [=](bool triggered) {
        QString ini = QFileDialog::getOpenFileName(this, "Open configuration file", ".", "Configuration files (*.ini)");
        if(ini.endsWith(".ini")) {
            readIni(ini);
        }
    });
    connect(ui->saveConfig, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [=](bool triggered) {
        QString ini = QFileDialog::getSaveFileName(this, "Save configuration file", ".", "Configuration files (*.ini)");
        if(ini.endsWith(".ini")) {
            saveIni(ini);
        }
    });
    connect(ui->MountType, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            [=](int index) {
        switch(mounttype[index]) {
        case isEQ6:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(180);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(180);
            ui->isAZEQ->setCheckState(Qt::CheckState::Unchecked);
            break;
        case isHEQ5:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(180);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(180);
            ui->isAZEQ->setCheckState(Qt::CheckState::Unchecked);
            break;
        case isEQ5:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(144);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(144);
            ui->isAZEQ->setCheckState(Qt::CheckState::Unchecked);
            break;
        case isEQ3:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(144);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(144);
            ui->isAZEQ->setCheckState(Qt::CheckState::Unchecked);
            break;
        case isEQ8:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(360);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(360);
            ui->isAZEQ->setCheckState(Qt::CheckState::Unchecked);
            break;
        case isAZEQ6:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(180);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(180);
            ui->isAZEQ->setCheckState(Qt::CheckState::Checked);
            break;
        case isAZEQ5:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(144);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(144);
            ui->isAZEQ->setCheckState(Qt::CheckState::Checked);
            break;
        case isGT:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(144);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(144);
            ui->isAZEQ->setCheckState(Qt::CheckState::Unchecked);
            break;
        case isMF:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(144);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(144);
            ui->isAZEQ->setCheckState(Qt::CheckState::Unchecked);
            break;
        case is114GT:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(144);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(144);
            ui->isAZEQ->setCheckState(Qt::CheckState::Unchecked);
            break;
        case isDOB:
            ui->Motor_0->setValue(10);
            ui->Worm_0->setValue(40);
            ui->Crown_0->setValue(100);
            ui->Motor_1->setValue(10);
            ui->Worm_1->setValue(40);
            ui->Crown_1->setValue(100);
            ui->isAZEQ->setCheckState(Qt::CheckState::Checked);
            break;
        default:
            break;
        }
        UpdateValues(0);
        UpdateValues(1);
        saveIni(ini);
    });
    connect(ui->Invert_0, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
            [=](int state){
        saveIni(ini);
        ahp_gt_set_direction_invert(0, ui->Invert_0->isChecked());
    });
    connect(ui->Invert_1, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
            [=](int state){
        saveIni(ini);
        ahp_gt_set_direction_invert(1, ui->Invert_1->isChecked());
    });
    connect(ui->SteppingMode_0, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [=](int index) {
        saveIni(ini);
        ahp_gt_set_stepping_mode(0, (GT1SteppingMode)ui->SteppingMode_0->currentIndex());
        UpdateValues(0);
    });
    connect(ui->MotorSteps_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_motor_steps(0, ui->MotorSteps_0->value());
        UpdateValues(0);
    });
    connect(ui->Worm_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_worm_teeth(0, ui->Worm_0->value());
        UpdateValues(0);
    });
    connect(ui->Motor_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_motor_teeth(0, ui->Motor_0->value());
        UpdateValues(0);
    });
    connect(ui->Crown_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_crown_teeth(0, ui->Crown_0->value());
        UpdateValues(0);
    });
    connect(ui->SteppingMode_1, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [=](int index) {
        saveIni(ini);
        ahp_gt_set_stepping_mode(1, (GT1SteppingMode)ui->SteppingMode_1->currentIndex());
        UpdateValues(1);
    });
    connect(ui->MotorSteps_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_motor_steps(1, ui->MotorSteps_1->value());
        UpdateValues(1);
    });
    connect(ui->Worm_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_worm_teeth(1, ui->Worm_1->value());
        UpdateValues(1);
    });
    connect(ui->Motor_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_motor_teeth(1, ui->Motor_1->value());
        UpdateValues(1);
    });
    connect(ui->Crown_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_crown_teeth(1, ui->Crown_1->value());
        UpdateValues(1);
    });
    connect(ui->Ra_P, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
                   [=](){
        oldTracking = ui->Tracking->isChecked();
        ui->Tracking->setChecked(false);
        while(ahp_gt_is_axis_moving(0))
            QThread::msleep(100);
       ahp_gt_start_motion(0, ui->Ra_Speed->value());
    });
    connect(ui->Ra_N, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
                   [=](){
        oldTracking = ui->Tracking->isChecked();
        ui->Tracking->setChecked(false);
        while(ahp_gt_is_axis_moving(0))
            QThread::msleep(100);
       ahp_gt_start_motion(0, -ui->Ra_Speed->value());
    });
    connect(ui->Dec_P, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
                   [=](){
        while(ahp_gt_is_axis_moving(1))
            QThread::msleep(100);
       ahp_gt_start_motion(1, ui->Dec_Speed->value());
    });
    connect(ui->Dec_N, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
                   [=](){
        while(ahp_gt_is_axis_moving(1))
            QThread::msleep(100);
       ahp_gt_start_motion(1, -ui->Dec_Speed->value());
    });
    connect(ui->Ra_P, static_cast<void (QPushButton::*)()>(&QPushButton::released),
                   [=](){
        ahp_gt_stop_motion(0);
        while(ahp_gt_is_axis_moving(0))
            QThread::msleep(100);
        ui->Tracking->setChecked(oldTracking);
    });
    connect(ui->Ra_N, static_cast<void (QPushButton::*)()>(&QPushButton::released),
                   [=](){
        ahp_gt_stop_motion(0);
        while(ahp_gt_is_axis_moving(0))
            QThread::msleep(100);
        ui->Tracking->setChecked(oldTracking);
    });
    connect(ui->Dec_P, static_cast<void (QPushButton::*)()>(&QPushButton::released),
                   [=](){
        ahp_gt_stop_motion(1);
        while(ahp_gt_is_axis_moving(1))
            QThread::msleep(100);
    });
    connect(ui->Dec_N, static_cast<void (QPushButton::*)()>(&QPushButton::released),
                   [=](){
        ahp_gt_stop_motion(1);
        while(ahp_gt_is_axis_moving(1))
            QThread::msleep(100);
    });
    connect(ui->Tracking, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
                   [=](int state){
        if(ui->Tracking->isChecked())
            ahp_gt_start_tracking(0);
        else
            ahp_gt_stop_motion(0);
    });
    connect(ui->Acceleration_0, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_acceleration_angle(0, (ui->Acceleration_0->maximum()-ui->Acceleration_0->value())*M_PI/1800.0);
        ui->Acceleration_label_0->setText("Acceleration: "+QString::number(2.0-(double)value/10.0)+"°");
    });
    connect(ui->Acceleration_1, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_acceleration_angle(1,  (ui->Acceleration_1->maximum()-ui->Acceleration_1->value())*M_PI/1800.0);
        ui->Acceleration_label_1->setText("Acceleration: "+QString::number(2.0-(double)value/10.0)+"°");
    });
    connect(ui->MaxSpeed_0, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_max_speed(0, ui->MaxSpeed_0->value());
        ui->MaxSpeed_label_0->setText("Maximum speed: "+QString::number(value)+"x");
    });
    connect(ui->MaxSpeed_1, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_max_speed(1, ui->MaxSpeed_1->value());
        ui->MaxSpeed_label_1->setText("Maximum speed: "+QString::number(value)+"x");
    });
    connect(ui->Ra_Speed, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
                   [=](int value){
        ui->Ra_Speed_label->setText("Ra speed: "+QString::number(value)+"x");
    });
    connect(ui->Dec_Speed, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
                   [=](int value){
        ui->Dec_Speed_label->setText("Dec speed: "+QString::number(value)+"x");
    });
    connect(ui->PWMFreq, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
                   [=](int value){
        saveIni(ini);
        ahp_gt_set_pwm_frequency(ui->PWMFreq->value());
        ahp_gt_set_mount_type(mounttype[ui->MountType->currentIndex()]);
        ui->PWMFreq_label->setText("PWM Frequency: "+QString::number(1500+700*value)+" Hz");
    });
    connect(ui->isAZEQ, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
                   [=](int state){
        saveIni(ini);
        ahp_gt_set_features(0, (SkywatcherFeature)((ui->isAZEQ->isChecked() ? isAZEQ : 0)|hasPPEC));
        ahp_gt_set_features(1, (SkywatcherFeature)((ui->isAZEQ->isChecked() ? isAZEQ : 0)|hasPPEC));
    });
    connect(ui->Coil_0, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                   [=](int index){
        saveIni(ini);
        ahp_gt_set_stepping_conf(0, (GT1SteppingConfiguration)ui->Coil_0->currentIndex());
    });
    connect(ui->Coil_1, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                   [=](int index){
        saveIni(ini);
        ahp_gt_set_stepping_conf(1, (GT1SteppingConfiguration)ui->Coil_1->currentIndex());
    });
    connect(ui->GPIO_0, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                   [=](int index){
        saveIni(ini);
        switch(ui->GPIO_0->currentIndex()) {
        case 0:
            ahp_gt_set_feature(0, GpioUnused);
            break;
        case 1:
            ahp_gt_set_feature(0, GpioAsST4);
            break;
        case 2:
            ahp_gt_set_feature(0, GpioAsPulseDrive);
            break;
        default: break;
        }
    });
    connect(ui->GPIO_1, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                   [=](int index){
        saveIni(ini);
        switch(ui->GPIO_1->currentIndex()) {
        case 0:
            ahp_gt_set_feature(1, GpioUnused);
            break;
        case 1:
            ahp_gt_set_feature(1, GpioAsST4);
            break;
        case 2:
            ahp_gt_set_feature(1, GpioAsPulseDrive);
            break;
        default: break;
        }
    });
    connect(ui->Write, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [=](bool checked=false){
        if(ui->Write->text() == "Write") {
            //UpdateValues(0);
            //UpdateValues(1);
        } else {
            ui->ComPort->setEnabled(false);
        }
        std::thread(MainWindow::WriteValues, this).detach();
    });
    connect(this, static_cast<void (MainWindow::*)()>(&MainWindow::onUpdateProgress), this, [=]() {
        ui->WorkArea->setEnabled(finished);
        ui->progress->setValue(percent);
    });
    connect(ui->Inductance_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        UpdateValues(0);
    });
    connect(ui->Resistance_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        UpdateValues(0);
    });
    connect(ui->Current_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        UpdateValues(0);
    });
    connect(ui->Voltage_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        UpdateValues(0);
    });
    connect(ui->Inductance_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        UpdateValues(1);
    });
    connect(ui->Resistance_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        UpdateValues(1);
    });
    connect(ui->Current_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        UpdateValues(1);
    });
    connect(ui->Voltage_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        saveIni(ini);
        UpdateValues(1);
    });
    std::thread(MainWindow::Progress, this).detach();
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
    if(axis == 0) {
        ahp_gt_set_wormsteps(0, fmin(pow(2,24)-1, ahp_gt_get_motor_steps(0)*ahp_gt_get_multiplier(0)*ahp_gt_get_worm_teeth(0)/ahp_gt_get_motor_teeth(0)/ahp_gt_get_divider(0)));
        ahp_gt_set_totalsteps(0, fmin(pow(2,24)-1, ahp_gt_get_wormsteps(0)*ahp_gt_get_crown_teeth(0)));
        ui->Divider0->setText(QString::number(ahp_gt_get_divider(0)));
        ui->Multiplier0->setText(QString::number(ahp_gt_get_multiplier(0)));
        ui->WormSteps0->setText(QString::number(ahp_gt_get_wormsteps(0)));
        ui->TotalSteps0->setText(QString::number(ahp_gt_get_totalsteps(0)));
        ui->TrackingFrequency_0->setText("Step freq (Steps/s): " + QString::number(ahp_gt_get_totalsteps(0)/SIDEREAL_DAY));
        double L = (double)ui->Inductance_0->value()/1000.0;
        double R = (double)ui->Resistance_0->value()/1000.0;
        double mI = (double)ui->Current_0->value()/1000.0;
        double mV = (double)ui->Voltage_0->value();
        double Z = sqrt(fmax(0, pow(mV/mI, 2.0)-pow(R, 2.0)));
        double f = (2.0*M_PI*Z/L);
        ui->MinFrequency_0->setText("PWM (Hz): " + QString::number(f));
    } else if (axis == 1) {
        ahp_gt_set_wormsteps(1, fmin(pow(2,24)-1, ahp_gt_get_motor_steps(1)*ahp_gt_get_multiplier(1)*ahp_gt_get_worm_teeth(1)/ahp_gt_get_motor_teeth(1)/ahp_gt_get_divider(1)));
        ahp_gt_set_totalsteps(1, fmin(pow(2,24)-1, ahp_gt_get_wormsteps(1)*ahp_gt_get_crown_teeth(1)));
        ui->Divider1->setText(QString::number(ahp_gt_get_divider(1)));
        ui->Multiplier1->setText(QString::number(ahp_gt_get_multiplier(1)));
        ui->WormSteps1->setText(QString::number(ahp_gt_get_wormsteps(1)));
        ui->TotalSteps1->setText(QString::number(ahp_gt_get_totalsteps(1)));
        ui->TrackingFrequency_1->setText("RPM: " + QString::number(ahp_gt_get_totalsteps(1)/SIDEREAL_DAY));
        double L = (double)ui->Inductance_1->value()/1000.0;
        double R = (double)ui->Resistance_1->value()/1000.0;
        double mI = (double)ui->Current_1->value()/1000.0;
        double mV = (double)ui->Voltage_1->value();
        double Z = sqrt(fmax(0, pow(mV/mI, 2.0)-pow(R, 2.0)));
        double f = (2.0*M_PI*Z/L);
        ui->MinFrequency_1->setText("PWM (Hz): " + QString::number(f));
    }
}
