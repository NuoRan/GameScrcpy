#include <QtCore/qstring.h>
#include <QtQml/qqmlcontext.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

#include "../../unit/support/ThreadCpuTime.h"

Q_IMPORT_QML_PLUGIN(GameScrcpy_ThemePlugin)
Q_IMPORT_QML_PLUGIN(GameScrcpy_IconsPlugin)
Q_IMPORT_QML_PLUGIN(GameScrcpy_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(GameScrcpy_BlueprintPlugin)

class ThreadCpu final : public QObject
{
    Q_OBJECT

public:
    ThreadCpu() = default;

    Q_INVOKABLE bool usable() const { return m_clock.usable(); }

    Q_INVOKABLE double microsNow() const { return m_clock.microsNow(); }

private:
    gs::testing::ThreadCpuTime m_clock;
};

class Setup final : public QObject
{
    Q_OBJECT

public:
    Setup() = default;

public slots:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        static ThreadCpu cpu;
        engine->rootContext()->setContextProperty(QStringLiteral("ThreadCpu"), &cpu);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(gs_blueprint, Setup)

#include "BlueprintTests.moc"
