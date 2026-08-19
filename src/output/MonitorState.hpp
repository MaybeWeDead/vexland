#pragma once

#include <vector>
#include <memory>
#include "Monitor.hpp"

extern "C" {
#include <xcb/xcb.h>
}

// -----------------------------------------------------------------------
// MonitorState.hpp — реестр мониторов + реальный опрос геометрии через
// XRandR (не заглушка 1920x1080, как было в Workspace.cpp). Тот же
// принцип реестра, что и CWindowState/CWorkspaceState.
// -----------------------------------------------------------------------

class CMonitorState {
  public:
    static CMonitorState* get() {
        static CMonitorState instance;
        return &instance;
    }

    // опрашивает X-сервер через XRandR и (пере)строит список мониторов.
    // Вызывается при старте и на XCB_RANDR_SCREEN_CHANGE_NOTIFY (монитор
    // подключили/отключили/изменили разрешение).
    bool refresh(xcb_connection_t* conn, xcb_window_t root);

    std::vector<PHLMONITOR> all() const {
        return m_monitors;
    }

    PHLMONITOR byName(const std::string& name) const {
        for (const auto& m : m_monitors) {
            if (m && m->name() == name)
                return m;
        }
        return nullptr;
    }

    PHLMONITOR primary() const {
        return m_monitors.empty() ? nullptr : m_monitors.front();
    }

    // находит монитор, чья область содержит точку (x,y) — нужно для
    // определения "на каком мониторе сейчас курсор/фокус"
    PHLMONITOR atPoint(double x, double y) const {
        for (const auto& m : m_monitors) {
            if (!m)
                continue;
            if (x >= m->x() && x < m->x() + m->w() && y >= m->y() && y < m->y() + m->h())
                return m;
        }
        return nullptr;
    }

  private:
    CMonitorState()  = default;
    ~CMonitorState() = default;

    std::vector<PHLMONITOR> m_monitors;
};
