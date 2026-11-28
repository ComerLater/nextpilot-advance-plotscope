#include "UlogFormatParser.h"
#include "Intent.h"
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;

QString UlogFormatParser::id() const
{
    return QStringLiteral("ulog");
}

QString UlogFormatParser::displayName() const
{
    return QStringLiteral("ULog 格式解析器");
}

nextpilot::plotscope::parser::Domain UlogFormatParser::domain() const
{
    return nextpilot::plotscope::parser::Domain::Autopilot;
}

QStringList UlogFormatParser::suffixes() const
{
    return {QStringLiteral("ulg"), QStringLiteral("ulog")};
}

bool UlogFormatParser::acceptsFormat(const QString &format) const
{
    return format == QStringLiteral("ulog") || format == QStringLiteral(".ulg");
}
