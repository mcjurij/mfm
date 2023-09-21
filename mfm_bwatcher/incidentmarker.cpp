#include "incidentmarker.h"

IncidentMarker::IncidentMarker(QCustomPlot *parentPlot, ColorTheme color)
    : QCPItemLine(parentPlot),
    m_plot(parentPlot), m_line_cl(nullptr), m_line_cr(nullptr)
{
    m_textBox = new QCPItemRect( m_plot );
    m_line_cl = new QCPItemLine( m_plot );
    m_line_cr = new QCPItemLine( m_plot );
    m_lineLabel = new QCPItemText(parentPlot);

    m_lineLabel->setText("");
    m_lineLabel->position->setParentAnchor(this->start);
    m_lineLabel->setTextAlignment(Qt::AlignLeft);
    QFont font;
    font.setFamily(m_lineLabel->font().family());
    font.setPointSize(10);
    m_lineLabel->setFont( font );

    m_textBox->topLeft->setParentAnchor(m_lineLabel->topLeft);
    m_textBox->bottomRight->setParentAnchor(m_lineLabel->bottomRight);
    m_line_cl->start->setParentAnchor(m_textBox->topLeft);
    m_line_cl->end->setParentAnchor( this->start );
    m_line_cr->start->setParentAnchor(this->start);
    m_line_cr->end->setParentAnchor( m_textBox->topRight );

    m_textBox->topLeft->setCoords(-5, -5);
    m_textBox->bottomRight->setCoords( 5, 5);

    QBrush brush;
    QColor brushColor;
    brushColor.setRgb(192 ,237, 166);
    brush.setColor(brushColor);
    brush.setStyle(Qt::SolidPattern);

    m_textBox->setBrush(brush);

    if (color == ColorTheme::Light)
    {
        this->setPen(QPen(Qt::lightGray));
        m_lineLabel->setColor(Qt::darkGray);
    }
    else
    {
        this->setPen(QPen(QColor(100, 100, 100)));
        m_lineLabel->setColor(Qt::darkMagenta);
    }
}

IncidentMarker::~IncidentMarker()
{
    // delete m_lineLabel; -> can't be done. Obviously the parent plot is deleting all its items.
}

void IncidentMarker::setText( const QString& text )
{
    m_lineLabel->setText(text);

    QRectF textRect( m_lineLabel->topLeft->pixelPosition(), m_lineLabel->bottomRight->pixelPosition());


    //qDebug() << " x1 " << textRect.left() << " y1 " << textRect.top();
    //qDebug() << " x2 " << textRect.right() << " y1 " << textRect.bottom();
    //double l = m_plot->xAxis->pixelToCoord( textRect.width() );
    //double r = m_plot->xAxis->pixelToCoord( textRect.height() );
    //qDebug() << " l " << l << " r " << r;
    // m_lineLabel->position->setType(QCPItemPosition::ptAbsolute);

    double tdist = 20.;
    m_lineLabel->position->setCoords(0, textRect.height()/2+tdist);
}

void IncidentMarker::SetVisibleCustom(bool value)
{
    m_lineLabel->setVisible(value);
    m_line_cr->setVisible(value);
    m_line_cl->setVisible(value);
    m_textBox->setVisible(value);
    this->setVisible(value);
}


void IncidentMarker::removeItems()
{
    m_plot->removeItem( m_lineLabel );
    m_lineLabel = nullptr;

    m_plot->removeItem( m_line_cr );
    m_line_cr = nullptr;

    m_plot->removeItem( m_line_cl );
    m_line_cl = nullptr;

    m_plot->removeItem( m_textBox );
    m_textBox = nullptr;
}
