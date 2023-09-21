#ifndef POINTOFINTEREST_H
#define POINTOFINTEREST_H

enum class POIReason
{
    FreqTooLow,
    FreqTooHigh
};

class PointOfInterest
{
public:
    PointOfInterest();

    POIReason reason;
    double regionBegin;
    double regionEnd;
};

#endif // POINTOFINTEREST_H
