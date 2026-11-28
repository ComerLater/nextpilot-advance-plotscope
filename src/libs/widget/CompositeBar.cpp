#include "CompositeBar.h"

#include <QBoxLayout>
#include <QFile>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QToolButton>

namespace {
QIcon compositeIcon(const QString &name)
{
    if (name.isEmpty())
        return QIcon();
    const QString path = QStringLiteral(":/icons/%1.svg").arg(name);
    return QFile::exists(path) ? QIcon(path) : QIcon();
}

// 条目的按钮本体：图标 + 选中态 + 右上角徽标（VSCode `ActivityAction` 的呈现面）。
class CompositeBarItem final : public QToolButton
{
public:
    CompositeBarItem(const QString &id, const QString &label, const QString &icon, QWidget *parent)
        : QToolButton(parent)
    {
        setObjectName(QStringLiteral("composite.") + id);
        setToolTip(label);
        setAccessibleName(label);
        setIcon(compositeIcon(icon));
        setIconSize(QSize(20, 20));
        setFixedSize(40, 40);
        setToolButtonStyle(Qt::ToolButtonIconOnly);
        setCheckable(true);
        setAutoRaise(true);
    }

    void setBadge(int count)
    {
        if (m_badge == count)
            return;
        m_badge = qMax(0, count);
        update();
    }
    int badge() const { return m_badge; }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QToolButton::paintEvent(event);
        if (m_badge <= 0)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QString text = m_badge > 99 ? QStringLiteral("99+") : QString::number(m_badge);
        QFont font = painter.font();
        font.setPixelSize(8);
        font.setBold(true);
        painter.setFont(font);
        const int textWidth = QFontMetrics(font).horizontalAdvance(text);
        const int badgeWidth = qMax(12, textWidth + 5);
        const int badgeHeight = 12;
        const QRect rect(width() - badgeWidth - 1, 1, badgeWidth, badgeHeight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xE5, 0x14, 0x00));
        painter.drawRoundedRect(rect, 5, 5);
        painter.setPen(Qt::white);
        painter.drawText(rect, Qt::AlignCenter, text);
    }

private:
    int m_badge = 0;
};
} // namespace

namespace nextpilot::plotscope::gui {

CompositeBar::CompositeBar(Qt::Orientation orientation, QWidget *parent) : QWidget(parent)
{
    m_layout = new QBoxLayout(orientation == Qt::Vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight, this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);
    m_layout->addStretch(1);
}

void CompositeBar::addItem(const QString &id, const QString &label, const QString &icon, const QString &objectName)
{
    if (id.isEmpty())
        return;
    if (auto *existing = m_items.value(id, nullptr))
    {
        existing->setToolTip(label);
        existing->setAccessibleName(label);
        existing->setIcon(compositeIcon(icon));
        return;
    }
    auto *item = new CompositeBarItem(id, label, icon, this);
    if (!objectName.isEmpty())
        item->setObjectName(objectName);
    item->setChecked(id == m_activeId);
    connect(item, &QToolButton::clicked, this, [this, id]() {
        setActiveId(id);
        Q_EMIT activated(id);
    });
    // 条目插在末尾 stretch 之前：stretch 恒为最后一项。
    m_layout->insertWidget(m_layout->count() - 1, item, 0, Qt::AlignHCenter);
    m_items.insert(id, item);
    m_ids << id;
}

void CompositeBar::clearItems()
{
    for (auto *item : m_items)
    {
        m_layout->removeWidget(item);
        item->deleteLater();
    }
    m_items.clear();
    m_ids.clear();
    m_activeId.clear();
}

void CompositeBar::setActiveId(const QString &id)
{
    if (!m_items.contains(id))
        return;
    m_activeId = id;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it)
        it.value()->setChecked(it.key() == id);
}

void CompositeBar::setBadge(const QString &id, int count)
{
    if (auto *item = static_cast<CompositeBarItem *>(m_items.value(id, nullptr)))
        item->setBadge(count);
}

int CompositeBar::badge(const QString &id) const
{
    auto *item = static_cast<CompositeBarItem *>(m_items.value(id, nullptr));
    return item ? item->badge() : 0;
}

QString CompositeBar::itemAt(const QPoint &globalPos) const
{
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it)
        if (it.value()->rect().contains(it.value()->mapFromGlobal(globalPos)))
            return it.key();
    return QString();
}

} // namespace nextpilot::plotscope::gui
