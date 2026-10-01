#include "colorgrabwidget.h"
#include "sidepanelwidget.h"

#include "colorutils.h"
#include "confighandler.h"
#include "overlaymessage.h"
#include "src/core/qguiappcurrentscreen.h"
#include <QApplication>
#include <QDebug>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QShortcut>
#include <QTimer>
#include <stdexcept>

// Width (= height) and zoom level of the widget before the user clicks
#define WIDTH1 77
#define ZOOM1 11
// Width (= height) and zoom level of the widget after the user clicks
#define WIDTH2 165
#define ZOOM2 15

// NOTE: WIDTH1(2) should be divisible by ZOOM1(2) for best precision.
//       WIDTH1 should be odd so the cursor can be centered on a pixel.

ColorGrabWidget::ColorGrabWidget(QPixmap* p, QWidget* parent)
  : QWidget(parent)
  , m_pixmap(p)
  , m_mousePressReceived(false)
  , m_extraZoomActive(false)
  , m_magnifierActive(true)
{
    if (p == nullptr) {
        throw std::logic_error("Pixmap must not be null");
    }
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    // We don't need this widget to receive mouse events because we use
    // eventFilter on other objects that do
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_QuitOnClose, false);
    // On Windows: don't activate the widget so CaptureWidget remains active
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlags(Qt::BypassWindowManagerHint | Qt::WindowStaysOnTopHint |
                   Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    setMouseTracking(true);
}

void ColorGrabWidget::startGrabbing()
{
    // NOTE: grabMouse() would prevent move events being received
    // With this method we just need to make sure that mouse press and release
    // events get consumed before they reach their target widget.
    // This is undone in the destructor.
    qApp->setOverrideCursor(Qt::CrossCursor);
    qApp->installEventFilter(this);
    // flameshot-ocr: 进入取色即显示放大镜（跟随光标），无需先移动/点击
    if (m_magnifierActive) {
        updateWidget();
        show();
    }
    OverlayMessage::pushKeyMap(
      { { tr("Enter or Left Click"), tr("Accept color") },
        { tr("Hold Left Click"), tr("Precisely select color") },
        { tr("Space or Right Click"), tr("Toggle magnifier") },
        { tr("Esc"), tr("Cancel") } });
}

QColor ColorGrabWidget::color()
{
    return m_color;
}

bool ColorGrabWidget::eventFilter(QObject*, QEvent* event)
{
    // Consume shortcut events and handle key presses from whole app
    if (event->type() == QEvent::KeyPress ||
        event->type() == QEvent::Shortcut) {
        QKeySequence key = event->type() == QEvent::KeyPress
                             ? static_cast<QKeyEvent*>(event)->key()
                             : static_cast<QShortcutEvent*>(event)->key();
        if (key == Qt::Key_Escape) {
            emit grabAborted();
            finalize();
        } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            emit colorGrabbed(m_color);
            finalize();
        } else if (key == Qt::Key_Space && !m_extraZoomActive) {
            setMagnifierActive(!m_magnifierActive);
        }
        return true;
    } else if (event->type() == QEvent::MouseMove) {
        // NOTE: This relies on the fact that CaptureWidget tracks mouse moves

        if (m_extraZoomActive && !geometry().contains(cursorPos())) {
            setExtraZoomActive(false);
            return true;
        }
        if (!m_extraZoomActive && !m_magnifierActive) {
            // This fixes an issue when the mouse leaves the zoom area before
            // the widget even appears.
            hide();
        }
        if (!m_extraZoomActive) {
            // Update only before the user clicks the mouse, after the mouse
            // press the widget remains static.
            updateWidget();
        }

        // Hide overlay message when cursor is over it
        OverlayMessage* overlayMsg = OverlayMessage::instance();
        overlayMsg->setVisibility(
          !overlayMsg->geometry().contains(cursorPos()));

        m_color = getColorAtPoint(cursorPos());
        emit colorUpdated(m_color);
        return true;
    } else if (event->type() == QEvent::MouseButtonPress) {
        m_mousePressReceived = true;
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->buttons() == Qt::RightButton) {
            setMagnifierActive(!m_magnifierActive);
        } else if (e->buttons() == Qt::LeftButton) {
            setExtraZoomActive(true);
        }
        return true;
    } else if (event->type() == QEvent::MouseButtonRelease) {
        if (!m_mousePressReceived) {
            // Do not consume event if it corresponds to the mouse press that
            // triggered the color grabbing in the first place. This prevents
            // focus issues in the capture widget when the color grabber is
            // closed.
            return false;
        }
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() == Qt::LeftButton && m_extraZoomActive) {
            emit colorGrabbed(getColorAtPoint(cursorPos()));
            finalize();
        }
        return true;
    } else if (event->type() == QEvent::MouseButtonDblClick) {
        return true;
    }
    return false;
}

void ColorGrabWidget::paintEvent(QPaintEvent*)
{
    // flameshot-ocr: 圆形放大镜 —— 环形描边（UI 颜色）+ 中心十字准星 +
    // 底部色值徽标，替代原先的裸方块
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.drawImage(QRectF(0, 0, width(), height()), m_previewImage);

    const qreal radius = qMin(width(), height()) / 2.0 - 3.0;
    const QPointF center(width() / 2.0, height() / 2.0);

    QPainterPath circle;
    circle.addEllipse(center, radius, radius);
    painter.setPen(QPen(ConfigHandler().uiColor(), 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(circle);

    painter.setPen(QPen(Qt::white, 1));
    painter.drawLine(QPointF(center.x() - 9, center.y()),
                     QPointF(center.x() + 9, center.y()));
    painter.drawLine(QPointF(center.x(), center.y() - 9),
                     QPointF(center.x(), center.y() + 9));

    const QString label = m_color.name(QColor::HexRgb).toUpper();
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const QRectF badge = QRectF(
      center.x() - metrics.horizontalAdvance(label) / 2.0 - 8,
      height() - metrics.height() - 10,
      metrics.horizontalAdvance(label) + 16, metrics.height() + 8);
    painter.setPen(QPen(QColor(63, 63, 70), 1));
    painter.setBrush(QColor(20, 20, 24, 235));
    painter.drawRoundedRect(badge, 4, 4);
    painter.setPen(Qt::white);
    painter.drawText(badge, Qt::AlignCenter, label);
}

void ColorGrabWidget::showEvent(QShowEvent*)
{
    updateWidget();
}

QPoint ColorGrabWidget::cursorPos() const
{
    return QCursor::pos(QGuiAppCurrentScreen().currentScreen());
}

/// @note The point is in screen coordinates.
QColor ColorGrabWidget::getColorAtPoint(const QPoint& p) const
{
    if (m_extraZoomActive && geometry().contains(p)) {
        // flameshot-ocr: 预览图与控件尺寸的比例随 dpr 变化，
        // 按实际比例换算像素下标（原先固定 /ZOOM2 在缩放屏上会取偏）
        if (m_previewImage.isNull() || width() <= 0 || height() <= 0) {
            return m_color;
        }
        const QPoint point = mapFromGlobal(p);
        const qreal sx = qreal(m_previewImage.width()) / width();
        const qreal sy = qreal(m_previewImage.height()) / height();
        const int ix = qBound(0, int(point.x() * sx),
                              m_previewImage.width() - 1);
        const int iy = qBound(0, int(point.y() * sy),
                              m_previewImage.height() - 1);
        return m_previewImage.pixel(ix, iy);
    }
    // flameshot-ocr: 逻辑坐标 → 设备像素（缩放屏取色点精确对准光标）
    QPoint point = p;
    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
    if (currentScreen) {
        const qreal dpr = m_pixmap->devicePixelRatio();
        point = QPoint(
          qRound((p.x() - currentScreen->geometry().x()) * dpr),
          qRound((p.y() - currentScreen->geometry().y()) * dpr));
    }
    point.setX(qBound(0, point.x(), m_pixmap->width() - 1));
    point.setY(qBound(0, point.y(), m_pixmap->height() - 1));
    QPixmap pixel = m_pixmap->copy(QRect(point, point));
    return pixel.toImage().pixel(0, 0);
}

void ColorGrabWidget::setExtraZoomActive(bool active)
{
    m_extraZoomActive = active;
    if (!active && !m_magnifierActive) {
        hide();
    } else {
        if (!isVisible()) {
            QTimer::singleShot(250, this, [this]() { show(); });
        } else {
            QTimer::singleShot(250, this, [this]() { updateWidget(); });
        }
    }
}

void ColorGrabWidget::setMagnifierActive(bool active)
{
    m_magnifierActive = active;
    setVisible(active);
}

void ColorGrabWidget::updateWidget()
{
    int width = m_extraZoomActive ? WIDTH2 : WIDTH1;
    float zoom = m_extraZoomActive ? ZOOM2 : ZOOM1;
    // Set window size and move its center to the mouse cursor
    QRect rect(0, 0, width, width);

    auto realCursorPos = cursorPos();
    // flameshot-ocr: 截图是物理像素（缩放屏如 125% 时 dpr=1.25），
    // QPixmap::copy 按设备像素取坐标 —— 必须把光标的逻辑坐标换算成
    // 设备像素后再采样，否则放大镜区域与取色点会整体偏向左上。
    // 换算用「截图自身」的 dpr（采样对象是截图，须与其像素网格对齐）
    const qreal dpr = m_pixmap->devicePixelRatio();
    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
    QPoint adjustedCursorPos = realCursorPos;
    if (currentScreen) {
        adjustedCursorPos =
          QPoint(qRound((realCursorPos.x() - currentScreen->geometry().x()) *
                        dpr),
                 qRound((realCursorPos.y() - currentScreen->geometry().y()) *
                        dpr));
    }

    // flameshot-ocr: 放大镜偏置在光标右下方（不遮挡取色点/放置点），
    // 贴近屏幕右/下边缘时自动翻到另一侧
    const QPoint cur = cursorPos();
    const int offset = 18;
    QPoint topLeft = cur + QPoint(offset, offset);
    if (QScreen* scr = QGuiAppCurrentScreen().currentScreen()) {
        const QRect avail = scr->geometry();
        if (topLeft.x() + width > avail.right()) {
            topLeft.setX(cur.x() - offset - width);
        }
        if (topLeft.y() + width > avail.bottom()) {
            topLeft.setY(cur.y() - offset - width);
        }
        topLeft.setX(qMax(avail.left(), topLeft.x()));
        topLeft.setY(qMax(avail.top(), topLeft.y()));
    }
    rect.moveTo(topLeft);
    setGeometry(rect);
    // Store a pixmap containing the zoomed-in section around the cursor
    // （采样边长同样按设备像素换算，保持既定放大倍率不变）
    const int sampleSide = qRound(static_cast<qreal>(width) / zoom * dpr);
    QRect sourceRect(0, 0, sampleSide, sampleSide);
    sourceRect.moveCenter(adjustedCursorPos);
    sourceRect = sourceRect.intersected(m_pixmap->rect());
    m_previewImage = m_pixmap->copy(sourceRect).toImage();
    if (qEnvironmentVariableIsSet("FLAMESHOT_OCR_SELFTEST")) {
        qWarning() << "MAGNIFIER-DBG pixmapDpr:" << dpr
                   << "screenDpr:"
                   << (currentScreen ? currentScreen->devicePixelRatio() : 0.0)
                   << "cursor(logical):" << realCursorPos
                   << "deviceCenter:" << adjustedCursorPos
                   << "sampleSide:" << sampleSide;
    }
    // Repaint
    update();
}

void ColorGrabWidget::finalize()
{
    qApp->removeEventFilter(this);
    qApp->restoreOverrideCursor();
    OverlayMessage::pop();
    close();
}
