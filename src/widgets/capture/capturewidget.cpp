// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

// Based on Lightscreen areadialog.cpp, Copyright 2017  Christian Kaiser
// <info@ckaiser.com.ar> released under the GNU GPL2
// <https://www.gnu.org/licenses/gpl-2.0.txt>

// Based on KDE's KSnapshot regiongrabber.cpp, revision 796531, Copyright 2007
// Luca Gugelmann <lucag@student.ethz.ch> released under the GNU LGPL
// <http://www.gnu.org/licenses/old-licenses/library.txt>

#include "capturewidget.h"
#include "abstractlogger.h"
#include "copytool.h"
#include "src/tools/abstractpathtool.h"
#include "src/tools/abstracttwopointtool.h"
#include "src/tools/pin/pinwidget.h"
#include "src/config/cacheutils.h"
#include "src/core/flameshot.h"
#include "src/core/qguiappcurrentscreen.h"
#include "src/utils/screengrabber.h"
#include "src/utils/screenshotsaver.h"
#include "src/utils/systemnotification.h"
#include "src/widgets/capture/colorpicker.h"
#include "src/widgets/capture/hovereventfilter.h"
#include "src/widgets/capture/modificationcommand.h"
#include "src/widgets/capture/notifierbox.h"
#include "src/widgets/capture/ocrpanel.h"
#include "src/widgets/capture/pretoolbar.h"
#include "src/widgets/panel/colorgrabwidget.h"
#include "src/config/configwindow.h"
#include "src/widgets/capture/overlaymessage.h"
#include "src/widgets/orientablepushbutton.h"
#include "src/widgets/panel/sidepanelwidget.h"
#include "src/widgets/panel/utilitypanel.h"
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QFontMetrics>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QScreen>
#include <QShortcut>
#include <QClipboard>
#include <QEventLoop>
#include <QMimeData>
#include <QInputMethodEvent>
#include <QLabel>
#include <QToolButton>
#include <QPropertyAnimation>
#include <QToolTip>
#include <QVariantAnimation>
#include <cstdio>
#include <functional>
#include <memory>
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <draggablewidgetmaker.h>

#if !defined(DISABLE_UPDATE_CHECKER)
#include "src/widgets/updatenotificationwidget.h"
#endif

#define MOUSE_DISTANCE_TO_START_MOVING 3

// CaptureWidget is the main component used to capture the screen. It contains
// an area of selection with its respective buttons.

// enableSaveWindow

CaptureWidget::CaptureWidget(const CaptureRequest& req,
                             bool fullScreen,
                             QWidget* parent)
  : QWidget(parent)
  , m_toolSizeByKeyboard(0)
  , m_mouseIsClicked(false)
  , m_captureDone(false)
  , m_previewEnabled(true)
  , m_adjustmentButtonPressed(false)
  , m_configError(false)
  , m_configErrorResolved(false)
#if !defined(DISABLE_UPDATE_CHECKER)
  , m_updateNotificationWidget(nullptr)
#endif
  , m_lastMouseWheel(0)
  , m_activeButton(nullptr)
  , m_activeTool(nullptr)
  , m_activeToolIsMoved(false)
  , m_toolWidget(nullptr)
  , m_panel(nullptr)
  , m_sidePanel(nullptr)
  , m_colorPicker(nullptr)
  , m_selection(nullptr)
  , m_magnifier(nullptr)
  , m_xywhDisplay(false)
  , m_existingObjectIsChanged(false)
  , m_startMove(false)
  , m_clipboardWorkaroundDone(false)

{
    m_undoStack.setUndoLimit(ConfigHandler().undoLimit());
    m_context.circleCount = 1;

    // Base config of the widget
    m_eventFilter = new HoverEventFilter(this);
    connect(m_eventFilter,
            &HoverEventFilter::hoverIn,
            this,
            &CaptureWidget::childEnter);
    connect(m_eventFilter,
            &HoverEventFilter::hoverOut,
            this,
            &CaptureWidget::childLeave);
    connect(&m_xywhTimer, &QTimer::timeout, this, &CaptureWidget::xywhTick);
    // else xywhTick keeps triggering when not needed
    m_xywhTimer.setSingleShot(true);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, false);
    m_opacity = m_config.contrastOpacity();
    m_uiColor = m_config.uiColor();
    m_contrastUiColor = m_config.contrastUiColor();
    setMouseTracking(true);
    initContext(fullScreen, req);
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS))
    // Top left of the whole set of screens
    QPoint topLeft(0, 0);
#endif
    if (fullScreen) {
        // Grab Screenshot
        bool ok = true;
        m_context.screenshot = ScreenGrabber().grabEntireDesktop(ok);
        if (!ok) {
            AbstractLogger::error() << tr("Unable to capture screen");
            this->close();
        }
        m_context.origScreenshot = m_context.screenshot;

#if defined(Q_OS_WIN)
// Call cmake with -DFLAMESHOT_DEBUG_CAPTURE=ON to enable easier debugging
#if !defined(FLAMESHOT_DEBUG_CAPTURE)
        setWindowFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint |
                       Qt::SubWindow // Hides the taskbar icon
        );
#endif

        for (QScreen* const screen : QGuiApplication::screens()) {
            QPoint topLeftScreen = screen->geometry().topLeft();

            if (topLeftScreen.x() < topLeft.x()) {
                topLeft.setX(topLeftScreen.x());
            }
            if (topLeftScreen.y() < topLeft.y()) {
                topLeft.setY(topLeftScreen.y());
            }
        }
        move(topLeft);
        resize(pixmap().size());
#elif defined(Q_OS_MACOS)
        // Emulate fullscreen mode
        //        setWindowFlags(Qt::WindowStaysOnTopHint |
        //        Qt::BypassWindowManagerHint |
        //                       Qt::FramelessWindowHint |
        //                       Qt::NoDropShadowWindowHint | Qt::ToolTip |
        //                       Qt::Popup
        //                       );
        QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
        move(currentScreen->geometry().x(), currentScreen->geometry().y());
        resize(currentScreen->size());
// LINUX
#else
// Call cmake with -DFLAMESHOT_DEBUG_CAPTURE=ON to enable easier debugging
#if !defined(FLAMESHOT_DEBUG_CAPTURE)
        setWindowFlags(Qt::BypassWindowManagerHint | Qt::WindowStaysOnTopHint |
                       Qt::FramelessWindowHint | Qt::Tool);
        // Fix for Qt6 dual monitor offset: position widget to cover entire
        // desktop
        QRect desktopGeom = ScreenGrabber().desktopGeometry();
        move(desktopGeom.topLeft());
        resize(desktopGeom.size());
#endif
        // Need to move to the top left screen
        QPoint topLeft(0, INT_MAX);
        for (QScreen* const screen : QGuiApplication::screens()) {
            qreal dpr = screen->devicePixelRatio();
            QPoint topLeftScreen = screen->geometry().topLeft() / dpr;
            if (topLeftScreen.x() == 0) {
                if (topLeftScreen.y() < topLeft.y()) {
                    topLeft.setY(topLeftScreen.y());
                }
            }
        }
        move(topLeft);
#endif
    }
    QVector<QRect> areas;
    if (m_context.fullscreen) {
        QPoint topLeftOffset = QPoint(0, 0);
#if defined(Q_OS_WIN)
        topLeftOffset = topLeft;
#endif

#if defined(Q_OS_MACOS)
        // MacOS works just with one active display, so we need to append
        // just one current display and keep multiple displays logic for
        // other OS
        QRect r;
        QScreen* screen = QGuiAppCurrentScreen().currentScreen();
        r = screen->geometry();
        // all calculations are processed according to (0, 0) start
        // point so we need to move current object to (0, 0)
        r.moveTo(0, 0);
        areas.append(r);
#else
        // LINUX & WINDOWS
        for (QScreen* const screen : QGuiApplication::screens()) {
            QRect r = screen->geometry();
            r.moveTo(r.x() / screen->devicePixelRatio(),
                     r.y() / screen->devicePixelRatio());
            r.moveTo(r.topLeft() - topLeftOffset);
            areas.append(r);
        }
#endif
    } else {
        areas.append(rect());
    }

    m_buttonHandler = new ButtonHandler(this);
    m_buttonHandler->updateScreenRegions(areas);
    m_buttonHandler->hide();

    initButtons();
    initSelection(); // button handler must be initialized before
    initShortcuts(); // must be called after initSelection
    // init magnify
    if (m_config.showMagnifier()) {
        m_magnifier = new MagnifierWidget(
          m_context.screenshot, m_uiColor, m_config.squareMagnifier(), this);
    }

    // Init color picker
    m_colorPicker = new ColorPicker(this);
    // Init notification widget
    m_notifierBox = new NotifierBox(this);
    initPanel();

    // TODO: Make it more clear why this has moved. In Qt6 some timing related
    // to constructors / connect signals has changed so if initPanel is called
    // after the connect a SEGFAULT occurs
    connect(m_colorPicker,
            &ColorPicker::colorSelected,
            this,
            [this](const QColor& c) {
                m_context.mousePos = mapFromGlobal(QCursor::pos());
                setDrawColor(c);
            });
    m_colorPicker->hide();

    // Init tool size sigslots
    connect(this,
            &CaptureWidget::toolSizeChanged,
            this,
            &CaptureWidget::onToolSizeChanged);

    m_notifierBox->hide();
    connect(m_notifierBox, &NotifierBox::hidden, this, [this]() {
        // Show cursor if it was hidden while adjusting tool size
        updateCursor();
        m_toolSizeByKeyboard = 0;
        onToolSizeChanged(m_context.toolSize);
        onToolSizeSettled(m_context.toolSize);
    });

    m_config.checkAndHandleError();
    if (m_config.hasError()) {
        m_configError = true;
    }
    connect(
      ConfigHandler::getInstance(), &ConfigHandler::error, this, [=, this]() {
          m_configError = true;
          m_configErrorResolved = false;
          OverlayMessage::instance()->update();
      });
    connect(ConfigHandler::getInstance(),
            &ConfigHandler::errorResolved,
            this,
            [=, this]() {
                m_configError = false;
                m_configErrorResolved = true;
                OverlayMessage::instance()->update();
            });

    // Qt6 has only sizes in logical values, position is in physical values.
    // Move Help message to the logical pixel with devicePixelRatio.
    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
    QRect currentScreenGeometry = currentScreen->geometry();
    qreal currentScreenDpr = currentScreen->devicePixelRatio();
    currentScreenGeometry.moveTo(
      int(currentScreenGeometry.x() / currentScreenDpr),
      int(currentScreenGeometry.y() / currentScreenDpr));
    OverlayMessage::init(this, currentScreenGeometry);

    if (m_config.showHelp()) {
        initHelpMessage();
        OverlayMessage::push(m_helpMessage);
    }

    initQuitPrompt();

    updateCursor();

    // flameshot-ocr: Win11 风格预选区悬浮工具条
    m_preToolbar = new PreToolbar(this);
    connect(m_preToolbar,
            &PreToolbar::selectModeRequested,
            this,
            [this]() {
                if (m_activeButton) {
                    uncheckActiveTool();
                }
                if (m_eraserActive) {
                    m_eraserActive = false;
                    m_preToolbar->setEraserChecked(false);
                }
                m_preToolbar->setToolChecked(CaptureTool::NONE);
                updatePreToolbar();
            });
    connect(m_preToolbar,
            &PreToolbar::toolRequested,
            this,
            &CaptureWidget::onPreToolbarToolRequested);
    connect(m_preToolbar,
            &PreToolbar::drawColorChanged,
            this,
            &CaptureWidget::setDrawColor);
    connect(m_preToolbar,
            &PreToolbar::colorGrabRequested,
            this,
            &CaptureWidget::openColorGrab);
    connect(m_preToolbar,
            &PreToolbar::fullscreenCopyRequested,
            this,
            &CaptureWidget::fullscreenCopy);
    connect(m_preToolbar,
            &PreToolbar::saveRequested,
            this,
            &CaptureWidget::saveFullCapture);
    connect(m_preToolbar,
            &PreToolbar::settingsRequested,
            this,
            &CaptureWidget::openSettings);
    connect(m_preToolbar,
            &PreToolbar::undoRequested,
            this,
            &CaptureWidget::undo);
    // 形状填充开关切换后重绘（矩形/椭圆渲染时读取该配置）
    connect(m_preToolbar, &PreToolbar::fillToggled, this, [this]() {
        drawToolsData();
        update();
    });
    connect(m_preToolbar,
            &PreToolbar::redoRequested,
            this,
            &CaptureWidget::redo);
    connect(m_preToolbar,
            &PreToolbar::eraserRequested,
            this,
            [this]() {
                m_eraserActive = !m_eraserActive;
                if (m_eraserActive && m_activeButton) {
                    // 橡皮擦与绘制工具互斥
                    m_activeButton = nullptr;
                    releaseActiveTool();
                }
                if (m_eraserActive && m_panel->activeLayerIndex() >= 0) {
                    // 橡皮擦与对象选中态互斥（选中会让拖动变成移动）
                    m_panel->setActiveLayer(-1);
                }
                updateSelectionState();
                updateCursor();
                m_preToolbar->setEraserChecked(m_eraserActive);
            });
    m_preToolbar->show();
    positionPreToolbar();

    // flameshot-ocr: automated test hook — run OCR shortly after the GUI shows.
    // FLAMESHOT_OCR_AUTOTEST=1: whole-screen OCR; =2: half-screen selection
    // first (so the tool button bar is visible), then OCR.
    if (qEnvironmentVariableIsSet("FLAMESHOT_OCR_AUTOTEST")) {
        QTimer::singleShot(1200, this, [this]() {
            if (qEnvironmentVariable("FLAMESHOT_OCR_AUTOTEST") ==
                QLatin1String("2")) {
                QRect sel(rect().width() * 0.1,
                          rect().height() * 0.3,
                          rect().width() * 0.8,
                          rect().height() * 0.4);
                m_selection->show();
                m_selection->setGeometry(sel);
                emit m_selection->geometrySettled();
                m_buttonHandler->show();
                updateSelectionState();
                m_context.selection = sel;
            }
            runOcr();
        });
    }

    // flameshot-ocr: 状态机自测 —— 用合成鼠标事件驱动真实处理器，
    // 运行时验证「自动收笔/点击选中/角点缩放/Esc 确认」
    if (qEnvironmentVariableIsSet("FLAMESHOT_OCR_SELFTEST")) {
        QTimer::singleShot(1500, this, &CaptureWidget::runSelfTest);
    }
}

CaptureWidget::~CaptureWidget()
{
#if defined(Q_OS_MACOS)
    for (QWidget* widget : qApp->topLevelWidgets()) {
        QString className(widget->metaObject()->className());
        if (0 ==
            className.compare(CaptureWidget::staticMetaObject.className())) {
            widget->showNormal();
            widget->hide();
            break;
        }
    }
#endif
    if (m_captureDone) {
        auto lastRegion = m_selection->geometry();
        setLastRegion(lastRegion);
        QRect geometry(m_context.selection);
        geometry.setTopLeft(geometry.topLeft() + m_context.widgetOffset);
        Flameshot::instance()->exportCapture(
          pixmap(), geometry, m_context.request);
    } else {
        emit Flameshot::instance()->captureFailed();
    }
}

void CaptureWidget::initButtons()
{
    auto allButtonTypes = CaptureToolButton::getIterableButtonTypes();
    auto visibleButtonTypes = m_config.buttons();
    if ((m_context.request.tasks() == CaptureRequest::NO_TASK) ||
        (m_context.request.tasks() == CaptureRequest::PRINT_GEOMETRY)) {
        allButtonTypes.removeOne(CaptureTool::TYPE_ACCEPT);
        visibleButtonTypes.removeOne(CaptureTool::TYPE_ACCEPT);
    } else {
        // Remove irrelevant buttons from both lists
        for (auto* buttonList : { &allButtonTypes, &visibleButtonTypes }) {
            buttonList->removeOne(CaptureTool::TYPE_SAVE);
            buttonList->removeOne(CaptureTool::TYPE_COPY);
#ifdef ENABLE_IMGUR
            buttonList->removeOne(CaptureTool::TYPE_IMAGEUPLOADER);
#endif
            buttonList->removeOne(CaptureTool::TYPE_OPEN_APP);
            buttonList->removeOne(CaptureTool::TYPE_PIN);
        }
    }
    QVector<CaptureToolButton*> vectorButtons;

    // Add all buttons but hide those that were disabled in the Interface config
    // This will allow keyboard shortcuts for those buttons to work
    for (CaptureTool::Type t : allButtonTypes) {
        auto* b = new CaptureToolButton(t, this);
        b->setColor(m_uiColor);
        b->hide();
        // must be enabled for SelectionWidget's eventFilter to work correctly
        b->setAttribute(Qt::WA_NoMousePropagation);
        makeChild(b);

        switch (t) {
            case CaptureTool::TYPE_UNDO:
            case CaptureTool::TYPE_REDO:
                // nothing to do, just skip non-dynamic buttons with existing
                // hard coded slots
                break;
            default:
                // Set shortcuts for a tool
                QString shortcut =
                  ConfigHandler().shortcut(QVariant::fromValue(t).toString());
                if (!shortcut.isNull()) {
                    auto shortcuts = newShortcut(shortcut, this, nullptr);
                    for (auto* sc : shortcuts) {
                        connect(sc, &QShortcut::activated, this, [=, this]() {
                            setState(b);
                        });
                    }
                }
                break;
        }

        m_tools[t] = b->tool();
        m_buttonsByType[t] = b;

        connect(b->tool(),
                &CaptureTool::requestAction,
                this,
                &CaptureWidget::handleToolSignal);

        if (visibleButtonTypes.contains(t)) {
            connect(b,
                    &CaptureToolButton::pressedButtonLeftClick,
                    this,
                    &CaptureWidget::handleButtonLeftClick);

            if (b->tool()->isSelectable()) {
                connect(b,
                        &CaptureToolButton::pressedButtonRightClick,
                        this,
                        &CaptureWidget::handleButtonRightClick);
            }

            vectorButtons << b;
        }
    }
    m_buttonHandler->setButtons(vectorButtons);
}

void CaptureWidget::handleButtonRightClick(CaptureToolButton* b)
{
    if (!b) {
        return;
    }

    // if button already selected, do not deselect it on right click
    if (!m_activeButton || m_activeButton != b) {
        setState(b);
    }
    if (!m_panel->isVisible()) {
        m_panel->show();
    }
}

void CaptureWidget::handleButtonLeftClick(CaptureToolButton* b)
{
    if (!b) {
        return;
    }
    if (m_ocrPanel && m_ocrPanel->isVisible() && b->tool() &&
        b->tool()->type() != CaptureTool::TYPE_OCR) {
        m_ocrPanel->hide();
    }
    setState(b);
}

void CaptureWidget::xywhTick()
{
    m_xywhDisplay = false;
    update();
}

void CaptureWidget::onDisplayGridChanged(bool display)
{
    m_displayGrid = display;
    repaint();
}

void CaptureWidget::onGridSizeChanged(int size)
{
    m_gridSize = size;
    repaint();
}

void CaptureWidget::startColorGrab()
{
    if (m_sidePanel) {
        m_sidePanel->startColorGrab();
    }
}

// flameshot-ocr: 预选区悬浮工具条 —— 显隐与位置
void CaptureWidget::updatePreToolbar()
{
    if (!m_preToolbar) {
        return;
    }
    const bool grabbing = m_colorGrabber && m_colorGrabber->isVisible();
    const bool show =
      !m_selection->isVisible() && !m_mouseIsClicked && !grabbing;
    m_preToolbar->setVisible(show);
    if (show) {
        positionPreToolbar();
    }
}

void CaptureWidget::positionPreToolbar()
{
    if (!m_preToolbar) {
        return;
    }
    m_preToolbar->adjustSize();
    const QSize size = m_preToolbar->size();
    m_preToolbar->move(qMax(0, (width() - size.width()) / 2), 12);
}

// flameshot-ocr: 预工具条按钮处理 ——
// 再次点击已选中的工具 = 取消选中（回到框选态）；点击其它工具自动切换
void CaptureWidget::onPreToolbarToolRequested(CaptureTool::Type type)
{
    if (m_activeButton && activeButtonToolType() == type) {
        uncheckActiveTool();
        m_preToolbar->setToolChecked(CaptureTool::NONE);
        return;
    }
    // 选工具时退出橡皮擦模式（两者互斥）
    if (m_eraserActive) {
        m_eraserActive = false;
        m_preToolbar->setEraserChecked(false);
    }
    if (auto* button = m_buttonsByType.value(type)) {
        setState(button);
    }
    m_preToolbar->setToolChecked(type);
}

// flameshot-ocr: 取色（按 colorPickFormat 自动复制）
void CaptureWidget::openColorGrab()
{
    if (m_colorGrabber) {
        return;
    }
    m_preToolbar->hide();
    m_colorGrabber = new ColorGrabWidget(&m_context.origScreenshot, this);
    connect(m_colorGrabber,
            &ColorGrabWidget::colorGrabbed,
            this,
            &CaptureWidget::onColorGrabbed);
    connect(m_colorGrabber,
            &ColorGrabWidget::grabAborted,
            this,
            &CaptureWidget::onColorGrabAborted);
    m_colorGrabber->startGrabbing();
}

void CaptureWidget::onColorGrabbed()
{
    if (!m_colorGrabber) {
        return;
    }
    const QColor color = m_colorGrabber->color();
    m_colorGrabber->deleteLater();
    m_colorGrabber = nullptr;

    QString text;
    if (ConfigHandler().colorPickFormat() == QLatin1String("rgb")) {
        text = QStringLiteral("%1, %2, %3")
                 .arg(color.red())
                 .arg(color.green())
                 .arg(color.blue());
    } else {
        text = color.name(QColor::HexRgb);
    }
    QApplication::clipboard()->setText(text);
    QToolTip::showText(mapToGlobal(m_context.mousePos),
                       OcrPanel::tr2("已复制颜色：", "Copied color: ") + text,
                       this);
    updatePreToolbar();
}

void CaptureWidget::onColorGrabAborted()
{
    if (m_colorGrabber) {
        m_colorGrabber->deleteLater();
        m_colorGrabber = nullptr;
    }
    updatePreToolbar();
}

// flameshot-ocr: 合成鼠标事件自测（FLAMESHOT_OCR_SELFTEST=1）
void CaptureWidget::synthMousePress(const QPoint& pos)
{
    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(pos),
                      QPointF(pos),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    mousePressEvent(&press);
}

void CaptureWidget::synthMouseMove(const QPoint& pos)
{
    QMouseEvent move(QEvent::MouseMove,
                     QPointF(pos),
                     QPointF(pos),
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    mouseMoveEvent(&move);
}

void CaptureWidget::synthMouseRelease(const QPoint& pos)
{
    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(pos),
                        QPointF(pos),
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    mouseReleaseEvent(&release);
}

void CaptureWidget::runSelfTest()
{
    // ① 免选区画笔画一笔 → 应自动收笔
    setState(m_buttonsByType.value(CaptureTool::TYPE_PENCIL));
    synthMousePress(QPoint(300, 300));
    synthMouseMove(QPoint(350, 340));
    synthMouseMove(QPoint(400, 380));
    synthMouseRelease(QPoint(400, 380));
    qWarning() << "SELFTEST 3a tool-stays-armed:"
               << (m_activeButton != nullptr ? "PASS" : "FAIL")
               << "cursor:" << cursor().shape();

    // ② 点击笔迹 → 对象被选中，光标应为移动
    synthMousePress(QPoint(350, 340));
    const bool selected = m_panel->activeLayerIndex() >= 0;
    qWarning() << "SELFTEST 3b click-select:"
               << (selected ? "PASS" : "FAIL")
               << "cursor:" << cursor().shape();

    // ③ 角点命中测试
    auto object = activeToolObject();
    const QRect before =
      object ? object->boundingRect().normalized() : QRect();
    const QPoint corner = before.topLeft();
    const int handle = objectResizeHandleAt(corner);
    qWarning() << "SELFTEST 3c handle-hit:"
               << (handle > 0 ? "PASS" : "FAIL") << "handle:" << handle
               << "rect:" << before;

    // 缩放自测段：固定到 50% 灵敏度以保证确定性，结束后恢复用户配置
    const int savedResizeSensitivity = ConfigHandler().resizeSensitivity();
    ConfigHandler().setResizeSensitivity(50);

    // ④ 拖左上角向左上 → 包围盒应变大
    synthMousePress(corner);
    synthMouseMove(corner - QPoint(60, 60));
    synthMouseRelease(corner - QPoint(60, 60));
    const QRect after =
      activeToolObject() ? activeToolObject()->boundingRect().normalized()
                         : QRect();
    qWarning() << "SELFTEST 3d resize:"
               << (!after.isNull() && after.width() > before.width() &&
                         after.height() > before.height()
                     ? "PASS"
                     : "FAIL")
               << before << "->" << after;

    // 3e-3g: 矩形/椭圆双向缩放（含从框外沿抓取，复现用户报告的问题）
    auto lastRect = [this]() {
        if (m_captureToolObjects.size() <= 0) {
            return QRect();
        }
        auto object = m_captureToolObjects.at(m_captureToolObjects.size() - 1);
        return object ? object->boundingRect().normalized() : QRect();
    };

    setState(m_buttonsByType.value(CaptureTool::TYPE_RECTANGLE));
    synthMousePress(QPoint(700, 260));
    synthMouseMove(QPoint(850, 360));
    synthMouseRelease(QPoint(850, 360));
    const QRect rectBefore = lastRect();
    // 放大：从右边缘框外 3px 起拖，向右 60px
    synthMousePress(QPoint(rectBefore.right() + 3, rectBefore.center().y()));
    synthMouseMove(QPoint(rectBefore.right() + 63, rectBefore.center().y()));
    synthMouseRelease(QPoint(rectBefore.right() + 63, rectBefore.center().y()));
    const QRect rectWide = lastRect();
    qWarning() << "SELFTEST 3e rect-enlarge-from-outside:"
               << (rectWide.width() > rectBefore.width() ? "PASS" : "FAIL")
               << rectBefore << "->" << rectWide;

    // 缩小：从右下角框外 2px 起拖，向左上
    synthMousePress(rectWide.bottomRight() + QPoint(2, 2));
    synthMouseMove(rectWide.bottomRight() - QPoint(40, 30));
    synthMouseRelease(rectWide.bottomRight() - QPoint(40, 30));
    const QRect rectSmaller = lastRect();
    qWarning() << "SELFTEST 3f rect-shrink-from-outside:"
               << ((rectSmaller.width() < rectWide.width() &&
                    rectSmaller.height() < rectWide.height())
                     ? "PASS"
                     : "FAIL")
               << rectWide << "->" << rectSmaller;

    // 3g: 椭圆放大（右边缘框外 3px 起拖）
    setState(m_buttonsByType.value(CaptureTool::TYPE_CIRCLE));
    synthMousePress(QPoint(700, 480));
    synthMouseMove(QPoint(820, 560));
    synthMouseRelease(QPoint(820, 560));
    const QRect ellipseBefore = lastRect();
    synthMousePress(
      QPoint(ellipseBefore.right() + 3, ellipseBefore.center().y()));
    synthMouseMove(
      QPoint(ellipseBefore.right() + 63, ellipseBefore.center().y()));
    synthMouseRelease(
      QPoint(ellipseBefore.right() + 63, ellipseBefore.center().y()));
    const QRect ellipseAfter = lastRect();
    qWarning() << "SELFTEST 3g ellipse-enlarge-from-outside:"
               << (ellipseAfter.width() > ellipseBefore.width() ? "PASS"
                                                                : "FAIL")
               << ellipseBefore << "->" << ellipseAfter;

    // 3h: 空心矩形内部按压拖动 → 整体平移（修复“方框不能拖动”）
    setState(m_buttonsByType.value(CaptureTool::TYPE_RECTANGLE));
    synthMousePress(QPoint(400, 700));
    synthMouseMove(QPoint(520, 780));
    synthMouseRelease(QPoint(520, 780));
    const QRect mvBefore = lastRect();
    const QPoint mvCenter = mvBefore.center();
    synthMousePress(mvCenter);
    synthMouseMove(mvCenter + QPoint(20, 15));
    synthMouseMove(mvCenter + QPoint(80, 60));
    synthMouseMove(mvCenter + QPoint(120, 90));
    synthMouseRelease(mvCenter + QPoint(120, 90));
    const QRect mvAfter = lastRect();
    const QPoint shift = mvAfter.topLeft() - mvBefore.topLeft();
    qWarning() << "SELFTEST 3h rect-move-by-interior-drag:"
               << ((shift.manhattanLength() >= 50 &&
                    qAbs(mvAfter.width() - mvBefore.width()) <= 2 &&
                    qAbs(mvAfter.height() - mvBefore.height()) <= 2)
                     ? "PASS"
                     : "FAIL")
               << mvBefore << "->" << mvAfter << "shift:" << shift;

    // 3i: 自适应增益 —— 慢速微拖精调（慢速增益），快速拖动 1:1 跟手
    {
        const int sensitivity =
          qBound(1, ConfigHandler().resizeSensitivity(), 100);
        const qreal slowGain = sensitivity / 100.0;
        const QRect gainBefore = lastRect();
        synthMousePress(
          QPoint(gainBefore.right() + 3, gainBefore.center().y()));
        // 20 次 2px 微小移动（慢速 → 精调增益）
        for (int i = 1; i <= 20; ++i) {
            synthMouseMove(QPoint(gainBefore.right() + 3 + i * 2,
                                  gainBefore.center().y()));
        }
        // 一次 100px 大幅移动（快速 → 1:1 跟手）
        synthMouseMove(QPoint(gainBefore.right() + 3 + 140,
                              gainBefore.center().y()));
        synthMouseRelease(QPoint(gainBefore.right() + 3 + 140,
                                 gainBefore.center().y()));
        const QRect gainAfter = lastRect();
        const int expected =
          100 + qRound(20 * 2 * slowGain); // 快段 100px + 慢段 40px×慢增益
        const int actual = gainAfter.width() - gainBefore.width();
        qWarning() << "SELFTEST 3i adaptive-gain:"
                   << (qAbs(actual - expected) <= 8 ? "PASS" : "FAIL")
                   << "slowGain:" << slowGain << "expected~" << expected
                   << "actual:" << actual;
    }
    // 恢复用户配置的灵敏度
    ConfigHandler().setResizeSensitivity(savedResizeSensitivity);

    // 3j: 工具按钮再点一次 → 取消选中（走生产处理函数）
    {
        onPreToolbarToolRequested(CaptureTool::TYPE_RECTANGLE);
        const bool armed = (m_activeButton != nullptr);
        onPreToolbarToolRequested(CaptureTool::TYPE_RECTANGLE);
        const bool disarmed = (m_activeButton == nullptr);
        qWarning() << "SELFTEST 3j tool-toggle-off:"
                   << ((armed && disarmed) ? "PASS" : "FAIL")
                   << "armed:" << armed << "afterSecondClick:" << disarmed;
    }

    // ⑤ 选区尺寸标签渲染验证（实心深底 + 白字，可读性）
    {
        // 取色放大镜：startGrabbing 后无需移动鼠标即应可见
        ColorGrabWidget grabber(&m_context.origScreenshot, this);
        grabber.startGrabbing();
        qWarning() << "SELFTEST 2 magnifier-visible:"
                   << (grabber.isVisible() ? "PASS" : "FAIL");
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(qApp, &esc);
    }

    m_selection->show();
    const QRect selRect(rect().width() * 0.25, rect().height() * 0.25,
                        rect().width() * 0.4, rect().height() * 0.3);
    m_selection->setGeometry(selRect);
    emit m_selection->geometrySettled();
    m_context.selection = selRect;
    showxywh();
    repaint();
    {
        const qreal scale = m_context.screenshot.devicePixelRatio();
        QFont boxFont = font();
        const QFontMetrics boxMetrics(boxFont);
        const QString text =
          QStringLiteral("%1 × %2 px")
            .arg(QString::number(int(selRect.width() * scale)),
                 QString::number(int(selRect.height() * scale)));
        QRect box = boxMetrics.boundingRect(text);
        box.adjust(0, 0, 10, 12);
        const QRect labelRect(selRect.left(), selRect.top() - box.height() - 6,
                              box.width(), box.height());
        const QImage grabbed = grab(labelRect.adjusted(-15, -15, 45, 45))
                                 .toImage();
        grabbed.save(QStringLiteral("/tmp/label_grab.png"));
        int darkPixels = 0;
        int whitePixels = 0;
        for (int y = 0; y < grabbed.height(); ++y) {
            for (int x = 0; x < grabbed.width(); ++x) {
                const QColor c = grabbed.pixelColor(x, y);
                if (c.red() < 70 && c.green() < 70 && c.blue() < 90) {
                    ++darkPixels;
                } else if (c.red() > 225 && c.green() > 225 &&
                           c.blue() > 225) {
                    ++whitePixels;
                }
            }
        }
        qWarning() << "SELFTEST 4 label-render:"
                   << ((darkPixels > 100 && whitePixels > 20) ? "PASS"
                                                              : "FAIL")
                   << "dark:" << darkPixels << "white:" << whitePixels;
    }

    // ⑥ 钉图：工具条可见 + 滚轮双向缩放 + 复制含图
    {
        const QPixmap pinSource = m_context.origScreenshot.scaled(
          400, 300, Qt::KeepAspectRatio, Qt::FastTransformation);
        // 先归零配置（防上一轮测试把 true 写进 ini 造成断言污染）
        ConfigHandler().setPinShowToolbar(false);
        auto* pin = new PinWidget(pinSource, QRect(150, 150, 400, 300));
        pin->show();
        for (int i = 0; i < 5; ++i) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        auto* toolBar = pin->findChild<QWidget*>(QStringLiteral("pinToolBar"));
        // 默认隐藏
        const bool defaultHidden = toolBar && !toolBar->isVisible();
        qWarning() << "SELFTEST 5 pin-toolbar-default-hidden:"
                   << (defaultHidden ? "PASS" : "FAIL");
        // 开启后应具有真实尺寸（仅 isVisible() 会放过“未加入布局的 0 尺寸控件”）
        ConfigHandler().setPinShowToolbar(true);
        auto* pin2 = new PinWidget(pinSource, QRect(150, 150, 400, 300));
        pin2->show();
        for (int i = 0; i < 5; ++i) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        auto* toolBar2 =
          pin2->findChild<QWidget*>(QStringLiteral("pinToolBar"));
        const bool toolBarOk = toolBar2 && toolBar2->isVisible() &&
                               toolBar2->width() > 150 && toolBar2->height() > 12;
        qWarning() << "SELFTEST 5 pin-toolbar-toggle:"
                   << (toolBarOk ? "PASS" : "FAIL")
                   << "size:" << (toolBar2 ? toolBar2->size() : QSize());
        ConfigHandler().setPinShowToolbar(false);

        const int baseHeight = pin->height();
        for (int i = 0; i < 3; ++i) {
            QWheelEvent up(QPointF(200, 200), QPointF(200, 200), QPoint(0, 0),
                           QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
            QApplication::sendEvent(pin, &up);
            QApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        pin->grab();
        const int upHeight = pin->height();
        for (int i = 0; i < 6; ++i) {
            QWheelEvent down(QPointF(200, 200), QPointF(200, 200),
                             QPoint(0, 0), QPoint(0, -120), Qt::NoButton,
                             Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(pin, &down);
            QApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        pin->grab();
        const int downHeight = pin->height();
        qWarning() << "SELFTEST 4 wheel-zoom:"
                   << ((upHeight > baseHeight && downHeight < upHeight)
                         ? "PASS"
                         : "FAIL")
                   << "base:" << baseHeight << "up:" << upHeight
                   << "down:" << downHeight;

        pin->copyToClipboard();
        bool hasImage = false;
        QStringList clipboardFormats;
        for (int i = 0; i < 30 && !hasImage; ++i) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
            hasImage = mime && mime->hasImage();
            if (!hasImage && mime) {
                clipboardFormats = mime->formats();
            }
        }
        qWarning() << "SELFTEST 5 pin-copy:"
                   << ((hasImage || clipboardFormats.contains(
                                          QStringLiteral("image/png")))
                         ? "PASS"
                         : "FAIL")
                   << clipboardFormats;
        // 注意：测试中不关闭 pin —— 在合成流程里 close 会触发应用的
        // captureFailed→exit 机制，打断后续嵌套事件循环测试；
        // 生产流程中钉图是“先关截图窗再钉”，不存在此路径。

        // 10: 钉图标注引擎实证 —— 画矩形 → 清空（闪退金丝雀）→ 再画 → 撤销
        {
            // 先归零配置（前序测试曾把 true 写进 ini 造成断言污染）
            ConfigHandler().setPinShowToolbar(false);
            auto* annotator = pin2->findChild<PinAnnotator*>();
            qWarning() << "SELFTEST 10 pin-annotator-found:"
                       << (annotator ? "PASS" : "FAIL")
                       << "geo:" << (annotator ? annotator->geometry() : QRect())
                       << "labelGeo:" << pin2->findChild<QLabel*>()->geometry();
            auto annoPress = [&](const QPoint& p) {
                QMouseEvent ev(QEvent::MouseButtonPress, QPointF(p),
                               QPointF(p), Qt::LeftButton, Qt::LeftButton,
                               Qt::NoModifier);
                QApplication::sendEvent(annotator, &ev);
            };
            auto annoMove = [&](const QPoint& p) {
                QMouseEvent ev(QEvent::MouseMove, QPointF(p), QPointF(p),
                               Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(annotator, &ev);
            };
            auto annoRelease = [&](const QPoint& p) {
                QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(p),
                               QPointF(p), Qt::LeftButton, Qt::NoButton,
                               Qt::NoModifier);
                QApplication::sendEvent(annotator, &ev);
            };
            // 选矩形工具（走工具条按钮的生产路径）
            QToolButton* rectBtn = nullptr;
            const auto buttons = toolBar2->findChildren<QToolButton*>();
            for (auto* b : buttons) {
                if (b->toolTip().contains(
                      OcrPanel::tr2("矩形", "Rectangle"))) {
                    rectBtn = b;
                }
            }
            qWarning() << "SELFTEST 10 pin-rect-btn-found:"
                       << (rectBtn ? "PASS" : "FAIL");
            if (rectBtn) {
                rectBtn->click();
            }
            annoPress(QPoint(60, 60));
            annoMove(QPoint(160, 130));
            annoRelease(QPoint(160, 130));
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            qWarning() << "SELFTEST 10 pin-rect-drawn:"
                       << ((annotator && annotator->hasShapes()) ? "PASS"
                                                                  : "FAIL");
            // 清空（用户报告的闪退点）—— 崩溃则测试进程直接终止
            annotator->clearShapes();
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            qWarning() << "SELFTEST 10 pin-clear-survived:"
                       << ((!annotator->hasShapes()) ? "PASS" : "FAIL");
            // 再画一笔并撤销
            annoPress(QPoint(200, 200));
            annoMove(QPoint(260, 250));
            annoRelease(QPoint(260, 250));
            annotator->undo();
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            qWarning() << "SELFTEST 10 pin-undo:"
                       << ((!annotator->hasShapes()) ? "PASS" : "FAIL");

            // 12-15: 钉图新工具 —— 文字 / 序号 / 橡皮擦 / 重做
            fflush(stderr);

            // 12: 文字工具 —— 放置 + 键入 + 回车提交
            annotator->setTool(PinAnnotator::Text);
            annoPress(QPoint(80, 80));
            const std::initializer_list<std::pair<Qt::Key, QString>> keys = {
                { Qt::Key_H, QStringLiteral("H") },
                { Qt::Key_I, QStringLiteral("i") },
            };
            for (const auto& k : keys) {
                QKeyEvent keyEv(QEvent::KeyPress, k.first, Qt::NoModifier,
                                k.second);
                QApplication::sendEvent(annotator, &keyEv);
            }
            QKeyEvent enterEv(QEvent::KeyPress, Qt::Key_Return,
                              Qt::NoModifier);
            QApplication::sendEvent(annotator, &enterEv);
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            const int afterText = annotator->shapeCount();
            qWarning() << "SELFTEST 12 pin-text:" << (afterText >= 1 ? "PASS" : "FAIL")
                       << "shapes:" << afterText;

            // 13: 序号工具 —— 点击放置递增编号
            annotator->setTool(PinAnnotator::Number);
            annoPress(QPoint(300, 80));
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            const int afterNumber = annotator->shapeCount();
            qWarning() << "SELFTEST 13 pin-number:"
                       << (afterNumber == afterText + 1 ? "PASS" : "FAIL")
                       << afterText << "->" << afterNumber;

            // 14: 橡皮擦 —— 点击文字包围盒删除
            annotator->setTool(PinAnnotator::Eraser);
            annoPress(QPoint(90, 95));
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            const int afterErase = annotator->shapeCount();
            qWarning() << "SELFTEST 14 pin-eraser:"
                       << (afterErase == afterNumber - 1 ? "PASS" : "FAIL")
                       << afterNumber << "->" << afterErase;

            // 15: 重做 —— 撤销再重做，形状数恢复
            annotator->undo();
            const int afterUndo = annotator->shapeCount();
            annotator->redo();
            const int afterRedo = annotator->shapeCount();
            qWarning() << "SELFTEST 15 pin-redo:"
                       << (afterUndo == afterErase - 1 &&
                                 afterRedo == afterUndo + 1
                             ? "PASS"
                             : "FAIL")
                       << afterErase << "-> undo" << afterUndo << "-> redo"
                       << afterRedo;

            // 16: displayScale 含 DPR（125% 屏应 ≈1.25，修复坐标偏移）
            {
                const QPixmap labelPix =
                  pin2->findChild<QLabel*>()->pixmap();
                qWarning() << "GEODBG pin2 size:" << pin2->size()
                           << "label:" << pin2->findChild<QLabel*>()->size()
                           << "pixmap:" << labelPix.size()
                           << "dpr:" << labelPix.devicePixelRatio();
            }
            qWarning() << "SELFTEST 16 pin-displayscale:"
                       << (qAbs(annotator->displayScale() - 1.25) < 0.01
                             ? "PASS"
                             : "FAIL")
                       << annotator->displayScale();

            // 17: 填充开关 —— 描边矩形中心透明，填充后不透明
            annotator->setTool(PinAnnotator::Rectangle);
            // base 图 raw 500x375（displayScale=1.25）：
            // annotator (200,80)-(360,160) → base (250,100)-(450,200)
            annoPress(QPoint(200, 80));
            annoMove(QPoint(360, 160));
            annoRelease(QPoint(360, 160));
            {
                const QImage outlineImg =
                  annotator->renderToImage(pin2->findChild<QLabel*>()
                                             ->pixmap()
                                             .size());
                const QColor center1 =
                  outlineImg.pixelColor(QPoint(350, 150));
                annotator->setFill(true);
                const QImage filledImg =
                  annotator->renderToImage(pin2->findChild<QLabel*>()
                                             ->pixmap()
                                             .size());
                const QColor center2 =
                  filledImg.pixelColor(QPoint(350, 150));
                annotator->setFill(false);
                qWarning() << "SELFTEST 17 pin-fill:"
                           << (center1.alpha() == 0 && center2.alpha() > 0
                                 ? "PASS"
                                 : "FAIL")
                           << "outline:" << center1.alpha()
                           << "filled:" << center2.alpha();
            }

            // 18: 马赛克 —— 采样底图像素化，合成图与原区域不同
            annotator->setTool(PinAnnotator::Pixelate);
            // annotator (60,200)-(200,260) → base (75,250)-(250,325)，图内
            annoPress(QPoint(60, 200));
            annoMove(QPoint(200, 260));
            annoRelease(QPoint(200, 260));
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            {
                const QImage base =
                  pin2->findChild<QLabel*>()->pixmap().toImage();
                const QImage composited =
                  annotator->renderToImage(base.size());
                bool differs = false;
                for (int y = 260; y < 315 && !differs; y += 5) {
                    for (int x = 85; x < 240; x += 10) {
                        if (base.pixelColor(x, y) !=
                            composited.pixelColor(x, y)) {
                            differs = true;
                            break;
                        }
                    }
                }
                qWarning() << "SELFTEST 18 pin-pixelate:"
                           << (annotator->hasShapes() && differs ? "PASS"
                                                                 : "FAIL");
            }

            // 19: 输入法 —— commitString 提交中文
            annotator->setTool(PinAnnotator::Text);
            annoPress(QPoint(80, 430));
            QKeyEvent imeStart(QEvent::KeyPress, Qt::Key_Space,
                               Qt::NoModifier);
            QApplication::sendEvent(annotator, &imeStart);
            QInputMethodEvent ime;
            ime.setCommitString(QStringLiteral("中文"));
            QApplication::sendEvent(annotator, &ime);
            QApplication::sendEvent(annotator, &enterEv);
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            qWarning() << "SELFTEST 19 pin-ime:"
                       << ((annotator->lastShapeText() ==
                              QStringLiteral("中文"))
                             ? "PASS"
                             : "FAIL")
                       << annotator->lastShapeText();

            // 20: Ctrl+滚轮 —— 透明度变化 + 中央提示
            {
                const qreal opBefore = pin2->pinOpacity();
                QWheelEvent ctrlWheel(
                  QPointF(200, 200), QPointF(200, 200), QPoint(0, 0),
                  QPoint(0, -120), Qt::NoButton, Qt::ControlModifier,
                  Qt::NoScrollPhase, false);
                QApplication::sendEvent(pin2, &ctrlWheel);
                QApplication::processEvents(QEventLoop::AllEvents, 50);
                const qreal opAfter = pin2->pinOpacity();
                auto* toast = pin2->findChild<QLabel*>();
                bool toastOk = false;
                const auto labels = pin2->findChildren<QLabel*>();
                for (auto* l : labels) {
                    if (l->isVisible() &&
                        l->text().contains(
                          OcrPanel::tr2("透明度", "Opacity"))) {
                        toastOk = true;
                    }
                }
                qWarning() << "SELFTEST 20 pin-ctrlwheel-opacity:"
                           << (opAfter < opBefore && toastOk ? "PASS"
                                                             : "FAIL")
                           << opBefore << "->" << opAfter
                           << "toast:" << toastOk;
                pin2->setWindowOpacity(1.0);
            }
            fflush(stderr);
        }

        // 11: 橡皮擦 —— 包围盒命中（空心矩形内部点击即删）
        {
            // 画一个空心矩形（默认不填充）
            onPreToolbarToolRequested(CaptureTool::TYPE_RECTANGLE);
            synthMousePress(QPoint(900, 300));
            synthMouseMove(QPoint(1050, 400));
            synthMouseRelease(QPoint(1050, 400));
            const int countBefore = m_captureToolObjects.size();
            // 进入橡皮擦，点击矩形“内部”（非边框）
            onPreToolbarToolRequested(CaptureTool::TYPE_RECTANGLE); // 先取消选中
            handleToolSignal(CaptureTool::REQ_CLEAR_SELECTION);
            m_eraserActive = true;
            updateSelectionState();
            updateCursor();
            synthMousePress(QPoint(975, 350));
            synthMouseRelease(QPoint(975, 350));
            const int countAfter = m_captureToolObjects.size();
            m_eraserActive = false;
            updateSelectionState();
            updateCursor();
            qWarning() << "SELFTEST 11 eraser-bbox-delete:"
                       << (countAfter == countBefore - 1 ? "PASS" : "FAIL")
                       << countBefore << "->" << countAfter;
        }
    }

    // 22: 填充开关端到端 —— 点击真实按钮 → 画矩形 → 像素断言
    {
        auto* preFill = m_preToolbar->findChild<QToolButton*>(
          QStringLiteral("preFillBtn"));
        qWarning() << "SELFTEST 22 fill-btn-found:"
                   << (preFill ? "PASS" : "FAIL");
        if (preFill) {
            // 描边矩形
            preFill->setChecked(false);
            emit preFill->toggled(false);
            onPreToolbarToolRequested(CaptureTool::TYPE_RECTANGLE);
            synthMousePress(QPoint(300, 500));
            synthMouseMove(QPoint(500, 620));
            synthMouseRelease(QPoint(500, 620));
            const QImage outlineShot = m_context.screenshot.copy(
              QRect(300, 500, 200, 120)).toImage();
            const QColor o = outlineShot.pixelColor(QPoint(100, 60));
            // 切到填充，再画一个（画笔色 = drawColor，中心应为画笔色）
            preFill->setChecked(true);
            emit preFill->toggled(true);
            // 第二个矩形画在不重叠位置（点到已有形状会被解释为选中）
            synthMousePress(QPoint(300, 700));
            synthMouseMove(QPoint(500, 820));
            synthMouseRelease(QPoint(500, 820));
            const QImage filledShot = m_context.screenshot.copy(
              QRect(300, 700, 200, 120)).toImage();
            const QColor f = filledShot.pixelColor(QPoint(100, 60));
            const QColor pen = ConfigHandler().drawColor();
            const bool outlineIsBg =
              !(o == pen);
            const bool filledIsPen =
              (f == pen);
            qWarning() << "SELFTEST 22 pretoolbar-fill-e2e:"
                       << (outlineIsBg && filledIsPen ? "PASS" : "FAIL")
                       << "outline-center:" << o.name()
                       << "filled-center:" << f.name()
                       << "pen:" << pen.name();
            // 恢复
            preFill->setChecked(false);
            emit preFill->toggled(false);
            onPreToolbarToolRequested(CaptureTool::TYPE_RECTANGLE);
            onPreToolbarToolRequested(CaptureTool::TYPE_RECTANGLE);
        }
    }

    // 21: 滚轮粗细提示圈跟随光标（上游固定在屏幕左上角）。
    // Wayland 不允许程序移动光标，故验证定位公式=光标位置+偏移，
    // 且不等于旧的“屏幕左上角+偏移”
    {
        const QPoint cursorLocal = mapFromGlobal(QCursor::pos());
        QWheelEvent wheelEv(QPointF(cursorLocal), QPointF(cursorLocal),
                            QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                            Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(this, &wheelEv);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        const QPoint notifierPos = m_notifierBox->pos();
        const int offset = m_notifierBox->width() / 4;
        const QPoint expected = cursorLocal + QPoint(offset, offset);
        // 定位公式 = 光标位置 + 偏移（上游为屏幕左上角 + 偏移）。
        // 光标恰在屏幕角时两公式重合，故只断言公式匹配
        qWarning() << "SELFTEST 21 wheel-notifier-follows-cursor:"
                   << (notifierPos == expected ? "PASS" : "FAIL")
                   << "notifier:" << notifierPos << "expected:" << expected;
        // 视觉留证：提示圈渲染在光标旁（保存提示圈局部截图）
        if (notifierPos == expected) {
            const QRect around(notifierPos - QPoint(30, 30), QSize(120, 100));
            grab(around).save(QStringLiteral("/tmp/notifier_visual.png"));
        }
    }


    // ⑦ 确认框与「关闭返回悬浮工具条」行为（Esc / ✕ / 钉图三条路径）
    // 用轮询代替固定延时（消除弹窗出现时机与定时器的竞态）
    auto runConfirmTest =
      [this](const char* label,
             const std::function<void()>& action,
             bool expectPrompt,
             const std::function<bool()>& extra = {}) {
          auto promptShown = std::make_shared<bool>(false);
          auto* poll = new QTimer(this);
          poll->setInterval(50);
          connect(poll,
                  &QTimer::timeout,
                  this,
                  [this, promptShown, poll]() {
                      if (m_quitPrompt && m_quitPrompt->isVisible()) {
                          *promptShown = true;
                          poll->stop();
                          poll->deleteLater();
                          m_quitPrompt->done(QMessageBox::No);
                      }
                  });
          poll->start();
          action();
          poll->stop();
          poll->deleteLater();
          const bool stillOpen = isVisible();
          const bool extraOk = extra ? extra() : true;
          const bool pass =
            (*promptShown == expectPrompt) && stillOpen && extraOk;
          qWarning() << "SELFTEST" << label << ":" << (pass ? "PASS" : "FAIL")
                     << "prompt:" << *promptShown << "(expect" << expectPrompt
                     << ") open:" << stillOpen << "extra:" << extraOk;
          fflush(stderr);
      };

    runConfirmTest(
      "6 esc-confirm",
      [this]() {
          // 规范化前置状态（消除前序测试对工具/选区/选中的残留影响），
          // Esc 直达退出确认。（递增链见测试 8）
          uncheckActiveTool();
          m_panel->setActiveLayer(-1);
          m_selection->hide();
          deleteToolWidgetOrClose();
      },
      true);
    runConfirmTest("7 exit-button-confirm",
                   [this]() { handleToolSignal(CaptureTool::REQ_CLOSE_GUI); },
                   true);
    // 8: 有选区时“关闭”应取消选区、回到悬浮工具条页（不弹退出确认、保留标注）
    runConfirmTest(
      "8 selection-close-to-toolbar",
      [this]() {
          m_selection->show();
          const QRect selRect(
            rect().width() / 5, rect().height() / 5, 400, 300);
          m_selection->setGeometry(selRect);
          m_context.selection = selRect;
          emit m_selection->geometrySettled();
          deleteToolWidgetOrClose();
      },
      false,
      [this]() {
          return !m_selection->isVisible() && m_preToolbar->isVisible() &&
                 !m_captureToolObjects.captureToolObjects().isEmpty();
      });
    // 9: 钉图（任务型关闭）不应弹退出确认 —— 最后执行（正常关闭窗口）
    {
        auto pinPrompt = std::make_shared<bool>(false);
        auto* pinPoll = new QTimer(this);
        pinPoll->setInterval(50);
        connect(pinPoll,
                &QTimer::timeout,
                this,
                [this, pinPrompt, pinPoll]() {
                    if (m_quitPrompt && m_quitPrompt->isVisible()) {
                        *pinPrompt = true;
                        pinPoll->stop();
                        pinPoll->deleteLater();
                        m_quitPrompt->done(QMessageBox::No);
                    }
                });
        pinPoll->start();
        m_context.request.addTask(CaptureRequest::PIN);
        handleToolSignal(CaptureTool::REQ_CLOSE_GUI);
        pinPoll->stop();
        qWarning() << "SELFTEST 9 pin-no-prompt:"
                   << (!*pinPrompt ? "PASS" : "FAIL")
                   << "prompt:" << *pinPrompt;
        fflush(stderr);
    }
    qWarning() << "SELFTEST: end";
    fflush(stderr);

    // flameshot-ocr: 自测完毕后优雅退出（关闭所有窗口 → 事件循环自然结束），
    // 让 LeakSanitizer 在进程退出时输出完整的泄漏报告
    if (qEnvironmentVariableIsSet("FLAMESHOT_OCR_SELFTEST")) {
        QTimer::singleShot(500, this, [this]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (widget && widget->isVisible() && widget != this &&
                    widget->metaObject()->className() ==
                      QStringLiteral("PinWidget")) {
                    widget->close();
                }
            }
            close();
        });
    }
}

// flameshot-ocr: 全屏复制 / 全屏保存 / 设置入口（直接导出，不走析构路径）
void CaptureWidget::fullscreenCopy()
{
    if (m_activeTool) {
        commitCurrentTool();
    }
    const QPixmap capture = pixmap();
    m_captureDone = true;
    m_context.request = CaptureRequest::GRAPHICAL_MODE;
    hide();
    saveToClipboard(capture);
    close();
}

void CaptureWidget::saveFullCapture()
{
    if (m_activeTool) {
        commitCurrentTool();
    }
    const QPixmap capture = pixmap();
    m_captureDone = true;
    m_context.request = CaptureRequest::GRAPHICAL_MODE;
    hide();
    saveToFilesystemGUI(capture);
    close();
}

void CaptureWidget::openSettings()
{
    Flameshot::instance()->config();
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* configWindow = qobject_cast<ConfigWindow*>(widget)) {
            configWindow->show();
            configWindow->raise();
            configWindow->activateWindow();
        }
    }
}

void CaptureWidget::showxywh()
{
    m_xywhDisplay = true;
    update();
    int timeout = m_config.showSelectionGeometryHideTime();
    if (timeout != 0) {
        m_xywhTimer.start(timeout);
    }
}

void CaptureWidget::initHelpMessage()
{
    QList<QPair<QString, QString>> keyMap;
    keyMap << std::pair(tr("Mouse"), tr("Select screenshot area"));
    using CT = CaptureTool;
    for (auto toolType : { CT::TYPE_ACCEPT, CT::TYPE_SAVE, CT::TYPE_COPY }) {
        if (!m_tools.contains(toolType)) {
            continue;
        }
        auto* tool = m_tools[toolType];
        QString shortcut =
          ConfigHandler().shortcut(QVariant::fromValue(toolType).toString());
        shortcut.replace("Return", "Enter");
        if (!shortcut.isEmpty()) {
            keyMap << std::pair(shortcut, tool->description());
        }
    }
    keyMap << std::pair(tr("Mouse Wheel"), tr("Change tool size"));
    keyMap << std::pair(tr("Right Click"), tr("Show color picker"));
    keyMap << std::pair(ConfigHandler().shortcut("TYPE_TOGGLE_PANEL"),
                        tr("Open side panel"));
    keyMap << std::pair(tr("Esc"), tr("Exit"));

    m_helpMessage = OverlayMessage::compileFromKeyMap(keyMap);
}

QPixmap CaptureWidget::pixmap()
{
    return m_context.selectedScreenshotArea();
}

// Finish whatever the current tool is doing, if there is a current active
// tool.
bool CaptureWidget::commitCurrentTool()
{
    if (m_activeTool) {
        processPixmapWithTool(&m_context.screenshot, m_activeTool);
        if (m_activeTool->isValid() && !m_activeTool->editMode() &&
            m_toolWidget) {
            pushToolToStack();
        }
        if (m_toolWidget) {
            m_toolWidget->update();
        }
        releaseActiveTool();
        return true;
    }
    return false;
}

void CaptureWidget::initQuitPrompt()
{
    m_quitPrompt = new QMessageBox;
    makeChild(m_quitPrompt);

    QString baseSheet = "QDialog { background-color: %1; }"
                        "QLabel, QCheckBox { color: %2 }"
                        "QPushButton { background-color: %1; color: %2 }";
    QColor text = ColorUtils::colorIsDark(m_uiColor) ? Qt::white : Qt::black;
    QString styleSheet = baseSheet.arg(m_uiColor.name(), text.name());

    m_quitPrompt->setStyleSheet(styleSheet);
    m_quitPrompt->setWindowTitle(tr("Quit Capture"));
    m_quitPrompt->setText(tr("Are you sure you want to quit capture?"));
    m_quitPrompt->setIcon(QMessageBox::Icon::Question);
    m_quitPrompt->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    m_quitPrompt->setDefaultButton(QMessageBox::No);

    auto* check = new QCheckBox(tr("Do not show this again"));
    m_quitPrompt->setCheckBox(check);

    // Call show() first, otherwise the correct geometry cannot be fetched
    // for centering the window on the screen
    m_quitPrompt->show();
    QRect position = m_quitPrompt->frameGeometry();
    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
    position.moveCenter(currentScreen->availableGeometry().center());
    m_quitPrompt->move(position.topLeft());
    m_quitPrompt->hide();

    QObject::connect(check, &QCheckBox::clicked, [](bool checked) {
        ConfigHandler().setShowQuitPrompt(!checked);
    });
}

void CaptureWidget::resetSelectionToToolbar()
{
    // flameshot-ocr: 取消选区并回到预选区悬浮工具条状态（标注全部保留）
    cancel();
    m_context.selection = QRect();
    m_buttonHandler->hide();
    updateSelectionState();
    updateCursor();
    updatePreToolbar();
    // 整屏重绘：否则选区外的压暗层会以“与选区等大的亮斑”形式残留
    // （未失效的区域仍显示上一帧的压暗画面）
    update();
}

bool CaptureWidget::promptQuit()
{
    return m_quitPrompt->exec() == QMessageBox::Yes;
}

void CaptureWidget::deleteToolWidgetOrClose()
{
    if (m_activeButton != nullptr) {
        uncheckActiveTool();
    } else if (m_panel->activeLayerIndex() >= 0) {
        // remove active tool selection
        m_panel->setActiveLayer(-1);
    } else if (m_panel->isVisible()) {
        // hide panel if visible
        m_panel->hide();
    } else if (m_toolWidget) {
        // delete toolWidget if exists
        m_toolWidget->hide();
        delete m_toolWidget;
        m_toolWidget = nullptr;
    } else if (m_colorPicker && m_colorPicker->isVisible()) {
        m_colorPicker->hide();
    } else if (m_selection->isVisible()) {
        // flameshot-ocr: 有选区时先取消选区，回到预选区悬浮工具条页（保留标注）
        resetSelectionToToolbar();
    } else {
        // close CaptureWidget
        // flameshot-ocr: 已有手绘标注时强制确认，避免误触丢失
        const bool hasDrawings =
          !m_captureToolObjects.captureToolObjects().isEmpty();
        if (hasDrawings || m_config.showQuitPrompt()) {
            // need to show prompt
            if (m_quitPrompt->isHidden() && promptQuit()) {
                close();
            }
        } else {
            close();
        }
    }
}

void CaptureWidget::releaseActiveTool()
{
    if (m_activeTool) {
        if (m_activeTool->editMode()) {
            // Object shouldn't be deleted here because it is in the undo/redo
            // stack, just set current pointer to null
            m_activeTool->setEditMode(false);
            if (m_activeTool->isChanged()) {
                pushObjectsStateToUndoStack();
            }
        } else {
            delete m_activeTool;
        }
        m_activeTool = nullptr;
    }
    if (m_toolWidget) {
        m_toolWidget->hide();
        delete m_toolWidget;
        m_toolWidget = nullptr;
    }
}

void CaptureWidget::uncheckActiveTool()
{
    // uncheck active tool
    m_panel->setToolWidget(nullptr);
    m_activeButton->setColor(m_uiColor);
    updateTool(activeButtonTool());
    m_activeButton = nullptr;
    releaseActiveTool();
    updateSelectionState();
    updateCursor();
}

void CaptureWidget::closeEvent(QCloseEvent* event)
{
#if !(defined(Q_OS_MACOS) || defined(Q_OS_WIN))
    /* GNOME copy problem workaround, copy
       operation seems to work only when there
       is a visible window to retrieve the
       data from. On GNOME, the GUI should
       handle the copy operation, not the
       daemon.
    */
    const bool copyRequested =
      (m_context.request.tasks() & CaptureRequest::COPY);

    if (m_captureDone && copyRequested) {
        DesktopInfo desktopInfo;
        const bool needGnomeWorkaround =
          desktopInfo.waylandDetected() &&
          desktopInfo.windowManager() == DesktopInfo::GNOME;

        if (needGnomeWorkaround && !m_clipboardWorkaroundDone) {
            event->ignore();
            m_clipboardWorkaroundDone = true;
            m_context.request.removeTask(CaptureRequest::COPY);
            AbstractLogger::info()
              << "GNOME Wayland detected; keeping capture window alive until "
                 "clipboard data is fetched.";
            saveToClipboardGnomeWorkaround(pixmap(), this);
            return;
        }
    }
#endif

    QWidget::closeEvent(event);
}

void CaptureWidget::paintEvent(QPaintEvent* paintEvent)
{
    Q_UNUSED(paintEvent)
    QPainter painter(this);
    GeneralConf::xywh_position position =
      static_cast<GeneralConf::xywh_position>(m_config.showSelectionGeometry());
    /* QPainter::save and restore is somewhat costly so we try to guess
       if we need to do it here. What that means is that if you add
       anything to the paintEvent and want to save/restore you should
       add a test to the below if statement -- also if you change
       any of the conditions that current trigger it you'll need to change here,
       too
    */
    bool save = false;
    if (m_xywhDisplay ||                           // clause 1: xywh display
        m_displayGrid ||                           // clause 2: display grid
        (m_activeTool && m_mouseIsClicked) ||      // clause 3: tool/click
        (m_previewEnabled && activeButtonTool() && // clause 4: mouse preview
         m_activeButton->tool()->showMousePreview())) {
        painter.save();
        save = true;
    }
    painter.drawPixmap(0, 0, m_context.screenshot);
    if (m_selection && m_xywhDisplay) {
        const QRect& selection = m_selection->geometry().normalized();
        const qreal scale = m_context.screenshot.devicePixelRatio();
        QRect xybox;
        QFontMetrics fm = painter.fontMetrics();

        QString xy =
          QStringLiteral("%1 × %2 px")
            .arg(QString::number(static_cast<int>(selection.width() * scale)),
                 QString::number(static_cast<int>(selection.height() * scale)));

        xybox = fm.boundingRect(xy);
        // the small numbers here are just margins so the text doesn't
        // smack right up to the box; they aren't critical and the box
        // size itself is tied to the font metrics
        xybox.adjust(0, 0, 10, 12);
        int x0, y0;
        // Move these to header

        switch (position) {
            case GeneralConf::xywh_top_left:
                // 左上角：贴在选区外上方，空间不足时退到选区内侧
                x0 = selection.left();
                y0 = selection.top() - xybox.height() - 6;
                if (y0 < 0) {
                    y0 = selection.top() + 6;
                }
                break;
            case GeneralConf::xywh_bottom_left:
                x0 = selection.left();
                y0 = selection.bottom() - xybox.height();
                break;
            case GeneralConf::xywh_top_right:
                x0 = selection.right() - xybox.width();
                y0 = selection.top();
                break;
            case GeneralConf::xywh_bottom_right:
                x0 = selection.right() - xybox.width();
                y0 = selection.bottom() - xybox.height();
                break;
            case GeneralConf::xywh_center:
            default:
                x0 = selection.left() + (selection.width() - xybox.width()) / 2;
                y0 =
                  selection.top() + (selection.height() - xybox.height()) / 2;
        }

        // flameshot-ocr: 实心深色底 + UI 色描边 + 白色粗体，任何背景下清晰可读
        painter.fillRect(x0,
                         y0,
                         xybox.width(),
                         xybox.height(),
                         QColor(20, 20, 24, 245));
        painter.setPen(QPen(ConfigHandler().uiColor(), 1));
        painter.drawRect(x0, y0, xybox.width() - 1, xybox.height() - 1);
        QFont xyFont = painter.font();
        xyFont.setBold(true);
        painter.setFont(xyFont);
        painter.setPen(Qt::white);
        painter.drawText(x0,
                         y0,
                         xybox.width(),
                         xybox.height(),
                         Qt::AlignVCenter | Qt::AlignHCenter,
                         xy);
    }

    if (m_displayGrid) {
        QColor uicolor = ConfigHandler().uiColor();
        uicolor.setAlpha(100);
        painter.setPen(uicolor);
        painter.setBrush(QBrush(uicolor));

        const auto scale{ m_context.screenshot.devicePixelRatio() };
        auto topLeft = mapToGlobal(m_context.selection.topLeft() / scale);
        topLeft.rx() -= topLeft.x() % m_gridSize;
        topLeft.ry() -= topLeft.y() % m_gridSize;
        topLeft = mapFromGlobal(topLeft);

        const auto step{ m_gridSize / scale };
        const auto radius{ 1 * scale };

        for (int y = topLeft.y(); y < m_context.selection.bottom() / scale;
             y += step) {
            for (int x = topLeft.x(); x < m_context.selection.right() / scale;
                 x += step) {
                painter.drawEllipse(x, y, radius, radius);
            }
        }
    }

    if (m_activeTool && m_mouseIsClicked) {
        m_activeTool->process(painter, m_context.screenshot);
    } else if (m_previewEnabled && activeButtonTool() &&
               m_activeButton->tool()->showMousePreview()) {
        m_activeButton->tool()->paintMousePreview(painter, m_context);
    }
    if (save)
        painter.restore();
    // draw inactive region
    drawInactiveRegion(&painter);

    if (!isActiveWindow()) {
        drawErrorMessage(
          tr("Flameshot has lost focus. Keyboard shortcuts won't "
             "work until you click somewhere."),
          &painter);
    } else if (m_configError) {
        drawErrorMessage(ConfigHandler().errorMessage(), &painter);
    } else if (m_configErrorResolved) {
        drawErrorMessage(tr("Configuration error resolved. Launch `flameshot "
                            "gui` again to apply it."),
                         &painter);
    }
}

void CaptureWidget::showColorPicker(const QPoint& pos)
{
    // Try to select new object if current pos out of active object
    auto toolItem = activeToolObject();
    if (!toolItem || (toolItem && !toolItem->boundingRect().contains(pos))) {
        selectToolItemAtPos(pos);
    }

    // save current state for undo/redo stack
    if (m_panel->activeLayerIndex() >= 0) {
        m_captureToolObjectsBackup = m_captureToolObjects;
    }

    // Call color picker
    m_colorPicker->move(pos.x() - m_colorPicker->width() / 2,
                        pos.y() - m_colorPicker->height() / 2);
    m_colorPicker->raise();
    m_colorPicker->show();
}

bool CaptureWidget::startDrawObjectTool(const QPoint& pos)
{
    if (activeButtonToolType() != CaptureTool::NONE &&
        activeButtonToolType() != CaptureTool::TYPE_MOVESELECTION) {
        if (commitCurrentTool()) {
            return false;
        }
        m_activeTool = m_activeButton->tool()->copy(this);

        connect(this,
                &CaptureWidget::colorChanged,
                m_activeTool,
                &CaptureTool::onColorChanged);
        connect(this,
                &CaptureWidget::toolSizeChanged,
                m_activeTool,
                &CaptureTool::onSizeChanged);
        connect(m_activeTool,
                &CaptureTool::requestAction,
                this,
                &CaptureWidget::handleToolSignal);

        m_context.mousePos = m_displayGrid ? snapToGrid(pos) : pos;
        m_activeTool->drawStart(m_context);
        // TODO this is the wrong place to do this

        if (m_activeTool->type() == CaptureTool::TYPE_CIRCLECOUNT) {
            m_activeTool->setCount(m_context.circleCount++);
        }

        return true;
    }
    return false;
}

void CaptureWidget::pushObjectsStateToUndoStack()
{
    m_undoStack.push(new ModificationCommand(
      this, m_captureToolObjects, m_captureToolObjectsBackup));
    m_captureToolObjectsBackup.clear();
}

int CaptureWidget::selectToolItemAtPos(const QPoint& pos)
{
    // Try to select existing tool, "-1" - no active tool
    int activeLayerIndex = -1;
    auto selectionMouseSide = m_selection->getMouseSide(pos);
    if (m_activeButton.isNull() &&
        m_captureToolObjects.captureToolObjects().size() > 0 &&
        (selectionMouseSide == SelectionWidget::NO_SIDE ||
         selectionMouseSide == SelectionWidget::CENTER)) {
        auto toolItem = activeToolObject();
        if (!toolItem ||
            (toolItem && !toolItem->boundingRect().contains(pos))) {
            activeLayerIndex = m_captureToolObjects.find(pos, size());
            int oldToolSize = m_context.toolSize;
            m_panel->setActiveLayer(activeLayerIndex);
            drawObjectSelection();
            if (oldToolSize != m_context.toolSize) {
                emit toolSizeChanged(m_context.toolSize);
            }
        }
    }
    return activeLayerIndex;
}

void CaptureWidget::mousePressEvent(QMouseEvent* e)
{
    activateWindow();
    if (m_preToolbar && m_preToolbar->isVisible()) {
        m_preToolbar->hide();
    }
    // flameshot-ocr: 橡皮擦模式 —— 点击标注直接删除
    if (m_eraserActive && e->button() == Qt::LeftButton) {
        // flameshot-ocr: 包围盒优先命中（空心矩形/椭圆内部无绘制像素，
        // find() 点中间会落空），线状笔画退回像素级命中
        int index = objectIndexContainingPoint(e->pos());
        if (index < 0) {
            index = m_captureToolObjects.find(e->pos(), size());
        }
        if (index >= 0) {
            m_captureToolObjectsBackup = m_captureToolObjects;
            m_captureToolObjects.removeAt(index);
            pushObjectsStateToUndoStack();
            drawToolsData();
        }
        e->accept();
        return;
    }
    if (m_ocrPanel && m_ocrPanel->isVisible()) {
        if (m_ocrPanel->geometry().contains(e->pos())) {
            // 面板区域的事件应由面板处理，兜底防止隐藏面板/触发取色器
            return;
        }
        m_ocrPanel->hide();
    }
    m_startMove = false;
    m_startMovePos = QPoint();
    m_mousePressedPos = e->pos();
    m_activeToolOffsetToMouseOnStart = QPoint();
    if (m_colorPicker->isVisible()) {
        updateCursor();
        return;
    }

    // flameshot-ocr: 免选区连续绘制 —— 工具保持激活，点到已有形状则收起
    // 工具（下方缩放/选中流程随之接管：可拖动/缩放），点到空白继续画
    if (e->button() == Qt::LeftButton && m_activeButton &&
        !m_selection->isVisible() && !m_eraserActive) {
        int hitIdx = m_captureToolObjects.find(e->pos(), size());
        if (hitIdx < 0) {
            hitIdx = objectIndexContainingPoint(e->pos());
        }
        if (hitIdx >= 0) {
            uncheckActiveTool();
        }
    }

    // flameshot-ocr: 角点缩放优先拦截（早于“取消选中”逻辑，
    // 且抓取区向框外扩展——放大方向从手柄外沿起拖也能抓住）
    if (e->button() == Qt::LeftButton && m_activeButton.isNull() &&
        !m_eraserActive) {
        int candidateIndex = m_panel->activeLayerIndex();
        if (candidateIndex < 0) {
            candidateIndex = objectIndexWithGrabZone(e->pos());
        }
        if (candidateIndex >= 0) {
            auto candidate = m_captureToolObjects.at(candidateIndex);
            if (candidate) {
                const QRect candidateRect =
                  candidate->boundingRect().normalized();
                const int handle =
                  resizeGrabHandleAt(candidateRect, e->pos());
                if (handle > 0) {
                    if (m_panel->activeLayerIndex() != candidateIndex) {
                        m_panel->setActiveLayer(candidateIndex);
                    }
                    // flameshot-ocr: 预烘焙底图（原图 + 除本对象外的全部标注）。
                    // 拖动期间每帧只需「拷贝底图 + 渲染被拖对象」，
                    // 避免全量重绘导致跳帧、缩放跟手性差（“速度过快”观感）
                    m_resizeBase = m_context.origScreenshot;
                    for (const auto& toolItem :
                         m_captureToolObjects.captureToolObjects()) {
                        if (toolItem && toolItem != candidate) {
                            processPixmapWithTool(&m_resizeBase, toolItem);
                        }
                    }
                    m_captureToolObjectsBackup = m_captureToolObjects;
                    m_objectResizing = true;
                    m_objectResizeHandle = handle;
                    m_objectStartRect = candidateRect;
                    m_objectResizeStartPos = e->pos();
                    m_resizeVirtualDelta = QPointF(0, 0);
                    m_resizeLastPos = e->pos();
                    m_resizeLastRect = candidateRect;
                    e->accept();
                    return;
                }
            }
        }
    }

    // reset object selection if capture area selection is active
    if (m_selection->getMouseSide(e->pos()) != SelectionWidget::CENTER) {
        m_panel->setActiveLayer(-1);
    }
    if (e->button() == Qt::RightButton) {
        if (m_activeTool && m_activeTool->editMode()) {
            return;
        }
        showColorPicker(m_mousePressedPos);
        return;
    } else if (e->button() == Qt::LeftButton) {
        m_mouseIsClicked = true;

        // Click using a tool excluding tool MOVE
        if (startDrawObjectTool(m_mousePressedPos)) {
            // return if success
            return;
        }
    }

    // Commit current tool if it has edit widget and mouse click is outside
    // of it
    if (m_toolWidget && !m_toolWidget->geometry().contains(e->pos())) {
        commitCurrentTool();
        m_panel->setToolWidget(nullptr);
        drawToolsData();
        updateLayersPanel();
    }

    selectToolItemAtPos(m_mousePressedPos);

    // flameshot-ocr: 空心形状内部点击也能选中（find() 基于已绘制像素，
    // 轮廓内部会落空），选中后即可拖动移动
    if (m_panel->activeLayerIndex() < 0 && e->button() == Qt::LeftButton &&
        !m_eraserActive && m_activeButton.isNull()) {
        const int interiorIndex = objectIndexContainingPoint(m_mousePressedPos);
        if (interiorIndex >= 0) {
            m_panel->setActiveLayer(interiorIndex);
            drawObjectSelection();
        }
    }

    updateSelectionState();
    updateCursor();
}

void CaptureWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    int activeLayerIndex = m_panel->activeLayerIndex();
    if (activeLayerIndex != -1) {
        // Start object editing
        auto activeTool = m_captureToolObjects.at(activeLayerIndex);
        if (activeTool && activeTool->type() == CaptureTool::TYPE_TEXT) {
            m_activeTool = activeTool;
            m_mouseIsClicked = false;
            m_context.mousePos = *m_activeTool->pos();
            m_captureToolObjectsBackup = m_captureToolObjects;
            m_activeTool->setEditMode(true);
            drawToolsData();
            updateLayersPanel();
            handleToolSignal(CaptureTool::REQ_ADD_CHILD_WIDGET);
            if (!m_activeTool.isNull()) {
                m_panel->setToolWidget(m_activeTool->configurationWidget());
            }
        }
    } else if (m_selection->geometry().contains(event->pos())) {
        if ((event->button() == Qt::LeftButton) &&
            (m_config.copyOnDoubleClick())) {
            CopyTool copyTool;
            connect(&copyTool,
                    &CopyTool::requestAction,
                    this,
                    &CaptureWidget::handleToolSignal);
            copyTool.pressed(m_context);
            qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
}

void CaptureWidget::mouseMoveEvent(QMouseEvent* e)
{
    // flameshot-ocr: 选中对象的角点拖拽缩放
    if (m_objectResizing) {
        CaptureTool* object = activeToolObject().data();
        if (object) {
            // 与上游移动对象相同的重绘模式：失效旧区域 → 改对象 →
            // drawToolsData 从原图重画（清残影）→ 再画选择框 → 失效新区域
            update(paddedUpdateRect(object->boundingRect()));
            // flameshot-ocr: 自适应增益缩放 ——
            // 慢速微拖：按 resizeSensitivity 精调（默认 20% = 比鼠标慢 5 倍）；
            // 快速拖动：增益升到 1:1，手柄紧跟鼠标（连续拖动、不脱手）。
            // 增量累积（含小数），慢速微动不会因取整丢失。
            const QPoint frameDelta = e->pos() - m_resizeLastPos;
            m_resizeLastPos = e->pos();
            const qreal slowGain =
              qBound(0.05, ConfigHandler().resizeSensitivity() / 100.0, 1.0);
            constexpr qreal SLOW_SPEED = 4.0;   // px/事件
            constexpr qreal FAST_SPEED = 22.0;  // px/事件
            const qreal speed = frameDelta.manhattanLength();
            qreal gain;
            if (speed >= FAST_SPEED) {
                gain = 1.0;
            } else if (speed <= SLOW_SPEED) {
                gain = slowGain;
            } else {
                gain = slowGain + (speed - SLOW_SPEED) /
                                    (FAST_SPEED - SLOW_SPEED) *
                                    (1.0 - slowGain);
            }
            m_resizeVirtualDelta +=
              QPointF(frameDelta.x() * gain, frameDelta.y() * gain);
            if (qEnvironmentVariableIsSet("FLAMESHOT_OCR_SELFTEST")) {
                qWarning() << "RESIZE-DBG frame:" << frameDelta
                           << "speed:" << speed << "gain:" << gain
                           << "virtual:" << m_resizeVirtualDelta;
            }
            const QPoint delta(qRound(m_resizeVirtualDelta.x()),
                               qRound(m_resizeVirtualDelta.y()));
            QRect newRect = m_objectStartRect;
            if (m_objectResizeHandle & 1) {
                newRect.setLeft(newRect.left() + delta.x());
            }
            if (m_objectResizeHandle & 2) {
                newRect.setRight(newRect.right() + delta.x());
            }
            if (m_objectResizeHandle & 4) {
                newRect.setTop(newRect.top() + delta.y());
            }
            if (m_objectResizeHandle & 8) {
                newRect.setBottom(newRect.bottom() + delta.y());
            }
            newRect = newRect.normalized();
            if (newRect.width() >= 8 && newRect.height() >= 8) {
                // 关键：从「上一帧矩形」映射到「本帧矩形」（增量映射）。
                // 若始终从 m_objectStartRect 映射，已放大过的点位会被
                // 反复按比例放大（复利效应），导致缩放指数式爆炸、完全失控
                scaleToolToRect(object, m_resizeLastRect, newRect);
                m_resizeLastRect = newRect;
                // 轻量重绘：拷贝预烘焙底图 + 只渲染被拖动对象，
                // 帧间开销恒定（不随标注数量增长），缩放跟手更平滑
                if (!m_resizeBase.isNull()) {
                    m_context.screenshot = m_resizeBase;
                    processPixmapWithTool(&m_context.screenshot, object);
                } else {
                    drawToolsData(false);
                }
                drawObjectSelection();
                update(paddedUpdateRect(object->boundingRect()));
            }
        }
        e->accept();
        return;
    }

    if (m_magnifier) {
        if (!m_activeButton) {
            m_magnifier->show();
            m_magnifier->update();
        } else {
            m_magnifier->hide();
        }
    }

    m_context.mousePos = e->pos();
    if (e->buttons() != Qt::LeftButton) {
        updateTool(activeButtonTool());
        updateCursor();
        return;
    }

    // The rest assumes that left mouse button is clicked
    if (!m_activeButton && m_panel->activeLayerIndex() >= 0) {
        // Move existing object
        if (!m_startMove) {
            // Check for the minimal offset to start moving an object
            if (m_startMovePos.isNull()) {
                m_startMovePos = e->pos();
            }
            if ((e->pos() - m_startMovePos).manhattanLength() >
                MOUSE_DISTANCE_TO_START_MOVING) {
                m_startMove = true;
            }
        }
        if (m_startMove) {
            QPointer<CaptureTool> activeTool =
              m_captureToolObjects.at(m_panel->activeLayerIndex());
            if (m_activeToolOffsetToMouseOnStart.isNull()) {
                setCursor(Qt::ClosedHandCursor);
                m_activeToolOffsetToMouseOnStart =
                  e->pos() - *activeTool->pos();
            }
            if (!m_activeToolIsMoved) {
                // save state before movement for undo stack
                m_captureToolObjectsBackup = m_captureToolObjects;
            }
            m_activeToolIsMoved = true;
            // update the old region of the selection, margins are added to
            // ensure selection outline is updated too
            update(paddedUpdateRect(activeTool->boundingRect()));
            activeTool->move(e->pos() - m_activeToolOffsetToMouseOnStart);
            drawToolsData();
        }
    } else if (m_activeTool) {
        // drawing with a tool
        if (m_adjustmentButtonPressed) {
            m_activeTool->drawMoveWithAdjustment(e->pos());
        } else {
            m_activeTool->drawMove(m_displayGrid ? snapToGrid(e->pos())
                                                 : e->pos());
        }
        // update drawing object
        updateTool(m_activeTool);
        // Hides the buttons under the mouse. If the mouse leaves, it shows
        // them.
        if (m_buttonHandler->buttonsAreInside()) {
            const bool containsMouse =
              m_buttonHandler->contains(m_context.mousePos);
            if (containsMouse) {
                m_buttonHandler->hide();
            } else if (m_selection->isVisible()) {
                m_buttonHandler->show();
            }
        }
    }
    updateCursor();
}

void CaptureWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_colorPicker->isVisible()) {
        // Color picker
        if (m_colorPicker->isVisible() && m_panel->activeLayerIndex() >= 0 &&
            m_context.color.isValid()) {
            pushObjectsStateToUndoStack();
        }
        m_colorPicker->setNewColor();
        m_colorPicker->hide();
        if (!m_context.color.isValid()) {
            m_context.color = ConfigHandler().drawColor();
            m_panel->show();
        }
    } else if (m_mouseIsClicked) {
        if (m_activeTool) {
            // end draw/edit
            m_activeTool->drawEnd(m_context.mousePos);
            if (m_activeTool->isValid()) {
                pushToolToStack();
            } else if (!m_toolWidget) {
                releaseActiveTool();
            }
        } else {
            if (m_activeToolIsMoved) {
                m_activeToolIsMoved = false;
                pushObjectsStateToUndoStack();
            }
        }
    }
    m_mouseIsClicked = false;
    m_activeToolIsMoved = false;
    const bool wasResizing = m_objectResizing;
    m_objectResizing = false;
    if (wasResizing) {
        // 缩放结束：写入撤销栈、清空底图、做一次全量重绘恢复正常渲染，
        // 并重画选中框/手柄（drawToolsData 会擦掉手柄，重画后
        // 手柄保持在原位、可立即再次拖动——支持连续缩放）
        pushObjectsStateToUndoStack();
        m_resizeBase = QPixmap();
        drawToolsData();
        if (m_panel->activeLayerIndex() >= 0) {
            drawObjectSelection();
        }
        update();
    }

    updateSelectionState();
    updateCursor();
    updatePreToolbar();
}

/**
 * Was updateThickness.
 * - Update tool mouse preview
 * - Show notifier box displaying the new thickness
 * - Update selected object thickness
 */
void CaptureWidget::setToolSize(int size)
{
    int oldSize = m_context.toolSize;
    m_context.toolSize = qBound(1, size, maxToolSize);
    updateTool(activeButtonTool());

    // flameshot-ocr: 提示圈跟随当前光标（上游固定放在屏幕左上角）
    int offset = m_notifierBox->width() / 4;
    m_notifierBox->move(mapFromGlobal(QCursor::pos()) +
                        QPoint(offset, offset));
    m_notifierBox->showMessage(QString::number(m_context.toolSize));

    if (m_context.toolSize != oldSize) {
        emit toolSizeChanged(m_context.toolSize);
    }
}

void CaptureWidget::keyPressEvent(QKeyEvent* e)
{
    // If the key is a digit, change the tool size
    bool ok;
    int digit = e->text().toInt(&ok);
    if (ok && ((e->modifiers() == Qt::NoModifier) ||
               e->modifiers() == Qt::KeypadModifier)) { // digit received
        m_toolSizeByKeyboard = 10 * m_toolSizeByKeyboard + digit;
        setToolSize(m_toolSizeByKeyboard);
        if (m_context.toolSize != m_toolSizeByKeyboard) {
            // The tool size was out of range and was clipped by setToolSize
            m_toolSizeByKeyboard = 0;
        }
    } else {
        m_toolSizeByKeyboard = 0;
    }

    if (!m_selection->isVisible()) {
        return;
    } else if (e->key() == Qt::Key_Control) {
        m_adjustmentButtonPressed = true;
        updateCursor();
    } else if (e->key() == Qt::Key_Enter) {
        // Make no difference for Return and Enter keys
        QCoreApplication::postEvent(
          this,
          new QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier));
    }
}

// flameshot-ocr: 对象缩放辅助 —— 命中测试与点位等比缩放
int CaptureWidget::objectResizeHandleAt(const QPoint& pos)
{
    if (m_eraserActive || m_panel->activeLayerIndex() < 0) {
        return -1;
    }
    auto object = activeToolObject();
    if (!object) {
        return -1;
    }
    return resizeHandleForRect(object->boundingRect().normalized(), pos);
}

// 手柄判定：距离任一边 <= 12px 即命中该边（左右/上下同时命中视为无效）；
// 该判定同时用于“框内贴边”和“框外贴边”，保证放大与缩小两个方向都能抓住
int CaptureWidget::resizeHandleForRect(const QRect& rect, const QPoint& pos)
{
    if (rect.isNull()) {
        return -1;
    }
    const int margin = 12;
    int handle = 0;
    if (qAbs(pos.x() - rect.left()) <= margin) {
        handle |= 1;
    }
    if (qAbs(pos.x() - rect.right()) <= margin) {
        handle |= 2;
    }
    if (qAbs(pos.y() - rect.top()) <= margin) {
        handle |= 4;
    }
    if (qAbs(pos.y() - rect.bottom()) <= margin) {
        handle |= 8;
    }
    if (handle == 0 || handle == 3 || handle == 12) {
        return -1;
    }
    return handle;
}

// 找到抓取区（对象包围盒外扩 12px）内最上层的对象
int CaptureWidget::objectIndexWithGrabZone(const QPoint& pos)
{
    const int zone = 12;
    const auto objects = m_captureToolObjects.captureToolObjects();
    for (int i = objects.size() - 1; i >= 0; --i) {
        auto object = objects.at(i);
        if (!object) {
            continue;
        }
        if (object->boundingRect()
              .normalized()
              .adjusted(-zone, -zone, zone, zone)
              .contains(pos)) {
            return i;
        }
    }
    return -1;
}

// 实际可触发的缩放手柄：框外任意边/角均可抓取；
// 框内仅角点（同时贴近两条边）触发缩放，边中部让位给“拖动移动”，
// 否则小方框的整个区域都是缩放热区、无法拖动
int CaptureWidget::resizeGrabHandleAt(const QRect& rect, const QPoint& pos)
{
    const int handle = resizeHandleForRect(rect, pos);
    if (handle <= 0) {
        return -1;
    }
    if (!rect.contains(pos)) {
        return handle;
    }
    const bool corner =
      (handle == 5 || handle == 6 || handle == 9 || handle == 10);
    return corner ? handle : -1;
}

// 包围盒内部命中的最上层对象（空心形状内部无绘制像素，find() 会落空，
// 用包围盒补充判定，使空心方框内部也能点选/拖动）
int CaptureWidget::objectIndexContainingPoint(const QPoint& pos)
{
    const auto objects = m_captureToolObjects.captureToolObjects();
    for (int i = objects.size() - 1; i >= 0; --i) {
        auto object = objects.at(i);
        if (object && object->boundingRect().normalized().contains(pos)) {
            return i;
        }
    }
    return -1;
}

void CaptureWidget::scaleToolToRect(CaptureTool* tool,
                                    const QRect& from,
                                    const QRect& to)
{
    if (!tool || from.width() <= 0 || from.height() <= 0) {
        return;
    }
    const qreal scaleX = qreal(to.width()) / from.width();
    const qreal scaleY = qreal(to.height()) / from.height();
    // 注意：用 qRound 而非 int 截断——增量缩放时每帧的增长往往不足
    // 1 像素，截断会把慢速微调的全部增量逐帧吃掉
    auto mapPoint = [&](const QPoint& p) {
        return QPoint(qRound(to.left() + (p.x() - from.left()) * scaleX),
                      qRound(to.top() + (p.y() - from.top()) * scaleY));
    };
    if (auto* twoPointTool = dynamic_cast<AbstractTwoPointTool*>(tool)) {
        const auto points = twoPointTool->points();
        twoPointTool->setPoints({ mapPoint(points.first),
                                  mapPoint(points.second) });
    } else if (auto* pathTool = dynamic_cast<AbstractPathTool*>(tool)) {
        QVector<QPoint> mapped;
        mapped.reserve(pathTool->points().size());
        for (const QPoint& p : pathTool->points()) {
            mapped.append(mapPoint(p));
        }
        pathTool->setPoints(mapped);
    }
}

void CaptureWidget::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Control) {
        m_adjustmentButtonPressed = false;
        updateCursor();
    }
}

void CaptureWidget::wheelEvent(QWheelEvent* e)
{
    // flameshot-ocr: 滚轮时以当前光标位置显示粗细提示圈
    // （上游固定放在屏幕左上角，未移动过鼠标时会出现在 (0,0)）
    m_context.mousePos = mapFromGlobal(QCursor::pos());
    /* Mouse scroll usually gives value 120, not more or less, just how many
     * times.
     * Touchpad gives the value 2 or more (usually 2-8), it doesn't give
     * too big values like mouse wheel on normal scrolling, so it is almost
     * impossible to scroll. It's easier to calculate number of requests and do
     * not accept events faster that one in 200ms.
     * */
    int toolSizeOffset = 0;
    if (e->angleDelta().y() >= 60) {
        // mouse scroll (wheel) increment
        toolSizeOffset = 1;
    } else if (e->angleDelta().y() <= -60) {
        // mouse scroll (wheel) decrement
        toolSizeOffset = -1;
    } else {
        // touchpad scroll
        qint64 current = QDateTime::currentMSecsSinceEpoch();
        if ((current - m_lastMouseWheel) > 200) {
            if (e->angleDelta().y() > 0) {
                toolSizeOffset = 1;
            } else if (e->angleDelta().y() < 0) {
                toolSizeOffset = -1;
            }
            m_lastMouseWheel = current;
        } else {
            return;
        }
    }

    setToolSize(m_context.toolSize + toolSizeOffset);
}

void CaptureWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    m_context.widgetOffset = mapToGlobal(QPoint(0, 0));
    if (!m_context.fullscreen) {
        m_panel->setFixedHeight(height());
        m_buttonHandler->updateScreenRegions(rect());
    }
    positionPreToolbar();
}

void CaptureWidget::moveEvent(QMoveEvent* e)
{
    QWidget::moveEvent(e);
    m_context.widgetOffset = mapToGlobal(QPoint(0, 0));
}

void CaptureWidget::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::ActivationChange) {
        QPoint bottomRight = rect().bottomRight();
        // Update the message in the bottom right corner. A rough estimate is
        // used for the update rect
        update(QRect(bottomRight - QPoint(1000, 200), bottomRight));
    }
}

void CaptureWidget::initContext(bool fullscreen, const CaptureRequest& req)
{
    m_context.color = m_config.drawColor();
    m_context.widgetOffset = mapToGlobal(QPoint(0, 0));
    m_context.mousePos = mapFromGlobal(QCursor::pos());
    m_context.toolSize = m_config.drawThickness();
    m_context.fullscreen = fullscreen;

    // initialize m_context.request
    m_context.request = req;
}

void CaptureWidget::initPanel()
{
    QRect panelRect = rect();
    if (m_context.fullscreen) {
#if (defined(Q_OS_MACOS) || defined(Q_OS_LINUX))
        QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
        panelRect = currentScreen->geometry();
        auto devicePixelRatio = currentScreen->devicePixelRatio();
        panelRect.moveTo(static_cast<int>(panelRect.x() / devicePixelRatio),
                         static_cast<int>(panelRect.y() / devicePixelRatio));
#else
        panelRect = QGuiApplication::primaryScreen()->geometry();
        auto devicePixelRatio =
          QGuiApplication::primaryScreen()->devicePixelRatio();
        panelRect.moveTo(panelRect.x() / devicePixelRatio,
                         panelRect.y() / devicePixelRatio);
#endif
    }

    if (ConfigHandler().showSidePanelButton()) {
        auto* panelToggleButton =
          new OrientablePushButton(tr("Tool Settings"), this);
        makeChild(panelToggleButton);
        panelToggleButton->setColor(m_uiColor);
        panelToggleButton->setOrientation(
          OrientablePushButton::VerticalBottomToTop);
#if defined(Q_OS_MACOS)
        panelToggleButton->move(
          0,
          static_cast<int>(panelRect.height() / 2) -
            static_cast<int>(panelToggleButton->width() / 2));
#else
        panelToggleButton->move(panelRect.x(),
                                panelRect.y() + panelRect.height() / 2 -
                                  panelToggleButton->width() / 2);
#endif
        panelToggleButton->setCursor(Qt::ArrowCursor);
        (new DraggableWidgetMaker(this))->makeDraggable(panelToggleButton);
        connect(panelToggleButton,
                &QPushButton::clicked,
                this,
                &CaptureWidget::togglePanel);
    }

    m_panel = new UtilityPanel(this);
    m_panel->hide();
    makeChild(m_panel);
#if defined(Q_OS_MACOS)
    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
    panelRect.moveTo(mapFromGlobal(panelRect.topLeft()));
    m_panel->setFixedWidth(static_cast<int>(m_colorPicker->width() * 1.5));
    m_panel->setFixedHeight(currentScreen->geometry().height());
#else
    panelRect.moveTo(mapFromGlobal(panelRect.topLeft()));
    panelRect.setWidth(m_colorPicker->width() * 1.5);
    m_panel->setGeometry(panelRect);
#endif
    connect(m_panel,
            &UtilityPanel::layerChanged,
            this,
            &CaptureWidget::updateActiveLayer);
    connect(m_panel,
            &UtilityPanel::moveUpClicked,
            this,
            &CaptureWidget::onMoveCaptureToolUp);
    connect(m_panel,
            &UtilityPanel::moveDownClicked,
            this,
            &CaptureWidget::onMoveCaptureToolDown);

    m_sidePanel = new SidePanelWidget(&m_context.screenshot, this);
    connect(m_sidePanel,
            &SidePanelWidget::colorChanged,
            this,
            &CaptureWidget::setDrawColor);
    connect(m_sidePanel,
            &SidePanelWidget::toolSizeChanged,
            this,
            &CaptureWidget::onToolSizeChanged);
    connect(this,
            &CaptureWidget::colorChanged,
            m_sidePanel,
            &SidePanelWidget::onColorChanged);
    connect(this,
            &CaptureWidget::toolSizeChanged,
            m_sidePanel,
            &SidePanelWidget::onToolSizeChanged);
    connect(m_sidePanel,
            &SidePanelWidget::togglePanel,
            m_panel,
            &UtilityPanel::toggle);
    connect(
      m_sidePanel, &SidePanelWidget::showPanel, m_panel, &UtilityPanel::show);
    connect(
      m_sidePanel, &SidePanelWidget::hidePanel, m_panel, &UtilityPanel::hide);
    connect(m_sidePanel,
            &SidePanelWidget::displayGridChanged,
            this,
            &CaptureWidget::onDisplayGridChanged);
    connect(m_sidePanel,
            &SidePanelWidget::gridSizeChanged,
            this,
            &CaptureWidget::onGridSizeChanged);
    // TODO replace with a CaptureWidget signal
    emit m_sidePanel->colorChanged(m_context.color);
    emit toolSizeChanged(m_context.toolSize);
    m_panel->pushWidget(m_sidePanel);

    // Fill undo/redo/history list widget
    m_panel->fillCaptureTools(m_captureToolObjects.captureToolObjects());
}

#if !defined(DISABLE_UPDATE_CHECKER)
void CaptureWidget::showAppUpdateNotification(const QString& appLatestVersion,
                                              const QString& appLatestUrl)
{
    if (!ConfigHandler().checkForUpdates()) {
        // option check for updates disabled
        return;
    }
    if (nullptr == m_updateNotificationWidget) {
        m_updateNotificationWidget =
          new UpdateNotificationWidget(this, appLatestVersion, appLatestUrl);
    }
#if defined(Q_OS_MACOS)
    int ax = (width() - m_updateNotificationWidget->width()) / 2;
#else
    QRect helpRect;
    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
    if (currentScreen) {
        helpRect = currentScreen->geometry();
    } else {
        helpRect = QGuiApplication::primaryScreen()->geometry();
    }
    int ax = helpRect.left() +
             ((helpRect.width() - m_updateNotificationWidget->width()) / 2);
#endif
    m_updateNotificationWidget->move(ax, 0);
    makeChild(m_updateNotificationWidget);
    m_updateNotificationWidget->show();
}
#endif

void CaptureWidget::initSelection()
{
    // Be mindful of the order of statements, so that slots are called properly
    m_selection = new SelectionWidget(m_uiColor, this);
    QRect initialSelection = m_context.request.initialSelection();
    connect(m_selection, &SelectionWidget::geometryChanged, this, [this]() {
        QRect constrainedToCaptureArea =
          m_selection->geometry().intersected(rect());
        m_context.selection = extendedRect(constrainedToCaptureArea);

        m_buttonHandler->hide();
        updateCursor();
        updateSizeIndicator();
        OverlayMessage::pop();
    });
    connect(m_selection, &SelectionWidget::geometrySettled, this, [this]() {
        if (m_selection->isVisibleTo(this)) {
            auto& req = m_context.request;
            if (req.tasks() & CaptureRequest::ACCEPT_ON_SELECT) {
                req.removeTask(CaptureRequest::ACCEPT_ON_SELECT);
                m_captureDone = true;
                close();
            }
            m_buttonHandler->updatePosition(m_selection->geometry());
            m_buttonHandler->show();
        } else {
            m_buttonHandler->hide();
        }
    });
    connect(m_selection, &SelectionWidget::visibilityChanged, this, [this]() {
        if (!m_selection->isVisible() && !m_helpMessage.isEmpty()) {
            OverlayMessage::push(m_helpMessage);
        }
    });
    if (!initialSelection.isNull()) {
        const qreal scale = m_context.screenshot.devicePixelRatio();
        initialSelection.moveTopLeft(initialSelection.topLeft() -
                                     mapToGlobal(QPoint(0, 0)));
        initialSelection.setTop(initialSelection.top() / scale);
        initialSelection.setBottom(initialSelection.bottom() / scale);
        initialSelection.setLeft(initialSelection.left() / scale);
        initialSelection.setRight(initialSelection.right() / scale);
    }
    m_selection->setGeometry(initialSelection);
    m_selection->setVisible(!initialSelection.isNull());
    if (!initialSelection.isNull()) {
        m_context.selection = extendedRect(m_selection->geometry());
        emit m_selection->geometrySettled();
    }
}

void CaptureWidget::setState(CaptureToolButton* b)
{
    if (!b) {
        return;
    }

    commitCurrentTool();
    if (m_toolWidget && m_activeTool) {
        if (m_activeTool->isValid()) {
            pushToolToStack();
        } else {
            releaseActiveTool();
        }
    }
    if (m_activeButton != b) {
        auto backup = m_activeTool;
        // The tool is active during the pressed().
        // This must be done in order to handle tool requests correctly.
        m_activeTool = b->tool();
        m_activeTool->pressed(m_context);
        m_activeTool = backup;
    }

    if (b->tool()->isSelectable()) {
        if (m_activeButton != b) {
            if (m_activeButton) {
                m_activeButton->setColor(m_uiColor);
            }
            m_activeButton = b;
            m_activeButton->setColor(m_contrastUiColor);
            m_panel->setActiveLayer(-1);
            m_panel->setToolWidget(b->tool()->configurationWidget());
        } else if (m_activeButton) {
            m_panel->clearToolWidget();
            m_activeButton->setColor(m_uiColor);
            m_activeButton = nullptr;
        }
        m_context.toolSize = ConfigHandler().toolSize(activeButtonToolType());
        emit toolSizeChanged(m_context.toolSize);
        updateCursor();
        updateSelectionState();
        updateTool(b->tool());
    }
}

void CaptureWidget::handleToolSignal(CaptureTool::Request r)
{
    switch (r) {
        case CaptureTool::REQ_CLOSE_GUI:
            // flameshot-ocr: 仅当“纯关闭”意图（无导出任务）时才做保护；
            // 钉图/复制/保存等带任务的关闭属于正常导出，不应打断
            if (m_context.request.tasks() == CaptureRequest::NO_TASK) {
                if (m_selection->isVisible()) {
                    // 有选区：取消选区、回到预选区悬浮工具条页（保留标注）
                    resetSelectionToToolbar();
                    break;
                }
                if (!m_captureToolObjects.captureToolObjects().isEmpty()) {
                    if (m_quitPrompt->isHidden() && !promptQuit()) {
                        break; // 用户取消，保持截图界面
                    }
                }
            }
            close();
            break;
        case CaptureTool::REQ_HIDE_GUI:
            hide();
            break;
        case CaptureTool::REQ_UNDO_MODIFICATION:
            undo();
            break;
        case CaptureTool::REQ_REDO_MODIFICATION:
            redo();
            break;
        case CaptureTool::REQ_SHOW_COLOR_PICKER:
            // TODO
            break;
        case CaptureTool::REQ_CAPTURE_DONE_OK:
            m_captureDone = true;
            break;
        case CaptureTool::REQ_CLEAR_SELECTION:
            if (m_panel->activeLayerIndex() >= 0) {
                m_panel->setActiveLayer(-1);
                drawToolsData(false);
            }
            break;
        case CaptureTool::REQ_ADD_CHILD_WIDGET:
            if (!m_activeTool) {
                break;
            }
            if (m_toolWidget) {
                m_toolWidget->hide();
                delete m_toolWidget;
                m_toolWidget = nullptr;
            }
            m_toolWidget = m_activeTool->widget();
            if (m_toolWidget) {
                makeChild(m_toolWidget);
                m_toolWidget->move(m_context.mousePos);
                m_toolWidget->show();
                m_toolWidget->setFocus();
            }
            break;
        case CaptureTool::REQ_ADD_EXTERNAL_WIDGETS:
            if (!m_activeTool) {
                break;
            } else {
                QWidget* w = m_activeTool->widget();
                w->setAttribute(Qt::WA_DeleteOnClose);
                w->activateWindow();
                w->show();
                Flameshot::instance()->setExternalWidget(true);
            }
            break;
        case CaptureTool::REQ_OCR:
            runOcr();
            break;
        case CaptureTool::REQ_INCREASE_TOOL_SIZE:
            setToolSize(m_context.toolSize + 1);
            break;
        case CaptureTool::REQ_DECREASE_TOOL_SIZE:
            setToolSize(m_context.toolSize - 1);
            break;
        default:
            break;
    }
}

// flameshot-ocr: run OCR on the current selection (or the whole capture when
// nothing is selected) and display the result in a panel beside the selection.
void CaptureWidget::runOcr()
{
    QRect sel = m_context.selection.isNull() ? rect() : m_context.selection;
    sel = sel.normalized().intersected(rect());
    if (sel.isEmpty()) {
        return;
    }

    if (!m_ocrPanel) {
        m_ocrPanel = new OcrPanel(this);
    }
    m_ocrPanel->showLoading(sel);

    const qreal dpr = m_context.origScreenshot.devicePixelRatio();
    QRect deviceRect(sel.topLeft() * dpr, sel.bottomRight() * dpr);
    deviceRect = deviceRect.intersected(m_context.origScreenshot.rect());
    if (deviceRect.isEmpty()) {
        m_ocrPanel->showFailure();
        return;
    }
    const QImage crop = m_context.origScreenshot.toImage().copy(deviceRect);

    if (!m_ocrProcess) {
        m_ocrProcess = new QProcess(this);
        connect(m_ocrProcess,
                &QProcess::finished,
                this,
                &CaptureWidget::onOcrFinished);
        connect(m_ocrProcess,
                &QProcess::errorOccurred,
                this,
                [this](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart && m_ocrPanel) {
                        m_ocrPanel->showFailure(
                          QStringLiteral("tesseract: FailedToStart"));
                    }
                });
    }
    if (m_ocrProcess->state() != QProcess::NotRunning) {
        m_ocrProcess->kill();
        m_ocrProcess->waitForFinished(3000);
    }

    delete m_ocrTempFile;
    m_ocrTempFile = new QTemporaryFile(this);
    m_ocrTempFile->setFileTemplate(
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QStringLiteral("/flameshot-ocr-XXXXXX.png"));
    if (!m_ocrTempFile->open() || !crop.save(m_ocrTempFile, "PNG")) {
        m_ocrPanel->showFailure(QStringLiteral("Cannot write temporary image"));
        return;
    }

    QString command = ConfigHandler().ocrCommand();
    command.replace(QStringLiteral("%i"), m_ocrTempFile->fileName());
    QStringList args = QProcess::splitCommand(command);
    if (args.isEmpty()) {
        m_ocrPanel->showFailure(QStringLiteral("Empty ocrCommand"));
        return;
    }
    const QString program = args.takeFirst();
    m_ocrProcess->start(program, args);
}

void CaptureWidget::onOcrFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_ocrPanel) {
        return;
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        const QString err =
          QString::fromLocal8Bit(m_ocrProcess->readAllStandardError())
            .simplified();
        m_ocrPanel->showFailure(err.left(300));
        return;
    }
    QString text = QString::fromUtf8(m_ocrProcess->readAllStandardOutput());
    text.remove(QChar('\f'));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")),
                 QStringLiteral("\n\n"));
    text = text.trimmed();
    if (text.isEmpty()) {
        m_ocrPanel->showFailure();
        return;
    }
    m_ocrPanel->showText(text);
}

/**
 * Was setDrawThickness
 * - Update config options
 * - Update tool object thickness
 */
void CaptureWidget::onToolSizeChanged(int t)
{

    m_context.toolSize = t;
    CaptureTool* tool = activeButtonTool();
    if (tool && tool->showMousePreview()) {
        setCursor(Qt::BlankCursor);
        tool->onSizeChanged(t);
    }

    // update tool size of object being drawn
    if (m_activeTool != nullptr) {
        updateTool(m_activeTool);
    }

    // update tool size of selected object
    auto toolItem = activeToolObject();
    if (toolItem) {
        // Change thickness
        toolItem->onSizeChanged(t);
        if (!m_existingObjectIsChanged) {
            m_captureToolObjectsBackup = m_captureToolObjects;
            m_existingObjectIsChanged = true;
        }
        drawToolsData();
        updateTool(toolItem);
    }

    // Force a repaint to prevent artifacting
    this->repaint();
}

void CaptureWidget::onToolSizeSettled(int size)
{
    m_config.setToolSize(activeButtonToolType(), size);
}

void CaptureWidget::setDrawColor(const QColor& c)
{
    m_context.color = c;
    if (m_context.color.isValid()) {
        ConfigHandler().setDrawColor(m_context.color);
        emit colorChanged(c);
        // Update mouse preview
        updateTool(activeButtonTool());

        // change color for the active tool
        auto toolItem = activeToolObject();
        if (toolItem) {
            // Change color
            toolItem->onColorChanged(c);
            drawToolsData();
        }
    }
}

void CaptureWidget::updateActiveLayer(int layer)
{
    // TODO - refactor this part, make all objects to work with
    // m_activeTool->isChanged() and remove m_existingObjectIsChanged
    if (m_activeTool && m_activeTool->type() == CaptureTool::TYPE_TEXT &&
        m_activeTool->isChanged()) {
        commitCurrentTool();
    }

    if (m_toolWidget) {
        // Release active tool if it is in the editing mode but not changed and
        // has editing widget (ex: text tool)
        releaseActiveTool();
    }

    if (m_existingObjectIsChanged) {
        m_existingObjectIsChanged = false;
        pushObjectsStateToUndoStack();
    }
    drawToolsData();
    drawObjectSelection();
    updateSelectionState();
}

void CaptureWidget::onMoveCaptureToolUp(int captureToolIndex)
{
    m_captureToolObjectsBackup = m_captureToolObjects;
    pushObjectsStateToUndoStack();
    auto tool = m_captureToolObjects.at(captureToolIndex);
    m_captureToolObjects.removeAt(captureToolIndex);
    m_captureToolObjects.insert(captureToolIndex - 1, tool);
    updateLayersPanel();
}

void CaptureWidget::onMoveCaptureToolDown(int captureToolIndex)
{
    m_captureToolObjectsBackup = m_captureToolObjects;
    pushObjectsStateToUndoStack();
    auto tool = m_captureToolObjects.at(captureToolIndex);
    m_captureToolObjects.removeAt(captureToolIndex);
    m_captureToolObjects.insert(captureToolIndex + 1, tool);
    updateLayersPanel();
}

void CaptureWidget::selectAll()
{
    m_selection->show();
    m_selection->setGeometry(rect());
    emit m_selection->geometrySettled();
    m_buttonHandler->show();
    updateSelectionState();
}

void CaptureWidget::removeToolObject(int index)
{
    --index;
    if (index >= 0 && index < m_captureToolObjects.size()) {
        // in case this tool is circle counter
        const CaptureTool::Type currentToolType =
          m_captureToolObjects.at(index)->type();
        m_captureToolObjectsBackup = m_captureToolObjects;
        update(
          paddedUpdateRect(m_captureToolObjects.at(index)->boundingRect()));
        if (currentToolType == CaptureTool::TYPE_CIRCLECOUNT) {
            int removedCircleCount = m_captureToolObjects.at(index)->count();
            --m_context.circleCount;
            // Decrement circle counter numbers starting from deleted circle
            for (int cnt = 0; cnt < m_captureToolObjects.size(); cnt++) {
                auto toolItem = m_captureToolObjects.at(cnt);
                if (toolItem->type() != CaptureTool::TYPE_CIRCLECOUNT) {
                    continue;
                }
                auto circleTool = m_captureToolObjects.at(cnt);
                if (circleTool->count() >= removedCircleCount) {
                    circleTool->setCount(circleTool->count() - 1);
                }
            }
        }
        m_captureToolObjects.removeAt(index);
        pushObjectsStateToUndoStack();
        drawToolsData();
        updateLayersPanel();
    }
}

void CaptureWidget::initShortcuts()
{
    newShortcut(
      QKeySequence(ConfigHandler().shortcut("TYPE_UNDO")), this, SLOT(undo()));

    newShortcut(
      QKeySequence(ConfigHandler().shortcut("TYPE_REDO")), this, SLOT(redo()));

    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_TOGGLE_PANEL")),
                this,
                SLOT(togglePanel()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_GRAB_COLOR")),
                this,
                SLOT(startColorGrab()));

    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_RESIZE_LEFT")),
                m_selection,
                SLOT(resizeLeft()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_RESIZE_RIGHT")),
                m_selection,
                SLOT(resizeRight()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_RESIZE_UP")),
                m_selection,
                SLOT(resizeUp()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_RESIZE_DOWN")),
                m_selection,
                SLOT(resizeDown()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_SYM_RESIZE_LEFT")),
                m_selection,
                SLOT(symResizeLeft()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_SYM_RESIZE_RIGHT")),
                m_selection,
                SLOT(symResizeRight()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_SYM_RESIZE_UP")),
                m_selection,
                SLOT(symResizeUp()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_SYM_RESIZE_DOWN")),
                m_selection,
                SLOT(symResizeDown()));

    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_MOVE_LEFT")),
                m_selection,
                SLOT(moveLeft()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_MOVE_RIGHT")),
                m_selection,
                SLOT(moveRight()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_MOVE_UP")),
                m_selection,
                SLOT(moveUp()));
    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_MOVE_DOWN")),
                m_selection,
                SLOT(moveDown()));

    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_CANCEL")),
                this,
                SLOT(cancel()));

    newShortcut(
      QKeySequence(ConfigHandler().shortcut("TYPE_DELETE_CURRENT_TOOL")),
      this,
      SLOT(deleteCurrentTool()));

    newShortcut(
      QKeySequence(ConfigHandler().shortcut("TYPE_COMMIT_CURRENT_TOOL")),
      this,
      SLOT(commitCurrentTool()));

    newShortcut(QKeySequence(ConfigHandler().shortcut("TYPE_SELECT_ALL")),
                this,
                SLOT(selectAll()));

    newShortcut(Qt::Key_Escape, this, SLOT(deleteToolWidgetOrClose()));
}

void CaptureWidget::deleteCurrentTool()
{
    int oldToolSize = m_context.toolSize;
    m_panel->slotButtonDelete(true);
    drawObjectSelection();
    if (oldToolSize != m_context.toolSize) {
        emit toolSizeChanged(m_context.toolSize);
    }
}

void CaptureWidget::updateSizeIndicator()
{
    if (m_config.showSelectionGeometry()) {
        showxywh();
    }
    if (m_sizeIndButton) {
        const QRect& selection = extendedSelection();
        m_sizeIndButton->setText(
          QStringLiteral("%1\n%2").arg(selection.width(), selection.height()));
    }
}

void CaptureWidget::updateCursor()
{
    // flameshot-ocr: 橡皮擦模式光标
    if (m_eraserActive) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    // flameshot-ocr: 标注对象悬停光标（手柄=缩放，对象=移动）
    if (!m_activeButton && !m_mouseIsClicked) {
        const QPoint cursorPos = mapFromGlobal(QCursor::pos());
        int hoverIndex = m_panel->activeLayerIndex();
        if (hoverIndex < 0) {
            hoverIndex = objectIndexWithGrabZone(cursorPos);
        }
        if (hoverIndex >= 0) {
            auto hoverObject = m_captureToolObjects.at(hoverIndex);
            if (hoverObject &&
                resizeGrabHandleAt(hoverObject->boundingRect().normalized(),
                                   cursorPos) > 0) {
                setCursor(Qt::SizeFDiagCursor);
                return;
            }
        }
        if (m_captureToolObjects.find(cursorPos, size()) >= 0 ||
            objectIndexContainingPoint(cursorPos) >= 0) {
            setCursor(Qt::SizeAllCursor);
            return;
        }
    }
    if (m_colorPicker && m_colorPicker->isVisible()) {
        setCursor(Qt::ArrowCursor);
    } else if (m_activeButton != nullptr &&
               activeButtonToolType() != CaptureTool::TYPE_MOVESELECTION) {
        setCursor(Qt::CrossCursor);
    } else if (m_selection->getMouseSide(mapFromGlobal(QCursor::pos())) !=
               SelectionWidget::NO_SIDE) {
        setCursor(m_selection->cursor());
    } else if (activeButtonToolType() == CaptureTool::TYPE_MOVESELECTION) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

void CaptureWidget::updateSelectionState()
{
    // flameshot-ocr: 橡皮擦模式下屏蔽选区控件——
    // 否则拖动空白处会画出区域选区（用户未开启框选时也会发生）
    if (m_eraserActive) {
        m_selection->setIgnoreMouse(true);
        return;
    }
    auto toolType = activeButtonToolType();
    if (toolType == CaptureTool::TYPE_MOVESELECTION) {
        m_selection->setIdleCentralCursor(Qt::OpenHandCursor);
        m_selection->setIgnoreMouse(false);
    } else {
        m_selection->setIdleCentralCursor(Qt::ArrowCursor);
        if (toolType == CaptureTool::NONE) {
            m_selection->setIgnoreMouse(m_panel->activeLayerIndex() != -1);
        } else {
            m_selection->setIgnoreMouse(true);
        }
    }
}

void CaptureWidget::updateTool(CaptureTool* tool)
{
    if (!tool || !tool->showMousePreview()) {
        return;
    }

    static QRect oldPreviewRect, oldToolObjectRect;

    QRect previewRect(tool->mousePreviewRect(m_context));
    previewRect += QMargins(previewRect.width(),
                            previewRect.height(),
                            previewRect.width(),
                            previewRect.height());

    QRect toolObjectRect = paddedUpdateRect(tool->boundingRect());

    // old rects are united with current rects to handle sudden mouse movement
    update(previewRect);
    update(toolObjectRect);
    update(oldPreviewRect);
    update(oldToolObjectRect);

    oldPreviewRect = previewRect;
    oldToolObjectRect = toolObjectRect;
}

void CaptureWidget::updateLayersPanel()
{
    m_panel->fillCaptureTools(m_captureToolObjects.captureToolObjects());
}

void CaptureWidget::pushToolToStack()
{
    // append current tool to the new state
    if (m_activeTool && m_activeButton) {
        disconnect(this,
                   &CaptureWidget::colorChanged,
                   m_activeTool,
                   &CaptureTool::onColorChanged);
        disconnect(this,
                   &CaptureWidget::toolSizeChanged,
                   m_activeTool,
                   &CaptureTool::onSizeChanged);
        if (m_panel->toolWidget()) {
            disconnect(m_panel->toolWidget(), nullptr, m_activeTool, nullptr);
        }

        // disable signal connect for updating layer because it may call this
        // function again on text objects
        m_panel->blockSignals(true);

        m_captureToolObjectsBackup = m_captureToolObjects;
        m_captureToolObjects.append(m_activeTool);
        pushObjectsStateToUndoStack();
        releaseActiveTool();
        drawToolsData();
        updateLayersPanel();

        // restore signal connection for updating layer
        m_panel->blockSignals(false);
    }
}

void CaptureWidget::drawToolsData(bool drawSelection)
{
    // TODO refactor this for performance. The objects should not all be updated
    // at once every time
    QPixmap pixmapItem = m_context.origScreenshot;
    for (const auto& toolItem : m_captureToolObjects.captureToolObjects()) {
        processPixmapWithTool(&pixmapItem, toolItem);
        update(paddedUpdateRect(toolItem->boundingRect()));
    }

    m_context.screenshot = pixmapItem;
    if (drawSelection) {
        drawObjectSelection();
    }
}

void CaptureWidget::drawObjectSelection()
{
    auto toolItem = activeToolObject();
    if (toolItem && !toolItem->editMode()) {
        QPainter painter(&m_context.screenshot);
        toolItem->drawObjectSelection(painter);
        // flameshot-ocr: 四角缩放手柄
        if (!m_eraserActive) {
            const QRect handleRect = toolItem->boundingRect().normalized();
            painter.setPen(QPen(Qt::white, 1));
            painter.setBrush(ConfigHandler().uiColor());
            for (const QPoint& corner :
                 { handleRect.topLeft(),
                   handleRect.topRight(),
                   handleRect.bottomLeft(),
                   handleRect.bottomRight() }) {
                painter.drawRect(QRect(corner - QPoint(4, 4), QSize(8, 8)));
            }
        }
        // TODO move this elsewhere
        if (m_context.toolSize != toolItem->size()) {
            m_context.toolSize = toolItem->size();
        }
        if (activeToolObject() && m_activeButton) {
            uncheckActiveTool();
        }
    }
}

void CaptureWidget::processPixmapWithTool(QPixmap* pixmap, CaptureTool* tool)
{
    QPainter painter(pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    tool->process(painter, *pixmap);
}

CaptureTool* CaptureWidget::activeButtonTool() const
{
    if (m_activeButton == nullptr) {
        return nullptr;
    }
    return m_activeButton->tool();
}

CaptureTool::Type CaptureWidget::activeButtonToolType() const
{
    auto* activeTool = activeButtonTool();
    if (activeTool == nullptr) {
        return CaptureTool::NONE;
    }
    return activeTool->type();
}

QPoint CaptureWidget::snapToGrid(const QPoint& point) const
{
    QPoint snapPoint = mapToGlobal(point);

    const auto scale{ m_context.screenshot.devicePixelRatio() };

    snapPoint.setX((qRound(snapPoint.x() / double(m_gridSize)) * m_gridSize));
    snapPoint.setY((qRound(snapPoint.y() / double(m_gridSize)) * m_gridSize));

    return mapFromGlobal(snapPoint);
}

QPointer<CaptureTool> CaptureWidget::activeToolObject()
{
    return m_captureToolObjects.at(m_panel->activeLayerIndex());
}

void CaptureWidget::makeChild(QWidget* w)
{
    w->setParent(this);
    w->installEventFilter(m_eventFilter);
}

void CaptureWidget::restoreCircleCountState()
{
    int largest = 0;
    for (int cnt = 0; cnt < m_captureToolObjects.size(); cnt++) {
        auto toolItem = m_captureToolObjects.at(cnt);
        if (toolItem->type() != CaptureTool::TYPE_CIRCLECOUNT) {
            continue;
        }
        if (toolItem->count() > largest) {
            largest = toolItem->count();
        }
    }
    m_context.circleCount = largest + 1;
}

/**
 * @brief Wrapper around `new QShortcut`, properly handling Enter/Return.
 */
QList<QShortcut*> CaptureWidget::newShortcut(const QKeySequence& key,
                                             QWidget* parent,
                                             const char* slot)
{
    QList<QShortcut*> shortcuts;
    QString strKey = key.toString();
    if (strKey.contains("Enter") || strKey.contains("Return")) {
        strKey.replace("Enter", "Return");
        shortcuts << new QShortcut(strKey, parent, slot);
        strKey.replace("Return", "Enter");
        shortcuts << new QShortcut(strKey, parent, slot);
    } else {
        shortcuts << new QShortcut(key, parent, slot);
    }
    return shortcuts;
}

void CaptureWidget::togglePanel()
{
    m_panel->toggle();
}

void CaptureWidget::childEnter()
{
    m_previewEnabled = false;
    updateTool(activeButtonTool());
}

void CaptureWidget::childLeave()
{
    m_previewEnabled = true;
    updateTool(activeButtonTool());
}

void CaptureWidget::setCaptureToolObjects(
  const CaptureToolObjects& captureToolObjects)
{
    // Used for undo/redo
    m_captureToolObjects = captureToolObjects;
    drawToolsData();
    updateLayersPanel();
    drawObjectSelection();
}

void CaptureWidget::undo()
{
    if (m_activeTool &&
        (m_activeTool->isChanged() || m_activeTool->editMode())) {
        // Remove selection on undo, at the same time commit current tool will
        // be called
        m_panel->setActiveLayer(-1);
    }

    // drawToolsData is called twice to update both previous and new regions
    // FIXME this is a temporary workaround
    drawToolsData();
    m_undoStack.undo();
    drawToolsData();
    updateLayersPanel();

    restoreCircleCountState();
}

void CaptureWidget::redo()
{
    // drawToolsData is called twice to update both previous and new regions
    // FIXME this is a temporary workaround
    drawToolsData();
    m_undoStack.redo();
    drawToolsData();
    update();
    updateLayersPanel();

    restoreCircleCountState();
}

void CaptureWidget::cancel()
{
    if (m_activeButton != nullptr) {
        uncheckActiveTool();
    }
    if (m_panel) {
        m_panel->setActiveLayer(-1);
    }
    if (m_toolWidget) {
        m_toolWidget->hide();
        delete m_toolWidget;
        m_toolWidget = nullptr;
    }
    m_selection->hide();
    emit m_selection->geometrySettled();
}

QRect CaptureWidget::extendedSelection() const
{
    if (m_selection == nullptr) {
        return {};
    }
    QRect r = m_selection->geometry();
    return extendedRect(r);
}

QRect CaptureWidget::extendedRect(const QRect& r) const
{
    auto devicePixelRatio = m_context.screenshot.devicePixelRatio();
    return { static_cast<int>(r.left() * devicePixelRatio),
             static_cast<int>(r.top() * devicePixelRatio),
             static_cast<int>(r.width() * devicePixelRatio),
             static_cast<int>(r.height() * devicePixelRatio) };
}

QRect CaptureWidget::paddedUpdateRect(const QRect& r) const
{
    if (r.isNull()) {
        return r;
    } else {
        return r + QMargins(20, 20, 20, 20);
    }
}

void CaptureWidget::drawErrorMessage(const QString& msg, QPainter* painter)
{
    auto textRect = painter->fontMetrics().boundingRect(msg);
    int w = textRect.width(), h = textRect.height();
    textRect = {
        size().width() - w - 10, size().height() - h - 5, w + 100, h + 100
    };
    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();

    if (!textRect.contains(QCursor::pos(currentScreen))) {
        QColor textColor(Qt::white);
        painter->setPen(textColor);
        painter->drawText(textRect, msg);
    }
}

void CaptureWidget::drawInactiveRegion(QPainter* painter)
{
    // flameshot-ocr: 未框选时不压暗屏幕（Win11 风格，保持画面原样，
    // 预选区悬浮工具条和取色都在这个状态下工作）
    if (!m_selection->isVisible()) {
        // 选区刚消失时恢复明亮，同时把渐入状态复位
        if (m_dimAlpha > 0) {
            m_dimAlpha = 0;
        }
        return;
    }
    // 选区首次出现：把压暗从 0 渐入到目标值，避免“闪一下”变暗
    if (m_dimAlpha <= 0) {
        m_dimAlpha = 1;
        auto* dimAnim = new QVariantAnimation(this);
        dimAnim->setDuration(150);
        dimAnim->setStartValue(1);
        dimAnim->setEndValue(m_opacity);
        dimAnim->setEasingCurve(QEasingCurve::InOutQuad);
        connect(dimAnim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) {
                    m_dimAlpha = v.toReal();
                    update();
                });
        connect(dimAnim, &QVariantAnimation::finished, dimAnim,
                &QVariantAnimation::deleteLater);
        dimAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    QColor overlayColor(0, 0, 0, int(m_dimAlpha));
    painter->setBrush(overlayColor);
    QRect r = m_selection->geometry().normalized();
    QRegion grey(rect());
    grey = grey.subtracted(r);

    painter->setClipRegion(grey);
    painter->drawRect(-1, -1, rect().width() + 1, rect().height() + 1);
}

// flameshot-ocr: 截图窗渐入，消除 Wayland 首帧闪变
void CaptureWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(150);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::InOutQuad);
    connect(fade, &QPropertyAnimation::finished, fade,
            &QPropertyAnimation::deleteLater);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}
