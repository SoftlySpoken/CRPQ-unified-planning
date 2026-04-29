#pragma once

#include <cstdint>
#include <type_traits>
#include <functional>

class VarId {
public:
    uint_fast32_t id;

    explicit VarId(uint_fast32_t id) : id(id) { }

    inline bool operator<(const VarId& rhs) const noexcept {
        return id < rhs.id;
    }

    inline bool operator<=(const VarId& rhs) const noexcept {
        return id <= rhs.id;
    }

    inline bool operator==(const VarId& rhs) const noexcept {
        return id == rhs.id;
    }

    inline bool operator!=(const VarId& rhs) const noexcept {
        return id != rhs.id;
    }
};

static_assert(std::is_trivially_copyable<VarId>::value);

// Hash function specialization for VarId
namespace std {
    template <>
    struct hash<VarId> {
        std::size_t operator()(const VarId& var) const noexcept {
            return std::hash<uint_fast32_t>{}(var.id);
        }
    };
}
