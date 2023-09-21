#ifndef CONF_H
#define CONF_H

class Conf
{
public:
    static constexpr double MAINS_FREQ = 50.;

    static constexpr double GRAPH_DEFAULT_YMIN = MAINS_FREQ - 0.2;
    static constexpr double GRAPH_DEFAULT_YMAX = MAINS_FREQ + 0.2;

    static constexpr double POI_MAINS_FREQ_TOO_LOW  = MAINS_FREQ - 0.1;
    static constexpr double POI_MAINS_FREQ_TOO_HIGH = MAINS_FREQ + 0.1;

    static constexpr double GRAPH_GRIDTIME_YMIN = - 10.;
    static constexpr double GRAPH_GRIDTIME_YMAX = 10;

    static constexpr double INCIDENTMARKER_YBEG = MAINS_FREQ - 0.18;
    static constexpr double INCIDENTMARKER_YEND = MAINS_FREQ + 0.20;

private:
    Conf();
};

#endif // CONF_H
