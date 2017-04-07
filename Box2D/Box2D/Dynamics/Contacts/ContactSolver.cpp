/*
 * Original work Copyright (c) 2006-2011 Erin Catto http://www.box2d.org
 * Modified work Copyright (c) 2017 Louis Langholtz https://github.com/louis-langholtz/Box2D
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

#include <Box2D/Dynamics/Contacts/ContactSolver.hpp>
#include <Box2D/Collision/Collision.hpp>
#include <Box2D/Collision/WorldManifold.hpp>
#include <Box2D/Dynamics/Contacts/PositionSolverManifold.hpp>
#include <Box2D/Dynamics/Contacts/VelocityConstraint.hpp>
#include <Box2D/Dynamics/Contacts/PositionConstraint.hpp>

using namespace box2d;

#if !defined(NDEBUG)
// Solver debugging is normally disabled because the block solver sometimes has to deal with a poorly conditioned effective mass matrix.
//#define B2_DEBUG_SOLVER 1
#endif

#if defined(B2_DEBUG_SOLVER)
static constexpr auto k_errorTol = RealNum(2e-3); ///< error tolerance
static constexpr auto k_majorErrorTol = RealNum(1e-2); ///< error tolerance
#endif

struct VelocityPair
{
	Velocity vel_a;
	Velocity vel_b;
};

struct VelocitySolution
{
	Velocity velA;
	Velocity velB;
	RealNum newImpulse;
};

/// Impulse change.
///
/// @detail
/// This describes the change in impulse necessary for a solution.
/// To apply this: let P = magnitude * direction, then
///   the change to body A's velocity is
///   -Velocity{vc.bodyA.GetInvMass() * P, Radian * vc.bodyA.GetInvRotInertia() * Cross(vcp.rA, P)}
///   the change to body B's velocity is
///   +Velocity{vc.bodyB.GetInvMass() * P, Radian * vc.bodyB.GetInvRotInertia() * Cross(vcp.rB, P)}
///   and the new impulse = oldImpulse + magnitude.
///
struct ImpulseChange
{
	RealNum magnitude; ///< Magnitude.
	UnitVec2 direction; ///< Direction.
};

static inline ImpulseChange SolveTangentConstraint(const VelocityConstraint& vc,
													  const VelocityConstraint::size_type i)
{
	const auto direction = vc.GetTangent();
	const auto velA = vc.bodyA.GetVelocity();
	const auto velB = vc.bodyB.GetVelocity();
	const auto vcp = vc.GetPointAt(i);
	
	// Compute tangent force
	const auto lambda = [&]() {
		const auto dv = GetContactRelVelocity(velA, vcp.rA, velB, vcp.rB);
		const auto closingVel = Vec2{dv.x / MeterPerSecond, dv.y / MeterPerSecond};
		const auto directionalVel = vc.GetTangentSpeed() - Dot(closingVel, direction);
		return vcp.tangentMass * directionalVel;
	}();
	
	// Clamp the accumulated force
	//
	// Notes:
	//
	//   vc.GetFriction() can return any value between 0 and +Inf. If it's +Inf,
	//   multiplying it by any non-zero non-NaN value results in +/-Inf, and multiplying
	//   it by zero or NaN results in NaN.
	//
	//   Meanwhile the normal impulse at the point can often be 0.
	//
	const auto maxImpulse = vc.GetFriction() * vcp.normalImpulse;
	const auto oldImpulse = vcp.tangentImpulse;
	const auto newImpulse = Clamp(oldImpulse + lambda, -maxImpulse, maxImpulse);
	const auto incImpulse = newImpulse - oldImpulse;
	
	return ImpulseChange{incImpulse, direction};
}

static inline ImpulseChange SolveNormalConstraint(const VelocityConstraint& vc,
													 const VelocityConstraint::size_type i)
{
	const auto direction = vc.GetNormal();
	const auto velA = vc.bodyA.GetVelocity();
	const auto velB = vc.bodyB.GetVelocity();
	const auto vcp = vc.GetPointAt(i);
	
	// Compute normal impulse
	const auto lambda = [&](){
		const auto dv = GetContactRelVelocity(velA, vcp.rA, velB, vcp.rB);
		const auto closingVel = Vec2{dv.x / MeterPerSecond, dv.y / MeterPerSecond};
		const auto directionalVel = Dot(closingVel, direction);
		return vcp.normalMass * (directionalVel - vcp.velocityBias);
	}();
	
	// Clamp the accumulated impulse
	const auto oldImpulse = vcp.normalImpulse;
	const auto newImpulse = Max(oldImpulse - lambda, RealNum{0});
	const auto incImpulse = newImpulse - oldImpulse;
	
	return ImpulseChange{incImpulse, direction};
}

/// Solves the tangential portion of the velocity constraint.
/// @detail
/// This imposes friction on the velocity.
/// Specifically, this updates the tangent impulses on the velocity constraint points and
///   updates the two given velocity structures.
/// @warning Behavior is undefined unless the velocity constraint point count is 1 or 2.
/// @param vc Velocity constraint.
static inline RealNum SolveTangentConstraint(VelocityConstraint& vc)
{
	auto maxIncImpulse = RealNum{0};
	
	const auto invRotInertiaA = vc.bodyA.GetInvRotInertia() * (SquareMeter * Kilogram / SquareRadian);
	const auto invRotInertiaB = vc.bodyB.GetInvRotInertia() * (SquareMeter * Kilogram / SquareRadian);

	const auto count = vc.GetPointCount();
	assert((count == 1) || (count == 2));
	switch (count)
	{
		case 2:
		{
			const auto solution = SolveTangentConstraint(vc, 1);
			const auto P = solution.magnitude * solution.direction;
			const auto vcp = vc.GetPointAt(1);
			vc.bodyA.SetVelocity(vc.bodyA.GetVelocity() - Velocity{
				RealNum{vc.bodyA.GetInvMass() * Kilogram} * P * MeterPerSecond, RadianPerSecond * invRotInertiaA * Cross(vcp.rA, P)
			});
			vc.bodyB.SetVelocity(vc.bodyB.GetVelocity() + Velocity{
				RealNum{vc.bodyB.GetInvMass() * Kilogram} * P * MeterPerSecond, RadianPerSecond * invRotInertiaB * Cross(vcp.rB, P)
			});
			vc.SetTangentImpulseAtPoint(1, vcp.tangentImpulse + solution.magnitude);
			maxIncImpulse = std::max(maxIncImpulse, std::abs(solution.magnitude));
		}
			// intentional fallthrough
		case 1:
		{
			const auto solution = SolveTangentConstraint(vc, 0);
			const auto P = solution.magnitude * solution.direction;
			const auto vcp = vc.GetPointAt(0);
			vc.bodyA.SetVelocity(vc.bodyA.GetVelocity() - Velocity{
				RealNum{vc.bodyA.GetInvMass() * Kilogram} * P * MeterPerSecond, RadianPerSecond * invRotInertiaA * Cross(vcp.rA, P)
			});
			vc.bodyB.SetVelocity(vc.bodyB.GetVelocity() + Velocity{
				RealNum{vc.bodyB.GetInvMass() * Kilogram} * P * MeterPerSecond, RadianPerSecond * invRotInertiaB * Cross(vcp.rB, P)
			});
			vc.SetTangentImpulseAtPoint(0, vcp.tangentImpulse + solution.magnitude);
			maxIncImpulse = std::max(maxIncImpulse, std::abs(solution.magnitude));
		}
			// intentional fallthrough
		default: break;
	}

	return maxIncImpulse;
}

static inline RealNum SeqSolveNormalConstraint(VelocityConstraint& vc)
{
	auto maxIncImpulse = RealNum{0};

	const auto invRotInertiaA = vc.bodyA.GetInvRotInertia() * (SquareMeter * Kilogram / SquareRadian);
	const auto invRotInertiaB = vc.bodyB.GetInvRotInertia() * (SquareMeter * Kilogram / SquareRadian);

	const auto count = vc.GetPointCount();
	assert((count == 1) || (count == 2));
	switch (count)
	{
		case 2:
		{
			const auto solution = SolveNormalConstraint(vc, 1);
			const auto P = solution.magnitude * solution.direction;
			const auto vcp = vc.GetPointAt(1);
			vc.bodyA.SetVelocity(vc.bodyA.GetVelocity() - Velocity{
				RealNum{vc.bodyA.GetInvMass() * Kilogram} * P * MeterPerSecond, RadianPerSecond * invRotInertiaA * Cross(vcp.rA, P)
			});
			vc.bodyB.SetVelocity(vc.bodyB.GetVelocity() + Velocity{
				RealNum{vc.bodyB.GetInvMass() * Kilogram} * P * MeterPerSecond, RadianPerSecond * invRotInertiaB * Cross(vcp.rB, P)
			});
			vc.SetNormalImpulseAtPoint(1, vcp.normalImpulse + solution.magnitude);
			maxIncImpulse = std::max(maxIncImpulse, std::abs(solution.magnitude));
		}
			// intentional fallthrough
		case 1:
		{
			const auto solution = SolveNormalConstraint(vc, 0);
			const auto P = solution.magnitude * solution.direction;
			const auto vcp = vc.GetPointAt(0);
			vc.bodyA.SetVelocity(vc.bodyA.GetVelocity() - Velocity{
				RealNum{vc.bodyA.GetInvMass() * Kilogram} * P * MeterPerSecond, RadianPerSecond * invRotInertiaA * Cross(vcp.rA, P)
			});
			vc.bodyB.SetVelocity(vc.bodyB.GetVelocity() + Velocity{
				RealNum{vc.bodyB.GetInvMass() * Kilogram} * P * MeterPerSecond, RadianPerSecond * invRotInertiaB * Cross(vcp.rB, P)
			});
			vc.SetNormalImpulseAtPoint(0, vcp.normalImpulse + solution.magnitude);
			maxIncImpulse = std::max(maxIncImpulse, std::abs(solution.magnitude));
		}
			// intentional fallthrough
		default: break;
	}
	return maxIncImpulse;
}

static inline VelocityPair ApplyImpulses(const VelocityConstraint& vc, const Vec2 impulses)
{
	assert(IsValid(impulses));

	const auto invRotInertiaA = vc.bodyA.GetInvRotInertia() * (SquareMeter * Kilogram / SquareRadian);
	const auto invRotInertiaB = vc.bodyB.GetInvRotInertia() * (SquareMeter * Kilogram / SquareRadian);

	// Apply incremental impulse
	const auto normal = GetNormal(vc);
	const auto P0 = impulses[0] * normal;
	const auto P1 = impulses[1] * normal;
	const auto P = P0 + P1;
	return VelocityPair{
		-Velocity{
			RealNum{vc.bodyA.GetInvMass() * Kilogram} * P * MeterPerSecond,
			RadianPerSecond * invRotInertiaA * (Cross(GetPointRelPosA(vc, 0), P0) + Cross(GetPointRelPosA(vc, 1), P1))
		},
		+Velocity{
			RealNum{vc.bodyB.GetInvMass() * Kilogram} * P * MeterPerSecond,
			RadianPerSecond * invRotInertiaB * (Cross(GetPointRelPosB(vc, 0), P0) + Cross(GetPointRelPosB(vc, 1), P1))
		}
	};
}

static inline RealNum BlockSolveUpdate(VelocityConstraint& vc, const Vec2 newImpulses)
{
	const auto delta_v = ApplyImpulses(vc, newImpulses - GetNormalImpulses(vc));
	vc.bodyA.SetVelocity(vc.bodyA.GetVelocity() + delta_v.vel_a);
	vc.bodyB.SetVelocity(vc.bodyB.GetVelocity() + delta_v.vel_b);
	SetNormalImpulses(vc, newImpulses);
	return std::max(std::abs(newImpulses[0]), std::abs(newImpulses[1]));
}

static inline RealNum BlockSolveNormalCase1(VelocityConstraint& vc, const Vec2 b_prime)
{
	//
	// Case 1: vn = 0
	//
	// 0 = A * x + b'
	//
	// Solve for x:
	//
	// x = -inv(A) * b'
	//
	const auto normalMass = vc.GetNormalMass();
	const auto newImpulses = -Transform(b_prime, normalMass);
	if ((newImpulses[0] >= 0) && (newImpulses[1] >= 0))
	{
		BlockSolveUpdate(vc, newImpulses);
		
#if defined(B2_DEBUG_SOLVER)
		auto& vcp1 = vc.PointAt(0);
		auto& vcp2 = vc.PointAt(1);

		// Postconditions
		const auto post_dv1 = (velB.linear + (velB.angular * GetRevPerpendicular(vcp1.rB))) - (velA.linear + (velA.angular * GetRevPerpendicular(vcp1.rA)));
		const auto post_dv2 = (velB.linear + (velB.angular * GetRevPerpendicular(vcp2.rB))) - (velA.linear + (velA.angular * GetRevPerpendicular(vcp2.rA)));
		
		// Compute normal velocity
		const auto post_vn1 = Dot(post_dv1, vc.normal);
		const auto post_vn2 = Dot(post_dv2, vc.normal);
		
		assert(Abs(post_vn1 - vcp1.velocityBias) < k_majorErrorTol);
		assert(Abs(post_vn2 - vcp2.velocityBias) < k_majorErrorTol);
		assert(Abs(post_vn1 - vcp1.velocityBias) < k_errorTol);
		assert(Abs(post_vn2 - vcp2.velocityBias) < k_errorTol);
#endif
		return std::max(newImpulses[0], newImpulses[1]);
	}
	return GetInvalid<RealNum>();
}

static inline RealNum BlockSolveNormalCase2(VelocityConstraint& vc, const Vec2 b_prime)
{
	//
	// Case 2: vn1 = 0 and x2 = 0
	//
	//   0 = a11 * x1 + a12 * 0 + b1' 
	// vn2 = a21 * x1 + a22 * 0 + b2'
	//
	const auto newImpulses = Vec2{-GetNormalMassAtPoint(vc, 0) * b_prime.x, 0};
	const auto K = vc.GetK();
	const auto vn2 = K.ex.y * newImpulses.x + b_prime.y;
	if ((newImpulses.x >= 0) && (vn2 >= 0))
	{
		BlockSolveUpdate(vc, newImpulses);
		
#if defined(B2_DEBUG_SOLVER)
		auto& vcp1 = vc.PointAt(0);
	
		// Postconditions
		const auto post_dv1 = (velB.linear + (velB.angular * GetRevPerpendicular(vcp1.rB))) - (velA.linear + (velA.angular * GetRevPerpendicular(vcp1.rA)));
		
		// Compute normal velocity
		const auto post_vn1 = Dot(post_dv1, vc.normal);
		
		assert(Abs(post_vn1 - vcp1.velocityBias) < k_majorErrorTol);
		assert(Abs(post_vn1 - vcp1.velocityBias) < k_errorTol);
#endif
		return std::max(newImpulses[0], newImpulses[1]);
	}
	return GetInvalid<RealNum>();
}

static inline RealNum BlockSolveNormalCase3(VelocityConstraint& vc, const Vec2 b_prime)
{
	//
	// Case 3: vn2 = 0 and x1 = 0
	//
	// vn1 = a11 * 0 + a12 * x2 + b1' 
	//   0 = a21 * 0 + a22 * x2 + b2'
	//
	const auto newImpulses = Vec2{0, -GetNormalMassAtPoint(vc, 1) * b_prime.y};
	const auto K = vc.GetK();
	const auto vn1 = K.ey.x * newImpulses.y + b_prime.x;
	if ((newImpulses.y >= 0) && (vn1 >= 0))
	{
		BlockSolveUpdate(vc, newImpulses);
		
#if defined(B2_DEBUG_SOLVER)
		auto& vcp2 = vc.PointAt(1);

		// Postconditions
		const auto post_dv2 = (velB.linear + (velB.angular * GetRevPerpendicular(vcp2.rB))) - (velA.linear + (velA.angular * GetRevPerpendicular(vcp2.rA)));
		
		// Compute normal velocity
		const auto post_vn2 = Dot(post_dv2, vc.normal);

		assert(Abs(post_vn2 - vcp2.velocityBias) < k_majorErrorTol);
		assert(Abs(post_vn2 - vcp2.velocityBias) < k_errorTol);
#endif
		return std::max(newImpulses[0], newImpulses[1]);
	}
	return GetInvalid<RealNum>();
}

static inline RealNum BlockSolveNormalCase4(VelocityConstraint& vc, const Vec2 b_prime)
{
	//
	// Case 4: x1 = 0 and x2 = 0
	// 
	// vn1 = b1
	// vn2 = b2;
	const auto newImpulses = Vec2_zero;
	const auto vn1 = b_prime.x;
	const auto vn2 = b_prime.y;
	if ((vn1 >= 0) && (vn2 >= 0))
	{
		BlockSolveUpdate(vc, newImpulses);
		return std::max(newImpulses[0], newImpulses[1]);
	}
	return GetInvalid<RealNum>();
}

static inline RealNum BlockSolveNormalConstraint(VelocityConstraint& vc)
{
	// Block solver developed in collaboration with Dirk Gregorius (back in 01/07 on Box2D_Lite).
	// Build the mini LCP for this contact patch
	//
	// vn = A * x + b, vn >= 0, x >= 0 and vn_i * x_i = 0 with i = 1..2
	//
	// A = J * W * JT and J = ( -n, -r1 x n, n, r2 x n )
	// b = vn0 - velocityBias
	//
	// The system is solved using the "Total enumeration method" (s. Murty). The complementary constraint vn_i * x_i
	// implies that we must have in any solution either vn_i = 0 or x_i = 0. So for the 2D contact problem the cases
	// vn1 = 0 and vn2 = 0, x1 = 0 and x2 = 0, x1 = 0 and vn2 = 0, x2 = 0 and vn1 = 0 need to be tested. The first valid
	// solution that satisfies the problem is chosen.
	// 
	// In order to account of the accumulated impulse 'a' (because of the iterative nature of the solver which only requires
	// that the accumulated impulse is clamped and not the incremental impulse) we change the impulse variable (x_i).
	//
	// Substitute:
	// 
	// x = a + d
	// 
	// a := old total impulse
	// x := new total impulse
	// d := incremental impulse 
	//
	// For the current iteration we extend the formula for the incremental impulse
	// to compute the new total impulse:
	//
	// vn = A * d + b
	//    = A * (x - a) + b
	//    = A * x + b - A * a
	//    = A * x + b'
	// b' = b - A * a;

	const auto b_prime = [=]{
		const auto K = vc.GetK();
		
		const auto normal = vc.GetNormal();
		
		const auto velA = vc.bodyA.GetVelocity();
		const auto velB = vc.bodyB.GetVelocity();
		const auto ra0 = vc.GetPointRelPosA(0);
		const auto rb0 = vc.GetPointRelPosB(0);
		const auto ra1 = vc.GetPointRelPosA(1);
		const auto rb1 = vc.GetPointRelPosB(1);
		
		const auto dv0 = GetContactRelVelocity(velA, ra0, velB, rb0);
		const auto dv1 = GetContactRelVelocity(velA, ra1, velB, rb1);
		
		// Compute normal velocities
		const auto vn1 = Dot(Vec2{dv0.x / MeterPerSecond, dv0.y / MeterPerSecond}, normal);
		const auto vn2 = Dot(Vec2{dv1.x / MeterPerSecond, dv1.y / MeterPerSecond}, normal);
		
		// Compute b
		const auto b = Vec2{vn1 - vc.GetVelocityBiasAtPoint(0), vn2 - vc.GetVelocityBiasAtPoint(1)};
		
		// Return b'
		return b - Transform(GetNormalImpulses(vc), K);
	}();
	
	RealNum maxIncImpulse;
	maxIncImpulse = BlockSolveNormalCase1(vc, b_prime);
	if (IsValid(maxIncImpulse))
		return maxIncImpulse;
	maxIncImpulse = BlockSolveNormalCase2(vc, b_prime);
	if (IsValid(maxIncImpulse))
		return maxIncImpulse;
	maxIncImpulse = BlockSolveNormalCase3(vc, b_prime);
	if (IsValid(maxIncImpulse))
		return maxIncImpulse;
	maxIncImpulse = BlockSolveNormalCase4(vc, b_prime);
	if (IsValid(maxIncImpulse))
		return maxIncImpulse;
	
	// No solution, give up. This is hit sometimes, but it doesn't seem to matter.
	return 0;
}

/// Solves the normal portion of the velocity constraint.
/// @detail
/// This prevents penetration and applies the contact restitution to the velocity.
static inline RealNum SolveNormalConstraint(VelocityConstraint& vc)
{
	const auto count = vc.GetPointCount();
	assert((count == 1) || (count == 2));
	
	if ((count == 1) || (!IsValid(vc.GetK())))
	{
		return SeqSolveNormalConstraint(vc);
	}
	return BlockSolveNormalConstraint(vc);
}
	
RealNum box2d::SolveVelocityConstraint(VelocityConstraint& vc)
{
	auto maxIncImpulse = RealNum{0};
	
	// Applies frictional changes to velocity.
	maxIncImpulse = std::max(maxIncImpulse, SolveTangentConstraint(vc));

	// Applies restitutional changes to velocity.
	maxIncImpulse = std::max(maxIncImpulse, SolveNormalConstraint(vc));

	return maxIncImpulse;
}

PositionSolution box2d::SolvePositionConstraint(const PositionConstraint& pc,
												bool moveA, bool moveB,
												ConstraintSolverConf conf)
{
	assert(moveA == 0 || moveA == 1);
	assert(moveB == 0 || moveB == 1);
	
	assert(IsValid(conf.resolutionRate));
	assert(IsValid(conf.linearSlop));
	assert(IsValid(conf.maxLinearCorrection));
	
	const auto invMassA = RealNum{pc.bodyA.GetInvMass() * Kilogram} * moveA;
	const auto invInertiaA = pc.bodyA.GetInvRotInertia() * (SquareMeter * Kilogram / SquareRadian) * moveA;
	const auto localCenterA = pc.bodyA.GetLocalCenter();
	
	const auto invMassB = RealNum{pc.bodyB.GetInvMass() * Kilogram} * moveB;
	const auto invInertiaB = pc.bodyB.GetInvRotInertia() * (SquareMeter * Kilogram / SquareRadian) * moveB;
	const auto localCenterB = pc.bodyB.GetLocalCenter();
	
	// Compute inverse mass total.
	// This must be > 0 unless doing TOI solving and neither bodies were the bodies specified.
	const auto invMassTotal = invMassA + invMassB;
	assert(invMassTotal >= 0);
	
	const auto totalRadius = pc.radiusA + pc.radiusB;
	
	const auto solver_fn = [&](const PositionSolverManifold psm, const Vec2 pA, const Vec2 pB) {
		const auto separation = psm.m_separation - totalRadius;
		// Positive separation means shapes not overlapping and not touching.
		// Zero separation means shapes are touching.
		// Negative separation means shapes are overlapping.
		
		const auto rA = psm.m_point - pA;
		const auto rB = psm.m_point - pB;
		
		// Compute the effective mass.
		const auto K = [&]() {
			const auto rnA = Cross(rA, psm.m_normal);
			const auto rnB = Cross(rB, psm.m_normal);
			return invMassTotal + (invInertiaA * Square(rnA)) + (invInertiaB * Square(rnB));
		}();
		
		// Prevent large corrections & don't push separation above -conf.linearSlop.
		const auto C = Clamp(conf.resolutionRate * (separation + conf.linearSlop),
							 -conf.maxLinearCorrection, RealNum{0});
		
		// Compute normal impulse
		const auto P = psm.m_normal * -C / K;
		
		return PositionSolution{
			-Position{invMassA * P, Radian * invInertiaA * Cross(rA, P)},
			+Position{invMassB * P, Radian * invInertiaB * Cross(rB, P)},
			separation
		};
	};

	auto posA = pc.bodyA.GetPosition();
	auto posB = pc.bodyB.GetPosition();
	
	// Solve normal constraints
	const auto pointCount = pc.manifold.GetPointCount();
	switch (pointCount)
	{
		case 1:
		{
			const auto psm0 = GetPSM(pc.manifold, 0, posA, localCenterA, posB, localCenterB);
			return PositionSolution{posA, posB, 0} + solver_fn(psm0, posA.linear, posB.linear);
		}
		case 2:
		{
			// solve most penatrating point first or solve simultaneously if about the same penetration
			const auto psm0 = GetPSM(pc.manifold, 0, posA, localCenterA, posB, localCenterB);
			const auto psm1 = GetPSM(pc.manifold, 1, posA, localCenterA, posB, localCenterB);
			
			assert(IsValid(psm0.m_separation) && IsValid(psm1.m_separation));
			
			if (almost_equal(psm0.m_separation, psm1.m_separation))
			{
				const auto s0 = solver_fn(psm0, posA.linear, posB.linear);
				const auto s1 = solver_fn(psm1, posA.linear, posB.linear);
				//assert(s0.pos_a.angular == -s1.pos_a.angular);
				//assert(s0.pos_b.angular == -s1.pos_b.angular);
				return PositionSolution{
					posA + s0.pos_a + s1.pos_a,
					posB + s0.pos_b + s1.pos_b,
					s0.min_separation
				};
			}
			if (psm0.m_separation < psm1.m_separation)
			{
				const auto s0 = solver_fn(psm0, posA.linear, posB.linear);
				posA += s0.pos_a;
				posB += s0.pos_b;
				const auto psm1_prime = GetPSM(pc.manifold, 1, posA, localCenterA, posB, localCenterB);
				const auto s1 = solver_fn(psm1_prime, posA.linear, posB.linear);
				posA += s1.pos_a;
				posB += s1.pos_b;
				return PositionSolution{posA, posB, s0.min_separation};
			}
			if (psm1.m_separation < psm0.m_separation)
			{
				const auto s1 = solver_fn(psm1, posA.linear, posB.linear);
				posA += s1.pos_a;
				posB += s1.pos_b;
				const auto psm0_prime = GetPSM(pc.manifold, 0, posA, localCenterA, posB, localCenterB);
				const auto s0 = solver_fn(psm0_prime, posA.linear, posB.linear);
				posA += s0.pos_a;
				posB += s0.pos_b;
				return PositionSolution{posA, posB, s1.min_separation};
			}

			// reaches here if one or both psm separation values was NaN (and NDEBUG is defined).
		}
		default: break;
	}
	return PositionSolution{posA, posB, std::numeric_limits<RealNum>::infinity()};
}

RealNum box2d::SolvePositionConstraints(Span<PositionConstraint> positionConstraints,
										ConstraintSolverConf conf)
{
	auto minSeparation = std::numeric_limits<RealNum>::infinity();
	
	for (auto&& pc: positionConstraints)
	{
		assert(&(pc.bodyA) != &(pc.bodyB)); // Confirms ContactManager::Add() did its job.
		const auto res = SolvePositionConstraint(pc, true, true, conf);
		pc.bodyA.SetPosition(res.pos_a);
		pc.bodyB.SetPosition(res.pos_b);
		minSeparation = Min(minSeparation, res.min_separation);
	}
	
	return minSeparation;
}

RealNum box2d::SolvePositionConstraints(Span<PositionConstraint> positionConstraints,
										const BodyConstraint* bodiesA, const BodyConstraint* bodiesB,
										ConstraintSolverConf conf)
{
	auto minSeparation = std::numeric_limits<RealNum>::infinity();
	
	// Intentionally copy position constraint to local variable in order to
	// modify the constraint temporarily if related to indexA or indexB.
	for (auto&& pc: positionConstraints)
	{
		const auto moveA = (&(pc.bodyA) == bodiesA) || (&(pc.bodyA) == bodiesB);
		const auto moveB = (&(pc.bodyB) == bodiesA) || (&(pc.bodyB) == bodiesB);
		const auto res = SolvePositionConstraint(pc, moveA, moveB, conf);
		pc.bodyA.SetPosition(res.pos_a);
		pc.bodyB.SetPosition(res.pos_b);
		minSeparation = Min(minSeparation, res.min_separation);
	}
	
	return minSeparation;
}

