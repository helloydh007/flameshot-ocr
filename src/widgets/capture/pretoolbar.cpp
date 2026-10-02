// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#include "pretoolbar.h"
#include "src/utils/confighandler.h"
#include "src/utils/pathinfo.h"
#include "src/utils/uitheme.h"
#include "src/widgets/capture/ocrpanel.h"
#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QMenu>
#include <QPropertyAnimation>
#include <QToolButton>

namespace
{
QString iconFile(const QString& name)
{
    // flameshot-ocr: 图标集随主题（暗→white，亮→black）
    return UiTheme::iconDir() + name;
}

QToolButton* flatButton(QWidget* parent,
                        const QString& icon,
                        const QString& tip)
{
    auto* button = new QToolButton(parent);
    // 记录图标名：主题切换时按 UiTheme::iconDir() 重建
    button->setProperty("iconName", icon);
    button->setIcon(QIcon(iconFile(icon)));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setIconSize(QSize(UiTheme::iconSize() * 5 / 6, UiTheme::iconSize() * 5 / 6));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}
} // namespace

PreToolbar::PreToolbar(QWidget* parent)
  : QWidget(parent)
{
    setObjectName(QStringLiteral("preToolbar"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);

    // flameshot-ocr: 主题/配置变化时即时刷新（配置文件监视器触发）
    connect(ConfigHandler::getInstance(), &ConfigHandler::fileChanged, this,
            &PreToolbar::applyTheme);

    m_selectBtn = flatButton(this, QStringLiteral("cursor-move"),
                             OcrPanel::tr2("框选截图", "Region select"));
    m_selectBtn->setCheckable(true);
    m_selectBtn->setChecked(true);
    layout->addWidget(m_selectBtn);
    connect(m_selectBtn, &QToolButton::clicked, this,
            [this]() { emit selectModeRequested(); });

    m_toolButtons = {
        { CaptureTool::TYPE_PENCIL, flatButton(this, "pencil",
                                               OcrPanel::tr2("画笔", "Pen")) },
        { CaptureTool::TYPE_MARKER, flatButton(this, "marker",
                                               OcrPanel::tr2("荧光笔", "Marker")) },
        { CaptureTool::TYPE_PIXELATE, flatButton(this, "pixelate",
                                                 OcrPanel::tr2("马赛克", "Pixelate")) },
        { CaptureTool::TYPE_ARROW, flatButton(this, "arrow-bottom-left",
                                              OcrPanel::tr2("箭头", "Arrow")) },
        { CaptureTool::TYPE_RECTANGLE, flatButton(this, "rectangle",
                                                  OcrPanel::tr2("矩形", "Rectangle")) },
        { CaptureTool::TYPE_CIRCLE, flatButton(this, "circle-outline",
                                               OcrPanel::tr2("椭圆", "Ellipse")) },
        { CaptureTool::TYPE_CIRCLECOUNT,
          flatButton(this, "circlecount-outline",
                     OcrPanel::tr2("序号标记（递增编号）",
                                   "Numbered marker (increments)")) },
    };
    for (auto& entry : m_toolButtons) {
        entry.second->setCheckable(true);
        layout->addWidget(entry.second);
        const CaptureTool::Type type = entry.first;
        connect(entry.second, &QToolButton::clicked, this,
                [this, type]() { emit toolRequested(type); });
    }

    // 形状填充开关：选中=实心填充，未选=只显示边框（矩形/椭圆）
    m_fillBtn = flatButton(
      this, "shape-fill",
      OcrPanel::tr2("形状填充：选中 = 实心，未选 = 只显示边框",
                    "Shape fill: on = solid, off = outline only"));
    m_fillBtn->setObjectName(QStringLiteral("preFillBtn"));
    m_fillBtn->setCheckable(true);
    m_fillBtn->setChecked(ConfigHandler().shapeFill());
    connect(m_fillBtn, &QToolButton::toggled, this, [this](bool checked) {
        ConfigHandler().setShapeFill(checked);
        emit fillToggled();
    });
    layout->addWidget(m_fillBtn);

    auto* separator1 = new QFrame(this);
    separator1->setFrameShape(QFrame::VLine);
    separator1->setStyleSheet(QStringLiteral("color: %1;").arg(UiTheme::panelBorder().name()));
    layout->addWidget(separator1);

    // 取色：点击进入取色模式（按所选格式自动复制）；旁边的 ▾ 切换格式
    auto* grabButton = flatButton(this, "colorize",
                                  OcrPanel::tr2("拾取颜色（自动复制）",
                                                "Pick color (auto copy)"));
    connect(grabButton, &QToolButton::clicked, this,
            [this]() { emit colorGrabRequested(); });
    layout->addWidget(grabButton);

    auto* grabMenu = new QMenu(this);
    auto* hexAction = grabMenu->addAction(
      OcrPanel::tr2("十六进制 (#RRGGBB)", "Hex (#RRGGBB)"));
    hexAction->setCheckable(true);
    auto* rgbAction = grabMenu->addAction(
      OcrPanel::tr2("RGB (r, g, b)", "RGB (r, g, b)"));
    rgbAction->setCheckable(true);
    auto* formatGroup = new QActionGroup(grabMenu);
    formatGroup->addAction(hexAction);
    formatGroup->addAction(rgbAction);
    const bool rgb = ConfigHandler().colorPickFormat() == QLatin1String("rgb");
    rgbAction->setChecked(rgb);
    hexAction->setChecked(!rgb);
    connect(hexAction, &QAction::triggered, this, []() {
        ConfigHandler().setColorPickFormat(QStringLiteral("hex"));
    });
    connect(rgbAction, &QAction::triggered, this, []() {
        ConfigHandler().setColorPickFormat(QStringLiteral("rgb"));
    });
    auto* grabFormatButton = new QToolButton(this);
    grabFormatButton->setText(QStringLiteral("▾"));
    grabFormatButton->setToolTip(
      OcrPanel::tr2("复制格式", "Copy format"));
    grabFormatButton->setAutoRaise(true);
    grabFormatButton->setFixedWidth(18);
    grabFormatButton->setCursor(Qt::PointingHandCursor);
    grabFormatButton->setMenu(grabMenu);
    grabFormatButton->setPopupMode(QToolButton::InstantPopup);
    layout->addWidget(grabFormatButton);

    // 画笔颜色（下拉调色板）
    m_colorBtn = new QToolButton(this);
    m_colorBtn->setToolTip(OcrPanel::tr2("画笔颜色", "Pen color"));
    m_colorBtn->setAutoRaise(true);
    m_colorBtn->setCursor(Qt::PointingHandCursor);
    m_drawColor = ConfigHandler().drawColor();
    setDrawColorPreview(m_drawColor);
    auto* colorMenu = new QMenu(m_colorBtn);
    const QVector<QPair<QString, QColor>> colors = {
        { OcrPanel::tr2("红色", "Red"), QColor(255, 59, 48) },
        { OcrPanel::tr2("黄色", "Yellow"), QColor(255, 204, 0) },
        { OcrPanel::tr2("绿色", "Green"), QColor(52, 199, 89) },
        { OcrPanel::tr2("青色", "Cyan"), QColor(0, 199, 190) },
        { OcrPanel::tr2("蓝色", "Blue"), QColor(0, 122, 255) },
        { OcrPanel::tr2("紫色", "Purple"), QColor(175, 82, 222) },
        { OcrPanel::tr2("黑色", "Black"), QColor(17, 17, 17) },
        { OcrPanel::tr2("白色", "White"), QColor(255, 255, 255) },
    };
    for (const auto& entry : colors) {
        QPixmap swatch(14, 14);
        swatch.fill(entry.second);
        QAction* action = colorMenu->addAction(QIcon(swatch), entry.first);
        connect(action, &QAction::triggered, this, [this, entry]() {
            m_drawColor = entry.second;
            setDrawColorPreview(entry.second);
            emit drawColorChanged(entry.second);
        });
    }
    m_colorBtn->setMenu(colorMenu);
    m_colorBtn->setPopupMode(QToolButton::InstantPopup);
    layout->addWidget(m_colorBtn);

    auto* separator2 = new QFrame(this);
    separator2->setFrameShape(QFrame::VLine);
    separator2->setStyleSheet(QStringLiteral("color: %1;").arg(UiTheme::panelBorder().name()));
    layout->addWidget(separator2);

    // 撤销 / 重做 / 橡皮擦
    auto* undoButton = flatButton(this, "undo-variant",
                                  OcrPanel::tr2("撤销", "Undo"));
    connect(undoButton, &QToolButton::clicked, this,
            [this]() { emit undoRequested(); });
    layout->addWidget(undoButton);

    auto* redoButton = flatButton(this, "redo-variant",
                                  OcrPanel::tr2("重做", "Redo"));
    connect(redoButton, &QToolButton::clicked, this,
            [this]() { emit redoRequested(); });
    layout->addWidget(redoButton);

    m_eraserBtn = flatButton(this, "delete",
                             OcrPanel::tr2("橡皮擦（点击标注删除）",
                                           "Eraser (click an annotation to remove it)"));
    m_eraserBtn->setCheckable(true);
    connect(m_eraserBtn, &QToolButton::clicked, this,
            [this]() { emit eraserRequested(); });
    layout->addWidget(m_eraserBtn);

    auto* separator3 = new QFrame(this);
    separator3->setFrameShape(QFrame::VLine);
    separator3->setStyleSheet(QStringLiteral("color: #3f3f46;"));
    layout->addWidget(separator3);

    auto* fullscreenButton = flatButton(this, "accept",
                                        OcrPanel::tr2("截取全屏并复制",
                                                      "Fullscreen to clipboard"));
    connect(fullscreenButton, &QToolButton::clicked, this,
            [this]() { emit fullscreenCopyRequested(); });
    layout->addWidget(fullscreenButton);

    auto* saveButton = flatButton(this, "content-save",
                                  OcrPanel::tr2("保存全屏截图",
                                                "Save fullscreen capture"));
    connect(saveButton, &QToolButton::clicked, this,
            [this]() { emit saveRequested(); });
    layout->addWidget(saveButton);

    auto* settingsButton = flatButton(this, "config",
                                      OcrPanel::tr2("设置", "Settings"));
    connect(settingsButton, &QToolButton::clicked, this,
            [this]() { emit settingsRequested(); });
    layout->addWidget(settingsButton);

    const QString accent = ConfigHandler().uiColor().name();
    setStyleSheet(QStringLiteral(
      "#preToolbar { background-color: #1a1a1fee; "
      "border: 1px solid #3f3f46; border-radius: 10px; }"
      "#preToolbar QToolButton { color: #d6d6dc; background: transparent; "
      "border: none; border-radius: 6px; padding: 4px; }"
      "#preToolbar QToolButton:hover { background: #3f3f46; }"
      "#preToolbar QToolButton:checked { background: %1; color: #ffffff; }"
      "#preToolbar QToolButton:pressed { background: %1; color: #ffffff; }")
      .arg(accent));
}

void PreToolbar::setToolChecked(CaptureTool::Type type)
{
    m_selectBtn->setChecked(type == CaptureTool::NONE);
    for (auto& entry : m_toolButtons) {
        entry.second->setChecked(entry.first == type);
    }
    if (type != CaptureTool::NONE) {
        setEraserChecked(false);
    }
}

void PreToolbar::setEraserChecked(bool checked)
{
    m_eraserBtn->setChecked(checked);
    if (checked) {
        m_selectBtn->setChecked(false);
        for (auto& entry : m_toolButtons) {
            entry.second->setChecked(false);
        }
    }
}

void PreToolbar::setDrawColorPreview(const QColor& color)
{
    QPixmap swatch(18, 18);
    swatch.fill(color);
    m_colorBtn->setIcon(QIcon(swatch));
    m_colorBtn->setIconSize(QSize(UiTheme::iconSize() * 3 / 4, UiTheme::iconSize() * 3 / 4));
}

// flameshot-ocr: 主题切换（配置文件变化触发）——按新令牌重建图标与配色
void PreToolbar::applyTheme()
{
    for (auto* button : findChildren<QToolButton*>()) {
        const QString name = button->property("iconName").toString();
        if (!name.isEmpty()) {
            button->setIcon(QIcon(iconFile(name)));
        }
    }
    for (auto* sep : findChildren<QFrame*>()) {
        sep->setStyleSheet(
          QStringLiteral("color: %1;").arg(UiTheme::panelBorder().name()));
    }
    update();
}

void PreToolbar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // flameshot-ocr: 渐入，避免工具条突然弹出
    auto* effect = graphicsEffect()
                     ? qobject_cast<QGraphicsOpacityEffect*>(graphicsEffect())
                     : nullptr;
    if (!effect) {
        effect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(effect);
    }
    effect->setOpacity(0.0);
    auto* fade = new QPropertyAnimation(effect, "opacity", this);
    fade->setDuration(120);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::InOutQuad);
    connect(fade, &QPropertyAnimation::finished, fade,
            &QPropertyAnimation::deleteLater);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void PreToolbar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(UiTheme::panelBorder(), 1));
    QColor bg = UiTheme::panelBg();
    bg.setAlpha(245);
    painter.setBrush(bg);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
}
