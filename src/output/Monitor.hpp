#pragma once

#include <string>
#include <memory>
#include <cstdint>

// -----------------------------------------------------------------------
// Monitor.hpp — представление физического монитора. Геометрия берётся
// реально через XRandR (не заглушка), см. MonitorState.cpp для опроса.
// Аналог Hyprland CMonitor, сильно упрощённый — без scale/transform/
// VRR/HDR (это всё Wayland output-protocol специфика или продвинутые
// XRandR фичи, которые добавим когда будет реальная потребность).
// -----------------------------------------------------------------------

class CMonitor;
using PHLMONITOR = std::shared_ptr<CMonitor>;
using WPMONITOR   = std::weak_ptr<CMonitor>;

class CMonitor : public std::enable_shared_from_this<CMonitor> {
  public:
    static PHLMONITOR create(uint32_t outputID, uint32_t crtcID, const std::string& name, double x, double y, double w, double h) {
        return std::shared_ptr<CMonitor>(new CMonitor(outputID, crtcID, name, x, y, w, h));
    }

    uint32_t outputID() const {
        return m_outputID;
    }
    uint32_t crtcID() const {
        return m_crtcID;
    }
    const std::string& name() const {
        return m_name;
    }

    double x() const {
        return m_x;
    }
    double y() const {
        return m_y;
    }
    double w() const {
        return m_w;
    }
    double h() const {
        return m_h;
    }

    void setGeometry(double x, double y, double w, double h) {
        m_x = x;
        m_y = y;
        m_w = w;
        m_h = h;
    }

    // ID активного workspace на этом мониторе — по значению, не по
    // прямому shared_ptr, тот же принцип "не убить WM, если workspace
    // исчезнет неожиданно"; резолвится через CWorkspaceState::byID()
    int activeWorkspaceID() const {
        return m_activeWorkspaceID;
    }
    void setActiveWorkspaceID(int id) {
        m_activeWorkspaceID = id;
    }

    bool enabled() const {
        return m_enabled;
    }
    void setEnabled(bool x) {
        m_enabled = x;
    }

  private:
    CMonitor(uint32_t outputID, uint32_t crtcID, std::string name, double x, double y, double w, double h) :
        m_outputID(outputID), m_crtcID(crtcID), m_name(std::move(name)), m_x(x), m_y(y), m_w(w), m_h(h) {}

    uint32_t    m_outputID;
    uint32_t    m_crtcID;
    std::string m_name;

    double m_x, m_y, m_w, m_h;

    int  m_activeWorkspaceID = -1;
    bool m_enabled            = true;
};
