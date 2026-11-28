#include "WorkbenchAdapters.h"

namespace nextpilot::plotscope::assembly {

// ==================== DatasetAccessAdapter ====================
DatasetAccessAdapter::DatasetAccessAdapter(ds::DatasetRegistry &hub, QObject *parent) : QObject(parent), m_hub(hub) {}

const ds::Dataset *DatasetAccessAdapter::get(ds::DatasetId id) const { return m_hub.get(id); }
QVector<ds::DatasetId> DatasetAccessAdapter::ids() const { return m_hub.ids(); }
int DatasetAccessAdapter::count() const { return m_hub.count(); }
ds::Dataset *DatasetAccessAdapter::getMutable(ds::DatasetId id) { return m_hub.getMutable(id); }
bool DatasetAccessAdapter::remove(ds::DatasetId id) { return m_hub.remove(id); }

foundation::Subscription DatasetAccessAdapter::onDidAddDocument(std::function<void(const QString &id)> l)
{
    auto c =
        connect(&m_hub, &ds::DatasetRegistry::datasetAdded, this, [l](ds::DatasetId id) { l(QString::number(id)); });
    return c; // Subscription = QMetaObject::Connection，调用方 disconnect(c) 退订
}

foundation::Subscription DatasetAccessAdapter::onDidRemoveDocument(std::function<void(const QString &id)> l)
{
    auto c =
        connect(&m_hub, &ds::DatasetRegistry::datasetRemoved, this, [l](ds::DatasetId id) { l(QString::number(id)); });
    return c; // Subscription = QMetaObject::Connection，调用方 disconnect(c) 退订
}

// ==================== SourceAccessAdapter ====================
SourceAccessAdapter::SourceAccessAdapter(ds::SourceRegistry &hub, QObject *parent) : QObject(parent), m_hub(hub) {}

const ds::DataSourceInfo *SourceAccessAdapter::get(int id) const { return m_hub.get(id); }
int SourceAccessAdapter::sourceOfDataset(int datasetId) const { return m_hub.sourceOfDataset(datasetId); }
QVector<int> SourceAccessAdapter::ids() const { return m_hub.ids(); }
bool SourceAccessAdapter::remove(int id, QVector<int> *datasetIds) { return m_hub.remove(id, datasetIds); }
int SourceAccessAdapter::sourceByOrigin(const QString &origin) const { return m_hub.sourceByOrigin(origin); }
bool SourceAccessAdapter::setState(int id, ds::SourceState state, const QString &error)
{
    return m_hub.setState(id, state, error);
}
void SourceAccessAdapter::releaseOrigin(const QString &origin) { m_hub.releaseOrigin(origin); }
bool SourceAccessAdapter::addDataset(int id, int datasetId) { return m_hub.addDataset(id, datasetId); }
int SourceAccessAdapter::add(ds::DataSourceInfo info) { return m_hub.add(info); }
bool SourceAccessAdapter::update(int id, const ds::DataSourceInfo &info) { return m_hub.update(id, info); }
void SourceAccessAdapter::demandOrigin(const QString &origin) { m_hub.demandOrigin(origin); }
int SourceAccessAdapter::count() const { return m_hub.count(); }

foundation::Subscription SourceAccessAdapter::onDidAddSource(std::function<void(const QString &id)> l)
{
    auto c = connect(&m_hub, &ds::SourceRegistry::sourceAdded, this, [l](int id) { l(QString::number(id)); });
    return c; // Subscription = QMetaObject::Connection，调用方 disconnect(c) 退订
}

foundation::Subscription SourceAccessAdapter::onDidChangeSource(std::function<void(const QString &id)> l)
{
    auto c = connect(&m_hub, &ds::SourceRegistry::sourceChanged, this, [l](int id) { l(QString::number(id)); });
    return c; // Subscription = QMetaObject::Connection，调用方 disconnect(c) 退订
}

foundation::Subscription SourceAccessAdapter::onDidRemoveSource(std::function<void(const QString &id)> l)
{
    auto c = connect(&m_hub, &ds::SourceRegistry::sourceRemoved, this, [l](int id) { l(QString::number(id)); });
    return c;
}

// ==================== IngestAdapter ====================
IngestAdapter::IngestAdapter(ctx::AppContext &appCtx, ctx::IngestPipeline &pipeline, QObject *parent)
    : QObject(parent), m_appCtx(appCtx), m_pipe(pipeline)
{
}

foundation::Result<int> IngestAdapter::bindParser(int sourceId, const ds::ParserBinding &binding,
                                                  ds::DatasetId preferred)
{
    return m_pipe.bindParser(sourceId, binding, preferred);
}

qint64 IngestAdapter::feed(int sourceId, const uint8_t *data, qint64 size) { return m_pipe.feed(sourceId, data, size); }

void IngestAdapter::finish(int sourceId) { m_pipe.finish(sourceId); }

QVector<ds::IngestChannel> IngestAdapter::channels(int sourceId) const
{
    QVector<ds::IngestChannel> out;
    for (const auto &pc : m_pipe.channels(sourceId))
    {
        ds::IngestChannel c;
        c.parserId = pc.parserId;
        c.topicFilter = pc.topicFilter;
        c.vtable = pc.vtable;
        c.ctx = pc.ctx;
        c.datasetId = pc.datasetId;
        c.active = pc.active;
        c.error = pc.error;
        out.append(c);
    }
    return out;
}

foundation::Subscription IngestAdapter::onDidStartLoad(std::function<void()> l)
{
    // 加载生命周期在 AppContext 上（DataLoadHandler 进入摄取前发）。装配层桥接，
    // 模块只见 IIngest，不碰宿主（评论 52436915 裁定）。
    auto c = connect(&m_appCtx, &ctx::AppContext::dataLoadStarted, this, [l]() { l(); });
    return c;
}

foundation::Subscription IngestAdapter::onDidFinishLoad(std::function<void(bool ok)> l)
{
    auto c = connect(&m_appCtx, &ctx::AppContext::dataLoadFinished, this, [l](bool ok) { l(ok); });
    return c;
}

foundation::Subscription IngestAdapter::onDidReportProgress(std::function<void(int, const QString &)> l)
{
    auto c = connect(&m_pipe, &ctx::IngestPipeline::progressChanged, this, [l](int, uint64_t done, uint64_t total) {
        // 文案与 baseline DataModule 状态栏一致：total>0 百分比，否则已读字节数。
        const int pct = total > 0 ? static_cast<int>((done * 100) / total) : 0;
        const QString text = total > 0 ? QStringLiteral("加载 %1%").arg(pct) : QStringLiteral("加载 %1 字节").arg(done);
        l(pct, text);
    });
    return c; // Subscription = QMetaObject::Connection，调用方 disconnect(c) 退订
}

// ==================== PlaybackAdapter ====================
bool PlaybackAdapter::load(const QJsonObject &jobJson, foundation::Result<void> *outError)
{
    foundation::Result<void> localErr;
    m_job = intent::Job::fromJson(jobJson, outError ? outError : &localErr);
    m_loaded = (outError ? outError->ok() : localErr.ok());
    return m_loaded;
}

const intent::Intent *PlaybackAdapter::at(int i) const { return &m_job.at(i); }

void PlaybackAdapter::clear()
{
    // Job 无 clear；用空 Job 重建。
    m_job = intent::Job();
    m_loaded = false;
}

} // namespace nextpilot::plotscope::assembly
