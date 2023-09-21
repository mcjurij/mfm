#ifndef GOTODIALOG_H
#define GOTODIALOG_H

#include <QDialog>

namespace Ui {
class GoToDialog;
}

class GoToDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GoToDialog(QWidget *parent = nullptr);
    ~GoToDialog();

    void setStartDateTime( const QDateTime& dt );
    QDateTime getStartDateTime() const;

    int getRange() const;

private:
    Ui::GoToDialog *ui;
};

#endif // GOTODIALOG_H
