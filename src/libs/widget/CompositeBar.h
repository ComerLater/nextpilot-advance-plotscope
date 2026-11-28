#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <QWidget>

class QBoxLayout;
class QToolButton;

namespace nextpilot::plotscope::gui {

// 复合条（VSCode `src/vs/base/browser/ui/compositebar/compositebar.ts`）。
//
// 一列/一行可勾选的图标条目：条目 id、选中态、徽标都归这里；
// 谁提供条目（Activity Bar、视图容器）由使用方决定，本类不认识业务。
class CompositeBar : public QWidget
{
    Q_OBJECT
public:
    explicit CompositeBar(Qt::Orientation orientation = Qt::Vertical, QWidget *parent = nullptr);

    // 追加一个条目；重复 id 覆盖原条目。objectName 供 findChild 按名定位，
    // 空 = 用 `composite.<id>`（使用方有历史命名口径时显式传入）。
    void addItem(const QString &id, const QString &label, const QString &icon, const QString &objectName = QString());
    void clearItems();

    QStringList itemIds() const { return m_ids; }
    int itemCount() const { return int(m_ids.size()); }
    QString activeId() const { return m_activeId; }
    // 选中态：id 不存在时为空操作（首帧未建条目时不产生假选中）。
    void setActiveId(const QString &id);

    void setBadge(const QString &id, int count);
    int badge(const QString &id) const;

    // 全局坐标命中的条目 id（拖放落点判定）；未命中返回空串。
    QString itemAt(const QPoint &globalPos) const;

signals:
    void activated(const QString &id);

private:
    QBoxLayout *m_layout = nullptr;
    QHash<QString, QToolButton *> m_items;
    QStringList m_ids;
    QString m_activeId;
};

} // namespace nextpilot::plotscope::gui
