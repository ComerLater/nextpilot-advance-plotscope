#include "StatusbarPart.h"

#include <QStatusBar>
#include <QWidget>

namespace nextpilot::plotscope::gui {

StatusbarPart::StatusbarPart() : Part(QStringLiteral("workbench.statusbar")) {}

void StatusbarPart::attach(QStatusBar *bar)
{
    if (bar)
        m_bar = bar;
}

bool StatusbarPart::visible() const { return m_bar && m_bar->isVisible(); }

QRect StatusbarPart::rectIn(QWidget *window) const
{
    if (!m_bar || !window)
        return QRect();
    return QRect(m_bar->mapTo(window, QPoint(0, 0)), m_bar->size());
}

QWidget *StatusbarPart::createContent(QWidget *parent)
{
    Q_UNUSED(parent);
    return m_bar;
}

void StatusbarPart::showMessage(const QString &text, int timeoutMs)
{
    if (m_bar)
        m_bar->showMessage(text, timeoutMs);
}

void StatusbarPart::clearMessage()
{
    if (m_bar)
        m_bar->clearMessage();
}

void StatusbarPart::addWidget(QWidget *widget, int stretch)
{
    if (m_bar && widget)
        m_bar->addWidget(widget, stretch);
}

void StatusbarPart::addPermanentWidget(QWidget *widget, int stretch)
{
    if (m_bar && widget)
        m_bar->addPermanentWidget(widget, stretch);
}

void StatusbarPart::removeWidget(QWidget *widget)
{
    if (m_bar && widget)
        m_bar->removeWidget(widget);
}

} // namespace nextpilot::plotscope::gui
