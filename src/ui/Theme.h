#pragma once
#include <QString>
#include <QApplication>
#include <QIcon>
#include <QStyle>
#include <QWidget>

// v4-P6-26 UI refresh: single source of truth for the visual language.
// Values mirror sketches/003-split-workbench (the chosen direction).
// QSS lives in resources/theme.qss; this header only carries the few
// constants the C++ side needs (fonts, icon tile tints, spacing).
namespace Theme {

// --- surfaces -----------------------------------------------------------
inline const QString kPageBg        = QStringLiteral("#EEF0F4");
inline const QString kSurface       = QStringLiteral("#FFFFFF");
inline const QString kSidebarBg     = QStringLiteral("#FAFBFD");
inline const QString kSidebarFoot   = QStringLiteral("#F7F9FC");
inline const QString kStripBg       = QStringLiteral("#FAFBFD");

// --- lines --------------------------------------------------------------
inline const QString kBorder        = QStringLiteral("#E7EAF0");
inline const QString kBorderSoft    = QStringLiteral("#EEF1F6");
inline const QString kBorderStrong  = QStringLiteral("#DDE1E8");

// --- text ---------------------------------------------------------------
inline const QString kTextPrimary   = QStringLiteral("#171A1F");
inline const QString kTextBody      = QStringLiteral("#3B4453");
inline const QString kTextSecondary = QStringLiteral("#5B6472");
inline const QString kTextMuted     = QStringLiteral("#8A93A3");
inline const QString kTextFaint     = QStringLiteral("#A6AEBB");

// --- accents ------------------------------------------------------------
inline const QString kAccent        = QStringLiteral("#2563EB");
inline const QString kAccentSoft    = QStringLiteral("#EAF0FE");
inline const QString kAccentTint    = QStringLiteral("#EFF4FF");
inline const QString kSuccess       = QStringLiteral("#047857");
inline const QString kSuccessSoft   = QStringLiteral("#ECFDF5");
inline const QString kSuccessTint   = QStringLiteral("#F0FDF4");
inline const QString kWarning       = QStringLiteral("#B45309");
inline const QString kWarningSoft   = QStringLiteral("#FFFBEB");
inline const QString kDanger        = QStringLiteral("#B91C1C");
inline const QString kDangerSoft    = QStringLiteral("#FEF2F2");
inline const QString kVioletTint    = QStringLiteral("#F5F3FF");

// --- typography ---------------------------------------------------------
// CJK-first stack: Windows ships Microsoft YaHei UI, Linux CI has
// Noto Sans CJK. Monospace is used for paths, hashes and timestamps.
inline const QString kFontUi =
    QStringLiteral("\"Microsoft YaHei UI\", \"Noto Sans CJK SC\", "
                   "\"Source Han Sans SC\", \"PingFang SC\", sans-serif");
inline const QString kFontMono =
    QStringLiteral("Consolas, \"JetBrains Mono\", \"DejaVu Sans Mono\", monospace");

// --- metrics ------------------------------------------------------------
inline constexpr int kSidebarWidth = 296;
inline constexpr int kRadiusCard   = 12;
inline constexpr int kRadiusPanel  = 10;
inline constexpr int kRadiusButton = 8;
inline constexpr int kGap          = 12;
inline constexpr int kPagePadding  = 26;

// Icon tile background tints, keyed by the accent an ActionBlock uses.
inline QString iconTintFor(const QString &accent) {
    if (accent == kAccent)     return kAccentTint;
    if (accent == QStringLiteral("#059669")) return kSuccessTint;
    if (accent == QStringLiteral("#7C3AED")) return kVioletTint;
    return kAccentTint;
}

// Loads a themed icon from the resource bundle, falling back to the
// platform standard icon. The resource bundle is attached to the
// application target, not to libraries or test binaries, so a missing
// resource must degrade to a usable icon instead of an empty one.
inline QIcon icon(const QString &fileName, QStyle::StandardPixmap fallback) {
    const QIcon themed(QStringLiteral(":/icons/") + fileName);
    if (!themed.isNull()) return themed;
    return QApplication::style()->standardIcon(fallback);
}

// QSS does not observe dynamic-property writes: repolish after changing
// one, or the new selector never applies.
inline void restyle(QWidget *widget) {
    if (!widget) return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

}  // namespace Theme
