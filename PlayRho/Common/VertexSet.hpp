/*
 * Copyright (c) 2017 Louis Langholtz https://github.com/louis-langholtz/PlayRho
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef PLAYRHO_VERTEX_SET_HPP
#define PLAYRHO_VERTEX_SET_HPP

#include <PlayRho/Common/Math.hpp>

#include <vector>
#include <algorithm>

namespace playrho
{
    /// Vertex Set.
    ///
    /// @details This is a container that enforces the invariant that no two
    /// vertices can be closer together than the minimum separation distance.
    ///
    class VertexSet
    {
    public:
        
        /// @brief Constant pointer type.
        using const_pointer = const Length2D*;

        /// @brief Gets the default minimum separation squared value.
        static Area GetDefaultMinSeparationSquared()
        {
            return Sqrt(std::numeric_limits<Vec2::value_type>::min()) * SquareMeter;
        }
        
        /// @brief Initializing constructor.
        VertexSet(Area minSepSquared = GetDefaultMinSeparationSquared()):
            m_minSepSquared{minSepSquared}
        {
            assert(minSepSquared >= Area{0});
        }

        /// @brief Gets the min separation squared.
        Area GetMinSeparationSquared() const noexcept { return m_minSepSquared; }

        /// @brief Adds the given vertex into the set if allowed.
        bool add(Length2D value)
        {
            if (find(value) != end())
            {
                return false;
            }
            m_elements.push_back(value);
            return true;
        }
        
        /// @brief Clear this set.
        void clear() noexcept
        {
            m_elements.clear();
        }

        /// @brief Gets the current size of this set.
        std::size_t size() const noexcept { return m_elements.size(); }
        
        /// @brief Gets the "begin" iterator value.
        const_pointer begin() const { return m_elements.data(); }
        
        /// @brief Gets the "end" iterator value.
        const_pointer end() const { return m_elements.data() + m_elements.size(); }

        /// Finds contained point whose delta with the given point has a squared length less
        /// than or equal to this set's minimum length squared value.
        const_pointer find(Length2D value) const
        {
            // squaring anything smaller than the sqrt(std::numeric_limits<Vec2::data_type>::min())
            // won't be reversible.
            // i.e. won't obey the property that square(sqrt(a)) == a and sqrt(square(a)) == a.
            return std::find_if(begin(), end(), [&](Length2D elem) {
                // length squared must be large enough to have a reasonable enough unit vector.
                return GetLengthSquared(value - elem) <= m_minSepSquared;
            });
        }

        /// @brief Indexed access.
        Length2D operator[](std::size_t index) const noexcept
        {
            return m_elements[index];
        }

    private:
        std::vector<Length2D> m_elements; ///< Elements.
        const Area m_minSepSquared; ///< Minimum length squared. sizeof(Vec2)/2 or 4-bytes.
    };
}

#endif /* PLAYRHO_VERTEX_SET_HPP */
