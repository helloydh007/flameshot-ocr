// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors
#include <QGraphicsDropShadowEffect>
#include <QPinchGesture>
#include <QWindow>

#include "pinwidget.h"
#include "flowlayout.h"
#include "pinannotator.h"
#include "qguiappcurrentscreen.h"
#include "screenshotsaver.h"
#include "src/utils/confighandler.h"
#include "src/utils/globalvalues.h"
#include "src/utils/ocrhelper.h"
#include "src/utils/uitheme.h"
#include "src/utils/pathinfo.h"
#include "src/widgets/capture/ocrpanel.h"

#include <QActionGroup>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QShortcut>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {
constexpr int MARGIN = 7;
constexpr int BLUR_RADIUS = 2 * MARGIN;
constexpr qreal STEP = 0.03;
constexpr qreal MIN_SIZE = 100.0;
}

// flameshot-ocr: 图片显示控件 —— 直接以 painter.setOpacity 绘制，
// 替代 QGraphicsOpacityEffect（特效渲染会丢 pixmap 的 DPR，缩放屏上
// 图片会缩小；painter 绘制则完全保真且透明度真实生效）
class PinImageView : public QWidget
{
public:
    explicit PinImageView(QWidget* parent = nullptr)
      : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
    void setViewPixmap(const QPixmap& p)
    {
        m_pix = p;
        setMinimumSize(m_pix.size() / (m_pix.devicePixelRatio() > 0
                                         ? m_pix.devicePixelRatio()
                                         : 1.0));
        update();
    }
    QPixmap viewPixmap() const { return m_pix; }
    void setViewOpacity(qreal o)
    {
        if (m_viewOpacity != o) {
            m_viewOpacity = o;
            update();
        }
    }

protected:
    QSize sizeHint() const override
    {
        return m_pix.size() / (m_pix.devicePixelRatio() > 0
                                 ? m_pix.devicePixelRatio()
                                 : 1.0);
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_viewOpacity < 1.0) {
            painter.setOpacity(m_viewOpacity);
        }
        painter.drawPixmap(QPoint(0, 0), m_pix);
    }

private:
    QPixmap m_pix;
    qreal m_viewOpacity = 1.0;
};

PinWidget::PinWidget(const QPixmap& pixmap,
                     const QRect& geometry,
                     QWidget* parent)
  : QWidget(parent)
  , m_pixmap(pixmap)
  , m_layout(new QVBoxLayout(this))
  , m_label(new PinImageView(this))
  , m_shadowEffect(new QGraphicsDropShadowEffect(this))
{
    setWindowIcon(QIcon(GlobalValues::iconPath()));
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint |
                   Qt::Dialog);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("flameshot-pin");
    ConfigHandler conf;
    m_baseColor = conf.uiColor();
    m_hoverColor = conf.contrastUiColor();

    m_layout->setContentsMargins(MARGIN, MARGIN, MARGIN, MARGIN);

    m_shadowEffect->setColor(m_baseColor);
    m_shadowEffect->setBlurRadius(BLUR_RADIUS);
    m_shadowEffect->setOffset(0, 0);
    setGraphicsEffect(m_shadowEffect);

    // flameshot-ocr: pixmap 贴 label 左上且 label 不被拉伸——
    // 保证标注层（= label 几何）与图片显示区原点重合，
    // 否则工具条比图片宽时 pixmap 居中、坐标整体错位
    m_label->setViewPixmap(m_pixmap);
    m_layout->addWidget(m_label);
    m_layout->setAlignment(m_label, Qt::AlignLeft | Qt::AlignTop);

    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this, SLOT(close()));
    new QShortcut(Qt::Key_Escape, this, SLOT(close()));

    qreal devicePixelRatio = 1;
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
    if (currentScreen != nullptr) {
        devicePixelRatio = currentScreen->devicePixelRatio();
    }
#endif
    const int margin =
      static_cast<int>(static_cast<double>(MARGIN) * devicePixelRatio);
    QRect adjusted_pos = geometry + QMargins(margin, margin, margin, margin);
    setGeometry(adjusted_pos);
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    if (currentScreen != nullptr) {
        QPoint topLeft = currentScreen->geometry().topLeft();
        adjusted_pos.setX((adjusted_pos.x() - topLeft.x()) / devicePixelRatio +
                          topLeft.x());

        adjusted_pos.setY((adjusted_pos.y() - topLeft.y()) / devicePixelRatio +
                          topLeft.y());
        adjusted_pos.setWidth(adjusted_pos.size().width() / devicePixelRatio);
        adjusted_pos.setHeight(adjusted_pos.size().height() / devicePixelRatio);
        resize(0, 0);
        move(adjusted_pos.x(), adjusted_pos.y());
    }
#endif
    grabGesture(Qt::PinchGesture);

    this->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this,
            &QWidget::customContextMenuRequested,
            this,
            &PinWidget::showContextMenu);

    // flameshot-ocr: 标注层（画笔/箭头/矩形/椭圆/直线/荧光笔）
    m_annotator = new PinAnnotator(this);
    connect(m_annotator,
            &PinAnnotator::moveRequested,
            this,
            [this]() {
                if (QWindow* window = windowHandle(); window != nullptr) {
                    window->startSystemMove();
                }
            });
    connect(m_annotator,
            &PinAnnotator::closeRequested,
            this,
            &PinWidget::closePin);
    connect(m_annotator,
            &PinAnnotator::contextMenuRequested,
            this,
            &PinWidget::showContextMenu);
    positionAnnotator();
    m_annotator->raise();
    m_annotator->setBasePixmap(&m_pixmap);
    m_annotator->setDisplayScale(m_pixmap.devicePixelRatio());
    m_annotator->setFill(ConfigHandler().shapeFill());
    buildToolBar();

    // flameshot-ocr: 配置文件变化（含主题切换）时即时刷新工具条
    connect(ConfigHandler::getInstance(), &ConfigHandler::fileChanged, this,
            &PinWidget::applyTheme);
    ensureKeepAboveRule();

    new QShortcut(QKeySequence::Undo, this, [this]() {
        m_annotator->undo();
    });
}

void PinWidget::buildToolBar()
{
    m_toolBarRow = new QWidget(this);
    m_toolBarRow->setObjectName(QStringLiteral("pinToolBar"));
    // flameshot-ocr: FlowLayout 自动换行——按钮多/图标大时折到下一行，
    // 不再溢出钉图宽度（40px 图标设置下尾部按钮曾跑出窗口点不到）
    auto* layout = new FlowLayout(m_toolBarRow, 4, 2, 2);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(2);

    auto syncTools = [this]() {
        const PinAnnotator::Tool current = m_annotator->tool();
        for (const auto& entry : m_toolButtons) {
            entry.second->setChecked(entry.first == current);
        }
    };

    const QString accent = ConfigHandler().uiColor().name();
    auto addToolButton = [&](const QString& icon, const QString& tip,
                             PinAnnotator::Tool tool) {
        auto* button = new QToolButton(m_toolBarRow);
        button->setProperty("iconName", icon);
        button->setIcon(QIcon(UiTheme::iconDir() + icon));
        button->setIconSize(QSize(UiTheme::iconSize() * 3 / 4, UiTheme::iconSize() * 3 / 4));
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setChecked(tool == m_annotator->tool());
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QToolButton::clicked, this,
                [this, tool, syncTools]() {
                    m_annotator->setTool(tool);
                    syncTools();
                });
        layout->addWidget(button);
        m_toolButtons.append({ tool, button });
    };

    addToolButton(QStringLiteral("cursor-move"),
                  OcrPanel::tr2("移动钉图", "Move pin"), PinAnnotator::None);
    addToolButton(QStringLiteral("pencil"), OcrPanel::tr2("画笔", "Pen"),
                  PinAnnotator::Pencil);
    addToolButton(QStringLiteral("marker"), OcrPanel::tr2("荧光笔", "Marker"),
                  PinAnnotator::Marker);
    addToolButton(QStringLiteral("arrow-bottom-left"),
                  OcrPanel::tr2("箭头", "Arrow"), PinAnnotator::Arrow);
    addToolButton(QStringLiteral("rectangle"),
                  OcrPanel::tr2("矩形", "Rectangle"), PinAnnotator::Rectangle);
    addToolButton(QStringLiteral("circle-outline"),
                  OcrPanel::tr2("椭圆", "Ellipse"), PinAnnotator::Ellipse);
    addToolButton(QStringLiteral("line"),
                  OcrPanel::tr2("直线", "Line"), PinAnnotator::Line);
    addToolButton(QStringLiteral("circlecount-outline"),
                  OcrPanel::tr2("序号标记（递增编号）",
                                "Numbered marker (increments)"),
                  PinAnnotator::Number);
    addToolButton(QStringLiteral("text"),
                  OcrPanel::tr2("添加文字", "Add text"), PinAnnotator::Text);
    addToolButton(QStringLiteral("pixelate"),
                  OcrPanel::tr2("马赛克", "Pixelate"),
                  PinAnnotator::Pixelate);
    addToolButton(QStringLiteral("eraser"),
                  OcrPanel::tr2("橡皮擦（点击标注删除）",
                                "Eraser (click an annotation to remove it)"),
                  PinAnnotator::Eraser);

    // 形状填充开关（与截图工具栏共享 shapeFill 配置）
    auto* fillButton = new QToolButton(m_toolBarRow);
    fillButton->setProperty("iconName", QStringLiteral("rectangle"));
    fillButton->setIcon(QIcon(UiTheme::iconDir() +
                              QStringLiteral("rectangle")));
    fillButton->setIconSize(QSize(UiTheme::iconSize() * 3 / 4, UiTheme::iconSize() * 3 / 4));
    fillButton->setToolTip(
      OcrPanel::tr2("形状填充：选中 = 实心，未选 = 只显示边框",
                    "Shape fill: on = solid, off = outline only"));
    fillButton->setCheckable(true);
    fillButton->setChecked(ConfigHandler().shapeFill());
    fillButton->setCursor(Qt::PointingHandCursor);
    connect(fillButton, &QToolButton::toggled, this, [this](bool checked) {
        ConfigHandler().setShapeFill(checked);
        m_annotator->setFill(checked);
    });
    layout->addWidget(fillButton);

    // 颜色下拉
    m_colorButton = new QToolButton(m_toolBarRow);
    m_colorButton->setToolTip(OcrPanel::tr2("颜色", "Color"));
    m_colorButton->setCursor(Qt::PointingHandCursor);
    m_colorButton->setPopupMode(QToolButton::InstantPopup);
    auto updateColorIcon = [this](const QColor& color) {
        QPixmap swatch(16, 16);
        swatch.fill(color);
        m_colorButton->setIcon(QIcon(swatch));
        m_colorButton->setIconSize(QSize(16, 16));
    };
    updateColorIcon(m_annotator->color());
    auto* colorMenu = new QMenu(m_colorButton);
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
        connect(action, &QAction::triggered, this,
                [this, entry, updateColorIcon]() {
                    m_annotator->setColor(entry.second);
                    updateColorIcon(entry.second);
                });
    }
    m_colorButton->setMenu(colorMenu);
    layout->addWidget(m_colorButton);

    // 粗细下拉
    auto* widthButton = new QToolButton(m_toolBarRow);
    widthButton->setProperty("iconName", QStringLiteral("minus.svg"));
    widthButton->setIcon(QIcon(UiTheme::iconDir() +
                               QStringLiteral("minus.svg")));
    widthButton->setIconSize(QSize(UiTheme::iconSize() * 3 / 4, UiTheme::iconSize() * 3 / 4));
    widthButton->setToolTip(OcrPanel::tr2("粗细", "Width"));
    widthButton->setCursor(Qt::PointingHandCursor);
    widthButton->setPopupMode(QToolButton::InstantPopup);
    auto* widthMenu = new QMenu(widthButton);
    const QVector<QPair<QString, int>> widths = {
        { OcrPanel::tr2("细", "Thin"), 2 },
        { OcrPanel::tr2("中", "Medium"), 4 },
        { OcrPanel::tr2("粗", "Thick"), 8 },
    };
    for (const auto& entry : widths) {
        QAction* action = widthMenu->addAction(entry.first);
        action->setCheckable(true);
        action->setChecked(m_annotator->width() == entry.second);
        connect(action, &QAction::triggered, this,
                [this, entry]() { m_annotator->setWidth(entry.second); });
    }
    widthButton->setMenu(widthMenu);
    layout->addWidget(widthButton);

    auto addActionButton = [&](const QString& icon, const QString& tip,
                               std::function<void()> fn) {
        auto* button = new QToolButton(m_toolBarRow);
        button->setProperty("iconName", icon);
        button->setIcon(QIcon(UiTheme::iconDir() + icon));
        button->setIconSize(QSize(UiTheme::iconSize() * 3 / 4, UiTheme::iconSize() * 3 / 4));
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QToolButton::clicked, this, [fn]() { fn(); });
        layout->addWidget(button);
    };
    addActionButton(QStringLiteral("undo-variant"),
                    OcrPanel::tr2("撤销标注 (Ctrl+Z)", "Undo (Ctrl+Z)"),
                    [this]() { m_annotator->undo(); });
    addActionButton(QStringLiteral("redo-variant"),
                    OcrPanel::tr2("重做标注", "Redo"),
                    [this]() { m_annotator->redo(); });
    addActionButton(QStringLiteral("delete"),
                    OcrPanel::tr2("清空标注", "Clear annotations"),
                    [this]() { m_annotator->clearShapes(); });

    auto* separator = new QFrame(m_toolBarRow);
    separator->setFrameShape(QFrame::VLine);
    separator->setStyleSheet(QStringLiteral("color: #3f3f46;"));
    layout->addWidget(separator);

    addActionButton(QStringLiteral("ocr"),
                    OcrPanel::tr2("文字识别", "OCR"),
                    [this]() { runOcr(); });
    addActionButton(QStringLiteral("content-copy"),
                    OcrPanel::tr2("复制到剪贴板（含标注）",
                                  "Copy to clipboard (with annotations)"),
                    [this]() { copyToClipboard(); });
    addActionButton(QStringLiteral("content-save"),
                    OcrPanel::tr2("保存到文件（含标注）",
                                  "Save to file (with annotations)"),
                    [this]() { saveToFile(); });
    addActionButton(QStringLiteral("close"), OcrPanel::tr2("关闭", "Close"),
                    [this]() { closePin(); });

    // 工具条配色走 UiTheme 令牌（亮/暗/跟随系统）
    QColor tbBg = UiTheme::panelBg();
    tbBg.setAlpha(238);
    m_toolBarRow->setStyleSheet(
      QStringLiteral("#pinToolBar { background-color: %2; "
                     "border: 1px solid %3; border-top: none; "
                     "border-radius: 0 0 8px 8px; }"
                     "#pinToolBar QToolButton { color: %4; "
                     "background: transparent; border: none; "
                     "border-radius: 14px; padding: 5px; }"
                     "#pinToolBar QToolButton:hover { background: %5; "
                     "color: %6; }"
                     "#pinToolBar QToolButton:checked { background: %1; "
                     "color: %6; }")
        .arg(accent,
             tbBg.name(QColor::HexArgb),
             UiTheme::panelBorder().name(),
             UiTheme::panelFg().name(),
             UiTheme::hoverBg().name(),
             UiTheme::copyTextFg().name()));

    // 关键：把工具条加入钉图主布局（否则它是 0 尺寸子控件、永远不可见/不可用）；
    // 默认隐藏，可在钉图右键菜单中开启（pinShowToolbar 记忆选择）
    m_layout->addWidget(m_toolBarRow);
    m_toolBarRow->setVisible(ConfigHandler().pinShowToolbar());
    adjustSize();
}

void PinWidget::closePin()
{
    update();
    close();
}
// flameshot-ocr: Wayland 下 setWindowOpacity 是空操作（无对应协议），
// 透明度改为渲染层实现：图片视图以 painter 透明度绘制。
// 仅影响显示，复制/保存仍导出原图
void PinWidget::applyOpacity()
{
    m_label->setViewOpacity(m_opacity);
}

void PinWidget::showCenterToast(const QString& text)
{
    if (!m_opacityToast) {
        m_opacityToast = new QLabel(this);
        m_opacityToast->setStyleSheet(
          QStringLiteral("background-color: #1a1a1fee; color: #ffffff; "
                         "border: 1px solid %1; border-radius: 6px; "
                         "padding: 6px 14px; font-size: 14px;")
            .arg(m_baseColor.name()));
        m_opacityToast->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_opacityToast->setAlignment(Qt::AlignCenter);
    }
    m_opacityToast->setText(text);
    m_opacityToast->adjustSize();
    m_opacityToast->move((width() - m_opacityToast->width()) / 2,
                         (height() - m_opacityToast->height()) / 2);
    m_opacityToast->show();
    m_opacityToast->raise();
    QTimer::singleShot(900, m_opacityToast, &QWidget::hide);
}

bool PinWidget::scrollEvent(QWheelEvent* event)
{
    // flameshot-ocr: Ctrl+滚轮 调整透明度，中央显示百分比提示
    if (event->modifiers() & Qt::ControlModifier) {
        const int angle = event->angleDelta().y();
        if (angle != 0) {
            m_opacity = qBound(0.1, m_opacity + (angle > 0 ? 0.05 : -0.05),
                               1.0);
            applyOpacity();
            showCenterToast(OcrPanel::tr2("透明度 %1%", "Opacity %1%")
                              .arg(qRound(m_opacity * 100)));
        }
        event->accept();
        return true;
    }
    const auto phase = event->phase();
    if (phase == Qt::ScrollPhase::ScrollUpdate
#if defined(Q_OS_LINUX) || defined(Q_OS_WINDOWS) || defined(Q_OS_MACOS)
        || phase == Qt::ScrollPhase::NoScrollPhase
#endif
    ) {
        const auto angle = event->angleDelta();
        if (angle.y() == 0) {
            return true;
        }
        m_currentStepScaleFactor = angle.y() > 0
                                     ? m_currentStepScaleFactor + STEP
                                     : m_currentStepScaleFactor - STEP;
        m_expanding = m_currentStepScaleFactor >= 1.0;
        // flameshot-ocr: 滚轮缩放实时显示当前缩放百分比
        showCenterToast(OcrPanel::tr2("缩放 %1%", "Zoom %1%")
                          .arg(qRound((m_scaleFactor * m_currentStepScaleFactor) *
                                      100)));
    }
#if defined(Q_OS_MACOS)
    // ScrollEnd is currently supported only on Mac OSX
    if (phase == Qt::ScrollPhase::ScrollEnd) {
#else
    else {
#endif
        m_scaleFactor *= m_currentStepScaleFactor;
        m_currentStepScaleFactor = 1.0;
        m_expanding = false;
    }

    m_sizeChanged = true;
    update();
    return true;
}

void PinWidget::enterEvent(QEnterEvent*)
{
    m_shadowEffect->setColor(m_hoverColor);
}

void PinWidget::leaveEvent(QEvent*)
{
    m_shadowEffect->setColor(m_baseColor);
}

void PinWidget::mouseDoubleClickEvent(QMouseEvent*)
{
    closePin();
}

void PinWidget::mousePressEvent(QMouseEvent* e)
{
    Q_UNUSED(e)
    if (QWindow* window = windowHandle(); window != nullptr) {
        window->startSystemMove();
        return;
    }
}

void PinWidget::mouseMoveEvent(QMouseEvent* e)
{
    Q_UNUSED(e)
}

void PinWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_0) {
        m_opacity = 1.0;
    } else if (event->key() == Qt::Key_9) {
        m_opacity = 0.9;
    } else if (event->key() == Qt::Key_8) {
        m_opacity = 0.8;
    } else if (event->key() == Qt::Key_7) {
        m_opacity = 0.7;
    } else if (event->key() == Qt::Key_6) {
        m_opacity = 0.6;
    } else if (event->key() == Qt::Key_5) {
        m_opacity = 0.5;
    } else if (event->key() == Qt::Key_4) {
        m_opacity = 0.4;
    } else if (event->key() == Qt::Key_3) {
        m_opacity = 0.3;
    } else if (event->key() == Qt::Key_2) {
        m_opacity = 0.2;
    } else if (event->key() == Qt::Key_1) {
        m_opacity = 0.1;
    }

    applyOpacity();
}
bool PinWidget::gestureEvent(QGestureEvent* event)
{
    if (QGesture* pinch = event->gesture(Qt::PinchGesture)) {
        pinchTriggered(static_cast<QPinchGesture*>(pinch));
    }
    return true;
}

void PinWidget::rotateLeft()
{
    const int oldWidth = m_pixmap.width();
    const int oldHeight = m_pixmap.height();
    m_sizeChanged = true;

    auto rotateTransform = QTransform().rotate(270);
    m_pixmap = m_pixmap.transformed(rotateTransform);
    m_annotator->applyRotateLeft(oldWidth, oldHeight);
}

void PinWidget::rotateRight()
{
    const int oldWidth = m_pixmap.width();
    const int oldHeight = m_pixmap.height();
    m_sizeChanged = true;

    auto rotateTransform = QTransform().rotate(90);
    m_pixmap = m_pixmap.transformed(rotateTransform);
    m_annotator->applyRotateRight(oldWidth, oldHeight);
}

void PinWidget::increaseOpacity()
{
    m_opacity += 0.1;
    if (m_opacity > 1.0) {
        m_opacity = 1.0;
    }
    applyOpacity();
}

void PinWidget::decreaseOpacity()
{
    m_opacity -= 0.1;
    if (m_opacity < 0.1) {
        m_opacity = 0.1;
    }

    applyOpacity();
}

bool PinWidget::event(QEvent* event)
{
    if (event->type() == QEvent::Gesture) {
        return gestureEvent(static_cast<QGestureEvent*>(event));
    } else if (event->type() == QEvent::Wheel) {
        return scrollEvent(static_cast<QWheelEvent*>(event));
    }
    return QWidget::event(event);
}

// flameshot-ocr: KWin Wayland 会忽略 Qt 的 WindowStaysOnTopHint，
// 通过常驻 KWin 脚本（windowAdded 钩子）强制 flameshot-pin 窗口置顶。
// 脚本安装到 ~/.local/share/flameshot-ocr/，仅首次加载，此后新钉图自动置顶。
namespace
{
// KeepAbove 脚本异步加载状态机（审查报告 §4）：
// 0 = 未开始或上次失败（可重试），1 = 进行中，2 = 已确认加载。
// 全部 QProcess 走 finished 回调，GUI 线程零阻塞（旧实现同步
// waitForFinished(3000)×2，首个钉图最坏卡 6 秒）。
int s_keepAboveState = 0;

QObject* keepAboveOwner()
{
    static QObject* owner = [] {
        auto* o = new QObject(qApp);
        return o;
    }();
    return owner;
}

void keepAboveFail(QProcess* proc)
{
    s_keepAboveState = 0; // 回到未开始态：下一个钉图自动重试
    proc->deleteLater();
}
} // namespace

// flameshot-ocr: 主题切换（配置文件变化触发）——重设工具条样式与图标
void PinWidget::applyTheme()
{
    if (!m_toolBarRow) {
        return;
    }
    QColor tbBg = UiTheme::panelBg();
    tbBg.setAlpha(238);
    const QString accent = ConfigHandler().uiColor().name();
    m_toolBarRow->setStyleSheet(
      QStringLiteral("#pinToolBar { background-color: %2; "
                     "border: 1px solid %3; border-top: none; "
                     "border-radius: 0 0 8px 8px; }"
                     "#pinToolBar QToolButton { color: %4; "
                     "background: transparent; border: none; "
                     "border-radius: 14px; padding: 5px; }"
                     "#pinToolBar QToolButton:hover { background: %5; "
                     "color: %6; }"
                     "#pinToolBar QToolButton:checked { background: %1; "
                     "color: %6; }")
        .arg(accent,
             tbBg.name(QColor::HexArgb),
             UiTheme::panelBorder().name(),
             UiTheme::panelFg().name(),
             UiTheme::hoverBg().name(),
             UiTheme::copyTextFg().name()));
    for (auto* button : m_toolBarRow->findChildren<QToolButton*>()) {
        const QString name = button->property("iconName").toString();
        if (!name.isEmpty()) {
            button->setIcon(QIcon(UiTheme::iconDir() + name));
        }
    }
    m_toolBarRow->update();
}

void PinWidget::ensureKeepAboveRule()
{
    if (s_keepAboveState != 0) {
        return; // 已加载(2)或已有一次加载在途(1)
    }
    // GNOME/其它桌面无 qdbus6：直接放弃置顶脚本（钉图仍可用，只是不置顶）
    if (QStandardPaths::findExecutable(QStringLiteral("qdbus6"))
          .isEmpty()) {
        s_keepAboveState = 2;
        return;
    }
    const QString scriptDir =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
      QStringLiteral("/flameshot-ocr");
    QDir().mkpath(scriptDir);
    const QString scriptPath = scriptDir +
      QStringLiteral("/pin-keepabove.js");
    QFile scriptFile(scriptPath);
    if (!scriptFile.exists()) {
        if (scriptFile.open(QIODevice::WriteOnly)) {
            scriptFile.write(
              "// flameshot-ocr: keep flameshot-pin windows above\n"
              "function apply(w) {\n"
              "    if (w.caption.indexOf(\"flameshot-pin\") !== -1 && "
              "!w.keepAbove) {\n"
              "        w.keepAbove = true;\n"
              "    }\n"
              "}\n"
              "workspace.windowList().forEach(apply);\n"
              "workspace.windowAdded.connect(apply);\n");
        }
    }
    if (!scriptFile.exists()) {
        return; // 磁盘异常：保持 0 态，下次重试
    }
    s_keepAboveState = 1;

    // 第 1 步：查询脚本是否已加载
    auto* check = new QProcess(keepAboveOwner());
    connect(check, &QProcess::errorOccurred, check, [check](auto) {
        keepAboveFail(check);
    });
    connect(
      check,
      qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
      check,
      [check, scriptPath](int code, QProcess::ExitStatus status) mutable {
          const QString out =
            QString::fromLocal8Bit(check->readAllStandardOutput()).trimmed();
          check->deleteLater();
          if (code != 0 || status != QProcess::NormalExit) {
              s_keepAboveState = 0;
              return;
          }
          if (out == QLatin1String("true")) {
              s_keepAboveState = 2;
              return;
          }
          // 第 2 步：加载脚本取编号
          auto* load = new QProcess(keepAboveOwner());
          connect(load, &QProcess::errorOccurred, load, [load](auto) {
              keepAboveFail(load);
          });
          connect(
            load,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            load,
            [load](int code2, QProcess::ExitStatus status2) mutable {
                const QString idStr =
                  QString::fromLocal8Bit(load->readAllStandardOutput())
                    .trimmed();
                load->deleteLater();
                if (code2 != 0 || status2 != QProcess::NormalExit) {
                    s_keepAboveState = 0;
                    return;
                }
                bool ok = false;
                const int scriptId = idStr.toInt(&ok);
                if (!ok || scriptId <= 0) {
                    s_keepAboveState = 0;
                    return;
                }
                // 第 3 步：run（分离进程，无需等待结果）
                QProcess::startDetached(
                  QStringLiteral("qdbus6"),
                  { QStringLiteral("org.kde.KWin"),
                    QStringLiteral("/Scripting/Script%1").arg(scriptId),
                    QStringLiteral("org.kde.kwin.Script.run") });
                s_keepAboveState = 2; // run 为 fire-and-forget，视作完成
            });
          load->start(
            QStringLiteral("qdbus6"),
            { QStringLiteral("org.kde.KWin"),
              QStringLiteral("/Scripting"),
              QStringLiteral("org.kde.kwin.Scripting.loadScript"),
              scriptPath });
      });
    check->start(
      QStringLiteral("qdbus6"),
      { QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting.isScriptLoaded"),
        scriptPath });
}

QWidget* PinWidget::viewWidget() const
{
    return m_label;
}

void PinWidget::positionAnnotator()
{
    if (m_annotator) {
        m_annotator->setGeometry(m_label->geometry());
        m_annotator->raise();
    }
}

void PinWidget::resizeEvent(QResizeEvent*)
{
    positionAnnotator();
}

QPixmap PinWidget::compositedPixmap() const
{
    // 复制/保存前提交正在输入的文字
    const_cast<PinAnnotator*>(m_annotator)->commitPendingText();
    QPixmap out = m_pixmap;
    if (m_annotator && m_annotator->hasShapes()) {
        // 审查报告 §7：标注直接画到输出 pixmap 上——旧实现先渲染
        // 一张整屏透明 overlay 再合成，4K 下多一张 ~32MB 临时 QImage
        QPainter painter(&out);
        m_annotator->paintAnnotations(painter);
    }
    return out;
}

void PinWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    if (m_sizeChanged) {
        // flameshot-ocr: 统一 KeepAspectRatio，滚轮放大/缩小表现一致
        const auto aspectRatio = Qt::KeepAspectRatio;
        const auto transformType = ConfigHandler().antialiasingPinZoom()
                                     ? Qt::SmoothTransformation
                                     : Qt::FastTransformation;
        const qreal iw = m_pixmap.width();
        const qreal ih = m_pixmap.height();
        const qreal nw = qBound(MIN_SIZE,
                                iw * m_currentStepScaleFactor * m_scaleFactor,
                                static_cast<qreal>(maximumWidth()));
        const qreal nh = qBound(MIN_SIZE,
                                ih * m_currentStepScaleFactor * m_scaleFactor,
                                static_cast<qreal>(maximumHeight()));

        const QPixmap pix = m_pixmap.scaled(nw, nh, aspectRatio, transformType);

        m_label->setViewPixmap(pix);
        adjustSize();
        positionAnnotator();
        if (m_annotator && !m_pixmap.isNull()) {
            // 真实映射 = 底图 DPR / 当前缩放。此前漏掉 DPR，125% 缩放屏上
            // 标注坐标整体偏小 1.25 倍（橡皮擦点边框删不中的根因）
            m_annotator->setDisplayScale(m_pixmap.devicePixelRatio() /
                                         m_scaleFactor);
        }
        m_sizeChanged = false;
    }
}

void PinWidget::pinchTriggered(QPinchGesture* gesture)
{
    const QPinchGesture::ChangeFlags changeFlags = gesture->changeFlags();
    if (changeFlags & QPinchGesture::ScaleFactorChanged) {
        m_currentStepScaleFactor = gesture->totalScaleFactor();
        m_expanding = m_currentStepScaleFactor > gesture->lastScaleFactor();
    }
    if (gesture->state() == Qt::GestureFinished) {
        m_scaleFactor *= m_currentStepScaleFactor;
        m_currentStepScaleFactor = 1;
        m_expanding = false;
    }
    m_sizeChanged = true;
    update();
}

void PinWidget::showContextMenu(const QPoint& pos)
{
    QMenu contextMenu(tr("Context menu"), this);

    // flameshot-ocr: 快捷工具条开关（默认隐藏，此处开启并记忆选择）
    QAction* toolbarAction = contextMenu.addAction(
      OcrPanel::tr2("显示快捷工具条", "Show quick toolbar"));
    toolbarAction->setCheckable(true);
    toolbarAction->setChecked(m_toolBarRow && m_toolBarRow->isVisible());
    connect(toolbarAction, &QAction::triggered, this, [this](bool checked) {
        ConfigHandler().setPinShowToolbar(checked);
        if (m_toolBarRow) {
            m_toolBarRow->setVisible(checked);
        }
        adjustSize();
        positionAnnotator();
    });
    contextMenu.addSeparator();

    QAction copyToClipboardAction(tr("Copy to clipboard"), this);
    connect(&copyToClipboardAction,
            &QAction::triggered,
            this,
            &PinWidget::copyToClipboard);
    contextMenu.addAction(&copyToClipboardAction);

    // flameshot-ocr: 对钉住的图片直接 OCR
    QAction ocrAction(OcrPanel::tr2("文字识别 (OCR)", "OCR"), this);
    connect(&ocrAction, &QAction::triggered, this, &PinWidget::runOcr);
    contextMenu.addAction(&ocrAction);

    QAction saveToFileAction(tr("Save to file"), this);
    connect(
      &saveToFileAction, &QAction::triggered, this, &PinWidget::saveToFile);
    contextMenu.addAction(&saveToFileAction);

    contextMenu.addSeparator();

    QAction rotateRightAction(tr("Rotate Right"), this);
    connect(
      &rotateRightAction, &QAction::triggered, this, &PinWidget::rotateRight);
    contextMenu.addAction(&rotateRightAction);

    QAction rotateLeftAction(tr("Rotate Left"), this);
    connect(
      &rotateLeftAction, &QAction::triggered, this, &PinWidget::rotateLeft);
    contextMenu.addAction(&rotateLeftAction);

    QAction increaseOpacityAction(tr("Increase Opacity"), this);
    connect(&increaseOpacityAction,
            &QAction::triggered,
            this,
            &PinWidget::increaseOpacity);
    contextMenu.addAction(&increaseOpacityAction);

    QAction decreaseOpacityAction(tr("Decrease Opacity"), this);
    connect(&decreaseOpacityAction,
            &QAction::triggered,
            this,
            &PinWidget::decreaseOpacity);
    contextMenu.addAction(&decreaseOpacityAction);

    QAction closePinAction(tr("Close"), this);
    connect(&closePinAction, &QAction::triggered, this, &PinWidget::closePin);
    contextMenu.addSeparator();
    contextMenu.addAction(&closePinAction);

    contextMenu.exec(mapToGlobal(pos));
}

void PinWidget::copyToClipboard()
{
    saveToClipboard(compositedPixmap());
}

// flameshot-ocr: 对钉住的整张图片跑 OCR，结果面板显示在钉图旁边
void PinWidget::runOcr()
{
    if (!m_ocrPanel) {
        // flameshot-ocr: 作为钉图子控件显示（Wayland 顶层窗口无法被程序
        // 定位——之前面板会被合成器丢到图片下方甚至屏幕外）。子控件模式
        // 下 positionBeside 在钉图范围内自动判断左右位置
        m_ocrPanel = new OcrPanel(this);
    }
    m_ocrPanel->showLoading(rect());
    // 审查报告 §2.2：世代计数只丢弃旧结果、不停止旧进程——这里先
    // cancel 旧任务（terminate→宽限→kill），保证最多一个活动引擎进程
    if (m_ocrTask) {
        m_ocrTask->cancel();
    }
    const quint64 generation = ++m_ocrGeneration;
    m_ocrTask = OcrHelper::run(m_pixmap.toImage(), this,
                               [this, generation](bool ok, const QString& result) {
        if (generation != m_ocrGeneration || !m_ocrPanel) {
            return; // 已被更新的识别任务取代，或面板已销毁
        }
        if (!ok) {
            m_ocrPanel->showFailure(result);
        } else if (result.isEmpty()) {
            m_ocrPanel->showFailure();
        } else {
            m_ocrPanel->showText(result);
        }
    });
}

void PinWidget::saveToFile()
{
    hide();
    saveToFilesystemGUI(compositedPixmap());
    show();
}
