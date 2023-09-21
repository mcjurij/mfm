#ifndef INCIDENTMARKER_H
#define INCIDENTMARKER_H

#include "qcustomplot.h"


enum class ColorTheme
{
    Light,
    Dark
};


class IncidentMarker : public QCPItemLine
{
public:
    IncidentMarker( QCustomPlot *parentPlot, ColorTheme color);
    virtual ~IncidentMarker();

    void setText( const QString& text);
    void SetVisibleCustom(bool value);

    void removeItems();

private:
    QCPItemText* m_lineLabel;
    QCustomPlot *m_plot;
    QCPItemLine *m_line_cl, *m_line_cr;
    QCPItemRect *m_textBox;
};

#endif // INCIDENTMARKER_H
