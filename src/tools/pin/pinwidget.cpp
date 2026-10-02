// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QPinchGesture>
#include <QWindow>

#include "pinwidget.h"
#include "pinannotator.h"
#include "qguiappcurrentscreen.h"
#include "screenshotsaver.h"
#include "src/utils/confighandler.h"
#include "src/utils/globalvalues.h"
#include "src/utils/ocrhelper.h"
#include "src/utils/pathinfo.h"
#include "src/widgets/capture/ocrpanel.h"

#include <QActionGroup>
#include <QActionGroup>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QShortcut>
#include <QProcess>
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

PinWidget::PinWidget(const QPixmap& pixmap,
                     const QRect& geometry,
                     QWidget* parent)
  : QWidget(parent)
  , m_pixmap(pixmap)
  , m_layout(new QVBoxLayout(this))
  , m_label(new QLabel())
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
    setWindowOpacity(m_opacity);

    // flameshot-ocr: pixmap 贴 label 左上且 label 不被拉伸——
    // 保证标注层（= label 几何）与图片显示区原点重合，
    // 否则工具条比图片宽时 pixmap 居中、坐标整体错位
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_label->setPixmap(m_pixmap);
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
    ensureKeepAboveRule();

    new QShortcut(QKeySequence::Undo, this, [this]() {
        m_annotator->undo();
    });
}

void PinWidget::buildToolBar()
{
    m_toolBarRow = new QWidget(this);
    m_toolBarRow->setObjectName(QStringLiteral("pinToolBar"));
    auto* layout = new QHBoxLayout(m_toolBarRow);
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
        button->setIcon(QIcon(PathInfo::whiteIconPath() + icon));
        button->setIconSize(QSize(18, 18));
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
    fillButton->setIcon(QIcon(PathInfo::whiteIconPath() +
                              QStringLiteral("rectangle")));
    fillButton->setIconSize(QSize(18, 18));
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
    widthButton->setIcon(QIcon(PathInfo::whiteIconPath() +
                               QStringLiteral("minus.svg")));
    widthButton->setIconSize(QSize(18, 18));
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
        button->setIcon(QIcon(PathInfo::whiteIconPath() + icon));
        button->setIconSize(QSize(18, 18));
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

    m_toolBarRow->setStyleSheet(
      QStringLiteral("#pinToolBar { background-color: #1a1a1fee; "
                     "border: 1px solid #3f3f46; border-top: none; "
                     "border-radius: 0 0 8px 8px; }"
                     "#pinToolBar QToolButton { color: #d6d6dc; "
                     "background: transparent; border: none; "
                     "border-radius: 14px; padding: 5px; }"
                     "#pinToolBar QToolButton:hover { background: #3f3f46; "
                     "color: #ffffff; }"
                     "#pinToolBar QToolButton:checked { background: %1; "
                     "color: #ffffff; }")
        .arg(accent));

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
void PinWidget::showOpacityToast()
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
    m_opacityToast->setText(
      OcrPanel::tr2("透明度 %1%", "Opacity %1%")
        .arg(qRound(m_opacity * 100)));
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
            setWindowOpacity(m_opacity);
            showOpacityToast();
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

    setWindowOpacity(m_opacity);
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
    setWindowOpacity(m_opacity);
}

void PinWidget::decreaseOpacity()
{
    m_opacity -= 0.1;
    if (m_opacity < 0.0) {
        m_opacity = 0.0;
    }

    setWindowOpacity(m_opacity);
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
// 通过 kwinrulesrc 窗口规则强制 flameshot-pin 窗口置顶（幂等）
void PinWidget::ensureKeepAboveRule()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    const QString desc = QStringLiteral("Flameshot pin keep above");
    auto read = [](const QString& key, const QString& def = QString()) {
        QProcess p;
        p.start(QStringLiteral("kreadconfig6"),
                { QStringLiteral("--file"), QStringLiteral("kwinrulesrc"),
                  QStringLiteral("--group"), QStringLiteral("General"),
                  QStringLiteral("--key"), key });
        p.waitForFinished(2000);
        const QString out =
          QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
        return out.isEmpty() ? def : out;
    };
    bool exists = false;
    int count = read(QStringLiteral("count")).toInt();
    for (int i = 1; i <= count && !exists; ++i) {
        QProcess p;
        p.start(QStringLiteral("kreadconfig6"),
                { QStringLiteral("--file"), QStringLiteral("kwinrulesrc"),
                  QStringLiteral("--group"), QString::number(i),
                  QStringLiteral("--key"), QStringLiteral("description") });
        p.waitForFinished(2000);
        exists = QString::fromLocal8Bit(p.readAllStandardOutput())
                   .trimmed() == desc;
    }
    if (exists) {
        return;
    }
    const int group = count + 1;
    auto write = [&](const QString& key, const QString& value) {
        QProcess::startDetached(
          QStringLiteral("kwriteconfig6"),
          { QStringLiteral("--file"), QStringLiteral("kwinrulesrc"),
            QStringLiteral("--group"), QString::number(group),
            QStringLiteral("--key"), key, value });
    };
    write(QStringLiteral("description"), desc);
    write(QStringLiteral("title"), QStringLiteral("flameshot-pin"));
    write(QStringLiteral("titlematch"), QStringLiteral("2"));
    write(QStringLiteral("keepon_top"), QStringLiteral("true"));
    write(QStringLiteral("keepon_toprule"), QStringLiteral("2"));
    QProcess::startDetached(
      QStringLiteral("kwriteconfig6"),
      { QStringLiteral("--file"), QStringLiteral("kwinrulesrc"),
        QStringLiteral("--group"), QStringLiteral("General"),
        QStringLiteral("--key"), QStringLiteral("count"),
        QString::number(group) });
    QProcess::startDetached(QStringLiteral("qdbus6"),
                            { QStringLiteral("org.kde.KWin"),
                              QStringLiteral("/KWin"),
                              QStringLiteral("reconfigure") });
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
        QImage overlay = m_annotator->renderToImage(m_pixmap.size());
        QPainter painter(&out);
        painter.drawImage(0, 0, overlay);
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

        m_label->setPixmap(pix);
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
        m_ocrPanel = new OcrPanel(nullptr);
        m_ocrPanel->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                                   Qt::WindowStaysOnTopHint);
    }
    m_ocrPanel->showLoading(geometry());
    OcrHelper::run(
      m_pixmap.toImage(), this, [this](bool ok, const QString& result) {
          if (!m_ocrPanel) {
              return;
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
