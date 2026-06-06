#include "scrollbehavior.h"

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QChildEvent>
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <QtMath>

namespace {

constexpr int kWheelStepPx = 64;
constexpr int kWheelAnimationMs = 180;
constexpr int kTouchpadAnimationMs = 90;
constexpr int kFadeDelayMs = 520;
constexpr int kIdleFadeDelayMs = 900;
constexpr int kFadeAnimationMs = 260;
constexpr auto kManagedProperty = "yyFloatingScrollManaged";
constexpr auto kOpacityAnimationName = "yyScrollOpacityAnimation";
constexpr auto kScrollAnimationName = "yySmoothScrollAnimation";
constexpr auto kScrollTargetProperty = "yySmoothScrollTarget";

struct AreaState {
    QPointer<QAbstractScrollArea> area;
    QPointer<QTimer> fadeTimer;
};

class FloatingScrollBehaviorFilter final : public QObject {
public:
    explicit FloatingScrollBehaviorFilter(QApplication& app)
        : QObject(&app)
    {
    }

    void scanExistingWidgets()
    {
        const auto widgets = QApplication::allWidgets();
        for (QWidget* widget : widgets) {
            if (auto* area = qobject_cast<QAbstractScrollArea*>(widget))
                installOnArea(area);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!watched || !event)
            return QObject::eventFilter(watched, event);

        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Polish:
            if (auto* area = qobject_cast<QAbstractScrollArea*>(watched))
                installOnArea(area);
            break;
        case QEvent::ChildAdded:
            installFromChildEvent(static_cast<QChildEvent*>(event));
            break;
        case QEvent::Enter:
            if (auto* area = areaFor(watched)) {
                installOnArea(area);
                showBars(area);
            }
            break;
        case QEvent::Leave:
            if (auto* area = areaFor(watched))
                scheduleFade(area, kFadeDelayMs);
            break;
        case QEvent::Wheel:
            if (auto* area = areaFor(watched)) {
                installOnArea(area);
                if (handleWheel(area, static_cast<QWheelEvent*>(event)))
                    return true;
            }
            break;
        default:
            break;
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void installFromChildEvent(QChildEvent* event)
    {
        if (!event)
            return;

        if (auto* area = qobject_cast<QAbstractScrollArea*>(event->child())) {
            installOnArea(area);
            return;
        }

        if (auto* widget = qobject_cast<QWidget*>(event->child())) {
            const auto areas = widget->findChildren<QAbstractScrollArea*>();
            for (QAbstractScrollArea* area : areas)
                installOnArea(area);
        }
    }

    QAbstractScrollArea* areaFor(QObject* object) const
    {
        if (auto* area = qobject_cast<QAbstractScrollArea*>(object))
            return area;

        auto* widget = qobject_cast<QWidget*>(object);
        while (widget) {
            if (auto* area = qobject_cast<QAbstractScrollArea*>(widget))
                return area;
            widget = widget->parentWidget();
        }
        return nullptr;
    }

    void installOnArea(QAbstractScrollArea* area)
    {
        if (!area)
            return;
        if (area->property(kManagedProperty).toBool())
            return;

        area->setProperty(kManagedProperty, true);
        area->setMouseTracking(true);
        if (QWidget* viewport = area->viewport()) {
            viewport->setMouseTracking(true);
        }

        if (auto* view = qobject_cast<QAbstractItemView*>(area)) {
            view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        }

        setupBar(area->verticalScrollBar());
        setupBar(area->horizontalScrollBar());

        AreaState state;
        state.area = area;
        state.fadeTimer = new QTimer(area);
        state.fadeTimer->setSingleShot(true);
        connect(state.fadeTimer, &QTimer::timeout, this, [this, areaPtr = QPointer<QAbstractScrollArea>(area)] {
            if (!areaPtr)
                return;
            if (!isCursorInside(areaPtr))
                fadeBars(areaPtr);
        });
        m_states.insert(area, state);

        connect(area, &QObject::destroyed, this, [this, area] {
            m_states.remove(area);
        });
        connect(area->verticalScrollBar(), &QScrollBar::rangeChanged, this,
                [this, areaPtr = QPointer<QAbstractScrollArea>(area)] {
                    if (areaPtr)
                        syncBarVisibility(areaPtr);
                });
        connect(area->horizontalScrollBar(), &QScrollBar::rangeChanged, this,
                [this, areaPtr = QPointer<QAbstractScrollArea>(area)] {
                    if (areaPtr)
                        syncBarVisibility(areaPtr);
                });

        syncBarVisibility(area);
    }

    void setupBar(QScrollBar* bar)
    {
        if (!bar)
            return;

        bar->setMouseTracking(true);
        bar->setSingleStep(16);
        ensureOpacityEffect(bar)->setOpacity(0.0);
        bar->setProperty(kScrollTargetProperty, bar->value());
    }

    QGraphicsOpacityEffect* ensureOpacityEffect(QScrollBar* bar)
    {
        if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(bar->graphicsEffect()))
            return effect;

        auto* effect = new QGraphicsOpacityEffect(bar);
        effect->setOpacity(0.0);
        bar->setGraphicsEffect(effect);
        return effect;
    }

    QPropertyAnimation* opacityAnimation(QGraphicsOpacityEffect* effect)
    {
        auto* animation = effect->findChild<QPropertyAnimation*>(QString::fromLatin1(kOpacityAnimationName));
        if (!animation) {
            animation = new QPropertyAnimation(effect, "opacity", effect);
            animation->setObjectName(QString::fromLatin1(kOpacityAnimationName));
            animation->setEasingCurve(QEasingCurve::OutCubic);
        }
        return animation;
    }

    QPropertyAnimation* scrollAnimation(QScrollBar* bar)
    {
        auto* animation = bar->findChild<QPropertyAnimation*>(QString::fromLatin1(kScrollAnimationName));
        if (!animation) {
            animation = new QPropertyAnimation(bar, "value", bar);
            animation->setObjectName(QString::fromLatin1(kScrollAnimationName));
            animation->setEasingCurve(QEasingCurve::OutCubic);
        }
        return animation;
    }

    bool barHasRange(QScrollBar* bar) const
    {
        return bar && bar->maximum() > bar->minimum();
    }

    void animateBar(QScrollBar* bar, qreal opacity, int durationMs)
    {
        if (!bar)
            return;

        if (!barHasRange(bar))
            opacity = 0.0;

        auto* effect = ensureOpacityEffect(bar);
        auto* animation = opacityAnimation(effect);
        animation->stop();
        animation->setDuration(durationMs);
        animation->setStartValue(effect->opacity());
        animation->setEndValue(opacity);
        animation->start();
    }

    void showBars(QAbstractScrollArea* area)
    {
        if (!area)
            return;
        if (AreaState* state = stateFor(area)) {
            if (state->fadeTimer)
                state->fadeTimer->stop();
        }
        animateBar(area->verticalScrollBar(), 1.0, 120);
        animateBar(area->horizontalScrollBar(), 1.0, 120);
    }

    void fadeBars(QAbstractScrollArea* area)
    {
        if (!area)
            return;
        animateBar(area->verticalScrollBar(), 0.0, kFadeAnimationMs);
        animateBar(area->horizontalScrollBar(), 0.0, kFadeAnimationMs);
    }

    void scheduleFade(QAbstractScrollArea* area, int delayMs)
    {
        if (!area)
            return;
        if (AreaState* state = stateFor(area)) {
            if (state->fadeTimer)
                state->fadeTimer->start(delayMs);
        }
    }

    void syncBarVisibility(QAbstractScrollArea* area)
    {
        if (!area)
            return;
        if (isCursorInside(area)) {
            showBars(area);
            return;
        }
        fadeBars(area);
    }

    bool isCursorInside(QAbstractScrollArea* area) const
    {
        if (!area || !area->isVisible())
            return false;
        const QPoint localPos = area->mapFromGlobal(QCursor::pos());
        return area->rect().contains(localPos);
    }

    AreaState* stateFor(QAbstractScrollArea* area)
    {
        auto it = m_states.find(area);
        if (it == m_states.end())
            return nullptr;
        return &it.value();
    }

    bool handleWheel(QAbstractScrollArea* area, QWheelEvent* event)
    {
        if (!area || !event)
            return false;
        if (event->modifiers().testFlag(Qt::ControlModifier))
            return false;

        const QPoint pixelDelta = event->pixelDelta();
        const QPoint angleDelta = event->angleDelta();
        const bool shiftHorizontal = event->modifiers().testFlag(Qt::ShiftModifier)
            && barHasRange(area->horizontalScrollBar());
        const bool naturalHorizontal = qAbs(pixelDelta.x()) > qAbs(pixelDelta.y())
            || qAbs(angleDelta.x()) > qAbs(angleDelta.y());
        const bool useHorizontal = shiftHorizontal || naturalHorizontal;
        QScrollBar* bar = useHorizontal ? area->horizontalScrollBar() : area->verticalScrollBar();

        if (!barHasRange(bar)) {
            bar = useHorizontal ? area->verticalScrollBar() : area->horizontalScrollBar();
            if (!barHasRange(bar))
                return false;
        }

        const bool horizontal = bar->orientation() == Qt::Horizontal;
        qreal delta = 0.0;
        const bool hasPixelDelta = !pixelDelta.isNull();
        if (hasPixelDelta) {
            delta = horizontal ? pixelDelta.x() : pixelDelta.y();
            if (qFuzzyIsNull(delta))
                delta = horizontal ? pixelDelta.y() : pixelDelta.x();
        } else {
            delta = horizontal ? angleDelta.x() : angleDelta.y();
            if (qFuzzyIsNull(delta) && horizontal)
                delta = angleDelta.y();
            delta = (delta / 120.0) * kWheelStepPx;
        }

        if (qFuzzyIsNull(delta))
            return false;

        const int currentValue = bar->value();
        const int baseTarget = scrollAnimation(bar)->state() == QAbstractAnimation::Running
            ? bar->property(kScrollTargetProperty).toInt()
            : currentValue;
        const int nextValue = qBound(bar->minimum(), baseTarget - qRound(delta), bar->maximum());
        if (nextValue == currentValue && nextValue == baseTarget)
            return false;

        bar->setProperty(kScrollTargetProperty, nextValue);
        auto* animation = scrollAnimation(bar);
        animation->stop();
        animation->setDuration(hasPixelDelta ? kTouchpadAnimationMs : kWheelAnimationMs);
        animation->setStartValue(currentValue);
        animation->setEndValue(nextValue);
        animation->start();

        showBars(area);
        scheduleFade(area, kIdleFadeDelayMs);
        event->accept();
        return true;
    }

    QHash<QAbstractScrollArea*, AreaState> m_states;
};

} // namespace

void ScrollBehavior::install(QApplication& app)
{
    if (app.property("yyScrollBehaviorInstalled").toBool())
        return;

    app.setProperty("yyScrollBehaviorInstalled", true);
    auto* filter = new FloatingScrollBehaviorFilter(app);
    app.installEventFilter(filter);
    filter->scanExistingWidgets();
}
