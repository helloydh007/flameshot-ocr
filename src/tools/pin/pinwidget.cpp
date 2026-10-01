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
#include "src/widgets/capture/ocrpanel.h"

#include <QActionGroup>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QShortcut>
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

    m_label->setPixmap(m_pixmap);
    m_layout->addWidget(m_label);

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

    new QShortcut(QKeySequence::Undo, this, [this]() {
        m_annotator->undo();
    });
}

void PinWidget::closePin()
{
    update();
    close();
}
bool PinWidget::scrollEvent(QWheelEvent* event)
{
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
    if (QWindow* window = windowHandle(); window != nullptr) {
        window->startSystemMove();
        return;
    }
}

void PinWidget::mouseMoveEvent(QMouseEvent* e) {}

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
    if (m_sizeChanged) {
        const auto aspectRatio =
          m_expanding ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio;
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
            m_annotator->setDisplayScale(qreal(pix.width()) /
                                         m_pixmap.width());
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

    // flameshot-ocr: 标注工具子菜单
    QMenu* annotateMenu =
      contextMenu.addMenu(OcrPanel::tr2("标注", "Annotate"));
    auto* toolGroup = new QActionGroup(annotateMenu);
    auto addToolAction = [&](const QString& name, PinAnnotator::Tool tool) {
        QAction* action = annotateMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(m_annotator->tool() == tool);
        toolGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, tool]() { m_annotator->setTool(tool); });
    };
    addToolAction(OcrPanel::tr2("移动（不标注）", "Move (no drawing)"),
                  PinAnnotator::None);
    addToolAction(OcrPanel::tr2("画笔", "Pen"), PinAnnotator::Pencil);
    addToolAction(OcrPanel::tr2("荧光笔", "Marker"), PinAnnotator::Marker);
    addToolAction(OcrPanel::tr2("箭头", "Arrow"), PinAnnotator::Arrow);
    addToolAction(OcrPanel::tr2("矩形", "Rectangle"), PinAnnotator::Rectangle);
    addToolAction(OcrPanel::tr2("椭圆", "Ellipse"), PinAnnotator::Ellipse);
    addToolAction(OcrPanel::tr2("直线", "Line"), PinAnnotator::Line);

    QMenu* colorMenu = annotateMenu->addMenu(OcrPanel::tr2("颜色", "Color"));
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
    auto* colorGroup = new QActionGroup(colorMenu);
    for (const auto& entry : colors) {
        QPixmap swatch(14, 14);
        swatch.fill(entry.second);
        QAction* action = colorMenu->addAction(QIcon(swatch), entry.first);
        action->setCheckable(true);
        action->setChecked(m_annotator->color() == entry.second);
        colorGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, entry]() { m_annotator->setColor(entry.second); });
    }

    QMenu* widthMenu = annotateMenu->addMenu(OcrPanel::tr2("粗细", "Width"));
    const QVector<QPair<QString, int>> widths = {
        { OcrPanel::tr2("细", "Thin"), 2 },
        { OcrPanel::tr2("中", "Medium"), 4 },
        { OcrPanel::tr2("粗", "Thick"), 8 },
    };
    auto* widthGroup = new QActionGroup(widthMenu);
    for (const auto& entry : widths) {
        QAction* action = widthMenu->addAction(entry.first);
        action->setCheckable(true);
        action->setChecked(m_annotator->width() == entry.second);
        widthGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, entry]() { m_annotator->setWidth(entry.second); });
    }

    annotateMenu->addSeparator();
    QAction* undoAct = annotateMenu->addAction(
      OcrPanel::tr2("撤销标注 (Ctrl+Z)", "Undo annotation (Ctrl+Z)"));
    undoAct->setEnabled(m_annotator->hasShapes());
    connect(undoAct, &QAction::triggered, this,
            [this]() { m_annotator->undo(); });
    QAction* clearAct = annotateMenu->addAction(
      OcrPanel::tr2("清空标注", "Clear annotations"));
    clearAct->setEnabled(m_annotator->hasShapes());
    connect(clearAct, &QAction::triggered, this,
            [this]() { m_annotator->clearShapes(); });

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
