#pragma once

#include "ModeAlgorithm.hpp"
#include <optional>
#include <string>

// -----------------------------------------------------------------------
// TiledAlgorithm.hpp — аналог Layout::ITiledAlgorithm. Добавляет
// getNextCandidate (кого фокусировать после закрытия окна) и опциональное
// имя алгоритма поверх базового IModeAlgorithm.
// -----------------------------------------------------------------------

class ITiledAlgorithm : public IModeAlgorithm {
  public:
    virtual ~ITiledAlgorithm() = default;

    virtual std::shared_ptr<ITarget> getNextCandidate(std::shared_ptr<ITarget> old) = 0;

    virtual std::optional<std::string> layoutName() const {
        return std::nullopt;
    }

  protected:
    ITiledAlgorithm() = default;

    friend class CAlgorithm;
};
