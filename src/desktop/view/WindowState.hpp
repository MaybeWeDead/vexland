#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include "Window.hpp"

// -----------------------------------------------------------------------
// WindowState — центральный реестр всех живых окон. Единственное место,
// где хранятся настоящие shared_ptr<CWindow>. Всё остальное (group,
// swallow, layout) обращается к окнам ТОЛЬКО через ID + этот реестр,
// никогда не хранит долгоживущий shared_ptr напрямую сверх момента
// использования — это и есть механизм "не убить возможность WM
// работать", если окно исчезло не так, как ожидалось: lookup просто
// вернёт nullptr, а не оставит висящий указатель.
// -----------------------------------------------------------------------

class CWindowState {
  public:
    static CWindowState* get() {
        static CWindowState instance;
        return &instance;
    }

    // регистрируем новое окно (вызывается из CWindow::create)
    void add(PHLWINDOW w) {
        if (!w)
            return;
        m_windows.push_back(w);
        m_byID[w->stableID()] = w;
        m_byXWin[w->getX11Window()] = w;
    }

    // убираем окно из реестра (вызывается на DestroyNotify)
    void remove(uint64_t stableID) {
        auto it = m_byID.find(stableID);
        if (it == m_byID.end())
            return;

        if (auto w = it->second.lock())
            m_byXWin.erase(w->getX11Window());

        m_byID.erase(it);

        std::erase_if(m_windows, [stableID](const PHLWINDOW& w) {
            return !w || w->stableID() == stableID;
        });
    }

    // безопасный lookup по ID — возвращает nullptr, если окна больше нет
    PHLWINDOW byID(uint64_t stableID) const {
        auto it = m_byID.find(stableID);
        if (it == m_byID.end())
            return nullptr;
        return it->second.lock(); // .lock() сам вернёт nullptr, если объект умер
    }

    // безопасный lookup по X11 window handle — нужен в event loop,
    // когда прилетает XCB event с конкретным xcb_window_t
    PHLWINDOW byXWindow(xcb_window_t xw) const {
        auto it = m_byXWin.find(xw);
        if (it == m_byXWin.end())
            return nullptr;
        return it->second.lock();
    }

    // все живые окна (для layout/iteration). Возвращает копию списка
    // валидных shared_ptr — так вызывающий код не держит устаревшие
    // weak_ptr и не спотыкается о .lock() на каждой итерации.
    std::vector<PHLWINDOW> all() const {
        std::vector<PHLWINDOW> result;
        result.reserve(m_windows.size());
        for (const auto& w : m_windows) {
            if (w)
                result.push_back(w);
        }
        return result;
    }

    size_t count() const {
        return m_windows.size();
    }

  private:
    CWindowState()  = default;
    ~CWindowState() = default;

    // основной список — держит РЕАЛЬНЫЕ shared_ptr, это единственное
    // место в программе, где окно гарантированно живо, пока оно тут
    std::vector<PHLWINDOW> m_windows;

    // индексы для быстрого lookup — только weak_ptr, чтобы не создавать
    // вторых "владельцев" объекта и не мешать нормальному удалению
    std::unordered_map<uint64_t, WPWINDOW>     m_byID;
    std::unordered_map<xcb_window_t, WPWINDOW> m_byXWin;
};
