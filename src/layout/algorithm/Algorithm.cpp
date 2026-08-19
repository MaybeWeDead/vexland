#include "Algorithm.hpp"
#include "../space/Space.hpp"
#include "../../config/ConfigManager_v2.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

// -----------------------------------------------------------------------
// Algorithm.cpp v3 — newTarget/removeTarget/swapTargets логика та же,
// что и в предыдущей версии (уже протестирована через ASan на
// collapse-сценарии), плюс новые методы, требуемые контрактом
// ITiledAlgorithm: resizeTarget, moveTargetInDirection, getNextCandidate.
// -----------------------------------------------------------------------

void CDwindleAlgorithm::newTarget(std::shared_ptr<ITarget> t) {
    if (!t)
        return;

    auto newLeaf    = std::make_unique<SDwindleNode>();
    newLeaf->target = t;

    if (!m_root) {
        m_root = std::move(newLeaf);
        m_leafCount++;
        return;
    }

    SDwindleNode* cur = m_root.get();
    while (!cur->isLeaf())
        cur = cur->right.get();

    auto oldLeaf    = std::make_unique<SDwindleNode>();
    oldLeaf->target = cur->target;

    cur->target.reset();
    cur->left           = std::move(oldLeaf);
    cur->right           = std::move(newLeaf);
    cur->splitVertical    = true;

    m_leafCount++;
}

void CDwindleAlgorithm::movedTarget(std::shared_ptr<ITarget> t, std::optional<SVec2> focalPoint) {
    (void)focalPoint; // TODO: вставлять рядом с focalPoint, не в конец дерева — нужна
                      // геометрическая логика "найти лист под точкой", пока используем
                      // то же поведение что и newTarget
    newTarget(t);
}

SDwindleNode* CDwindleAlgorithm::findLeaf(SDwindleNode* node, const std::shared_ptr<ITarget>& t, SDwindleNode** parentOut) {
    if (!node)
        return nullptr;

    if (node->isLeaf()) {
        if (node->target.lock() == t)
            return node;
        return nullptr;
    }

    if (auto found = findLeaf(node->left.get(), t, parentOut)) {
        if (parentOut && !*parentOut)
            *parentOut = node;
        return found;
    }
    if (auto found = findLeaf(node->right.get(), t, parentOut)) {
        if (parentOut && !*parentOut)
            *parentOut = node;
        return found;
    }

    return nullptr;
}

void CDwindleAlgorithm::removeTarget(std::shared_ptr<ITarget> t) {
    if (!m_root || !t)
        return;

    if (m_root->isLeaf()) {
        if (m_root->target.lock() == t) {
            m_root.reset();
            m_leafCount--;
        }
        return;
    }

    SDwindleNode* parent = nullptr;
    SDwindleNode* leaf   = findLeaf(m_root.get(), t, &parent);

    if (!leaf || !parent)
        return;

    SDwindleNode* siblingRaw = (parent->left.get() == leaf) ? parent->right.get() : parent->left.get();

    if (!siblingRaw) {
        m_leafCount--;
        return;
    }

    SDwindleNode movedSibling = std::move(*siblingRaw);
    *parent                    = std::move(movedSibling);

    m_leafCount--;
}

void CDwindleAlgorithm::swapTargets(std::shared_ptr<ITarget> a, std::shared_ptr<ITarget> b) {
    if (!a || !b)
        return;

    SDwindleNode* parentA = nullptr;
    SDwindleNode* leafA   = findLeaf(m_root.get(), a, &parentA);

    SDwindleNode* parentB = nullptr;
    SDwindleNode* leafB   = findLeaf(m_root.get(), b, &parentB);

    if (leafA)
        leafA->target = b;
    if (leafB)
        leafB->target = a;
}

// Аналог resizeTarget из ITiledAlgorithm. В настоящем dwindle это
// означало бы двигать разделительную линию между соседними тайлами
// (изменяя пропорцию сплита в родительском узле). Пока не реализовано
// по-настоящему — требует хранить пропорцию сплита в узле (сейчас
// всегда 50/50, см. recalcNode) и интерактивного drag tracking,
// которого у нас пока нет. Оставлено пустым намеренно, не крашится.
void CDwindleAlgorithm::resizeTarget(const SVec2& delta, std::shared_ptr<ITarget> target) {
    (void)delta;
    (void)target;
    // TODO: реализовать после того как SDwindleNode будет хранить
    // splitRatio (сейчас всегда 0.5) и появится InputManager для
    // interactive resize через мышь
}

// Аналог moveTargetInDirection — навигация фокуса по дереву в
// направлении (left/right/up/down). Требует геометрического поиска
// "какой лист ближе всего в этом направлении", которого пока нет.
// TODO реализовать когда появится необходимость (keybind типа
// "focus window in direction").
void CDwindleAlgorithm::moveTargetInDirection(std::shared_ptr<ITarget> t, eDirection dir, bool silent) {
    (void)t;
    (void)dir;
    (void)silent;
    // TODO
}

// Простая версия getNextCandidate — возвращает первый попавшийся
// оставшийся лист дерева (не old). В оригинале тут более умная
// логика (соседний по дереву элемент), но для базового "куда
// перевести фокус после закрытия окна" достаточно и этого.
std::shared_ptr<ITarget> CDwindleAlgorithm::getNextCandidate(std::shared_ptr<ITarget> old) {
    if (!m_root)
        return nullptr;

    std::shared_ptr<ITarget> result;

    std::function<void(SDwindleNode*)> walk = [&](SDwindleNode* node) {
        if (!node || result)
            return;

        if (node->isLeaf()) {
            auto t = node->target.lock();
            if (t && t != old)
                result = t;
            return;
        }

        walk(node->left.get());
        walk(node->right.get());
    };

    walk(m_root.get());
    return result;
}

void CDwindleAlgorithm::recalcNode(SDwindleNode* node, double x, double y, double w, double h, double gapsIn) {
    if (!node)
        return;

    if (node->isLeaf()) {
        if (auto t = node->target.lock()) {
            const double gx = x + gapsIn;
            const double gy = y + gapsIn;
            const double gw = std::max(1.0, w - 2 * gapsIn);
            const double gh = std::max(1.0, h - 2 * gapsIn);

            STargetBox box;
            box.logicalBox = SBox{gx, gy, gw, gh};
            t->setPositionGlobal(box);
        }
        return;
    }

    const bool splitVertical = w >= h;

    if (splitVertical) {
        const double halfW = w / 2.0;
        recalcNode(node->left.get(), x, y, halfW, h, gapsIn);
        recalcNode(node->right.get(), x + halfW, y, w - halfW, h, gapsIn);
    } else {
        const double halfH = h / 2.0;
        recalcNode(node->left.get(), x, y, w, halfH, gapsIn);
        recalcNode(node->right.get(), x, y + halfH, w, h - halfH, gapsIn);
    }
}

// Ключевое отличие от предыдущей версии: геометрия берётся не из
// параметров функции (сигнатура интерфейса их не даёт), а из
// привязанного m_space — вызывается CSpace::recalculate() уже ПОСЛЕ
// того как Space пересчитал свою workArea, так что данные всегда
// свежие на момент вызова.
void CDwindleAlgorithm::recalculate(eRecalculateReason reason) {
    (void)reason; // все причины пересчёта обрабатываются одинаково — нет
                  // per-reason оптимизаций (hard/soft recalculate), которые
                  // были бы нужны только при дорогом пересчёте (например,
                  // сложных decoration-эффектах, которых у нас пока нет)

    if (!m_root)
        return;

    auto space = m_space.lock();
    if (!space)
        return; // алгоритм не привязан к Space (attachSpace не вызван) — молча выходим

    const auto& area = space->workArea();

    double gapsIn = 5.0;
    if (auto* cfg = CConfigManager::get())
        gapsIn = cfg->getFloat("general.gaps_in", 5.0);

    recalcNode(m_root.get(), area.x, area.y, area.w, area.h, gapsIn);
}
