#pragma once

#include "ProfileEditor.h"

#include <gs/input/Geometry.h>
#include <gs/input/InputSource.h>
#include <gs/input/Profile.h>

#include <QAbstractListModel>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gs::app {

namespace form {

enum class Widget : std::uint8_t { Box, Slider, Number, Segmented, Select, Switch, Key };
enum class Measure : std::uint8_t {
    Plain,
    PositionX,
    PositionY,
    ExtentX,
    ExtentY,
    Length,
    HeroX,
    HeroY,
    Anchor,
    Choice,
    Flag,
    Pauses,
    Input,
    Places,
};
enum class Unit : std::uint8_t { None, Percent, Pixels, Times, Milliseconds, PerSecond, Degrees };
enum class Shown : std::uint8_t { Always, CursorAim, MouseBound };
enum class Reason : std::uint8_t { Identity, FixedRow, BehaviorPage, Canvas, Deferred, Inapplicable };

struct Choice {
    std::string_view value;
    const char* text = "";
};

struct Field {
    std::string_view key;
    std::string_view path;
    int component = -1;
    Widget widget = Widget::Number;
    Measure measure = Measure::Plain;
    const char* title = "";
    const char* hint = "";
    const char* info = "";
    const char* prefix = "";
    Unit unit = Unit::None;
    double sliderFrom = 0.0;
    double sliderTo = 0.0;
    bool logarithmic = false;
    double step = 1.0;
    int decimals = 0;
    std::span<const Choice> choices;
    const char* special = "";
    Shown shown = Shown::Always;
    int group = 0;
    bool half = false;
};

struct Excluded {
    std::string path;
    Reason reason = Reason::Identity;
};

using Value = std::variant<std::monostate, double, bool, std::string, input::Vec2>;

struct Declared {
    std::string key;
    double minimum = 0.0;
    double maximum = 0.0;
    bool whole = false;
    Value value;
};

struct Context {
    const input::Profile& profile;
    std::size_t index = 0;
    input::CoordinateMapper mapper;
    double width = 0.0;
};

using KeyFieldMaker = std::function<QVariantMap(const Field&, const std::optional<input::Chord>&)>;

[[nodiscard]] std::span<const Field> fieldsOf(const input::ControlKind& kind);
[[nodiscard]] std::vector<Excluded> excludedOf(const input::ControlKind& kind);
[[nodiscard]] std::vector<Declared> declaredOf(const input::Control& control);
[[nodiscard]] const Field* find(const Context& context, std::string_view key);
[[nodiscard]] const Declared* find(const std::vector<Declared>& values, std::string_view key);
[[nodiscard]] bool numeric(const Field& field);
[[nodiscard]] QString text(const char* source);
[[nodiscard]] QVariantList build(const Context& context, const KeyFieldMaker& keyField);
[[nodiscard]] QVariantList behavior(const Context& context);
[[nodiscard]] std::string bindingOf(std::string_view path);

}

class ProfileForm : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(gs::app::ProfileEditor* editor READ editor WRITE setEditor NOTIFY editorChanged FINAL)
    Q_PROPERTY(QString keyId READ keyId WRITE setKeyId NOTIFY keyIdChanged FINAL)
    Q_PROPERTY(bool behavior READ behavior WRITE setBehavior NOTIFY behaviorChanged FINAL)

public:
    enum Role : int { FieldsRole = Qt::UserRole + 1, FirstRole, LastRole, SeparatedRole };

    explicit ProfileForm(QObject* parent = nullptr);

    [[nodiscard]] ProfileEditor* editor() const noexcept { return m_editor; }
    void setEditor(ProfileEditor* editor);
    [[nodiscard]] QString keyId() const { return m_keyId; }
    void setKeyId(const QString& keyId);
    [[nodiscard]] bool behavior() const noexcept { return m_behavior; }
    void setBehavior(bool behavior);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void editorChanged();
    void keyIdChanged();
    void behaviorChanged();

private:
    struct Row {
        QString key;
        QVariantList fields;
        bool first = false;
        bool last = false;
        bool separated = false;
    };

    [[nodiscard]] std::vector<Row> rowsNow() const;
    void reload(bool reset);

    QPointer<ProfileEditor> m_editor;
    QMetaObject::Connection m_watch;
    QString m_keyId;
    bool m_behavior = false;
    std::vector<Row> m_rows;
};

}
