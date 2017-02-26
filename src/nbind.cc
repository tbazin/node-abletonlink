#include "./node-abletonlink.hpp"
#include "nbind/nbind.h"

using AbletonLink = bbb::AbletonLink;

NBIND_CLASS(AbletonLink) {
    construct<>();
    construct<double>();
    construct<double, double>();
    construct<double, double, bool>();
    construct<AbletonLink *>();

    getset(getBeat, setBeat);
    method(setBeatForce);

    getset(getPhase, setPhase);

    getset(getQuantum, setQuantum);

    method(getNumPeers);

    getset(getBPM, setBPM);
    getset(getLinkEnable, setLinkEnable);
    method(enable);
    method(disable);

    method(update);

    method(onTempoChanged);
    method(onNumPeersChanged);
    method(on);
    method(off);
}