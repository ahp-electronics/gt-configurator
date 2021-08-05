#include "mainwindow.h"
#include <ctime>
#include <cstring>
#include <QSerialPort>
#include <QSerialPortInfo>
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
        wnd->ui->progress->setValue(wnd->percent);
        QThread::msleep(100);
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    this->setFixedSize(890, 600);
    ui->setupUi(this);
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (int i = 0; i < ports.length(); i++)
        ui->ComPort->addItem(ports[i].portName());
    if(ports.length() == 0)
        ui->ComPort->addItem("No ports available");
    ui->ComPort->setCurrentIndex(0);
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
       ahp_gt_start_motion(0, ui->Ra_Speed->value());
    });
    connect(ui->Ra_N, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
                   [=](){
       ahp_gt_start_motion(0, -ui->Ra_Speed->value());
    });
    connect(ui->Dec_P, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
                   [=](){
       ahp_gt_start_motion(1, ui->Dec_Speed->value());
    });
    connect(ui->Dec_N, static_cast<void (QPushButton::*)()>(&QPushButton::pressed),
                   [=](){
       ahp_gt_start_motion(1, -ui->Dec_Speed->value());
    });
    connect(ui->Ra_P, static_cast<void (QPushButton::*)()>(&QPushButton::released),
                   [=](){
        ahp_gt_stop_motion(0);
    });
    connect(ui->Ra_N, static_cast<void (QPushButton::*)()>(&QPushButton::released),
                   [=](){
        ahp_gt_stop_motion(0);
    });
    connect(ui->Dec_P, static_cast<void (QPushButton::*)()>(&QPushButton::released),
                   [=](){
        ahp_gt_stop_motion(1);
    });
    connect(ui->Dec_N, static_cast<void (QPushButton::*)()>(&QPushButton::released),
                   [=](){
        ahp_gt_stop_motion(1);
    });
    connect(ui->Tracking, static_cast<void (QCheckBox::*)(bool)>(&QCheckBox::clicked),
                   [=](bool checked){
        if(ui->Tracking->isChecked())
            ahp_gt_start_tracking(0);
        else
            ahp_gt_stop_motion(0);
    });
    connect(ui->Write, static_cast<void (QPushButton::*)(bool)>(&QPushButton::clicked),
            [=](bool checked=false){
        percent = 0;
        UpdateValues(0);
        UpdateValues(1);
        ahp_gt_write_values(0, &percent, &finished);
        ahp_gt_write_values(1, &percent, &finished);
        percent = 0;
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
        if(ahp_gt_connect(port)) {
            ahp_gt_read_values(0);
            ahp_gt_read_values(1);
            ui->WriteArea->setEnabled(true);
            ui->WorkArea->setEnabled(true);
            ui->Control->setEnabled(true);
            int axis = 0;
            ui->Microsteps_0->setValue(ahp_gt_get_microsteps(axis));
            ui->MotorSteps_0->setValue(ahp_gt_get_motor_steps(axis));
            ui->Worm_0->setValue(ahp_gt_get_worm_teeth(axis));
            ui->Motor_0->setValue(ahp_gt_get_motor_teeth(axis));
            ui->Crown_0->setValue(ahp_gt_get_crown_teeth(axis));
            ui->Acceleration_0->setValue(ahp_gt_get_acceleration(axis));
            ui->MaxSpeed_0->setMaximum(ahp_gt_get_speed_limit(axis));
            ui->MaxSpeed_0->setValue(ahp_gt_get_max_speed(axis));
            ui->Coil_0->setCurrentIndex(ahp_gt_get_stepping_conf(axis));
            ui->Invert_0->setChecked(ahp_gt_get_direction_invert(axis));
            ui->GPIO_0->setCurrentIndex(ahp_gt_get_feature(axis));
            axis++;
            ui->Microsteps_1->setValue(ahp_gt_get_microsteps(axis));
            ui->MotorSteps_1->setValue(ahp_gt_get_motor_steps(axis));
            ui->Worm_1->setValue(ahp_gt_get_worm_teeth(axis));
            ui->Motor_1->setValue(ahp_gt_get_motor_teeth(axis));
            ui->Crown_1->setValue(ahp_gt_get_crown_teeth(axis));
            ui->Acceleration_1->setValue(ahp_gt_get_acceleration(axis));
            ui->MaxSpeed_1->setMaximum(ahp_gt_get_speed_limit(axis));
            ui->MaxSpeed_1->setValue(ahp_gt_get_max_speed(axis));
            ui->Coil_1->setCurrentIndex(ahp_gt_get_stepping_conf(axis));
            ui->Invert_1->setChecked(ahp_gt_get_direction_invert(axis));
            ui->GPIO_1->setCurrentIndex(ahp_gt_get_feature(axis));
        }
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
        ahp_gt_set_acceleration(axis, ui->Acceleration_0->value());
        ahp_gt_set_max_speed(axis, ui->MaxSpeed_0->value());
        ahp_gt_set_stepping_conf(axis, (GT1Stepping)ui->Coil_0->currentIndex());
        ahp_gt_set_direction_invert(axis, ui->Invert_0->isChecked());
        ahp_gt_set_features(axis, (SkywatcherFeature)((ui->isAZEQ->isChecked() ? isAZEQ : 0)|hasPPEC));

        ui->Microsteps_0->setValue(ahp_gt_get_microsteps(axis));
        ui->MotorSteps_0->setValue(ahp_gt_get_motor_steps(axis));
        ui->Worm_0->setValue(ahp_gt_get_worm_teeth(axis));
        ui->Motor_0->setValue(ahp_gt_get_motor_teeth(axis));
        ui->Crown_0->setValue(ahp_gt_get_crown_teeth(axis));
        ui->Acceleration_0->setValue(ahp_gt_get_acceleration(axis));
        ui->MaxSpeed_0->setMaximum(ahp_gt_get_speed_limit(axis));
        ui->MaxSpeed_0->setValue(ahp_gt_get_max_speed(axis));
        ui->Coil_0->setCurrentIndex(ahp_gt_get_stepping_conf(axis));
        ui->Invert_0->setChecked(ahp_gt_get_direction_invert(axis));
        ui->GPIO_0->setCurrentIndex(ahp_gt_get_feature(axis));
    } else if (axis == 1) {
        ahp_gt_set_motor_steps(axis, ui->MotorSteps_1->value());
        ahp_gt_set_worm_teeth(axis, ui->Worm_1->value());
        ahp_gt_set_motor_teeth(axis, ui->Motor_1->value());
        ahp_gt_set_crown_teeth(axis, ui->Crown_1->value());
        ahp_gt_set_acceleration(axis, ui->Acceleration_1->value());
        ahp_gt_set_max_speed(axis, ui->MaxSpeed_1->value());
        ahp_gt_set_stepping_conf(axis, (GT1Stepping)ui->Coil_1->currentIndex());
        ahp_gt_set_direction_invert(axis, ui->Invert_1->isChecked());
        ahp_gt_set_features(axis, (SkywatcherFeature)((ui->isAZEQ->isChecked() ? isAZEQ : 0)|hasPPEC));

        ui->Microsteps_1->setValue(ahp_gt_get_microsteps(axis));
        ui->MotorSteps_1->setValue(ahp_gt_get_motor_steps(axis));
        ui->Worm_1->setValue(ahp_gt_get_worm_teeth(axis));
        ui->Motor_1->setValue(ahp_gt_get_motor_teeth(axis));
        ui->Crown_1->setValue(ahp_gt_get_crown_teeth(axis));
        ui->Acceleration_1->setValue(ahp_gt_get_acceleration(axis));
        ui->MaxSpeed_1->setMaximum(ahp_gt_get_speed_limit(axis));
        ui->MaxSpeed_1->setValue(ahp_gt_get_max_speed(axis));
        ui->Coil_1->setCurrentIndex(ahp_gt_get_stepping_conf(axis));
        ui->Invert_1->setChecked(ahp_gt_get_direction_invert(axis));
        ui->GPIO_1->setCurrentIndex(ahp_gt_get_feature(axis));
    }
    ui->isAZEQ->setChecked(ahp_gt_get_features(0) & isAZEQ);
    ahp_gt_set_mount_type(mounttype[ui->MountType->currentIndex()]);
}
