#pragma once

/*
 * Two-threshold switch for gates (P6, #132): opens when the level rises
 * above openDb, closes only once it falls below closeDb (<= openDb), so a
 * signal hovering around a single threshold can't chatter the gate.
 */
namespace loudio {

class Hysteresis {
public:
    void setThresholds(float open, float close) {
        openDb = open;
        closeDb = close > open ? open : close;
    }

    /* Returns the new state: true = open. */
    bool process(float levelDb) {
        if (isOpen) {
            if (levelDb < closeDb) isOpen = false;
        } else {
            if (levelDb > openDb) isOpen = true;
        }
        return isOpen;
    }

    bool open() const { return isOpen; }
    void reset(bool state = false) { isOpen = state; }

private:
    float openDb = 0.0f;
    float closeDb = 0.0f;
    bool isOpen = false;
};

} // namespace loudio
