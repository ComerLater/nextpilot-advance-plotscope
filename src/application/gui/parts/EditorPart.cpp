#include "EditorPart.h"

// ADS（vendored）：用相对路径引入，避开与壳目录同名文件的歧义。
#include "../../../3rdparty/ads/src/DockAreaWidget.h"
#include "../../../3rdparty/ads/src/DockManager.h"
#include "../../../3rdparty/ads/src/DockWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

namespace nextpilot::plotscope::gui {

namespace {
const QString kEditorDockObjectName = QStringLiteral("ps.dock.editor");
const QString kPlaceholderText = QStringLiteral("暂无打开的视图");
constexpr int kEditorMinWidth = 320;
constexpr int kEditorMinHeight = 300;
} // namespace

EditorPart::EditorPart() : Part(QStringLiteral("workbench.editor")) {}

void EditorPart::attach(ads::CDockManager *dock)
{
    if (dock)
        m_dock = dock;
}

void EditorPart::install()
{
    if (!m_dock || m_installed)
        return;

    // 内容此刻可能还没到（画布模块可能尚未 attach）：先用空态占位占住中央位置，
    // 保证后续所有侧边 dock 都排在它之后。内容到达后由 setContent() 取走并销毁。
    QWidget *content = m_content;
    if (!content)
    {
        auto *placeholder = new QLabel(kPlaceholderText);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setObjectName(QStringLiteral("ps.editor.placeholder"));
        auto *placeholderLayout = new QHBoxLayout(placeholder);
        placeholderLayout->setContentsMargins(24, 24, 24, 24);
        content = placeholder;
    }

    m_editorDock = new ads::CDockWidget(m_dock, QStringLiteral("主视图"));
    m_editorDock->setObjectName(kEditorDockObjectName);
    content->setMinimumWidth(kEditorMinWidth);
    // 编辑器区高度下限：底部面板可拖高，但不能把画布挤成一条缝。
    content->setMinimumHeight(kEditorMinHeight);
    m_editorDock->setWidget(content);
    m_editorDock->setMinimumWidth(kEditorMinWidth);
    m_editorDock->setMinimumHeight(kEditorMinHeight);
    m_editorDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_editorDock->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    m_editorDock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);

    // 用 addDockWidget(CenterDockWidgetArea) 而不是 setCentralWidget()：后者会把
    // 中央控件排除在普通 dock 体系之外，与"画布区是一个可停靠区域"的语义不符。
    if (auto *area = m_dock->addDockWidget(ads::CenterDockWidgetArea, m_editorDock))
    {
        area->setDockAreaFlag(ads::CDockAreaWidget::HideSingleWidgetTitleBar, true);
        // Editor 区不接受任何视图停靠；中央页签自身也不可移动。
        area->setAllowedAreas(ads::NoDockWidgetArea);
    }
    m_installed = true;
}

ads::CDockWidget *EditorPart::setContent(QWidget *content, const QString &title, const QIcon &icon)
{
    if (!content || !m_dock)
        return nullptr;
    install();
    if (!m_editorDock)
        return nullptr;

    if (m_content)
    {
        // 已有真实内容（画布已就位）：后来者并成页签，与画布平级。
        auto *dock = new ads::CDockWidget(m_dock, title.isEmpty() ? QStringLiteral("视图") : title);
        dock->setObjectName(QStringLiteral("ps.dock.central.tab.%1").arg(m_seq++));
        if (!icon.isNull())
            dock->setIcon(icon);
        content->setMinimumWidth(kEditorMinWidth);
        dock->setWidget(content);
        dock->setMinimumWidth(kEditorMinWidth);
        // 页签可关闭，但不可拖出编辑器区：编辑器是壳层固定主体，不是可移动 dock。
        dock->setFeature(ads::CDockWidget::DockWidgetClosable, true);
        dock->setFeature(ads::CDockWidget::DockWidgetMovable, false);
        dock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
        if (ads::CDockAreaWidget *area = m_editorDock->dockAreaWidget())
            m_dock->addDockWidgetTabToArea(dock, area);
        return dock;
    }

    // 首个内容：把占位换成本体。
    if (QWidget *placeholder = m_editorDock->takeWidget())
        placeholder->deleteLater();
    m_editorDock->setWidget(content);
    m_content = content;
    return m_editorDock;
}

QWidget *EditorPart::createContent(QWidget *parent)
{
    Q_UNUSED(parent);
    return m_content;
}

QList<QWidget *> EditorPart::widgets() const
{
    if (!m_editorDock || !m_editorDock->dockAreaWidget())
        return {};
    QList<QWidget *> out;
    for (ads::CDockWidget *dw : m_editorDock->dockAreaWidget()->dockWidgets())
        out.append(dw->widget());
    return out;
}

QStringList EditorPart::titles() const
{
    if (!m_editorDock || !m_editorDock->dockAreaWidget())
        return {};
    QStringList out;
    for (ads::CDockWidget *dw : m_editorDock->dockAreaWidget()->dockWidgets())
        out.append(dw->windowTitle());
    return out;
}

int EditorPart::currentIndex() const
{
    if (!m_editorDock || !m_editorDock->dockAreaWidget())
        return -1;
    return m_editorDock->dockAreaWidget()->currentIndex();
}

bool EditorPart::activateIndex(int index)
{
    if (!m_editorDock || !m_editorDock->dockAreaWidget())
        return false;
    ads::CDockAreaWidget *area = m_editorDock->dockAreaWidget();
    if (index < 0 || index >= area->dockWidgets().size())
        return false;
    area->setCurrentIndex(index);
    return true;
}

QString EditorPart::currentTitle() const
{
    if (!m_editorDock || !m_editorDock->dockAreaWidget())
        return QString();
    ads::CDockWidget *current = m_editorDock->dockAreaWidget()->currentDockWidget();
    return current ? current->windowTitle() : QString();
}

bool EditorPart::closeCurrent()
{
    if (!m_editorDock || !m_editorDock->dockAreaWidget())
        return false;
    ads::CDockWidget *current = m_editorDock->dockAreaWidget()->currentDockWidget();
    if (!current || current == m_editorDock)
        return false;
    current->closeDockWidget();
    return true;
}

} // namespace nextpilot::plotscope::gui
