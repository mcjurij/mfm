#include "gotodialog.h"
#include "ui_gotodialog.h"

GoToDialog::GoToDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::GoToDialog)
{
    ui->setupUi(this);
}

GoToDialog::~GoToDialog()
{
    delete ui;
}

void GoToDialog::setStartDateTime( const QDateTime& dt )
{
    ui->startDTEdit->setDateTime(dt);
}

QDateTime GoToDialog::getStartDateTime() const
{
    return ui->startDTEdit->dateTime();
}

int GoToDialog::getRange() const
{
    return ui->rangeSpinBox->value();
}
