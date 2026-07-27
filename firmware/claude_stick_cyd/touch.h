#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// Driver de toque XPT2046 (resistivo), validado no bring-up físico —
// pinos e calibração confirmados em firmware/bringup_cyd/. Barramento SPI
// dedicado (HSPI), separado do SPI do display.
class XPT2046_TouchDrv {
public:
    XPT2046_TouchDrv(uint8_t clk, uint8_t miso, uint8_t mosi, uint8_t cs, uint8_t irq,
                      uint16_t xmin, uint16_t xmax, uint16_t ymin, uint16_t ymax)
        : _clk(clk), _miso(miso), _mosi(mosi), _cs(cs), _irq(irq),
          _xmin(xmin), _xmax(xmax), _ymin(ymin), _ymax(ymax),
          _spi(HSPI), _ts(cs, irq) {}

    bool begin() {
        _spi.begin(_clk, _miso, _mosi);
        return _ts.begin(_spi);
    }

    bool touched() { return _ts.touched(); }

    void readData(uint16_t *x, uint16_t *y) {
        TS_Point p = _ts.getPoint();
        long sx = map(p.x, _xmin, _xmax, 0, SCREEN_WIDTH - 1);
        long sy = map(p.y, _ymin, _ymax, 0, SCREEN_HEIGHT - 1);
        *x = (uint16_t)constrain(sx, 0, SCREEN_WIDTH - 1);
        *y = (uint16_t)constrain(sy, 0, SCREEN_HEIGHT - 1);
    }

private:
    uint8_t _clk, _miso, _mosi, _cs, _irq;
    uint16_t _xmin, _xmax, _ymin, _ymax;
    SPIClass _spi;
    XPT2046_Touchscreen _ts;
};

#endif // TOUCH_H
