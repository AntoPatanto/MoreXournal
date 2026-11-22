#include "HoldDetector.h"

HoldDetector::HoldDetector(double tolerancePx): tolerancePx(tolerancePx) {}

void HoldDetector::start(const xoj::util::Point<double>& pagePos) {
    this->anchor = pagePos;
    this->armed = true;
}

bool HoldDetector::update(const xoj::util::Point<double>& pagePos, double zoom) {
    if (this->anchor.distance(pagePos) * zoom <= this->tolerancePx) {
        return false;
    }
    start(pagePos);
    return true;
}

void HoldDetector::disarm() { this->armed = false; }

bool HoldDetector::isArmed() const { return this->armed; }

const xoj::util::Point<double>& HoldDetector::getAnchor() const { return this->anchor; }
