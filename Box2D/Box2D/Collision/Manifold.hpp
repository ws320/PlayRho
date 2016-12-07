/*
 * Original work Copyright (c) 2006-2009 Erin Catto http://www.box2d.org
 * Modified work Copyright (c) 2016 Louis Langholtz https://github.com/louis-langholtz/Box2D
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

#ifndef Manifold_hpp
#define Manifold_hpp

#include <Box2D/Common/Math.hpp>
#include <Box2D/Collision/ContactFeature.hpp>

#include <array>

namespace box2d
{
	class DistanceProxy;
	struct Transformation;

	/// Manifold for two convex shapes.
	///
	/// @detail
	/// Multiple types of contact are supported: clip point versus plane with radius, point versus
	/// point with radius (circles). Contacts are stored in this way so that position correction can
	/// account for movement, which is critical for continuous physics. All contact scenarios must
	/// be expressed in one of these types.
	///
	/// @note The local point and local normal usage depends on the manifold type. For details, see
	///   the documentation associated with the different manifold types.
	/// @note Every point adds computational overhead to the collision response calculation - so
	///   express collision manifolds with one point if possible instead of two.
	/// @note This data structure is at least 58-bytes large (60-bytes on one 64-bit platform).
	///
	/// @sa Contact.
	/// @sa PositionConstraint.
	/// @sa VelocityConstraint.
	///
	class Manifold
	{
	public:
		using size_type = std::remove_const<decltype(MaxManifoldPoints)>::type;

		/// Shape index type.
		using sidx_t = std::remove_const<decltype(MaxShapeVertices)>::type;
		
		/// Manifold type.
		/// @note This is by design a 1-byte sized type.
		enum Type: uint8
		{
			/// Unset type.
			/// @detail Manifold is unset. For manifolds of this type: the point count is zero,
			///   point data is undefined, and all other properties are invalid.
			e_unset,
			
			/// Circles type.
			/// @detail Manifold is for circle-to-circle like collisions.
			/// @note For manifolds of this type: the local point is local center of "circle-A"
			///     (where shape A wasn't necessarily a circle but treating it as such is useful),
			///     the local normal is invalid (and unused) and, the point count will be zero or
			///     one where the contact feature will be
			///               <code>ContactFeature{e_vertex, i, e_vertex, j}</code>
			///     where i and j are indexes of the vertexes of shapes A and B respectively.
			e_circles,

			/// Face-A type.
			/// @detail Indicates: local point is center of face A, local normal is normal on shape A, and the
			///   local points of Point instances are the local center of cirlce B or a clip point of polygon B
			///   where the contact feature will be <code>ContactFeature{e_face, i, e_vertex, j}</code> or
			///   <code>ContactFeature{e_face, i, e_face, j} where i and j are indexes for the vertex or edge
			///   of shapes A and B respectively.</code>.
			e_faceA,

			/// Face-B type.
			/// @detail Indicates: local point is center of face B, local normal is normal on shape B, and the
			///   local points of Point instances are the local center of cirlce A or a clip point of polygon A
			///   where the contact feature will be <code>ContactFeature{e_face, i, e_vertex, j}</code> or
			///   <code>ContactFeature{e_face, i, e_face, j} where i and j are indexes for the vertex or edge
			///   of shapes A and B respectively.</code>.
			e_faceB
		};
		
		/// Point data for a manifold.
		///
		/// @detail This is a contact point belonging to a contact manifold. It holds details
		/// related to the geometry and dynamics of the contact points.
		///
		/// @note The impulses are used for internal caching and may not provide reliable contact
		///    forces especially for high speed collisions.
		///
		/// @note This structure is at least 20-bytes large.
		///
		struct Point
		{
			/// Local point.
			/// @detail Usage depends on manifold type.
			/// For circles type manifolds, this is the local center of circle B.
			/// For face-A type manifolds, this is the local center of cirlce B or a clip point of polygon B.
			/// For face-B type manifolds, this is the local center of circle A or a clip point of polygon A.
			/// @note 8-bytes.
			Vec2 localPoint;

			/// Contact feature.
			/// @detail Uniquely identifies a contact point between two shapes - A and B.
			/// @note This field is 4-bytes.
			/// @sa GetPointStates.
			ContactFeature contactFeature;
			
			float_t normalImpulse = 0; ///< Normal impulse. This is the non-penetration impulse (4-bytes).
			
			float_t tangentImpulse = 0; ///< Tangent impulse. This is the friction impulse (4-bytes).
		};
		
		// For Circles type manifolds...
		
		/// Gets a circles-typed manifold with one point.
		/// @param vA Local center of "circle" A.
		/// @param iA Index of vertex from shape A representing the local center of "circle" A.
		/// @param vB Local center of "circle" B.
		/// @param iB Index of vertex from shape B representing the local center of "circle" B.
		static constexpr Manifold GetForCircles(Vec2 vA, sidx_t iA, Vec2 vB, sidx_t iB) noexcept
		{
			return Manifold{e_circles, GetInvalid<UnitVec2>(), vA, 1, {{Point{vB, GetVertexVertexContactFeature(iA, iB)}}}};
		}

		// For Face A type manifolds...
		
		/// Gets a face A typed manifold.
		/// @param ln Normal on polygon A.
		/// @param lp Center of face A.
		static constexpr Manifold GetForFaceA(UnitVec2 ln, Vec2 lp) noexcept
		{
			return Manifold{e_faceA, ln, lp, 0, {{}}};
		}
		
		/// Gets a face A typed manifold.
		/// @param ln Normal on polygon A.
		/// @param lp Center of face A.
		/// @param mp1 Manifold point 1 (of 1).
		static constexpr Manifold GetForFaceA(UnitVec2 ln, Vec2 lp, const Point& mp1) noexcept
		{
			//assert(mp1.contactFeature.typeA == ContactFeature::e_face || mp1.contactFeature.typeB == ContactFeature::e_face);
			return Manifold{e_faceA, ln, lp, 1, {{mp1}}};
		}
		
		/// Gets a face A typed manifold.
		/// @param ln Normal on polygon A.
		/// @param lp Center of face A.
		/// @param mp1 Manifold point 1 (of 2).
		/// @param mp2 Manifold point 2 (of 2).
		static constexpr Manifold GetForFaceA(UnitVec2 ln, Vec2 lp, const Point& mp1, const Point& mp2) noexcept
		{
			//assert(mp1.contactFeature.typeA == ContactFeature::e_face || mp1.contactFeature.typeB == ContactFeature::e_face);
			//assert(mp2.contactFeature.typeA == ContactFeature::e_face || mp2.contactFeature.typeB == ContactFeature::e_face);
			//assert(mp1.contactFeature != mp2.contactFeature);
			return Manifold{e_faceA, ln, lp, 2, {{mp1, mp2}}};
		}
		
		// For Face B...
		
		/// Gets a face B typed manifold.
		/// @param ln Normal on polygon B.
		/// @param lp Center of face B.
		static constexpr Manifold GetForFaceB(UnitVec2 ln, Vec2 lp) noexcept
		{
			return Manifold{e_faceB, ln, lp, 0, {{}}};
		}
		
		/// Gets a face B typed manifold.
		/// @param ln Normal on polygon B.
		/// @param lp Center of face B.
		/// @param mp1 Manifold point 1.
		static constexpr Manifold GetForFaceB(UnitVec2 ln, Vec2 lp, const Point& mp1) noexcept
		{
			//assert(mp1.contactFeature.typeA == ContactFeature::e_face || mp1.contactFeature.typeB == ContactFeature::e_face);
			return Manifold{e_faceB, ln, lp, 1, {{mp1}}};
		}
		
		/// Gets a face B typed manifold.
		/// @param ln Normal on polygon B.
		/// @param lp Center of face B.
		/// @param mp1 Manifold point 1 (of 2).
		/// @param mp2 Manifold point 2 (of 2).
		static constexpr Manifold GetForFaceB(UnitVec2 ln, Vec2 lp, const Point& mp1, const Point& mp2) noexcept
		{
			//assert(mp1.contactFeature.typeA == ContactFeature::e_face || mp1.contactFeature.typeB == ContactFeature::e_face);
			//assert(mp2.contactFeature.typeA == ContactFeature::e_face || mp2.contactFeature.typeB == ContactFeature::e_face);
			//assert(mp1.contactFeature != mp2.contactFeature);
			return Manifold{e_faceB, ln, lp, 2, {{mp1, mp2}}};
		}
		
		/// Default constructor.
		/// @detail
		/// Constructs an unset-type manifold.
		/// For an unset-type manifold:
		/// point count is zero, point data is undefined, and all other properties are invalid.
		Manifold() noexcept = default;
		
		Manifold(const Manifold& copy) noexcept = default;
		
		/// Gets the type of this manifold.
		Type GetType() const noexcept { return m_type; }
		
		/// Gets the manifold point count.
		/// @detail This is the count of contact points for this manifold.
		///   Only up to this many points can be validly accessed using the GetPoint() method.
		/// @note Non-zero values indicate that the two shapes are touching.
		/// @return Value between 0 and MaxManifoldPoints.
		/// @sa MaxManifoldPoints.
		/// @sa AddPoint().
		/// @sa GetPoint().
		size_type GetPointCount() const noexcept { return m_pointCount; }
		
		const Point& GetPoint(size_type index) const
		{
			assert((0 <= index) && (index < m_pointCount));
			return m_points[index];
		}
		
		void SetPointImpulses(size_type index, float_t n, float_t t)
		{
			assert((0 <= index) && (index < m_pointCount));
			m_points[index].normalImpulse = n;
			m_points[index].tangentImpulse = t;
		}
		
		/// Adds a new point.
		/// @detail This can be called once for circle type manifolds,
		///   and up to twice for face-A or face-B type manifolds.
		/// GetPointCount() can be called to find out how many points have already been added.
		/// @note Behavior is undefined if this object's type is e_unset.
		/// @note Behavior is undefined if this is called more than twice. 
		void AddPoint(const Point& mp) noexcept;
		
		/// Gets the local normal for a face-type manifold.
		/// @return Local normal if the manifold type is face A or face B, else invalid value.
		UnitVec2 GetLocalNormal() const noexcept
		{
			return m_localNormal;
		}
		
		/// Gets the local point.
		/// @detail
		/// This is the:
		/// local center of "circle" A for circles-type manifolds,
		/// the center of face A for face-A-type manifolds, and
		/// the center of face B for face-B-type manifolds.
		/// @note Value invalid for unset (e_unset) type manifolds.
		/// @return Local point.
		Vec2 GetLocalPoint() const noexcept
		{
			return m_localPoint;
		}
		
	private:
		using PointArray = std::array<Point, MaxManifoldPoints>;
		
		/// Constructs manifold with array of points using the given values.
		/// @param t Manifold type.
		/// @param ln Local normal.
		/// @param lp Local point.
		/// @param n number of points defined in arary.
		/// @param mpa Manifold point array.
		constexpr Manifold(Type t, UnitVec2 ln, Vec2 lp, size_type n, const PointArray& mpa) noexcept;
		
		Type m_type = e_unset; ///< Type of collision this manifold is associated with (1-byte).
		size_type m_pointCount = 0; ///< Number of defined manifold points (1-byte).
		
		/// Local normal.
		/// @detail Exact usage depends on manifold type (8-bytes).
		/// @note Invalid for the unset and circle manifold types.
		UnitVec2 m_localNormal = GetInvalid<decltype(m_localNormal)>();

		/// Local point.
		/// @detail Exact usage depends on manifold type (8-bytes).
		/// @note Invalid for the unset manifold type.
		Vec2 m_localPoint = GetInvalid<Vec2>();
		
		PointArray m_points; ///< Points of contact (at least 40-bytes). @sa pointCount.
	};
	
	bool operator==(const Manifold::Point& lhs, const Manifold::Point& rhs);
	
	bool operator!=(const Manifold::Point& lhs, const Manifold::Point& rhs);
	
	/// Equality operator.
	/// @note In-so-far as manifold points are concerned, order doesn't matter;
	///    only whether the two manifolds have the same point set.
	bool operator==(const Manifold& lhs, const Manifold& rhs);
	
	bool operator!=(const Manifold& lhs, const Manifold& rhs);

	constexpr Manifold::Manifold(Type t, UnitVec2 ln, Vec2 lp, size_type n, const PointArray& mpa) noexcept:
		m_type{t}, m_localNormal{ln}, m_localPoint{lp}, m_pointCount{n}, m_points{mpa}
	{
		assert(t != e_unset || n == 0);
		assert(t == e_unset || IsValid(lp));
		assert((t != e_circles) || (n == 1 && !IsValid(ln)));
		//assert((t != e_circles) || (n == 1 && !IsValid(ln) && mpa[0].contactFeature.typeA == ContactFeature::e_vertex && mpa[0].contactFeature.typeB == ContactFeature::e_vertex));
	}

	inline void Manifold::AddPoint(const Point& mp) noexcept
	{
		assert(m_type != e_unset);
		assert(m_type != e_circles || m_pointCount == 0);
		assert(m_pointCount < MaxManifoldPoints);
		// assert((m_pointCount == 0) || (mp.contactFeature != m_points[0].contactFeature));
		//assert((m_type != e_circles) || (mp.contactFeature.typeA == ContactFeature::e_vertex || mp.contactFeature.typeB == ContactFeature::e_vertex));
		//assert((m_type != e_faceA) || ((mp.contactFeature.typeA == ContactFeature::e_face) && (m_pointCount == 0 || mp.contactFeature.indexA == m_points[0].contactFeature.indexA)));
		//assert((m_type != e_faceB) || (mp.contactFeature.typeB == ContactFeature::e_face));
		m_points[m_pointCount] = mp;
		++m_pointCount;
	}

	template <>
	inline bool IsValid(const Manifold& value) noexcept
	{
		return value.GetType() != Manifold::e_unset;
	}

	Manifold GetManifold(const DistanceProxy& proxyA, const Transformation& transformA,
						 const DistanceProxy& proxyB, const Transformation& transformB);

	Vec2 GetLocalPoint(const DistanceProxy& proxy, ContactFeature::Type type, ContactFeature::index_t index);

	const char* GetName(Manifold::Type) noexcept;
	
} // namespace box2d

#endif /* Manifold_hpp */
