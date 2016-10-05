/*
* Original work Copyright (c) 2006-2013 Erin Catto http://www.box2d.org
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

#include "DebugDraw.h"

#include <Box2D/Dynamics/World.h>
#include <Box2D/Dynamics/Body.h>
#include <Box2D/Dynamics/Fixture.h>
#include <Box2D/Dynamics/Joints/Joint.h>
#include <Box2D/Dynamics/Joints/PulleyJoint.h>
#include <Box2D/Rope/Rope.h>
#include <Box2D/Collision/Shapes/CircleShape.h>
#include <Box2D/Collision/Shapes/EdgeShape.h>
#include <Box2D/Collision/Shapes/ChainShape.h>
#include <Box2D/Collision/Shapes/PolygonShape.h>

#if defined(__APPLE_CC__)
#include <OpenGL/gl3.h>
#else
#include <glew/glew.h>
#endif

#include <glfw/glfw3.h>
#include <stdio.h>
#include <stdarg.h>

#include "RenderGL3.h"

#define BUFFER_OFFSET(x)  ((const void*) (x))

namespace box2d {

Camera g_camera;

//
Vec2 ConvertScreenToWorld(const Camera& camera, const Vec2 ps)
{
    const auto w = float_t(camera.m_width);
    const auto h = float_t(camera.m_height);
	const auto u = ps.x / w;
	const auto v = (h - ps.y) / h;

	const auto ratio = w / h;
	const auto extents = Vec2(ratio * 25.0f, 25.0f) * camera.m_zoom;

	const auto lower = camera.m_center - extents;
	const auto upper = camera.m_center + extents;

	return Vec2{(float_t(1) - u) * lower.x + u * upper.x, (float_t(1) - v) * lower.y + v * upper.y};
}

//
Vec2 ConvertWorldToScreen(const Camera& camera, const Vec2 pw)
{
	const auto w = float_t(camera.m_width);
	const auto h = float_t(camera.m_height);
	const auto ratio = w / h;
	const auto extents = Vec2(ratio * 25.0f, 25.0f) * camera.m_zoom;

	const auto lower = camera.m_center - extents;
	const auto upper = camera.m_center + extents;

	const auto u = (pw.x - lower.x) / (upper.x - lower.x);
	const auto v = (pw.y - lower.y) / (upper.y - lower.y);

	return Vec2{u * w, (float_t(1) - v) * h};
}

// Convert from world coordinates to normalized device coordinates.
// http://www.songho.ca/opengl/gl_projectionmatrix.html
ProjectionMatrix GetProjectionMatrix(const Camera& camera, float_t zBias)
{
	const auto w = float_t(camera.m_width);
	const auto h = float_t(camera.m_height);
	const auto ratio = w / h;
	const auto extents = Vec2(ratio * 25.0f, 25.0f) * camera.m_zoom;

	const auto lower = camera.m_center - extents;
	const auto upper = camera.m_center + extents;

	return ProjectionMatrix{{
		2.0f / (upper.x - lower.x), // 0
		0.0f, // 1
		0.0f, // 2
		0.0f, // 3
		0.0f, // 4
		2.0f / (upper.y - lower.y), // 5
		0.0f, // 6
		0.0f, // 7
		0.0f, // 8
		0.0f, // 9
		1.0f, // 10
		0.0f, // 11
		-(upper.x + lower.x) / (upper.x - lower.x), // 12
		-(upper.y + lower.y) / (upper.y - lower.y), // 13
		zBias, // 14
		1.0f
}};
}

//
static void sCheckGLError()
{
	GLenum errCode = glGetError();
	if (errCode != GL_NO_ERROR)
	{
		fprintf(stderr, "OpenGL error = %d\n", errCode);
		assert(false);
	}
}

// Prints shader compilation errors
static void sPrintLog(GLuint object)
{
	GLint log_length = 0;
	if (glIsShader(object))
		glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
	else if (glIsProgram(object))
		glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
	else
	{
		fprintf(stderr, "printlog: Not a shader or a program\n");
		return;
	}

	char* log = (char*)malloc(log_length);

	if (glIsShader(object))
		glGetShaderInfoLog(object, log_length, nullptr, log);
	else if (glIsProgram(object))
		glGetProgramInfoLog(object, log_length, nullptr, log);

	fprintf(stderr, "%s", log);
	free(log);
}


//
static GLuint sCreateShaderFromString(const char* source, GLenum type)
{
	GLuint res = glCreateShader(type);
	const char* sources[] = { source };
	glShaderSource(res, 1, sources, nullptr);
	glCompileShader(res);
	GLint compile_ok = GL_FALSE;
	glGetShaderiv(res, GL_COMPILE_STATUS, &compile_ok);
	if (compile_ok == GL_FALSE)
	{
		fprintf(stderr, "Error compiling shader of type %d!\n", type);
		sPrintLog(res);
		glDeleteShader(res);
		return 0;
	}

	return res;
}

// 
static GLuint sCreateShaderProgram(const char* vs, const char* fs)
{
	GLuint vsId = sCreateShaderFromString(vs, GL_VERTEX_SHADER);
	GLuint fsId = sCreateShaderFromString(fs, GL_FRAGMENT_SHADER);
	assert(vsId != 0 && fsId != 0);

	GLuint programId = glCreateProgram();
	glAttachShader(programId, vsId);
	glAttachShader(programId, fsId);
    glBindFragDataLocation(programId, 0, "color");
	glLinkProgram(programId);

	glDeleteShader(vsId);
	glDeleteShader(fsId);

	GLint status = GL_FALSE;
	glGetProgramiv(programId, GL_LINK_STATUS, &status);
	assert(status != GL_FALSE);
	
	return programId;
}

//
struct GLRenderPoints
{
	void Create()
	{
		const char* vs = \
        "#version 400\n"
        "uniform mat4 projectionMatrix;\n"
        "layout(location = 0) in vec2 v_position;\n"
        "layout(location = 1) in vec4 v_color;\n"
		"layout(location = 2) in float v_size;\n"
        "out vec4 f_color;\n"
        "void main(void)\n"
        "{\n"
        "	f_color = v_color;\n"
        "	gl_Position = projectionMatrix * vec4(v_position, 0.0f, 1.0f);\n"
		"   gl_PointSize = v_size;\n"
        "}\n";
        
		const char* fs = \
        "#version 400\n"
        "in vec4 f_color;\n"
        "out vec4 color;\n"
        "void main(void)\n"
        "{\n"
        "	color = f_color;\n"
        "}\n";
        
		m_programId = sCreateShaderProgram(vs, fs);
		m_projectionUniform = glGetUniformLocation(m_programId, "projectionMatrix");
		m_vertexAttribute = 0;
		m_colorAttribute = 1;
		m_sizeAttribute = 2;
        
		// Generate
		glGenVertexArrays(1, &m_vaoId);
		glGenBuffers(3, m_vboIds);
        
		glBindVertexArray(m_vaoId);
		glEnableVertexAttribArray(m_vertexAttribute);
		glEnableVertexAttribArray(m_colorAttribute);
		glEnableVertexAttribArray(m_sizeAttribute);
        
		// Vertex buffer
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[0]);
		glVertexAttribPointer(m_vertexAttribute, 2, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0));
		glBufferData(GL_ARRAY_BUFFER, sizeof(m_vertices), m_vertices, GL_DYNAMIC_DRAW);
        
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[1]);
		glVertexAttribPointer(m_colorAttribute, 4, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0));
		glBufferData(GL_ARRAY_BUFFER, sizeof(m_colors), m_colors, GL_DYNAMIC_DRAW);
        
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[2]);
		glVertexAttribPointer(m_sizeAttribute, 1, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0));
		glBufferData(GL_ARRAY_BUFFER, sizeof(m_sizes), m_sizes, GL_DYNAMIC_DRAW);

		sCheckGLError();
        
		// Cleanup
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
        
		m_count = 0;
	}
    
	void Destroy()
	{
		if (m_vaoId)
		{
			glDeleteVertexArrays(1, &m_vaoId);
			glDeleteBuffers(2, m_vboIds);
			m_vaoId = 0;
		}
        
		if (m_programId)
		{
			glDeleteProgram(m_programId);
			m_programId = 0;
		}
	}
    
	void Vertex(const Vec2& v, const Color& c, float_t size)
	{
		if (m_count == e_maxVertices)
			Flush();
        
		m_vertices[m_count] = v;
		m_colors[m_count] = c;
		m_sizes[m_count] = size;
		++m_count;
	}
    
    void Flush()
	{
        if (m_count == 0)
            return;
        
		glUseProgram(m_programId);
        
		const auto proj = GetProjectionMatrix(g_camera, 0.0f);
        
		glUniformMatrix4fv(m_projectionUniform, 1, GL_FALSE, proj.m);
        
		glBindVertexArray(m_vaoId);
        
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[0]);
		glBufferSubData(GL_ARRAY_BUFFER, 0, m_count * sizeof(Vec2), m_vertices);
        
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[1]);
		glBufferSubData(GL_ARRAY_BUFFER, 0, m_count * sizeof(Color), m_colors);
        
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[2]);
		glBufferSubData(GL_ARRAY_BUFFER, 0, m_count * sizeof(float_t), m_sizes);

		glEnable(GL_PROGRAM_POINT_SIZE);
		glDrawArrays(GL_POINTS, 0, m_count);
        glDisable(GL_PROGRAM_POINT_SIZE);

		sCheckGLError();
        
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		glUseProgram(0);
        
		m_count = 0;
	}
    
	enum { e_maxVertices = 512 };
	Vec2 m_vertices[e_maxVertices];
	Color m_colors[e_maxVertices];
    float_t m_sizes[e_maxVertices];

	int32 m_count;
    
	GLuint m_vaoId;
	GLuint m_vboIds[3];
	GLuint m_programId;
	GLint m_projectionUniform;
	GLint m_vertexAttribute;
	GLint m_colorAttribute;
	GLint m_sizeAttribute;
};

//
struct GLRenderLines
{
	void Create()
	{
		const char* vs = \
        "#version 400\n"
        "uniform mat4 projectionMatrix;\n"
        "layout(location = 0) in vec2 v_position;\n"
        "layout(location = 1) in vec4 v_color;\n"
        "out vec4 f_color;\n"
        "void main(void)\n"
        "{\n"
        "	f_color = v_color;\n"
        "	gl_Position = projectionMatrix * vec4(v_position, 0.0f, 1.0f);\n"
        "}\n";
        
		const char* fs = \
        "#version 400\n"
        "in vec4 f_color;\n"
        "out vec4 color;\n"
        "void main(void)\n"
        "{\n"
        "	color = f_color;\n"
        "}\n";
        
		m_programId = sCreateShaderProgram(vs, fs);
		m_projectionUniform = glGetUniformLocation(m_programId, "projectionMatrix");
		m_vertexAttribute = 0;
		m_colorAttribute = 1;
        
		// Generate
		glGenVertexArrays(1, &m_vaoId);
		glGenBuffers(2, m_vboIds);
        
		glBindVertexArray(m_vaoId);
		glEnableVertexAttribArray(m_vertexAttribute);
		glEnableVertexAttribArray(m_colorAttribute);
        
		// Vertex buffer
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[0]);
		glVertexAttribPointer(m_vertexAttribute, 2, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0));
		glBufferData(GL_ARRAY_BUFFER, sizeof(m_vertices), m_vertices, GL_DYNAMIC_DRAW);
        
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[1]);
		glVertexAttribPointer(m_colorAttribute, 4, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0));
		glBufferData(GL_ARRAY_BUFFER, sizeof(m_colors), m_colors, GL_DYNAMIC_DRAW);
        
		sCheckGLError();
        
		// Cleanup
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
        
		m_count = 0;
	}
    
	void Destroy()
	{
		if (m_vaoId)
		{
			glDeleteVertexArrays(1, &m_vaoId);
			glDeleteBuffers(2, m_vboIds);
			m_vaoId = 0;
		}
        
		if (m_programId)
		{
			glDeleteProgram(m_programId);
			m_programId = 0;
		}
	}
    
	void Vertex(const Vec2& v, const Color& c)
	{
		if (m_count == e_maxVertices)
			Flush();
        
		m_vertices[m_count] = v;
		m_colors[m_count] = c;
		++m_count;
	}
    
    void Flush()
	{
        if (m_count == 0)
            return;
        
		glUseProgram(m_programId);
        
		const auto proj = GetProjectionMatrix(g_camera, 0.1f);
        
		glUniformMatrix4fv(m_projectionUniform, 1, GL_FALSE, proj.m);
        
		glBindVertexArray(m_vaoId);
        
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[0]);
		glBufferSubData(GL_ARRAY_BUFFER, 0, m_count * sizeof(Vec2), m_vertices);
        
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[1]);
		glBufferSubData(GL_ARRAY_BUFFER, 0, m_count * sizeof(Color), m_colors);
        
		glDrawArrays(GL_LINES, 0, m_count);
        
		sCheckGLError();
        
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		glUseProgram(0);
        
		m_count = 0;
	}
    
	enum { e_maxVertices = 2 * 512 };
	Vec2 m_vertices[e_maxVertices];
	Color m_colors[e_maxVertices];
    
	int32 m_count;
    
	GLuint m_vaoId;
	GLuint m_vboIds[2];
	GLuint m_programId;
	GLint m_projectionUniform;
	GLint m_vertexAttribute;
	GLint m_colorAttribute;
};

//
struct GLRenderTriangles
{
	void Create()
	{
		const char* vs = \
			"#version 400\n"
			"uniform mat4 projectionMatrix;\n"
			"layout(location = 0) in vec2 v_position;\n"
			"layout(location = 1) in vec4 v_color;\n"
			"out vec4 f_color;\n"
			"void main(void)\n"
			"{\n"
			"	f_color = v_color;\n"
			"	gl_Position = projectionMatrix * vec4(v_position, 0.0f, 1.0f);\n"
			"}\n";

		const char* fs = \
			"#version 400\n"
			"in vec4 f_color;\n"
            "out vec4 color;\n"
			"void main(void)\n"
			"{\n"
			"	color = f_color;\n"
			"}\n";

		m_programId = sCreateShaderProgram(vs, fs);
		m_projectionUniform = glGetUniformLocation(m_programId, "projectionMatrix");
		m_vertexAttribute = 0;
		m_colorAttribute = 1;

		// Generate
		glGenVertexArrays(1, &m_vaoId);
		glGenBuffers(2, m_vboIds);

		glBindVertexArray(m_vaoId);
		glEnableVertexAttribArray(m_vertexAttribute);
		glEnableVertexAttribArray(m_colorAttribute);

		// Vertex buffer
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[0]);
		glVertexAttribPointer(m_vertexAttribute, 2, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0));
		glBufferData(GL_ARRAY_BUFFER, sizeof(m_vertices), m_vertices, GL_DYNAMIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[1]);
		glVertexAttribPointer(m_colorAttribute, 4, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0));
		glBufferData(GL_ARRAY_BUFFER, sizeof(m_colors), m_colors, GL_DYNAMIC_DRAW);

		sCheckGLError();

		// Cleanup
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		m_count = 0;
	}

	void Destroy()
	{
		if (m_vaoId)
		{
			glDeleteVertexArrays(1, &m_vaoId);
			glDeleteBuffers(2, m_vboIds);
			m_vaoId = 0;
		}

		if (m_programId)
		{
			glDeleteProgram(m_programId);
			m_programId = 0;
		}
	}

	void Vertex(const Vec2& v, const Color& c)
	{
		if (m_count == e_maxVertices)
			Flush();

		m_vertices[m_count] = v;
		m_colors[m_count] = c;
		++m_count;
	}

    void Flush()
	{
        if (m_count == 0)
            return;
        
		glUseProgram(m_programId);
        
		const auto proj = GetProjectionMatrix(g_camera, 0.2f);
		
		glUniformMatrix4fv(m_projectionUniform, 1, GL_FALSE, proj.m);
        
		glBindVertexArray(m_vaoId);
        
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[0]);
		glBufferSubData(GL_ARRAY_BUFFER, 0, m_count * sizeof(Vec2), m_vertices);
        
		glBindBuffer(GL_ARRAY_BUFFER, m_vboIds[1]);
		glBufferSubData(GL_ARRAY_BUFFER, 0, m_count * sizeof(Color), m_colors);
        
        glEnable(GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDrawArrays(GL_TRIANGLES, 0, m_count);
        glDisable(GL_BLEND);
        
		sCheckGLError();
        
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		glUseProgram(0);
        
		m_count = 0;
	}
    
	enum { e_maxVertices = 3 * 512 };
	Vec2 m_vertices[e_maxVertices];
	Color m_colors[e_maxVertices];

	int32 m_count;

	GLuint m_vaoId;
	GLuint m_vboIds[2];
	GLuint m_programId;
	GLint m_projectionUniform;
	GLint m_vertexAttribute;
	GLint m_colorAttribute;
};

//
DebugDraw::DebugDraw()
{
	m_points = nullptr;
    m_lines = nullptr;
    m_triangles = nullptr;
}

//
DebugDraw::~DebugDraw()
{
	assert(m_points == nullptr);
	assert(m_lines == nullptr);
	assert(m_triangles == nullptr);
}

//
void DebugDraw::Create()
{
	m_points = new GLRenderPoints;
	m_points->Create();
	m_lines = new GLRenderLines;
	m_lines->Create();
	m_triangles = new GLRenderTriangles;
	m_triangles->Create();
}

//
void DebugDraw::Destroy()
{
	m_points->Destroy();
	delete m_points;
	m_points = nullptr;

	m_lines->Destroy();
	delete m_lines;
	m_lines = nullptr;

	m_triangles->Destroy();
	delete m_triangles;
	m_triangles = nullptr;
}

//
void DebugDraw::DrawPolygon(const Vec2* vertices, size_type vertexCount, const Color& color)
{
    Vec2 p1 = vertices[vertexCount - 1];
	for (auto i = decltype(vertexCount){0}; i < vertexCount; ++i)
	{
        Vec2 p2 = vertices[i];
		m_lines->Vertex(p1, color);
		m_lines->Vertex(p2, color);
        p1 = p2;
	}
}

//
void DebugDraw::DrawSolidPolygon(const Vec2* vertices, size_type vertexCount, const Color& color)
{
	Color fillColor(0.5f * color.r, 0.5f * color.g, 0.5f * color.b, 0.5f);

    for (auto i = decltype(vertexCount){1}; i < vertexCount - 1; ++i)
    {
        m_triangles->Vertex(vertices[0], fillColor);
        m_triangles->Vertex(vertices[i], fillColor);
        m_triangles->Vertex(vertices[i+1], fillColor);
    }

    Vec2 p1 = vertices[vertexCount - 1];
	for (auto i = decltype(vertexCount){0}; i < vertexCount; ++i)
	{
        Vec2 p2 = vertices[i];
		m_lines->Vertex(p1, color);
		m_lines->Vertex(p2, color);
        p1 = p2;
	}
}

//
void DebugDraw::DrawCircle(const Vec2& center, float_t radius, const Color& color)
{
	const float_t k_segments = 16.0f;
	const float_t k_increment = 2.0f * Pi / k_segments;
    float_t sinInc = sinf(k_increment);
    float_t cosInc = cosf(k_increment);
    Vec2 r1(1.0f, 0.0f);
    Vec2 v1 = center + radius * r1;
	for (int32 i = 0; i < k_segments; ++i)
	{
        // Perform rotation to avoid additional trigonometry.
        Vec2 r2;
        r2.x = cosInc * r1.x - sinInc * r1.y;
        r2.y = sinInc * r1.x + cosInc * r1.y;
		Vec2 v2 = center + radius * r2;
        m_lines->Vertex(v1, color);
        m_lines->Vertex(v2, color);
        r1 = r2;
        v1 = v2;
	}
}

//
void DebugDraw::DrawSolidCircle(const Vec2& center, float_t radius, const Vec2& axis, const Color& color)
{
	const float_t k_segments = 16.0f;
	const float_t k_increment = 2.0f * Pi / k_segments;
    float_t sinInc = sinf(k_increment);
    float_t cosInc = cosf(k_increment);
    Vec2 v0 = center;
    Vec2 r1(cosInc, sinInc);
    Vec2 v1 = center + radius * r1;
	Color fillColor(0.5f * color.r, 0.5f * color.g, 0.5f * color.b, 0.5f);
	for (int32 i = 0; i < k_segments; ++i)
	{
        // Perform rotation to avoid additional trigonometry.
        Vec2 r2;
        r2.x = cosInc * r1.x - sinInc * r1.y;
        r2.y = sinInc * r1.x + cosInc * r1.y;
		Vec2 v2 = center + radius * r2;
		m_triangles->Vertex(v0, fillColor);
        m_triangles->Vertex(v1, fillColor);
        m_triangles->Vertex(v2, fillColor);
        r1 = r2;
        v1 = v2;
	}

    r1 = Vec2(1.0f, 0.0f);
    v1 = center + radius * r1;
	for (int32 i = 0; i < k_segments; ++i)
	{
        Vec2 r2;
        r2.x = cosInc * r1.x - sinInc * r1.y;
        r2.y = sinInc * r1.x + cosInc * r1.y;
		Vec2 v2 = center + radius * r2;
        m_lines->Vertex(v1, color);
        m_lines->Vertex(v2, color);
        r1 = r2;
        v1 = v2;
	}

    // Draw a line fixed in the circle to animate rotation.
	Vec2 p = center + radius * axis;
	m_lines->Vertex(center, color);
	m_lines->Vertex(p, color);
}

//
void DebugDraw::DrawSegment(const Vec2& p1, const Vec2& p2, const Color& color)
{
	m_lines->Vertex(p1, color);
	m_lines->Vertex(p2, color);
}

//
void DebugDraw::DrawTransform(const Transformation& xf)
{
	const float_t k_axisScale = 0.4f;
    Color red(1.0f, 0.0f, 0.0f);
    Color green(0.0f, 1.0f, 0.0f);
	Vec2 p1 = xf.p, p2;

	m_lines->Vertex(p1, red);
	p2 = p1 + k_axisScale * GetXAxis(xf.q);
	m_lines->Vertex(p2, red);

	m_lines->Vertex(p1, green);
	p2 = p1 + k_axisScale * GetYAxis(xf.q);
	m_lines->Vertex(p2, green);
}

void DebugDraw::DrawPoint(const Vec2& p, float_t size, const Color& color)
{
    m_points->Vertex(p, color, size);
}

void DebugDraw::DrawString(int x, int y, const char *string, ...)
{
	float_t h = float_t(g_camera.m_height);

	char buffer[128];

	va_list arg;
	va_start(arg, string);
	vsprintf(buffer, string, arg);
	va_end(arg);

	AddGfxCmdText(float(x), h - float(y), TEXT_ALIGN_LEFT, buffer, SetRGBA(230, 153, 153, 255));
}

void DebugDraw::DrawString(const Vec2& pw, const char *string, ...)
{
	Vec2 ps = ConvertWorldToScreen(g_camera, pw);
	float_t h = float_t(g_camera.m_height);

	char buffer[128];

	va_list arg;
	va_start(arg, string);
	vsprintf(buffer, string, arg);
	va_end(arg);

	AddGfxCmdText(ps.x, h - ps.y, TEXT_ALIGN_LEFT, buffer, SetRGBA(230, 153, 153, 255));
}

void DebugDraw::DrawAABB(AABB* aabb, const Color& c)
{
    Vec2 p1 = aabb->GetLowerBound();
    Vec2 p2 = Vec2(aabb->GetUpperBound().x, aabb->GetLowerBound().y);
    Vec2 p3 = aabb->GetUpperBound();
    Vec2 p4 = Vec2(aabb->GetLowerBound().x, aabb->GetUpperBound().y);
    
    m_lines->Vertex(p1, c);
    m_lines->Vertex(p2, c);

    m_lines->Vertex(p2, c);
    m_lines->Vertex(p3, c);

    m_lines->Vertex(p3, c);
    m_lines->Vertex(p4, c);

    m_lines->Vertex(p4, c);
    m_lines->Vertex(p1, c);
}

//
void DebugDraw::Flush()
{
    m_triangles->Flush();
    m_lines->Flush();
    m_points->Flush();
}
	
void DebugDraw::Draw(const World& world)
{
	const auto flags = GetFlags();
	
	if (flags & Drawer::e_shapeBit)
	{
		for (auto&& b: world.GetBodies())
		{
			const auto xf = b.GetTransformation();
			for (auto&& f: b.GetFixtures())
			{
				if (!b.IsActive())
				{
					Draw(f, xf, Color(0.5f, 0.5f, 0.3f));
				}
				else if (b.GetType() == BodyType::Static)
				{
					Draw(f, xf, Color(0.5f, 0.9f, 0.5f));
				}
				else if (b.GetType() == BodyType::Kinematic)
				{
					Draw(f, xf, Color(0.5f, 0.5f, 0.9f));
				}
				else if (!b.IsAwake())
				{
					Draw(f, xf, Color(0.6f, 0.6f, 0.6f));
				}
				else
				{
					Draw(f, xf, Color(0.9f, 0.7f, 0.7f));
				}
			}
		}
	}
	
	if (flags & Drawer::e_jointBit)
	{
		for (auto&& j: world.GetJoints())
		{
			Draw(j);
		}
	}
	
	if (flags & Drawer::e_pairBit)
	{
		//const Color color(0.3f, 0.9f, 0.9f);
		//for (auto&& c: m_contactMgr.GetContacts())
		//{
		//Fixture* fixtureA = c.GetFixtureA();
		//Fixture* fixtureB = c.GetFixtureB();
		
		//Vec2 cA = fixtureA->GetAABB().GetCenter();
		//Vec2 cB = fixtureB->GetAABB().GetCenter();
		
		//draw.DrawSegment(cA, cB, color);
		//}
	}
	
	if (flags & Drawer::e_aabbBit)
	{
		const Color color(0.9f, 0.3f, 0.9f);
		const auto bp = &world.GetContactManager().m_broadPhase;
		
		for (auto&& b: world.GetBodies())
		{
			if (!b.IsActive())
			{
				continue;
			}
			
			for (auto&& f: b.GetFixtures())
			{
				const auto proxy_count = f.GetProxyCount();
				for (auto i = decltype(proxy_count){0}; i < proxy_count; ++i)
				{
					const auto proxy = f.GetProxy(i);
					const auto aabb = bp->GetFatAABB(proxy->proxyId);
					Vec2 vs[4];
					vs[0] = Vec2{aabb.GetLowerBound().x, aabb.GetLowerBound().y};
					vs[1] = Vec2{aabb.GetUpperBound().x, aabb.GetLowerBound().y};
					vs[2] = Vec2{aabb.GetUpperBound().x, aabb.GetUpperBound().y};
					vs[3] = Vec2{aabb.GetLowerBound().x, aabb.GetUpperBound().y};
					
					DrawPolygon(vs, 4, color);
				}
			}
		}
	}
	
	if (flags & Drawer::e_centerOfMassBit)
	{
		for (auto&& b: world.GetBodies())
		{
			auto xf = b.GetTransformation();
			xf.p = b.GetWorldCenter();
			DrawTransform(xf);
		}
	}
}

void DebugDraw::Draw(const Fixture& fixture, const Transformation& xf, const Color& color)
{
	switch (GetType(fixture))
	{
		case Shape::e_circle:
		{
			const auto circle = static_cast<const CircleShape*>(fixture.GetShape());
			const auto center = Transform(circle->GetPosition(), xf);
			const auto radius = circle->GetRadius();
			const auto axis = Rotate(Vec2{float_t{1}, float_t{0}}, xf.q);
			DrawSolidCircle(center, radius, axis, color);
		}
			break;
			
		case Shape::e_edge:
		{
			const auto edge = static_cast<const EdgeShape*>(fixture.GetShape());
			const auto v1 = Transform(edge->GetVertex1(), xf);
			const auto v2 = Transform(edge->GetVertex2(), xf);
			DrawSegment(v1, v2, color);
		}
			break;
			
		case Shape::e_chain:
		{
			const auto chain = static_cast<const ChainShape*>(fixture.GetShape());
			const auto count = chain->GetVertexCount();
			auto v1 = Transform(chain->GetVertex(0), xf);
			for (auto i = decltype(count){1}; i < count; ++i)
			{
				const auto v2 = Transform(chain->GetVertex(i), xf);
				DrawSegment(v1, v2, color);
				DrawCircle(v1, float_t(0.05), color);
				v1 = v2;
			}
		}
			break;
			
		case Shape::e_polygon:
		{
			const auto poly = static_cast<const PolygonShape*>(fixture.GetShape());
			const auto vertexCount = poly->GetVertexCount();
			assert(vertexCount <= MaxPolygonVertices);
			Vec2 vertices[MaxPolygonVertices];
			for (auto i = decltype(vertexCount){0}; i < vertexCount; ++i)
			{
				vertices[i] = Transform(poly->GetVertex(i), xf);
			}
			DrawSolidPolygon(vertices, vertexCount, color);
		}
			break;
			
		default:
			break;
	}
}

void DebugDraw::Draw(const Joint& joint)
{
	const auto bodyA = joint.GetBodyA();
	const auto bodyB = joint.GetBodyB();
	const auto xf1 = bodyA->GetTransformation();
	const auto xf2 = bodyB->GetTransformation();
	const auto x1 = xf1.p;
	const auto x2 = xf2.p;
	const auto p1 = joint.GetAnchorA();
	const auto p2 = joint.GetAnchorB();
	
	const Color color(float_t(0.5), float_t(0.8), float_t(0.8));
	
	switch (joint.GetType())
	{
		case JointType::Distance:
			DrawSegment(p1, p2, color);
			break;
			
		case JointType::Pulley:
		{
			const auto pulley = static_cast<const PulleyJoint&>(joint);
			const auto s1 = pulley.GetGroundAnchorA();
			const auto s2 = pulley.GetGroundAnchorB();
			DrawSegment(s1, p1, color);
			DrawSegment(s2, p2, color);
			DrawSegment(s1, s2, color);
		}
			break;
			
		case JointType::Mouse:
			// don't draw this
			break;
			
		default:
			DrawSegment(x1, p1, color);
			DrawSegment(p1, p2, color);
			DrawSegment(x2, p2, color);
	}
}

void DebugDraw::Draw(const Rope& rope)
{
	const auto c = Color(float_t(0.4), float_t(0.5), float_t(0.7));
	
	const auto count = rope.GetVertexCount();
	for (auto i = decltype(count - 1){0}; i < count - 1; ++i)
	{
		DrawSegment(rope.GetVertex(i), rope.GetVertex(i + 1), c);
	}
}
	
}