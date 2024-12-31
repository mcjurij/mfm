#include <fstream>

#include "conf.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "incidentmarker.h"
#include "gotodialog.h"

static const QString interaction_help("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN" "http://www.w3.org/TR/REC-html40/strict.dtd\">"
                                      "<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">p, li { white-space: pre-wrap; }"
                                      "</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">"
                                      "<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-weight:600;\">Select the axes</span> to drag and zoom them individually.</p>"
                                      "<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-weight:600;\">Double click labels</span> or legend items to set user specified strings.</p>"
                                      "<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-weight:600;\">Left click</span> on graphs or legend to select graphs.</p>"
                                      "<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-weight:600;\">Right click</span> for a popup menu to add/remove graphs and move the legend</p></body></html>"
    );


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    // std::srand(QDateTime::currentDateTime().toMSecsSinceEpoch()/1000.0);
    std::srand( 4711 );
    ui->setupUi(this);

    ui->customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectAxes | QCP::iSelectLegend | QCP::iSelectPlottables);
    //ui->customPlot->xAxis->setRange(-8, 8);
    ui->customPlot->yAxis->setRange( Conf::GRAPH_DEFAULT_YMIN, Conf::GRAPH_DEFAULT_YMAX);
    yAxisType = YAxisType::YAXIS_MEAS;
    ui->customPlot->axisRect()->axis(QCPAxis::atRight, 0)->setPadding(5);
    ui->customPlot->axisRect()->setupFullAxesBox();
    //ui->customPlot->axisRect()->setRangeZoom(Qt::Horizontal);
    ui->customPlot->axisRect()->setRangeZoomAxes(nullptr, nullptr);

    ui->customPlot->axisRect()->setRangeDragAxes( nullptr, nullptr);

    followTag = nullptr;

    ui->customPlot->yAxis->setTickLabels(true);
    ui->customPlot->yAxis2->setTickLabels(true);
    connect(ui->customPlot->yAxis2, SIGNAL(rangeChanged(QCPRange)), ui->customPlot->yAxis, SLOT(setRange(QCPRange))); // left axis mirrors right axis
    ui->customPlot->yAxis2->setVisible(true);

    ui->customPlot->yAxis->setNumberFormat("f");
    ui->customPlot->yAxis2->setNumberFormat("f");
    ui->customPlot->yAxis->setNumberPrecision(3);
    ui->customPlot->yAxis2->setNumberPrecision(3);

    QSharedPointer<QCPAxisTickerDateTime> dateTimeTicker(new QCPAxisTickerDateTime);
    // use this for UTC on y-axis: dateTimeTicker->setTimeZone(QTimeZone::utc());
    ui->customPlot->xAxis->setTicker(dateTimeTicker);
    // dateTimeTicker->setDateTimeFormat("yyyy-MM-dd HH:mm:ss");
    dateTimeTicker->setDateTimeFormat("HH:mm:ss");

    ui->customPlot->plotLayout()->insertRow(0);
    QCPTextElement *title = new QCPTextElement(ui->customPlot, "Mains Frequency", QFont("sans", 12, QFont::Bold));
    ui->customPlot->plotLayout()->addElement(0, 0, title);

    ui->customPlot->xAxis->setLabel("Time");
    ui->customPlot->yAxis->setLabel("Frequency");

    ui->customPlot->legend->setVisible(true);
    QFont legendFont = font();
    legendFont.setPointSize(10);
    ui->customPlot->legend->setFont(legendFont);
    ui->customPlot->legend->setSelectedFont(legendFont);
    ui->customPlot->legend->setSelectableParts(QCPLegend::spItems); // legend box shall not be selectable, only legend items

    // connect slot that ties some axis selections together (especially opposite axes):
    connect(ui->customPlot, &QCustomPlot::selectionChangedByUser, this, &MainWindow::selectionChanged);
    // connect slots that takes care that when an axis is selected, only that direction can be dragged and zoomed:
    // connect(ui->customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(mousePress()));
    connect(ui->customPlot,  &QCustomPlot::mousePress, this, &MainWindow::mousePress);

    // connect(ui->customPlot, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(mouseWheel()));
    connect(ui->customPlot, &QCustomPlot::mouseWheel, this, &MainWindow::mouseWheel);

    // make bottom and left axes transfer their ranges to top and right axes:
    connect(ui->customPlot->xAxis, SIGNAL(rangeChanged(QCPRange)), ui->customPlot->xAxis2, SLOT(setRange(QCPRange)));
    // does not work -> connect(ui->customPlot->xAxis, qOverload<QCPRange>(&QCPAxis::rangeChanged), ui->customPlot->xAxis2, qOverload<QCPRange>(&QCPAxis::setRange));
    connect(ui->customPlot->yAxis, SIGNAL(rangeChanged(QCPRange)), ui->customPlot->yAxis2, SLOT(setRange(QCPRange)));

    connect(ui->customPlot->xAxis, SIGNAL(rangeChanged(QCPRange)), this, SLOT(xAxisRangeChanged(QCPRange)));

    // connect some interaction slots:
    connect(ui->customPlot, &QCustomPlot::axisDoubleClick, this, &MainWindow::axisLabelDoubleClick);
    connect(ui->customPlot, &QCustomPlot::legendDoubleClick, this, &MainWindow::legendDoubleClick);
    connect(title, &QCPTextElement::doubleClicked, this, &MainWindow::titleDoubleClick);

    // connect slot that shows a message in the status bar when a graph is clicked:
    connect(ui->customPlot, &QCustomPlot::plottableClick, this, &MainWindow::graphClicked);

    // setup policy and connect slot for context menu popup:
    ui->customPlot->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->customPlot, &QCustomPlot::customContextMenuRequested, this, &MainWindow::contextMenuRequest);

    // File menu
    connect(ui->actionRead_measurements, &QAction::triggered, this, &MainWindow::fileReadMeasurements);
    connect(ui->actionRead_grid_time, &QAction::triggered, this, &MainWindow::fileReadGridTime);
    connect(ui->actionRead_incidents, &QAction::triggered, this, &MainWindow::fileReadIncidents);
    connect(ui->actionRemove_incidents, &QAction::triggered, this, &MainWindow::removeIncidents);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::quit);

    // View menu
    connect(ui->actionFollow_mode, &QAction::triggered, this, &MainWindow::followMode);
    connect(ui->actionFollow_mode_5, &QAction::triggered, this, &MainWindow::followMode5Min);
    connect(ui->actionFollow_mode_15, &QAction::triggered, this, &MainWindow::followMode15Min);
    connect(ui->actionGo_to, &QAction::triggered, this, &MainWindow::goTo);
    connect(ui->actionFirst_POI, &QAction::triggered, this, &MainWindow::jumpToFirstPOI);
    connect(ui->actionLast_POI, &QAction::triggered, this, &MainWindow::jumpToLastPOI);
    connect(ui->actionNext_POI, &QAction::triggered, this, &MainWindow::jumpToNextPOI);
    connect(ui->actionPrevious_POI, &QAction::triggered, this, &MainWindow::jumpToPreviousPOI);
    setJumpToPOIEnabled(false);

    // Analyze menu
    connect(ui->actionFind_PoIs, &QAction::triggered, this, &MainWindow::findPOIGraph0);

    // Help menu
    connect(ui->actionInteractions, &QAction::triggered, this, &MainWindow::interactionHelp);
    connect(ui->actionAbout_Qt, &QAction::triggered, this, &QApplication::aboutQt);

    // timer used for View->Follow modes
    connect(&followTimer, SIGNAL(timeout()), this, SLOT(doFollow()));

    incidFollow_prev_time_us = 0;
    followModeActive = false;
    ui->labelInfo->setText("\n\n\n\n\n");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::titleDoubleClick(QMouseEvent* event)
{
    Q_UNUSED(event)

    if (QCPTextElement *title = qobject_cast<QCPTextElement*>(sender()))
    {
        // Set the plot title by double clicking on it
        bool ok;
        QString newTitle = QInputDialog::getText(this, "", "New plot title:", QLineEdit::Normal, title->text(), &ok);
        if (ok)
        {
            title->setText(newTitle);
            ui->customPlot->replot();
        }
    }
}

void MainWindow::axisLabelDoubleClick(QCPAxis *axis, QCPAxis::SelectablePart part)
{
    // Set an axis label by double clicking on it
    if (part == QCPAxis::spAxisLabel) // only react when the actual axis label is clicked, not tick label or axis backbone
    {
        bool ok;
        QString newLabel = QInputDialog::getText(this, "", "New axis label:", QLineEdit::Normal, axis->label(), &ok);
        if (ok)
        {
            axis->setLabel(newLabel);
            ui->customPlot->replot();
        }
    }
}

void MainWindow::legendDoubleClick(QCPLegend *legend, QCPAbstractLegendItem *item)
{
    // Rename a graph by double clicking on its legend item
    Q_UNUSED(legend)
    Q_UNUSED(item)
    /*
    if (item) // only react if item was clicked (user could have clicked on border padding of legend where there is no item, then item is 0)
    {
        QCPPlottableLegendItem *plItem = qobject_cast<QCPPlottableLegendItem*>(item);
        bool ok;
        QString newName = QInputDialog::getText(this, "", "New graph name:", QLineEdit::Normal, plItem->plottable()->name(), &ok);
        if (ok)
        {
            plItem->plottable()->setName(newName);
            ui->customPlot->replot();
        }
    }
    */
}

void MainWindow::selectionChanged()
{
    /*
     normally, axis base line, axis tick labels and axis labels are selectable separately, but we want
     the user only to be able to select the axis as a whole, so we tie the selected states of the tick labels
     and the axis base line together. However, the axis label shall be selectable individually.

     The selection state of the left and right axes shall be synchronized as well as the state of the
     bottom and top axes.

     Further, we want to synchronize the selection of the graphs with the selection state of the respective
     legend item belonging to that graph. So the user can select a graph by either clicking on the graph itself
     or on its legend item.
    */

    // make top and bottom axes be selected synchronously, and handle axis and tick labels as one selectable object:
    if (ui->customPlot->xAxis->selectedParts().testFlag(QCPAxis::spAxis) || ui->customPlot->xAxis->selectedParts().testFlag(QCPAxis::spTickLabels) ||
        ui->customPlot->xAxis2->selectedParts().testFlag(QCPAxis::spAxis) || ui->customPlot->xAxis2->selectedParts().testFlag(QCPAxis::spTickLabels))
    {
        ui->customPlot->xAxis2->setSelectedParts(QCPAxis::spAxis|QCPAxis::spTickLabels);
        ui->customPlot->xAxis->setSelectedParts(QCPAxis::spAxis|QCPAxis::spTickLabels);
    }
    // make left and right axes be selected synchronously, and handle axis and tick labels as one selectable object:
    if (ui->customPlot->yAxis->selectedParts().testFlag(QCPAxis::spAxis) || ui->customPlot->yAxis->selectedParts().testFlag(QCPAxis::spTickLabels) ||
        ui->customPlot->yAxis2->selectedParts().testFlag(QCPAxis::spAxis) || ui->customPlot->yAxis2->selectedParts().testFlag(QCPAxis::spTickLabels))
    {
        ui->customPlot->yAxis2->setSelectedParts(QCPAxis::spAxis|QCPAxis::spTickLabels);
        ui->customPlot->yAxis->setSelectedParts(QCPAxis::spAxis|QCPAxis::spTickLabels);
    }

    // synchronize selection of graphs with selection of corresponding legend items:
    for( int i=0; i<ui->customPlot->graphCount(); i++)
    {
        QCPGraph *graph = ui->customPlot->graph(i);
        QCPPlottableLegendItem *item = ui->customPlot->legend->itemWithPlottable(graph);
        if (item->selected() || graph->selected())
        {
            item->setSelected(true);
            graph->setSelection(QCPDataSelection(graph->data()->dataRange()));
        }
    }
}

void MainWindow::xAxisRangeChanged(QCPRange r)
{
    doInfo( r );
}

void MainWindow::mousePress()
{
    // if an axis is selected, only allow the direction of that axis to be dragged
    // if no axis is selected, both directions may be dragged

    if (ui->customPlot->xAxis->selectedParts().testFlag(QCPAxis::spAxis))
    {
        ui->customPlot->axisRect()->setRangeDragAxes( ui->customPlot->xAxis, nullptr);
        ui->customPlot->axisRect()->setRangeDrag(ui->customPlot->xAxis->orientation());
    }
    else if (ui->customPlot->yAxis->selectedParts().testFlag(QCPAxis::spAxis))
    {
        ui->customPlot->axisRect()->setRangeDragAxes( nullptr, ui->customPlot->yAxis);
        ui->customPlot->axisRect()->setRangeDrag(ui->customPlot->yAxis->orientation());
    }
    else
        ui->customPlot->axisRect()->setRangeDragAxes( nullptr, nullptr);
    //  ui->customPlot->axisRect()->setRangeDrag(Qt::Horizontal|Qt::Vertical);
}

void MainWindow::mouseWheel()
{
    // if an axis is selected, only allow the direction of that axis to be zoomed

    if (ui->customPlot->xAxis->selectedParts().testFlag(QCPAxis::spAxis))
    {
        ui->customPlot->axisRect()->setRangeZoomAxes( ui->customPlot->xAxis, nullptr);
        ui->customPlot->axisRect()->setRangeZoom(ui->customPlot->xAxis->orientation());
        if( followModeActive )
            followRange = ui->customPlot->xAxis->range().upper - ui->customPlot->xAxis->range().lower;
    }
    else if (ui->customPlot->yAxis->selectedParts().testFlag(QCPAxis::spAxis))
    {
        ui->customPlot->axisRect()->setRangeZoomAxes( nullptr, ui->customPlot->yAxis);
        ui->customPlot->axisRect()->setRangeZoom(ui->customPlot->yAxis->orientation());
    }
    else
        ui->customPlot->axisRect()->setRangeZoomAxes(nullptr, nullptr);
    //  ui->customPlot->axisRect()->setRangeZoom(Qt::Horizontal|Qt::Vertical);
}


void MainWindow::addGraph( const QVector<double>& x, const QVector<double>& y, const QString& name, DType type)
{
    ui->customPlot->addGraph();

    ui->customPlot->graph()->setName( name );
    ui->customPlot->graph()->setData( x, y, true);
    ui->customPlot->graph()->setLineStyle( QCPGraph::lsLine );
    ui->customPlot->graph()->setScatterStyle( QCPScatterStyle::ssCross );

    // Internally QCustomPlot treats date/time as double (dateTimeToKey() returns a double)
    //ui->customPlot->xAxis->setRange(QCPAxisTickerDateTime::dateTimeToKey(QDate(2013, 11, 16)), QCPAxisTickerDateTime::dateTimeToKey(QDate(2015, 5, 2)));

    if( ui->customPlot->graphCount() == 1 )
        ui->customPlot->xAxis->setRange( x.first(), x.last());

    QPen graphPen;
    graphPen.setColor(QColor(std::rand()%245+10, std::rand()%45+10, std::rand()%245+10));
    graphPen.setWidthF(2.0);
    ui->customPlot->graph()->setPen(graphPen);


    if( yAxisType != YAxisType::YAXIS_MEAS && type == DType::MEAS )
    {
        ui->customPlot->yAxis->setRange( Conf::GRAPH_DEFAULT_YMIN, Conf::GRAPH_DEFAULT_YMAX);
        yAxisType = YAxisType::YAXIS_MEAS;
    }
    else if( yAxisType != YAxisType::YAXIS_GRIDTIME && type == DType::GRIDTIME ) {
        ui->customPlot->yAxis->setRange( Conf::GRAPH_GRIDTIME_YMIN, Conf::GRAPH_GRIDTIME_YMAX);
        yAxisType = YAxisType::YAXIS_GRIDTIME;
    }

    ui->customPlot->replot();
}


void MainWindow::removeSelectedGraph()
{
    if (ui->customPlot->selectedGraphs().size() > 0)
    {
        bool removedFirst = false;
        QCPGraph* graphSel = ui->customPlot->selectedGraphs().first();
        if( ui->customPlot->graphCount() == 0 )
            startFollowMode( false, 0.);
        else
        {
            QCPGraph *graph = ui->customPlot->graph(0);
            QString name = graph->name();

            if( graphSel == graph )
            {
                qDebug() << "Removed first " << name;
                removedFirst = true;
            }
        }

        oldFilePos.remove( graphSel->name() );

        ui->customPlot->removeGraph( graphSel );
        ui->customPlot->replot();

        doInfo( ui->customPlot->xAxis->range() );

        if( removedFirst && ui->customPlot->graphCount() > 0)
        {
            QCPGraph *graph = ui->customPlot->graph(0);
            QString name = graph->name();

            if( oldFilePos.constFind(name)->type == DType::MEAS )
                findPOIGraph0();
        }
    }
}


void MainWindow::removeAllGraphs()
{
    ui->customPlot->clearGraphs();
    ui->customPlot->replot();

    oldFilePos.clear();

    startFollowMode( false, 0.);
    doInfo( QCPRange( 0., 0.) );
}


void MainWindow::contextMenuRequest(QPoint pos)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    if (ui->customPlot->legend->selectTest(pos, false) >= 0) // context menu on legend requested
    {
        menu->addAction("Move to top left", this, &MainWindow::moveLegend)->setData((int)(Qt::AlignTop|Qt::AlignLeft));
        menu->addAction("Move to top center", this, &MainWindow::moveLegend)->setData((int)(Qt::AlignTop|Qt::AlignHCenter));
        menu->addAction("Move to top right", this, &MainWindow::moveLegend)->setData((int)(Qt::AlignTop|Qt::AlignRight));
        menu->addAction("Move to bottom right", this, &MainWindow::moveLegend)->setData((int)(Qt::AlignBottom|Qt::AlignRight));
        menu->addAction("Move to bottom left", this, &MainWindow::moveLegend)->setData((int)(Qt::AlignBottom|Qt::AlignLeft));
    }
    else  // general context menu on graphs requested
    {
        // menu->addAction("Add random graph", this, SLOT(addRandomGraph()));
        // menu->addAction("Add random graph", this, &MainWindow::addRandomGraph);
        if (ui->customPlot->selectedGraphs().size() > 0)
            menu->addAction("Remove selected graph", this, &MainWindow::removeSelectedGraph);
        if (ui->customPlot->graphCount() > 0)
            menu->addAction("Remove all graphs", this, &MainWindow::removeAllGraphs);
    }

    menu->popup(ui->customPlot->mapToGlobal(pos));
}


void MainWindow::moveLegend()
{
    if (QAction* contextAction = qobject_cast<QAction*>(sender())) // make sure this slot is really called by a context menu action, so it carries the data we need
    {
        bool ok;
        int dataInt = contextAction->data().toInt(&ok);
        if (ok)
        {
            ui->customPlot->axisRect()->insetLayout()->setInsetAlignment(0, (Qt::Alignment)dataInt);
            ui->customPlot->replot();
        }
    }
}


void MainWindow::graphClicked(QCPAbstractPlottable *plottable, int dataIndex)
{
    // since we know we only have QCPGraphs in the plot, we can immediately access interface1D()
    // usually it's better to first check whether interface1D() returns non-zero, and only then use it.
    double dataValue = plottable->interface1D()->dataMainValue(dataIndex);
    QString message = QString("Clicked on graph '%1' at data point #%2 with value %3.").arg(plottable->name()).arg(dataIndex).arg(dataValue);
    ui->statusBar->showMessage(message, 2500);
}


static double convertTimestamp( int64_t time_us );
static void convertTimestamps( const QVector<int64_t>& time, QVector<double>& time_s);

void MainWindow::fileReadMeasurements()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("Open measurement file"), "", tr("Text Files (*.txt)"));

    if( !filePath.isNull() )
    {
        QFileInfo fi( filePath );

        if( !fi.fileName().startsWith( "meas_" ) )
        {
            QMessageBox::StandardButton reply = QMessageBox::question( this, "", "File name does not start with \"meas_\", load anyway?",
                QMessageBox::Yes | QMessageBox::No);
            if( reply == QMessageBox::No )
                return;
        }

        QVector<int64_t> time;
        QVector<double> freq;

        int lines = readMeasurements( filePath, time, freq);

        qInfo() << "read " << lines << " lines";

        QFile f( filePath );
        oldFilePos.insert( filePath, posInfo { .pos = f.size() , .type = DType::MEAS} );
       //  oldFilePos.insert(filePath, f.size());

        if( lines > 0 )
        {
            QVector<double> time_s( time.size() );
            convertTimestamps( time, time_s);

            if( ui->customPlot->graphCount() == 0 )  // do PoIs only for first graph
            {
                int cnt = searchPointsOfInterest( time_s, freq);

                if( cnt > 0 )
                    setJumpToPOIEnabled( true );
                else
                    setJumpToPOIEnabled( false );

                QMessageBox msgBox(QMessageBox::Information, "PoIs", "Points of interest in '" + filePath + "' found: " + QString::number(cnt), {}, this);
                msgBox.exec();
                ui->statusBar->showMessage( QString::number(cnt) + " points of interest in measurement file", 0);
            }

            addGraph( time_s, freq, filePath, DType::MEAS);

            doInfo( ui->customPlot->xAxis->range() );
        }
    }
}


int MainWindow::searchPointsOfInterest( const QVector<double>& time_s, const QVector<double>& freq)
{
    Q_ASSERT( time_s.size() == freq.size() );
    removePointsOfInterest();

    QList<PointOfInterest> poiCollect;
    for( int n = 0; n < time_s.size(); n++)
    {

        if( freq[n] > Conf::POI_MAINS_FREQ_TOO_HIGH )
        {
            PointOfInterest poi;

            poi.reason = POIReason::FreqTooHigh;
            poi.regionBegin = time_s[n++];

            while( n < time_s.size() && freq[n] > Conf::POI_MAINS_FREQ_TOO_HIGH )
                n++;

            if( n < time_s.size() )
                poi.regionEnd = time_s[n];
            else
                poi.regionEnd = time_s.last();

            poiCollect.append( poi );
        }
        else if( freq[n] < Conf::POI_MAINS_FREQ_TOO_LOW )
        {
            PointOfInterest poi;

            poi.reason = POIReason::FreqTooLow;
            poi.regionBegin = time_s[n++];

            while( n < time_s.size() && freq[n] < Conf::POI_MAINS_FREQ_TOO_LOW )
                n++;

            if( n < time_s.size() )
                poi.regionEnd = time_s[n];
            else
                poi.regionEnd = time_s.last();

            poiCollect.append( poi );
        }
    }

    if( poiCollect.size() > 1 )
    {
        QList<PointOfInterest> poiTmp = poiCollect;
        QList<PointOfInterest> poiMerged;

        // find points of interest that are close together and merge them
        while( !poiTmp.isEmpty() )
        {
            PointOfInterest poi = poiTmp.takeFirst();
            if( poiTmp.isEmpty() )
            {
                poiMerged.append(poi);
                break;
            }

            double newEnd = poi.regionEnd;
            while( !poiTmp.isEmpty() && ( poiTmp.first().regionBegin - newEnd ) < 60. * 2. )
            {
                newEnd = poiTmp.first().regionEnd;
                poiTmp.removeFirst();
            }

            poi.regionEnd = newEnd;

            poiMerged.append(poi);
        }

        pointsOfInterest = poiMerged;

        qDebug() << "Reduced poi size from" << poiCollect.size() << "to" << pointsOfInterest.size();
    }
    else
        pointsOfInterest = poiCollect;

    return pointsOfInterest.size();
}


void MainWindow::removePointsOfInterest()
{
    pointsOfInterest.clear();
    lastPOSShown = 0;
}


void MainWindow::fileReadIncidents()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("Open incidents file"), "", tr("Text Files (*.txt)"));

    if( !filePath.isNull() )
    {
        removeIncidents();

        QFileInfo fi( filePath );

        if( !fi.fileName().startsWith( "incidents_" ) )
        {
            QMessageBox::StandardButton reply = QMessageBox::question( this, "", "File name does not start with \"incidents_\", load anyway?",
                QMessageBox::Yes | QMessageBox::No);
            if( reply == QMessageBox::No )
                return;
        }

        incidents.clear();
        incidTimestamps.clear();

        QFile f(filePath);
        f.open( QIODevice::ReadOnly );

        QTextStream fs(&f);

        bool firstLine = true;
        int64_t prev_time_us = 0;
        QStringList collect;

        while( !fs.atEnd() )
        {
            QString line = fs.readLine();
            int firstSpace = line.indexOf(' ');

            // timestamp is 16 digits long
            if( firstSpace < 16 )
                continue;

            QString tsStr = line.left( firstSpace );
            QString incidStr = line.right( line.length() - firstSpace - 1 );

            int64_t time_us = int64_t( atoll( tsStr.toStdString().c_str() ) );

            if( firstLine )
            {
                firstLine = false;
                prev_time_us = time_us;
            }

            //QString dbg = QString( ">%1< >%2<").arg(tsStr,incidStr);
            //qDebug() << "line has " << dbg << "      with time " << time_us;

            if( prev_time_us == time_us )
            {
                if( incidStr != "INCID_END" )
                    collect.append( incidStr );
                else
                {
                    if( collect.size() > 0 )
                    {
                        incidTimestamps.append( time_us );
                        incidents.insert( time_us, collect);

                        collect.clear();
                        firstLine = true;
                    }
                }
            }

            prev_time_us = time_us;
        }

        oldIncidFilePos = f.pos();

        f.close();

        addIncidents();
        incidFileName = filePath;
        doInfo( ui->customPlot->xAxis->range() );
    }
}


void MainWindow::addIncident( double time_s, const QString& text)
{
    IncidentMarker *im = new IncidentMarker( ui->customPlot, ColorTheme::Dark );
    incidMarkers.append( im );

    im->start->setCoords( time_s, Conf::INCIDENTMARKER_YBEG);
    im->end->setCoords( time_s, Conf::INCIDENTMARKER_YEND);

    // split fline to multiple lines so that the boxes don't get too wide
    QString localDT = text.left( 26 );
    QString msg = text.right( text.length() - 27 );

    QString labelText = localDT + "\n" + msg.split(",").join("\n");
    im->setText( labelText );
}


void MainWindow::addIncidents()
{
    incidMarkers.clear();

    for( int64_t ts : incidTimestamps )
    {
        QStringList collect = incidents.value( ts );
        QString fline = collect.at(0);

        addIncident( convertTimestamp( ts ), fline);
    }

    ui->customPlot->replot();
}


void MainWindow::removeIncidents()
{
    incidFileName = QString();

    for( IncidentMarker *im : incidMarkers)
    {
        im->removeItems();
        ui->customPlot->removeItem( im );
    }
    incidents.clear();
    incidTimestamps.clear();
    incidMarkers.clear();
    doInfo( ui->customPlot->xAxis->range() );

    ui->customPlot->replot();
}


void MainWindow::fileReadGridTime()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("Open grid time file"), "", tr("Text Files (*.txt)"));

    if( !filePath.isNull() )
    {
        QFileInfo fi( filePath );

        if( !fi.fileName().startsWith( "gridtime_" ) )
        {
            QMessageBox::StandardButton reply = QMessageBox::question( this, "", "File name does not start with \"gridtime_\", load anyway?",
                QMessageBox::Yes | QMessageBox::No);
            if( reply == QMessageBox::No )
                return;
        }

        QVector<double> time;
        QVector<double> gt_offset;

        int lines = readGridTime( filePath, time, gt_offset);

        qInfo() << "read " << lines << " lines";

        QFile f( filePath );
        oldFilePos.insert( filePath, posInfo { .pos = f.size(), .type = DType::GRIDTIME} );

        if( lines > 0 )
        {
            addGraph( time, gt_offset, filePath, DType::GRIDTIME);

            doInfo( ui->customPlot->xAxis->range() );
        }
    }
}


void MainWindow::quit()
{
    followTimer.stop();
    QApplication::quit();
}


void MainWindow::followMode(bool checked)
{
    if( checked )
        ui->actionFollow_mode_5->setChecked(false);
    if( checked )
        ui->actionFollow_mode_15->setChecked(false);
    startFollowMode( checked, ui->customPlot->xAxis->range().upper - ui->customPlot->xAxis->range().lower);
}

void MainWindow::followMode5Min(bool checked)
{
    if( checked )
        ui->actionFollow_mode->setChecked(false);
    if( checked )
        ui->actionFollow_mode_15->setChecked(false);
    startFollowMode( checked, 60. * 5.);
}

void MainWindow::followMode15Min(bool checked)
{
    if( checked )
        ui->actionFollow_mode->setChecked(false);
    if( checked )
        ui->actionFollow_mode_5->setChecked(false);
    startFollowMode( checked, 60. * 15.);
}


void MainWindow::goTo()
{
    GoToDialog  goToDialog( this );
    goToDialog.setStartDateTime(QDateTime::currentDateTime());

    if (goToDialog.exec() == QDialog::Accepted) {
        startFollowMode( false, 0.);

        qint64 start = goToDialog.getStartDateTime().toSecsSinceEpoch();
        double beg = start;
        double end = (double)(start + (qint64)goToDialog.getRange() * 60LL);
        ui->customPlot->xAxis->setRange( beg, end);
        ui->customPlot->replot();
    }
}


void MainWindow::setJumpToPOIEnabled( bool e )
{
    ui->menuJump_to->setEnabled(e);
}


void MainWindow::showPointOfInterest( const PointOfInterest& poi )
{
    double beg = poi.regionBegin - 60. * 5.;
    double end = poi.regionEnd + 60. * 5.;

    ui->customPlot->xAxis->setRange( beg, end);
    ui->customPlot->replot();
}


void MainWindow::jumpToFirstPOI()
{
    if( pointsOfInterest.size() > 0 )
    {
        startFollowMode( false, 0.);
        lastPOSShown = 0;
        ui->statusBar->showMessage( "Jumped to first point of interest. " + QString::number(pointsOfInterest.size()) + " points of interest in measurement file", 0);
        showPointOfInterest( pointsOfInterest.at( lastPOSShown ) );
    }
}

void MainWindow::jumpToLastPOI()
{
    if( pointsOfInterest.size() > 0 )
    {
        startFollowMode( false, 0.);
        lastPOSShown = pointsOfInterest.size()-1;
        ui->statusBar->showMessage( "Jumped to last point of interest. " + QString::number(pointsOfInterest.size()) + " points of interest in measurement file", 0);
        showPointOfInterest( pointsOfInterest.at( lastPOSShown ) );
    }
}

void MainWindow::jumpToNextPOI()
{
    if( pointsOfInterest.size() > 0 && lastPOSShown < pointsOfInterest.size() - 1 )
    {
        startFollowMode( false, 0.);
        lastPOSShown++;
        ui->statusBar->showMessage( "Jumped to point of interest #" + QString::number(lastPOSShown+1) + " of " + QString::number(pointsOfInterest.size()), 0);
        showPointOfInterest( pointsOfInterest.at( lastPOSShown ) );
    }
}

void MainWindow::jumpToPreviousPOI()
{
    if( pointsOfInterest.size() > 0 && lastPOSShown > 0 )
    {
        startFollowMode( false, 0.);
        lastPOSShown--;
        ui->statusBar->showMessage( "Jumped to point of interest #" + QString::number(lastPOSShown+1) + " of " + QString::number(pointsOfInterest.size()), 0);
        showPointOfInterest( pointsOfInterest.at( lastPOSShown ) );
    }
}


void MainWindow::findPOIGraph0()
{
    if( ui->customPlot->graphCount() > 0)
    {
        QCPGraph *graph = ui->customPlot->graph(0);
        QString name = graph->name();

        // unfortunately we have to copy data from the graph to QVectors for searchPointOfInterests()
        QSharedPointer<QCPGraphDataContainer> gd = graph->data();
        auto b = gd->constBegin();
        auto e = gd->constEnd();
        QVector<double> time_s(gd->size()), freq(gd->size());

        int i;
        for( i = 0; b != e; i++,b++)
        {
            time_s[i] = b->key;
            freq[i] = b->value;
        }

        int cnt = searchPointsOfInterest( time_s, freq);

        if( cnt > 0 )
            setJumpToPOIEnabled( true );
        else
            setJumpToPOIEnabled( false );

        QMessageBox msgBox( QMessageBox::Information, "PoIs", "Points of interest in '" + name + "' found: " + QString::number(cnt), {}, this);
        msgBox.exec();
        ui->statusBar->showMessage( QString::number(cnt) + " points of interest in measurement file", 0);
    }
    else
    {
        QMessageBox msgBox( QMessageBox::Information, "PoIs", "At least one measurement file has to be loaded", {}, this);
        msgBox.exec();
    }
}


void MainWindow::interactionHelp()
{
    QMessageBox msgBox;
    msgBox.setText( interaction_help );
    msgBox.setStyleSheet("QLabel{min-width: 700px;}");  // so that the text isn't wrapped
    msgBox.exec();
}


void MainWindow::startFollowMode(bool start, double range)
{
    if( start )
    {
        if( ui->customPlot->graphCount() > 0 )
        {
            ui->customPlot->axisRect()->axis(QCPAxis::atRight, 0)->setPadding(60);
            followRange = range;
            delete followTag;
            followTag = nullptr;

            QCPGraph *graph = ui->customPlot->graph(0);
            followTag = new AxisTag( graph->valueAxis() );
            followTag->setPen(graph->pen());

            ui->customPlot->replot();

            followTimer.start(1000);
            followModeActive = true;
        }
        else
        {
            QMessageBox msgBox;
            msgBox.setText( "To turn on follow mode at least one set of measurements has to be loaded" );
            msgBox.exec();
            ui->actionFollow_mode->setChecked(false);
            ui->actionFollow_mode_5->setChecked(false);
            ui->actionFollow_mode_15->setChecked(false);
        }
    }
    else
    {
        followTimer.stop();
        followModeActive = false;
        ui->customPlot->axisRect()->axis(QCPAxis::atRight, 0)->setPadding(5);

        delete followTag;
        followTag = nullptr;
        ui->customPlot->replot();

        ui->actionFollow_mode->setChecked(false);
        ui->actionFollow_mode_5->setChecked(false);
        ui->actionFollow_mode_15->setChecked(false);
    }
}


void MainWindow::doFollow()   // periodically called by followTimer
{
    for( int i=0; i<ui->customPlot->graphCount(); i++)
    {
        QCPGraph *graph = ui->customPlot->graph(i);
        // qInfo() << "doFollow(): " << graph->name();
        QString fileName = graph->name();
        // graph->data()->remove()

        if( oldFilePos.contains(fileName) )
        {
            QFile f(fileName);
            if( f.open( QIODevice::ReadOnly ) )
            {
                f.seek( oldFilePos.find(fileName)->pos );
                QTextStream fs(&f);

                while( !fs.atEnd() )
                {
                    QString line = fs.readLine();
                    QStringList cols = line.split(" ",Qt::SkipEmptyParts);

                    if( oldFilePos.find(fileName)->type == DType::MEAS )
                    {
                        int64_t time_us = int64_t( atoll( cols.takeFirst().trimmed().toStdString().c_str() ) );
                        double freq_val = cols.takeFirst().trimmed().toDouble();
                        //char buf[30];
                        //sprintf( buf, "%.6f", freq_val);
                        //qDebug() << "doFollow(): line has " << time_us << "  and " << buf;
                        graph->addData( convertTimestamp(time_us), freq_val);
                    }
                    else if( oldFilePos.find(fileName)->type == DType::GRIDTIME )
                    {
                        double time_s = cols.takeFirst().trimmed().toDouble();
                        double offs = cols.takeFirst().trimmed().toDouble();

                        graph->addData( time_s, offs);
                    }
                }

                oldFilePos.find(fileName)->pos = f.pos();
                f.close();
            }
            else
                // if the file is gone, rewind the file pointer
                oldFilePos.find(fileName)->pos = 0;
        }
    }

    if( ui->customPlot->graphCount() > 0 )
    {
        QCPGraph *graph = ui->customPlot->graph(0);
        double graphValue = graph->dataMainValue(graph->dataCount()-1);
        followTag->updatePosition( graphValue );
        followTag->setText(QString::number( graphValue, 'f', 5));
    }

    if( !incidFileName.isNull() )
    {
        QFile f( incidFileName );
        if( f.open( QIODevice::ReadOnly ) )
        {
            f.seek( oldIncidFilePos );
            QTextStream fs(&f);

            QList<int64_t> newIncidTimestamps;
            QMap<int64_t, QStringList> newIncidents;

            bool firstLine = true;
            if( incidFollowCollect.size() > 0 )
                firstLine = false;

            while( !fs.atEnd() )
            {
                QString line = fs.readLine();
                int firstSpace = line.indexOf(' ');

                // timestamp is 16 digits long
                if( firstSpace < 16 )
                    continue;

                QString tsStr = line.left( firstSpace );
                QString incidStr = line.right( line.length() - firstSpace - 1 );

                int64_t time_us = int64_t( atoll( tsStr.toStdString().c_str() ) );

                if( firstLine )
                {
                    firstLine = false;
                    incidFollow_prev_time_us = time_us;
                }

                //QString dbg = QString( ">%1< >%2<").arg(tsStr,incidStr);
                //qDebug() << "doFollow(): line has " << dbg << "  with time " << time_us;

                if( incidFollow_prev_time_us == time_us )
                {
                    if( incidStr != "INCID_END" )
                        incidFollowCollect.append( incidStr );
                    else
                    {
                        if( incidFollowCollect.size() > 0 )
                        {
                            newIncidTimestamps.append( time_us );
                            newIncidents.insert( time_us, incidFollowCollect);

                            incidFollowCollect.clear();
                            firstLine = true;
                        }
                    }
                }

                incidFollow_prev_time_us = time_us;
            }

            oldIncidFilePos = f.pos();
            f.close();

            if( newIncidTimestamps.size() > 0 )
            {
                for( int64_t time_us : newIncidTimestamps )
                {
                    QStringList collect = newIncidents.value( time_us );
                    QString fline = collect.at(0);

                    addIncident( convertTimestamp( time_us ), fline);

                    incidents.insert( time_us, collect);
                    incidTimestamps.append( time_us );
                }
            }
        }
        else
            // if the file is gone, rewind the file pointer
            oldIncidFilePos = 0;
    }

    ui->customPlot->xAxis->rescale();
    ui->customPlot->xAxis->setRange( ui->customPlot->xAxis->range().upper, followRange, Qt::AlignRight);
    ui->customPlot->replot();
}


int MainWindow::readMeasurements( const QString& fileName, QVector<int64_t>& time, QVector<double>& freq)
{
    int lines = 0;
    QFile f(fileName);
    if( f.open( QIODevice::ReadOnly ) )
    {
        QTextStream fs(&f);

        while( !fs.atEnd() )
        {
            QString line = fs.readLine();
            QStringList cols = line.split( " ", Qt::SkipEmptyParts);
            int64_t time_us = int64_t( atoll( cols.takeFirst().trimmed().toStdString().c_str() ) );
            double freq_val = cols.takeFirst().trimmed().toDouble();
            //char buf[30];
            //sprintf( buf, "%.6f", freq_val);
            //qDebug() << "readMeasurements(): line has " << time_us << "  and " << buf;
            time.append(time_us);
            freq.append(freq_val);
            lines++;
        }

        f.close();
    }

    if( time.size() != freq.size() )
    {
        qDebug() << "error: file open failed '" << fileName << "'.\n";
        lines = -1;
    }

    return lines;
}


int MainWindow::readGridTime( const QString& fileName, QVector<double>& time, QVector<double>& gt_offset)
{
    int lines = 0;
    QFile f(fileName);
    if( f.open( QIODevice::ReadOnly ) )
    {
        QTextStream fs(&f);

        while( !fs.atEnd() )
        {
            QString line = fs.readLine();
            if( line.startsWith('#') )
                continue;

            QStringList cols = line.split( " ", Qt::SkipEmptyParts);
            double time_s = cols.takeFirst().trimmed().toDouble();
            double offs = cols.takeFirst().trimmed().toDouble();

            time.append(time_s);
            gt_offset.append(offs);
            lines++;
        }

        f.close();
    }

    if( time.size() != gt_offset.size() )
    {
        qDebug() << "error: file open failed '" << fileName << "'.\n";
        lines = -1;
    }

    return lines;
}


static QString xRangeToString( const QCPRange& r );

void MainWindow::doInfo( const QCPRange& r )
{
    int lline = 0;

    QString lText;
    if( ui->customPlot->graphCount() > 0 )
    {
        lline = 1;
        QDateTime xBeginDT, xEndDT;
        xBeginDT.setMSecsSinceEpoch( (qint64)std::llround(r.lower * 1000.) );
        xEndDT.setMSecsSinceEpoch( (qint64)std::llround(r.upper * 1000.) );
        QString dtFormat = "dd.MM.yyyy hh:mm:ss.zzz";

        lText.append( xBeginDT.toString(dtFormat) + " - " + xEndDT.toString(dtFormat) + "  Range: " + xRangeToString(r) + "\n" );

        for( int i=0; i<ui->customPlot->graphCount(); i++)
        {
            QCPGraph *graph = ui->customPlot->graph(i);
            QString name = graph->name();

            double yMin = 100000.;
            double yMax = -100000.;

            if( graph->data()->size() > 0 )
            {
                auto e = graph->data()->findEnd(r.upper);
                for( auto i = graph->data()->findBegin(r.lower); i != e; i++)
                {
                    double v = i->value;
                    if( v > yMax )
                        yMax = v;
                    if( v < yMin )
                        yMin = v;
                }

                if( oldFilePos.find(name)->type == DType::MEAS )
                    lText.append( "Measurements #" + QString::number(i+1) + ": " + name + " Min: " + QString::number( yMin, 'f', 5) + " Max: " + QString::number( yMax, 'f', 5) + "\n" );
                else if( oldFilePos.find(name)->type == DType::GRIDTIME )
                    lText.append( "Grid time    #" + QString::number(i+1) + ": " + name + " Min: " + QString::number( yMin, 'f', 3) + " Max: " + QString::number( yMax, 'f', 3) + "\n" );
                else
                    lText.append( "?" );
                lline++;
            }
        }
    }

    if( !incidFileName.isNull() )
    {
        lText.append( "Incidents: " + incidFileName + "\n" );
        lline++;
    }

    for( int i = lline; i<5; i++)
        lText.append( '\n' );
    ui->labelInfo->setText(lText);
}


static double convertTimestamp( int64_t time_us )
{
    int64_t us = time_us % 1000000LL;

    double time_s = (double) (time_us / 1000000LL);

    if( us != 0LL )
        time_s += ((double)us) / 1000000.;

    return time_s;
}

static void convertTimestamps( const QVector<int64_t>& time, QVector<double>& time_s)
{
    Q_ASSERT( time.size() == time_s.size() );

    for( int i = 0; i < time.size(); i++ )
        time_s[i] = convertTimestamp( time[i] );
}


static QString xRangeToString( const QCPRange& r )
{
    long secs = std::lround( r.upper - r.lower );
    QString s;

    if( secs > 60 )
    {
        int mins = secs / 60;
        secs -= mins * 60;

        if( mins > 60 )
        {
            int hours = mins / 60;
            mins -= hours * 60;

            s = QString::number(hours) + "h " + QString::number(mins) + "m " + QString::number(secs) + "s";
        }
        else
            s = QString::number(mins) + "m " + QString::number(secs) + "s";
    }
    else
        s = QString::number(secs) + "s";
    return s;
}

