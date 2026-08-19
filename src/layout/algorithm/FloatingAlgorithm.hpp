#pragma once

#include "ModeAlgorithm.hpp"

// -----------------------------------------------------------------------
// FloatingAlgorithm.hpp — аналог Layout::IFloatingAlgorithm. Floating-
// окна не подчиняются тайловой математике — их двигают/ресайзят
// напрямую по дельте либо ставят на конкретный box.
// -----------------------------------------------------------------------

class IFloatingAlgorithm : public IModeAlgorithm {
  public:
    virtual ~IFloatingAlgorithm() = default;

    virtual void moveTarget(const SVec2& delta, std::shared_ptr<ITarget> target) = 0;

    virtual void setTargetGeom(const SBox& geom, std::shared_ptr<ITarget> target) = 0;

    // дефолтная реализация — floating-окна обычно не нужно "пересчитывать"
    // при обычном recalculate (в отличие от tiled), но метод оставлен
    // виртуальным на случай, если конкретная реализация захочет что-то
    // делать (например, удерживать окно внутри границ монитора)
    virtual void recenter(std::shared_ptr<ITarget> t) {
        // база ничего не делает — центрирование специфично для реализации
        (void)t;
    }

    void recalculate(eRecalculateReason reason = RECALCULATE_REASON_UNKNOWN) override {
        // база ничего не делает — floating-окна держат свой box сами,
        // никакого автоматического пересчёта позиции не требуется
        (void)reason;
    }

  protected:
    IFloatingAlgorithm() = default;

    friend class CAlgorithm;
};
