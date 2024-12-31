#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QInputDialog>
#include "qcustomplot.h"
#include "pointofinterest.h"
#include "axistag.h"


class IncidentMarker;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void titleDoubleClick(QMouseEvent *event);
    void axisLabelDoubleClick(QCPAxis* axis, QCPAxis::SelectablePart part);
    void legendDoubleClick(QCPLegend* legend, QCPAbstractLegendItem* item);
    void selectionChanged();
    void xAxisRangeChanged(QCPRange r);
    void mousePress();
    void mouseWheel();
    void removeSelectedGraph();
    void removeAllGraphs();
    void contextMenuRequest(QPoint pos);
    void moveLegend();
    void graphClicked(QCPAbstractPlottable *plottable, int dataIndex);

    void fileReadMeasurements();
    void fileReadGridTime();
    void fileReadIncidents();
    void removeIncidents();
    void quit();

    void followMode(bool checked);
    void followMode5Min(bool checked);
    void followMode15Min(bool checked);

    void goTo();

    // point of interests navigation
    void setJumpToPOIEnabled( bool e );

    void jumpToFirstPOI();
    void jumpToLastPOI();
    void jumpToNextPOI();
    void jumpToPreviousPOI();

    void findPOIGraph0();

    void doFollow();  // periodically called by followTimer

    void interactionHelp();

private:
    enum class DType { MEAS, GRIDTIME };
    enum class YAxisType { YAXIS_UNSET, YAXIS_MEAS, YAXIS_GRIDTIME};

    void addGraph( const QVector<double>& x, const QVector<double>& y, const QString& name, DType type);
    void addIncident( double time_s, const QString& text);
    void addIncidents();
    int searchPointsOfInterest( const QVector<double>& time_s, const QVector<double>& freq);
    void removePointsOfInterest();
    void showPointOfInterest( const PointOfInterest& pos);
    void startFollowMode(bool start, double range);
    int readMeasurements( const QString& fn, QVector<int64_t>& time, QVector<double>& freq);
    int readGridTime( const QString& fileName, QVector<double>& time, QVector<double>& gt_offset);
    void doInfo( const QCPRange& r );

private:    
    struct posInfo {
        qint64 pos;
        DType  type;
    };
    YAxisType yAxisType;
    Ui::MainWindow *ui;
    QTimer followTimer;
    QMap<QString, posInfo>  oldFilePos;
    QList<int64_t> incidTimestamps;
    QMap<int64_t, QStringList> incidents;
    QList<IncidentMarker *> incidMarkers;
    QString incidFileName;
    // QString gridTimeFileName;
    QList<PointOfInterest> pointsOfInterest;
    int lastPOSShown;

    bool   followModeActive;
    qint64 oldIncidFilePos;
    QStringList incidFollowCollect;
    int64_t incidFollow_prev_time_us;
    double followRange;
    AxisTag *followTag;  // see https://www.qcustomplot.com/index.php/tutorials/specialcases/axistags
};

#endif // MAINWINDOW_H
