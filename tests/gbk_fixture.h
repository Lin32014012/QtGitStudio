#pragma once
// GBK (GB18030 subset) byte fixtures, hard-coded so the fixture itself
// does not depend on a codec being available.
#include <QByteArray>

namespace GbkFixture {
// "版本=基线\r\n"
inline QByteArray base() { return QByteArray("\xB0\xE6\xB1\xBE\x3D\xBB\xF9\xCF\xDF\x0D\x0A"); }
// "版本=我们的修改\r\n"
inline QByteArray ours() { return QByteArray("\xB0\xE6\xB1\xBE\x3D\xCE\xD2\xC3\xC7\xB5\xC4\xD0\xDE\xB8\xC4\x0D\x0A"); }
// "版本=他们的修改\r\n"
inline QByteArray theirs() { return QByteArray("\xB0\xE6\xB1\xBE\x3D\xCB\xFB\xC3\xC7\xB5\xC4\xD0\xDE\xB8\xC4\x0D\x0A"); }
// "合并结果：保留双方\r\n第二行\r\n"
inline QByteArray resolved() { return QByteArray("\xBA\xCF\xB2\xA2\xBD\xE1\xB9\xFB\xA3\xBA\xB1\xA3\xC1\xF4\xCB\xAB\xB7\xBD\x0D\x0A\xB5\xDA\xB6\xFE\xD0\xD0\x0D\x0A"); }
} // namespace GbkFixture
