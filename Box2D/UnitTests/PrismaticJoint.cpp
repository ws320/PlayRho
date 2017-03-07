/*
 * Copyright (c) 2017 Louis Langholtz https://github.com/louis-langholtz/Box2D
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

#include "gtest/gtest.h"
#include <Box2D/Dynamics/Joints/PrismaticJoint.hpp>

using namespace box2d;

TEST(PrismaticJoint, ByteSizeIs_320_504_or_864)
{
	if (sizeof(RealNum) == 4)
	{
		EXPECT_EQ(sizeof(PrismaticJoint), size_t(320));
	}
	else if (sizeof(RealNum) == 8)
	{
		EXPECT_EQ(sizeof(PrismaticJoint), size_t(504));
	}
	else if (sizeof(RealNum) == 16)
	{
		EXPECT_EQ(sizeof(PrismaticJoint), size_t(864));
	}
}
