// Visual identity: semantic color tokens and the application stylesheet.
//
// Color communicates operational state, never decoration:
//   program/on-air = red, selected/next = green, transition = amber-yellow,
//   recording = orange-red, warning = amber, failure = rose + hatch + text.
#pragma once

#include <QColor>
#include <QFont>
#include <QString>

class QApplication;

namespace ms::theme {

// Surfaces (graphite → black)
inline const QColor bg0{0x0B, 0x0C, 0x0E};  // window background, gutters
inline const QColor bg1{0x12, 0x14, 0x17};  // panels
inline const QColor bg2{0x19, 0x1C, 0x20};  // panel headers, fields
inline const QColor bg3{0x21, 0x25, 0x2A};  // buttons
inline const QColor bg4{0x2A, 0x2F, 0x35};  // hover
inline const QColor line{0x26, 0x2B, 0x31};
inline const QColor lineStrong{0x38, 0x3E, 0x46};
inline const QColor text{0xE4, 0xE7, 0xEB};
inline const QColor text2{0x9A, 0xA2, 0xAC};
inline const QColor text3{0x68, 0x70, 0x7A};

// States
inline const QColor program{0xE5, 0x32, 0x2D};
inline const QColor selected{0x2F, 0xBF, 0x5E};
inline const QColor transition{0xF0, 0xC0, 0x3A};
inline const QColor recording{0xF4, 0x5B, 0x2E};
inline const QColor warning{0xF0, 0xA8, 0x30};
inline const QColor failure{0xF0, 0x4F, 0x6A};
inline const QColor focus{0x5B, 0x8D, 0xEF};
inline const QColor ok{0x45, 0xC2, 0x7A};

// Meter zones
inline const QColor meterLow{0x3C, 0xC0, 0x6A};
inline const QColor meterMid{0xE8, 0xC2, 0x3A};
inline const QColor meterHigh{0xEC, 0x45, 0x3A};

void apply(QApplication &app);
QFont monoFont(int pointSize = 9, bool bold = false);
QFont uiFont(int pointSize = 9, int weight = 400);
QString hex(const QColor &c);

} // namespace ms::theme
