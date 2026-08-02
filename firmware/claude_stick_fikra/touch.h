#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>
#include <Wire.h>

// Driver de toque FT6336G (capacitivo, I2C, protocolo FocalTech padrão).
// Painel nativo é 240x320 portrait; a rotação abaixo remapeia pro mesmo
// referencial landscape (320x240) usado pelo display — validar no bring-up
// físico (tocar nos 4 cantos) e ajustar os casos se sair espelhado/trocado.
#define FT63XX_REG_TD_STATUS 0x02
#define FT63XX_REG_P1_XH     0x03
#define FT63XX_REG_P1_XL     0x04
#define FT63XX_REG_P1_YH     0x05
#define FT63XX_REG_P1_YL     0x06

class FT6336_TouchDrv {
public:
    FT6336_TouchDrv(uint8_t sda, uint8_t scl, uint8_t rst, uint8_t int_pin, uint8_t addr,
                     uint8_t rotation = 1, uint16_t panel_w = 240, uint16_t panel_h = 320)
        : _sda(sda), _scl(scl), _rst(rst), _int_pin(int_pin), _addr(addr),
          _rotation(rotation), _panel_w(panel_w), _panel_h(panel_h) {}

    bool begin() {
        if (_rst >= 0) {
            pinMode(_rst, OUTPUT);
            digitalWrite(_rst, LOW);
            delay(10);
            digitalWrite(_rst, HIGH);
            delay(300);   // tempo de boot do controlador após reset (datasheet FT6336G)
        }
        pinMode(_int_pin, INPUT_PULLUP);
        return Wire.begin(_sda, _scl, 400000);
    }

    bool touched() { return _read(); }

    void readData(uint16_t *x, uint16_t *y) {
        *x = _point_x;
        *y = _point_y;
    }

private:
    uint8_t _sda, _scl, _rst, _int_pin, _addr, _rotation;
    uint16_t _panel_w, _panel_h;
    uint16_t _point_x = 0, _point_y = 0;

    bool _read() {
        Wire.beginTransmission(_addr);
        Wire.write(FT63XX_REG_TD_STATUS);
        if (Wire.endTransmission(false) != 0) return false;

        if (Wire.requestFrom(_addr, (uint8_t)5) != 5) return false;
        uint8_t td_status = Wire.read();
        uint8_t xh = Wire.read(), xl = Wire.read();
        uint8_t yh = Wire.read(), yl = Wire.read();

        if ((td_status & 0x0F) == 0) return false;   // sem toque ativo

        uint16_t raw_x = ((uint16_t)(xh & 0x0F) << 8) | xl;
        uint16_t raw_y = ((uint16_t)(yh & 0x0F) << 8) | yl;

        switch (_rotation) {
            case 0: _point_x = raw_x; _point_y = raw_y; break;
            case 1: _point_x = raw_y; _point_y = _panel_w - 1 - raw_x; break;
            case 2: _point_x = _panel_w - 1 - raw_x; _point_y = _panel_h - 1 - raw_y; break;
            case 3: _point_x = _panel_h - 1 - raw_y; _point_y = raw_x; break;
        }
        return true;
    }
};

#endif // TOUCH_H
