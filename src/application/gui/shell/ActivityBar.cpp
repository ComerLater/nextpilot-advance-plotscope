#include "ActivityBar.h"

#include <DockAreaWidget.h>
#include <DockWidget.h>
#include <DockWidgetTab.h>

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::registry;

namespace
{
QIcon activityIcon(const QString &name)
{
    if (name.isEmpty())
        return QIcon();
    const QString path = QStringLiteral(":/icons/%1.svg").arg(name);
    return QFile::exists(path) ? QIcon(path) : QIcon();
}
} // namespace

namespace nextpilot::plotscope::gui
{

ActivityBar::ActivityBar(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("ps.activityBar"));
    setFixedWidth(48);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setStyleSheet(QStringLiteral(
        "QToolButton{border:0;border-radius:3px;}"
        "QToolButton:hover{background:palette(base);}"
        "QToolButton:checked{background:palette(highlight);}"));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 6, 4, 6);
    m_layout->setSpacing(4);

    // 条目容器（VSCode compositebar）：本体透明，竖条底色由本类绘制。
    m_bar = new CompositeBar(Qt::Vertical, this);
    m_bar->setStyleSheet(QStringLiteral("background:transparent;"));
    m_layout->addWidget(m_bar, 1);
    connect(m_bar, &CompositeBar::activated, this, [this](const QString &id) {
        setActiveActivity(id);
        Q_EMIT activityActivated(id);
    });

    // ADS 的页签拖动是内部状态机，不走 QDrag；这里观察全局鼠标事件，
    // 在指针落到 Activity Bar 时把被拖 dock 交给外壳重新挂载。
    qApp->installEventFilter(this);
}

bool ActivityBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton)
            return false;
        m_dragDock = nullptr;
        m_dragging = false;
        QWidget *w = qobject_cast<QWidget *>(watched);
        if (!w)
            w = QApplication::widgetAt(me->globalPosition().toPoint());
        while (w)
        {
            if (auto *tab = qobject_cast<ads::CDockWidgetTab *>(w))
            {
                m_dragDock = tab->dockWidget();
                break;
            }
            if (auto *area = qobject_cast<ads::CDockAreaWidget *>(w))
            {
                m_dragDock = area->currentDockWidget();
                break;
            }
            w = w->parentWidget();
        }
        if (m_dragDock)
            m_pressPos = me->globalPosition().toPoint();
        return false;
    }
    if (event->type() == QEvent::MouseMove && m_dragDock)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if ((me->buttons() & Qt::LeftButton)
            && (me->globalPosition().toPoint() - m_pressPos).manhattanLength()
                   >= QApplication::startDragDistance())
            m_dragging = true;
        return false;
    }
    if (event->type() == QEvent::MouseButtonRelease && m_dragDock)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        const QPoint global = me->globalPosition().toPoint();
        const bool overBar = rect().contains(mapFromGlobal(global));
        QPointer<ads::CDockWidget> dock = m_dragDock;
        const QString target = overBar ? activityAt(global) : QString();
        const bool dropped = overBar && m_dragging;
        m_dragDock = nullptr;
        m_dragging = false;
        if (dropped)
        {
            // 等 ADS 先处理完自己的 release，再移动 dock，避免和拖动状态机抢生命周期。
            QTimer::singleShot(0, this, [this, dock, target]() {
                if (dock)
                    Q_EMIT dockDropped(dock, target);
            });
        }
        return false;
    }
    return false;
}

void ActivityBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    QColor background = palette().color(QPalette::Window);
    background = background.lightness() > 128 ? background.darker(112)
                                              : background.lighter(112);
    painter.fillRect(rect(), background);
    painter.fillRect(QRect(width() - 2, 0, 2, height()), palette().color(QPalette::Mid));
}

void ActivityBar::setActivities(const QList<ActivitySpec> &activities)
{
    // 重建前记住徽标：模块重声明不该把未读数清零。
    QHash<QString, int> badges;
    for (const QString &id : m_bar->itemIds())
        badges.insert(id, m_bar->badge(id));

    m_bar->clearItems();
    if (m_settings)
    {
        m_layout->removeWidget(m_settings);
        m_settings->deleteLater();
        m_settings = nullptr;
    }

    for (const ActivitySpec &spec : activities)
    {
        m_bar->addItem(spec.id, spec.label, spec.icon, QStringLiteral("activity.") + spec.id);
        m_bar->setBadge(spec.id, badges.value(spec.id, 0));
    }

    // 设置固定在底部，不计入 activity 顺序。
    m_settings = new QToolButton(this);
    m_settings->setObjectName(QStringLiteral("activity.settings"));
    m_settings->setToolTip(QStringLiteral("设置"));
    m_settings->setIcon(activityIcon(QStringLiteral("settings")));
    m_settings->setIconSize(QSize(18, 18));
    m_settings->setFixedSize(40, 40);
    m_settings->setAutoRaise(true);
    connect(m_settings, &QToolButton::clicked, this, &ActivityBar::settingsRequested);
    m_layout->addWidget(m_settings, 0, Qt::AlignHCenter);

    QStringList ids = m_bar->itemIds();
    if (!m_activeId.isEmpty() && ids.contains(m_activeId))
        setActiveActivity(m_activeId);
    else if (!ids.isEmpty())
        setActiveActivity(ids.first());
}

void ActivityBar::setActiveActivity(const QString &id)
{
    if (!m_bar->itemIds().contains(id))
        return;
    m_activeId = id;
    m_bar->setActiveId(id);
}

QStringList ActivityBar::activityIds() const { return m_bar->itemIds(); }

void ActivityBar::setBadge(const QString &id, int count) { m_bar->setBadge(id, count); }

int ActivityBar::badge(const QString &id) const { return m_bar->badge(id); }

QString ActivityBar::activityAt(const QPoint &globalPos) const { return m_bar->itemAt(globalPos); }

} // namespace nextpilot::plotscope::gui
