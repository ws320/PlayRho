//
//  Body.cpp
//  Box2D
//
//  Created by Louis D. Langholtz on 8/10/16.
//
//

#include "gtest/gtest.h"
#include <Box2D/Dynamics/Body.h>
#include <Box2D/Dynamics/World.h>
#include <Box2D/Dynamics/Fixture.h>
#include <Box2D/Dynamics/Joints/Joint.h>
#include <Box2D/Collision/Shapes/CircleShape.h>

#include <chrono>

using namespace box2d;

TEST(Body, ByteSizeIs160)
{
	// architecture dependent...
	EXPECT_EQ(sizeof(Body), size_t(160));
}

TEST(Body, WorldCreated)
{
	World world;
	
	auto body = world.CreateBody(BodyDef{});
	ASSERT_NE(body, nullptr);

	EXPECT_EQ(body->GetWorld(), &world);
	EXPECT_EQ(body->GetUserData(), nullptr);
	EXPECT_TRUE(body->IsAwake());
	EXPECT_TRUE(body->IsActive());
	EXPECT_FALSE(body->IsSpeedable());
	EXPECT_FALSE(body->IsAccelerable());
	
	EXPECT_TRUE(body->GetFixtures().empty());
	{
		int i = 0;
		for (auto&& fixture: body->GetFixtures())
		{
			EXPECT_EQ(fixture.GetBody(), body);
			++i;
		}
		EXPECT_EQ(i, 0);
	}

	EXPECT_TRUE(body->GetJoints().empty());
	{
		int i = 0;
		for (auto&& joint: body->GetJoints())
		{
			BOX2D_NOT_USED(joint);
			++i;
		}
		EXPECT_EQ(i, 0);		
	}
	
	EXPECT_TRUE(body->GetContactEdges().empty());
	{
		int i = 0;
		for (auto&& ce: body->GetContactEdges())
		{
			BOX2D_NOT_USED(ce);
			++i;
		}
		EXPECT_EQ(i, 0);		
	}
}

TEST(Body, CreateAndDestroyFixture)
{
	World world;

	auto body = world.CreateBody(BodyDef{});
	ASSERT_NE(body, nullptr);
	EXPECT_TRUE(body->GetFixtures().empty());
	EXPECT_FALSE(body->IsMassDataDirty());

	CircleShape shape{float_t(2.871), Vec2{float_t(1.912), float_t(-77.31)}};
	
	auto fixture = body->CreateFixture(FixtureDef{&shape, 1}, false);
	ASSERT_NE(fixture, nullptr);
	ASSERT_NE(fixture->GetShape(), nullptr);
	EXPECT_EQ(fixture->GetShape()->GetType(), shape.GetType());
	EXPECT_EQ(GetRadius(*fixture->GetShape()), GetRadius(shape));
	EXPECT_EQ(static_cast<CircleShape*>(fixture->GetShape())->GetPosition().x, shape.GetPosition().x);
	EXPECT_EQ(static_cast<CircleShape*>(fixture->GetShape())->GetPosition().y, shape.GetPosition().y);
	EXPECT_FALSE(body->GetFixtures().empty());
	{
		int i = 0;
		for (auto&& f: body->GetFixtures())
		{
			EXPECT_EQ(&f, fixture);
			++i;
		}
		EXPECT_EQ(i, 1);
	}
	EXPECT_TRUE(body->IsMassDataDirty());
	body->ResetMassData();
	EXPECT_FALSE(body->IsMassDataDirty());

	body->DestroyFixture(fixture, false);
	EXPECT_TRUE(body->GetFixtures().empty());
	EXPECT_TRUE(body->IsMassDataDirty());
	
	body->ResetMassData();
	EXPECT_FALSE(body->IsMassDataDirty());
}

TEST(Body, CreateLotsOfFixtures)
{
	BodyDef bd;
	bd.type = BodyType::Dynamic;
	CircleShape shape{float_t(2.871), Vec2{float_t(1.912), float_t(-77.31)}};
	const auto num = 5000;
	std::chrono::time_point<std::chrono::system_clock> start, end;
	
	start = std::chrono::system_clock::now();
	{
		World world;

		auto body = world.CreateBody(bd);
		ASSERT_NE(body, nullptr);
		EXPECT_TRUE(body->GetFixtures().empty());
		
		for (auto i = decltype(num){0}; i < num; ++i)
		{
			auto fixture = body->CreateFixture(FixtureDef{&shape, float_t(1.3)}, false);
			ASSERT_NE(fixture, nullptr);
		}
		body->ResetMassData();
		
		EXPECT_FALSE(body->GetFixtures().empty());
		{
			int i = decltype(num){0};
			for (auto&& f: body->GetFixtures())
			{
				BOX2D_NOT_USED(f);
				++i;
			}
			EXPECT_EQ(i, num);
		}
	}
	end = std::chrono::system_clock::now();
	const std::chrono::duration<double> elapsed_secs_resetting_at_end = end - start;

	start = std::chrono::system_clock::now();
	{
		World world;
		
		auto body = world.CreateBody(bd);
		ASSERT_NE(body, nullptr);
		EXPECT_TRUE(body->GetFixtures().empty());
		
		for (auto i = decltype(num){0}; i < num; ++i)
		{
			auto fixture = body->CreateFixture(FixtureDef{&shape, float_t(1.3)}, true);
			ASSERT_NE(fixture, nullptr);
		}
		
		EXPECT_FALSE(body->GetFixtures().empty());
		{
			int i = decltype(num){0};
			for (auto&& f: body->GetFixtures())
			{
				BOX2D_NOT_USED(f);
				++i;
			}
			EXPECT_EQ(i, num);
		}
	}
	end = std::chrono::system_clock::now();
	const std::chrono::duration<double> elapsed_secs_resetting_in_create = end - start;

	EXPECT_LT(elapsed_secs_resetting_at_end.count(), elapsed_secs_resetting_in_create.count());
}