#pragma once

#include <optional>
#include <string_view>
#include <memory>
#include "../target/Target.hpp"
#include "../space/Space.hpp"

// -----------------------------------------------------------------------
// ModeAlgorithm.hpp — аналог Layout::IModeAlgorithm. Базовый интерфейс,
// от которого наследуются ITiledAlgorithm/IFloatingAlgorithm. Общие
// операции (add/remove/move/resize/recalculate/swap) объявлены здесь,
// конкретная математика — в наследниках.
//
// Опущено по сравнению с оригиналом: layoutMsg (нет конфиг-системы,
// которая бы слала runtime-команды), getFSHandler (fullscreen — фича
// для отдельного будущего слоя, не блокирует базовый layout).
// -----------------------------------------------------------------------

class CAlgorithm; // forward — диспетчер, владеющий этим mode-алгоритмом

enum eDirection : uint8_t {
    DIRECTION_DEFAULT = 0,
    DIRECTION_LEFT,
    DIRECTION_RIGHT,
    DIRECTION_UP,
    DIRECTION_DOWN,
};

class IModeAlgorithm {
  public:
    virtual ~IModeAlgorithm() = default;

    virtual void newTarget(std::shared_ptr<ITarget> target) = 0;

    virtual void movedTarget(std::shared_ptr<ITarget> target, std::optional<SVec2> focalPoint = std::nullopt) = 0;

    virtual void removeTarget(std::shared_ptr<ITarget> target) = 0;

    virtual void resizeTarget(const SVec2& delta, std::shared_ptr<ITarget> target) = 0;

    virtual void recalculate(eRecalculateReason reason = RECALCULATE_REASON_UNKNOWN) = 0;

    virtual void swapTargets(std::shared_ptr<ITarget> a, std::shared_ptr<ITarget> b) = 0;

    virtual void moveTargetInDirection(std::shared_ptr<ITarget> t, eDirection dir, bool silent) = 0;

    // optional — предсказать размер для нового окна до его реального добавления
    virtual std::optional<SVec2> predictSizeForNewTarget() {
        return std::nullopt;
    }

  protected:
    IModeAlgorithm() = default;

    std::weak_ptr<CAlgorithm> m_parent;

    friend class CAlgorithm;
};
