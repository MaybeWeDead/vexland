#pragma once

#include <memory>
#include <vector>
#include <optional>
#include "TiledAlgorithm.hpp"

class CSpace; // forward — избегаем циклического инклуда (Space.hpp уже инклудит алгоритм)

// -----------------------------------------------------------------------
// Algorithm.hpp v3 — CDwindleAlgorithm теперь РЕАЛЬНО наследует
// ITiledAlgorithm (раньше был автономным классом с несовпадающими
// именами методов — addTarget вместо newTarget и т.д., это была
// известная несостыковка, исправляем сейчас).
//
// Геометрия для recalculate() берётся не через параметры (интерфейс
// IModeAlgorithm::recalculate принимает только reason), а через
// weak_ptr<CSpace> — алгоритм сам знает своего владельца и запрашивает
// workArea()/gapsIn при пересчёте. Тот же принцип, что m_parent в
// оригинальном IModeAlgorithm, просто напрямую на CSpace, а не через
// промежуточный CAlgorithm-диспетчер, которого у нас нет.
// -----------------------------------------------------------------------

struct SDwindleNode {
    std::weak_ptr<ITarget> target; // валиден только если это лист

    std::unique_ptr<SDwindleNode> left;
    std::unique_ptr<SDwindleNode> right;

    bool splitVertical = true;

    bool isLeaf() const {
        return !left && !right;
    }
};

class CDwindleAlgorithm : public ITiledAlgorithm {
  public:
    // связывает алгоритм с его Space — ОБЯЗАТЕЛЬНО вызывать сразу
    // после создания (см. CSpace::setAlgorithm), иначе recalculate()
    // не будет знать геометрию и молча ничего не сделает
    void attachSpace(std::weak_ptr<CSpace> space) {
        m_space = space;
    }

    void newTarget(std::shared_ptr<ITarget> t) override;
    void removeTarget(std::shared_ptr<ITarget> t) override;
    void movedTarget(std::shared_ptr<ITarget> t, std::optional<SVec2> focalPoint = std::nullopt) override;
    void swapTargets(std::shared_ptr<ITarget> a, std::shared_ptr<ITarget> b) override;
    void resizeTarget(const SVec2& delta, std::shared_ptr<ITarget> target) override;
    void moveTargetInDirection(std::shared_ptr<ITarget> t, eDirection dir, bool silent) override;
    void recalculate(eRecalculateReason reason = RECALCULATE_REASON_UNKNOWN) override;

    std::shared_ptr<ITarget> getNextCandidate(std::shared_ptr<ITarget> old) override;

    std::optional<std::string> layoutName() const override {
        return "dwindle";
    }

    bool empty() const {
        return !m_root;
    }

    size_t tiledTargets() const {
        return m_leafCount;
    }

  private:
    std::weak_ptr<CSpace>         m_space;
    std::unique_ptr<SDwindleNode> m_root;
    size_t                        m_leafCount = 0;

    void          recalcNode(SDwindleNode* node, double x, double y, double w, double h, double gapsIn);
    SDwindleNode* findLeaf(SDwindleNode* node, const std::shared_ptr<ITarget>& t, SDwindleNode** parentOut);
};
