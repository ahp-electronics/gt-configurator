#include "mainwindow.h"
#include <ctime>
#include <cstring>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QFileDialog>
#include <errno.h>
#include <libusb.h>
#include "dfu/dfu.h"
#include "./ui_mainwindow.h"

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
        wnd->flashFirmware();
        wnd->finished = 1;
    } else {
        ahp_gt_write_values(0, &wnd->percent, &wnd->finished);
        ahp_gt_write_values(1, &wnd->percent, &wnd->finished);
    }
    wnd->percent = 0;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    isConnected = false;
    this->setFixedSize(770, 645);
    ui->setupUi(this);
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (int i = 0; i < ports.length(); i++)
        ui->ComPort->addItem(ports[i].portName());
    if(ports.length() == 0)
        ui->ComPort->addItem("No ports available");
    ui->ComPort->setCurrentIndex(0);
    connect(ui->LoadFW, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [=](bool checked) {
        firmwareFilename = QFileDialog::getOpenFileName(this, "Select firmware file", ".", "Firmware files (*.dfu)");
        if(firmwareFilename.endsWith(".dfu")) {
            ui->Write->setText("Flash");
            ui->Write->setEnabled(true);
            ui->RA->setEnabled(false);
            ui->DEC->setEnabled(false);
            ui->Control->setEnabled(false);
            ui->commonSettings->setEnabled(false);
            ui->RaInfo->setEnabled(false);
            ui->DecInfo->setEnabled(false);
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
    });
    connect(ui->MotorSteps_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        UpdateValues(0);
    });
    connect(ui->Worm_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        UpdateValues(0);
    });
    connect(ui->Motor_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        UpdateValues(0);
    });
    connect(ui->Crown_0, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        UpdateValues(0);
    });
    connect(ui->MotorSteps_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        UpdateValues(1);
    });
    connect(ui->Worm_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        UpdateValues(1);
    });
    connect(ui->Motor_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
        UpdateValues(1);
    });
    connect(ui->Crown_1, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                   [=](int value){
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
        ui->Acceleration_label_0->setText("Acceleration: "+QString::number(2.0-(double)value/10.0)+"°");
    });
    connect(ui->Acceleration_1, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
                   [=](int value){
        ui->Acceleration_label_1->setText("Acceleration: "+QString::number(2.0-(double)value/10.0)+"°");
    });
    connect(ui->MaxSpeed_0, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
                   [=](int value){
        ui->MaxSpeed_label_0->setText("Maximum speed: "+QString::number(value)+"x");
    });
    connect(ui->MaxSpeed_1, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged),
                   [=](int value){
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
        ui->PWMFreq_label->setText("PWM Frequency: "+QString::number(1500+700*value)+" Hz");
    });

    connect(ui->Write, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [=](bool checked=false){
        if(ui->Write->text() == "Write") {
            UpdateValues(0);
            UpdateValues(1);
        } else {
            ui->ComPort->setEnabled(false);
        }
        std::thread(MainWindow::WriteValues, this).detach();
    });
    connect(ui->ComPort, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
            [=](int index) {
        if(ui->ComPort->currentText() == "No ports available")
            return;
        char port[150] = {0};
        #ifndef _WIN32
            strcat(port, "/dev/");
        #endif
        strcat(port, ports[index].portName().toStdString().c_str());
        if(!ahp_gt_connect(port)) {
            firmwareFilename = "";
            ui->Write->setText("Write");
            ui->Write->setEnabled(true);
            isConnected = true;
            ahp_gt_read_values(0);
            ahp_gt_read_values(1);
            ui->LoadFW->setEnabled(false);
            ui->RA->setEnabled(true);
            ui->DEC->setEnabled(true);
            ui->WorkArea->setEnabled(true);
            ui->Control->setEnabled(true);
            ui->commonSettings->setEnabled(true);
            ui->RaInfo->setEnabled(true);
            ui->DecInfo->setEnabled(true);
            int axis = 0;
            ui->MotorSteps_0->setValue(ahp_gt_get_motor_steps(axis));
            ui->Worm_0->setValue(ahp_gt_get_worm_teeth(axis));
            ui->Motor_0->setValue(ahp_gt_get_motor_teeth(axis));
            ui->Crown_0->setValue(ahp_gt_get_crown_teeth(axis));
            ui->Acceleration_0->setValue(ui->Acceleration_0->maximum()-ahp_gt_get_acceleration_angle(axis)*3600.0/M_PI);
            ui->Ra_Speed->setMaximum(ahp_gt_get_speed_limit(axis));
            ui->MaxSpeed_0->setMaximum(ahp_gt_get_speed_limit(axis));
            ui->MaxSpeed_0->setValue(ahp_gt_get_max_speed(axis));
            ui->Coil_0->setCurrentIndex(ahp_gt_get_stepping_conf(axis));
            ui->Invert_0->setChecked(ahp_gt_get_direction_invert(axis));
            ui->GPIO_0->setCurrentIndex(ahp_gt_get_feature(axis));
            axis++;
            ui->MotorSteps_1->setValue(ahp_gt_get_motor_steps(axis));
            ui->Worm_1->setValue(ahp_gt_get_worm_teeth(axis));
            ui->Motor_1->setValue(ahp_gt_get_motor_teeth(axis));
            ui->Crown_1->setValue(ahp_gt_get_crown_teeth(axis));
            ui->Acceleration_1->setValue(ui->Acceleration_1->maximum()-ahp_gt_get_acceleration_angle(axis)*3600.0/M_PI);
            ui->Dec_Speed->setMaximum(ahp_gt_get_speed_limit(axis));
            ui->MaxSpeed_1->setMaximum(ahp_gt_get_speed_limit(axis));
            ui->MaxSpeed_1->setValue(ahp_gt_get_max_speed(axis));
            ui->Coil_1->setCurrentIndex(ahp_gt_get_stepping_conf(axis));
            ui->Invert_1->setChecked(ahp_gt_get_direction_invert(axis));
            ui->GPIO_1->setCurrentIndex(ahp_gt_get_feature(axis));
            ui->PWMFreq->setValue(ahp_gt_get_pwm_frequency());
        }
    });
    connect(this, static_cast<void (MainWindow::*)()>(&MainWindow::onUpdateProgress), this, [=]() {
        ui->WorkArea->setEnabled(finished);
        ui->progress->setValue(percent);
    });
    std::thread(MainWindow::Progress, this).detach();
}

MainWindow::~MainWindow()
{
    threadsRunning = false;
    delete ui;
}

void MainWindow::UpdateValues(int axis)
{
    if(axis == 0) {
        ahp_gt_set_motor_steps(axis, ui->MotorSteps_0->value());
        ahp_gt_set_worm_teeth(axis, ui->Worm_0->value());
        ahp_gt_set_motor_teeth(axis, ui->Motor_0->value());
        ahp_gt_set_crown_teeth(axis, ui->Crown_0->value());
        ahp_gt_set_acceleration_angle(axis, (ui->Acceleration_0->maximum()-ui->Acceleration_0->value())*M_PI/1800.0);
        ahp_gt_set_max_speed(axis, ui->MaxSpeed_0->value());
        ahp_gt_set_stepping_conf(axis, (GT1Stepping)ui->Coil_0->currentIndex());
        ahp_gt_set_direction_invert(axis, ui->Invert_0->isChecked());
        ahp_gt_set_features(axis, (SkywatcherFeature)((ui->isAZEQ->isChecked() ? isAZEQ : 0)|hasPPEC));

        ui->MotorSteps_0->setValue(ahp_gt_get_motor_steps(axis));
        ui->Worm_0->setValue(ahp_gt_get_worm_teeth(axis));
        ui->Motor_0->setValue(ahp_gt_get_motor_teeth(axis));
        ui->Crown_0->setValue(ahp_gt_get_crown_teeth(axis));
        ui->Acceleration_0->setValue(ui->Acceleration_0->maximum()-ahp_gt_get_acceleration_angle(axis)*1800.0/M_PI);
        ui->Ra_Speed->setMaximum(ahp_gt_get_speed_limit(axis));
        ui->MaxSpeed_0->setMaximum(ahp_gt_get_speed_limit(axis));
        ui->MaxSpeed_0->setValue(ahp_gt_get_max_speed(axis));
        ui->Coil_0->setCurrentIndex(ahp_gt_get_stepping_conf(axis));
        ui->Invert_0->setChecked(ahp_gt_get_direction_invert(axis));
        ui->GPIO_0->setCurrentIndex(ahp_gt_get_feature(axis));
        ui->TotalSteps0->setText(QString::number(ahp_gt_get_totalsteps(axis)));
        ui->WormSteps0->setText(QString::number(ahp_gt_get_wormsteps(axis)));
        ui->Multiplier0->setText(QString::number(ahp_gt_get_multiplier(axis)));
        ui->Divider0->setText(QString::number(ahp_gt_get_divider(axis)));
    } else if (axis == 1) {
        ahp_gt_set_motor_steps(axis, ui->MotorSteps_1->value());
        ahp_gt_set_worm_teeth(axis, ui->Worm_1->value());
        ahp_gt_set_motor_teeth(axis, ui->Motor_1->value());
        ahp_gt_set_crown_teeth(axis, ui->Crown_1->value());
        ahp_gt_set_acceleration_angle(axis,  (ui->Acceleration_1->maximum()-ui->Acceleration_1->value())*M_PI/1800.0);
        ahp_gt_set_max_speed(axis, ui->MaxSpeed_1->value());
        ahp_gt_set_stepping_conf(axis, (GT1Stepping)ui->Coil_1->currentIndex());
        ahp_gt_set_direction_invert(axis, ui->Invert_1->isChecked());
        ahp_gt_set_features(axis, (SkywatcherFeature)((ui->isAZEQ->isChecked() ? isAZEQ : 0)|hasPPEC));

        ui->MotorSteps_1->setValue(ahp_gt_get_motor_steps(axis));
        ui->Worm_1->setValue(ahp_gt_get_worm_teeth(axis));
        ui->Motor_1->setValue(ahp_gt_get_motor_teeth(axis));
        ui->Crown_1->setValue(ahp_gt_get_crown_teeth(axis));
        ui->Acceleration_1->setValue(ui->Acceleration_1->maximum()-ahp_gt_get_acceleration_angle(axis)*1800.0/M_PI);
        ui->Dec_Speed->setMaximum(ahp_gt_get_speed_limit(axis));
        ui->MaxSpeed_1->setMaximum(ahp_gt_get_speed_limit(axis));
        ui->MaxSpeed_1->setValue(ahp_gt_get_max_speed(axis));
        ui->Coil_1->setCurrentIndex(ahp_gt_get_stepping_conf(axis));
        ui->Invert_1->setChecked(ahp_gt_get_direction_invert(axis));
        ui->GPIO_1->setCurrentIndex(ahp_gt_get_feature(axis));
        ui->TotalSteps1->setText(QString::number(ahp_gt_get_totalsteps(axis)));
        ui->WormSteps1->setText(QString::number(ahp_gt_get_wormsteps(axis)));
        ui->Multiplier1->setText(QString::number(ahp_gt_get_multiplier(axis)));
        ui->Divider1->setText(QString::number(ahp_gt_get_divider(axis)));
    }
    ahp_gt_set_pwm_frequency(ui->PWMFreq->value());
    ui->isAZEQ->setChecked(ahp_gt_get_features(0) & isAZEQ);
    ahp_gt_set_mount_type(mounttype[ui->MountType->currentIndex()]);
}
