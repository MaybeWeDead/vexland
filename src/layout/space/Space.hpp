#pragma once

#include <vector>
#include <memory>
#include <optional>
#include <string_view>
#include "../target/Target.hpp"

// -----------------------------------------------------------------------
// Space.hpp — аналог Layout::CSpace. Держит список targets одного
// workspace + указатель на алгоритм тайлинга. Сам не тайлит — всё
// реальное поведение (resize/move/swap/recalculate) делегируется в
// CDwindleAlgorithm через m_algorithm, ровно как в оригинале.
// -----------------------------------------------------------------------

class CDwindleAlgorithm; // forward, чтобы не тянуть Algorithm.hpp сюда напрямую

enum eRecalculateReason : uint8_t {
    RECALCULATE_REASON_UNKNOWN,
    RECALCULATE_REASON_WORKSPACE_CHANGE,
    RECALCULATE_REASON_INVALIDATE_MONITOR_GEOMETRIES,
};

class CSpace : public std::enable_shared_from_this<CSpace> {
  public:
    static std::shared_ptr<CSpace> create(int workspaceID, double monX, double monY, double monW, double monH);
    ~CSpace() = default;

    void add(std::shared_ptr<ITarget> t);
    void remove(std::shared_ptr<ITarget> t);
    void move(std::shared_ptr<ITarget> t, std::optional<SVec2> focalPoint = std::nullopt);
    void swap(std::shared_ptr<ITarget> a, std::shared_ptr<ITarget> b);

    void setAlgorithm(std::shared_ptr<CDwindleAlgorithm> algo);
    void recheckWorkArea();
    void recalculate(eRecalculateReason reason = RECALCULATE_REASON_UNKNOWN);

    void toggleTargetFloating(std::shared_ptr<ITarget> t);
    void resizeTarget(const SVec2& delta, std::shared_ptr<ITarget> target);
    void moveTarget(const SVec2& delta, std::shared_ptr<ITarget> target);
    void setTargetGeom(const SBox& box, std::shared_ptr<ITarget> target);

    const SBox& workArea() const {
        return m_workArea;
    }

    // Аналог MONITOR->logicalBox() из Hyprland setTargetSizeAndPosition() —
    // полная геометрия монитора БЕЗ вычета gaps_out (в отличие от workArea()).
    // Нужно fullscreen-пути в CDwindleAlgorithm::recalcNode(), чтобы окно
    // растягивалось на весь монитор, а не на рабочую область с отступами.
    double monitorX() const {
        return m_monitorX;
    }
    double monitorY() const {
        return m_monitorY;
    }
    double monitorW() const {
        return m_monitorW;
    }
    double monitorH() const {
        return m_monitorH;
    }

    int workspaceID() const {
        return m_workspaceID;
    }

    void setMonitorGeometry(double x, double y, double w, double h) {
        m_monitorX = x;
        m_monitorY = y;
        m_monitorW = w;
        m_monitorH = h;
        recheckWorkArea();
    }

    void setGapsOut(double gaps) {
        m_gapsOut = gaps;
        recheckWorkArea();
    }

    std::shared_ptr<CDwindleAlgorithm> algorithm() const {
        return m_algorithm;
    }

    const std::vector<std::weak_ptr<ITarget>>& targets() const {
        return m_targets;
    }

  private:
    explicit CSpace(int workspaceID, double monX, double monY, double monW, double monH);

    int    m_workspaceID;
    double m_monitorX, m_monitorY, m_monitorW, m_monitorH;
    double m_gapsOut = 10;

    std::vector<std::weak_ptr<ITarget>> m_targets;
    std::shared_ptr<CDwindleAlgorithm>  m_algorithm;

    SBox m_workArea;
};
