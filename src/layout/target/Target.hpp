#pragma once

#include "../../desktop/view/Window.hpp"
#include <expected>
#include <optional>
#include <memory>
#include <cstdint>

// -----------------------------------------------------------------------
// Target.hpp — аналог Layout::ITarget из Hyprland. Абстракция для всего,
// что можно разложить (окно или группа окон). Сохранена сигнатура
// методов из оригинала (виртуальный интерфейс, STargetBox, eTargetType,
// pseudo-size для псевдотайлинга) — только Vector2D/CBox используют
// наши собственные типы, а window() возвращает PHLWINDOW из нашего
// X11 Window.hpp вместо Hyprland-совместимого.
// -----------------------------------------------------------------------

class CSpace; // forward — Space.hpp подключает нас, мы его не подключаем (избегаем цикла)

enum eTargetType : uint8_t {
    TARGET_TYPE_WINDOW = 0,
    TARGET_TYPE_GROUP,
};

enum eGeometryFailure : uint8_t {
    GEOMETRY_NO_DESIRED      = 0,
    GEOMETRY_INVALID_DESIRED = 1,
};

enum eTargetUpdateFlags : uint8_t {
    TARGET_UPDATE_NONE                = 0,
    TARGET_UPDATE_NO_CLIENT_CONFIGURE = 1 << 0,
};

struct SVec2 {
    double x = 0, y = 0;
};

struct SBox {
    double x = 0, y = 0, w = 0, h = 0;
};

struct SGeometryRequested {
    SVec2                size;
    std::optional<SVec2> pos;
};

struct STargetBox {
    SBox logicalBox;
    SBox visualBox;
};

class ITarget : public std::enable_shared_from_this<ITarget> {
  public:
    virtual ~ITarget() = default;

    virtual eTargetType type() = 0;

    // геометрия — позиция внутри своего Space (аналог position())
    virtual void setPositionGlobal(const STargetBox& box, uint8_t flags = TARGET_UPDATE_NONE);
    void         setPositionGlobal(const SBox& box, uint8_t flags = TARGET_UPDATE_NONE);
    virtual SBox position() const;

    virtual void                     assignToSpace(std::shared_ptr<CSpace> space, std::optional<SVec2> focalPoint = std::nullopt);
    virtual std::shared_ptr<CSpace>  space() const;

    virtual PHLWINDOW window() const = 0;

    virtual void recalc();

    virtual bool wasTiling() const;
    virtual void setWasTiling(bool x);

    virtual void  rememberFloatingSize(const SVec2& size);
    virtual SVec2 lastFloatingSize() const;

    virtual void  setPseudo(bool x);
    virtual bool  isPseudo() const;
    virtual void  setPseudoSize(const SVec2& size);
    virtual SVec2 pseudoSize();

    virtual void swap(std::shared_ptr<ITarget> b);

    // чисто виртуальные — реализуются в конкретных подклассах
    virtual bool                                                floating()          = 0;
    virtual void                                                setFloating(bool x) = 0;
    virtual std::expected<SGeometryRequested, eGeometryFailure> desiredGeometry()   = 0;
    virtual std::optional<SVec2>                                minSize()           = 0;
    virtual std::optional<SVec2>                                maxSize()           = 0;
    virtual void                                                damageEntire()      = 0;
    virtual void                                                warpPositionSize()  = 0;
    virtual void                                                onUpdateSpace()     = 0;

  protected:
    ITarget() = default;

    STargetBox               m_box;
    std::shared_ptr<CSpace>  m_space;
    SVec2                    m_floatingSize;
    bool                     m_pseudo     = false;
    bool                     m_ghostSpace = false;
    SVec2                    m_pseudoSize = {1280, 720};
    bool                     m_wasTiling  = false;
};
