#pragma once

#include "Target.hpp"
#include "../../desktop/view/Window.hpp"

// -----------------------------------------------------------------------
// WindowTarget.hpp — аналог Layout::CWindowTarget. Оборачивает PHLWINDOW
// в ITarget-интерфейс, транслируя вызовы в реальные методы Window.hpp
// (setGoalGeometry, minSize/maxSize и т.д.).
// -----------------------------------------------------------------------

class CWindowTarget : public ITarget {
  public:
    static std::shared_ptr<ITarget> create(PHLWINDOW w);
    virtual ~CWindowTarget() = default;

    eTargetType type() override {
        return TARGET_TYPE_WINDOW;
    }

    void setPositionGlobal(const STargetBox& box, uint8_t flags = TARGET_UPDATE_NONE) override;
    void assignToSpace(std::shared_ptr<CSpace> space, std::optional<SVec2> focalPoint = std::nullopt) override;
    PHLWINDOW window() const override;

    bool                                                floating() override;
    void                                                setFloating(bool x) override;
    std::expected<SGeometryRequested, eGeometryFailure> desiredGeometry() override;
    std::optional<SVec2>                                minSize() override;
    std::optional<SVec2>                                maxSize() override;
    void                                                damageEntire() override;
    void                                                warpPositionSize() override;
    void                                                onUpdateSpace() override;

  private:
    explicit CWindowTarget(PHLWINDOW w);

    SVec2 clampSizeForDesired(const SVec2& size) const;
    void  updatePos(uint8_t flags = TARGET_UPDATE_NONE);

    WPWINDOW m_window;
};
