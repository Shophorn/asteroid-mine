#define _CRT_SECURE_NO_WARNINGS 1
// #define UNICODE
#if defined UNICODE
	#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <GL/gl.h>
#include <glext.h>

#include <iostream>
#define Assert(expr) {	if (!(expr)){ std::cout << "[ASSERT FAILED:" << __FILE__ << ":" << __LINE__ << "]: " << #expr << "\n"; abort(); }}

// Note(Leo): this contains generated definitions
// It also uses Assert macro above
#include "win32_am_opengl_loader.h"

#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <imgui/imgui.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_opengl3.h>

#include <imgui/imgui_impl_win32.cpp>
#include <imgui/imgui_impl_opengl3.cpp>
#include <imgui/imgui.cpp>
#include <imgui/imgui_draw.cpp>
#include <imgui/imgui_widgets.cpp>
#include <imgui/imgui_demo.cpp>

#include <Assimp/Importer.hpp>
#include <Assimp/scene.h>
#include <Assimp/postprocess.h>

#include <cmath>

#define local_persist static

using s64 = signed long long;

static_assert(sizeof(s64) == 8, "");

struct v2
{
	float x, y;
};

static v2 operator + (v2 a, v2 b)
{
	a.x += b.x;
	a.y += b.y;
	return a;
}

static v2 & operator += (v2 & a, v2 b)
{
	a.x += b.x;
	a.y += b.y;
	return a;
}

static v2 operator - (v2 a, v2 b)
{
	a.x -= b.x;
	a.y -= b.y;
	return a;
}

static v2 operator * (v2 v, float f)
{
	v.x *= f;
	v.y *= f;
	return v;
}

static v2 operator / (v2 v, float f)
{
	v.x /= f;
	v.y /= f;
	return v;
}

union v3 
{
	struct
	{
		float x, y, z;
	};
	
	struct
	{
		float r, g, b;
	};

	struct
	{
		v2 xy;
		float ignored_xy;
	};
};

// Note(Leo): I constantly make mistakes trying to type capitals in this, this should fix that
namespace imgui = ImGui;


void win32_opengl_debug_callback(	GLenum source,
									GLenum type,
									GLuint id,
									GLenum severity,
									GLsizei length,
									const GLchar * message,
									void const * userParam)
{
	std::cout << "[OPENGL DEBUG]: " << message << "\n";	
}

struct Win32State
{
	bool running;
	bool showMenu;

	int windowWidth;
	int windowHeight;

	bool mouseDown;
	v2 mouseDownPosition;
	
	// bool event_mouseDown;
	bool event_mouseUp;


	float mouseScroll;
};

static Win32State * win32_am_get_user_data(HWND window)
{
	Win32State * state = reinterpret_cast<Win32State*>(GetWindowLongPtr(window, GWLP_USERDATA));
	return state;
}

static s64 win32_am_get_current_time()
{
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);
	return time.QuadPart;
}

float win32_am_elapsed_time(s64 start, s64 end)
{
	auto get_frequency = []() -> double
	{
		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		return (double)frequency.QuadPart;
	};
	local_persist double frequency = get_frequency();

	float elapsedTime = (end - start) / frequency;
	return elapsedTime;
}

v2 win32_am_get_cursor_position()
{
	POINT position;
	GetCursorPos(&position);
	v2 result = { (float)position.x, (float)position.y };
	return result;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


static LRESULT win32_am_window_callback(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
	{
		return true;
	}

	LRESULT result;

	switch(message)
	{
		case WM_CREATE:
		{
			Win32State * userData = (Win32State*)(((CREATESTRUCT*)lParam)->lpCreateParams);
			SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)userData);
		} break;

		case WM_CLOSE:
			win32_am_get_user_data(window)->running = false;
			result = 0;
			break;

		case WM_LBUTTONDOWN:
		{
			auto * state 				= win32_am_get_user_data(window);
			state->mouseDown 			= true;
			state->mouseDownPosition 	= win32_am_get_cursor_position();
			result 						= 0;
		} break;

		case WM_LBUTTONUP:
		{
			auto * state 			= win32_am_get_user_data(window);
			state->mouseDown 		= false;
			state->event_mouseUp 	= true;
			result 					= 0;
		} break;

		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE)
			{
				bool & showMenu = win32_am_get_user_data(window)->showMenu;
				showMenu = !showMenu;
				result = 0;
			}
			result = DefWindowProc(window, message, wParam, lParam);
			break;

		case WM_SIZE:
		{
			auto * state 			= win32_am_get_user_data(window);
			state->windowWidth 		= LOWORD(lParam);
			state->windowHeight 	= HIWORD(lParam);
			result 					= 0;
		} break;

		case WM_MOUSEWHEEL:
		{
			float scroll = GET_WHEEL_DELTA_WPARAM(wParam);
			win32_am_get_user_data(window)->mouseScroll = scroll / 	WHEEL_DELTA;
		} break;

		default:
			result = DefWindowProc(window, message, wParam, lParam);
			break;
	}

	return result;
}

template<typename T, int Count>
static int array_count(T const (&array)[Count])
{
	return Count;
}

static int cstring_length(char const * cstring)
{
	int count = 0;
	while(*cstring)
	{
		count 	+= 1;
		cstring += 1;
	}
	return count;
}

constexpr GLint projectionMatrixLocation 	= 0;
constexpr GLint viewMatrixLocation 			= 1;
constexpr GLint modelPositionLocation 		= 2;
constexpr GLint modelSizeLocation			= 3;

constexpr char const * vertexShaderSource =
R"glsl(#version 450

layout(location = 0) uniform mat4 projectionMatrix;
layout(location = 1) uniform mat4 viewMatrix;
layout(location = 2) uniform vec3 modelPosition;
layout(location = 3) uniform float modelSize;

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 worldPosition;

void main()
{
	worldPosition 	= modelPosition + modelSize * vertexPosition;
	gl_Position 	= projectionMatrix * viewMatrix * vec4(worldPosition, 1);
	normal 			= vertexNormal;
}

)glsl";

constexpr GLint colorLocation 			= 4;
constexpr GLint fragShaderModeLocation 	= 5;

enum FragShaderMode : int
{
	FragShaderMode_planet = 0,
	FragShaderMode_star = 1,
};

constexpr char const * fragmentShaderSource = 
R"glsl(#version 450

#define MODE_PLANET 0
#define MODE_STAR 1

layout(location = 4) uniform vec3 color;
layout(location = 5) uniform int mode;

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 worldPosition;

out vec4 outColor;

void main()
{
	if (mode == MODE_PLANET)
	{
		vec3 lightDirection = normalize(worldPosition);
		float ndotl 		= dot(normalize(normal), -lightDirection);
		outColor 			= vec4(color * ndotl, 1.0);
	}
	else
	{
		outColor = vec4(color, 1.0);
	}
}

)glsl";

// MATH CONSTANTS
constexpr float pi 			= 3.14159265359f;
constexpr float toRadians 	= pi / 180.0f;

int main()
{
	TCHAR const * className = TEXT("Asteroid Mining Class");
	TCHAR const * windowName = TEXT("Asteroid Mining");

	Win32State state 	= {};
	state.windowWidth 	= 960;
	state.windowHeight 	= 540;

	HINSTANCE hInstance = GetModuleHandle(nullptr);

	WNDCLASSEX classInfo 	= {};
	classInfo.cbSize 		= sizeof(classInfo);
	// Todo(Leo): We probably do not need any of these :D
	classInfo.style 		= CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	classInfo.lpfnWndProc	= win32_am_window_callback;
	classInfo.hInstance 	= hInstance;
	classInfo.lpszClassName = className;

	if (RegisterClassEx(&classInfo) == 0)
	{
		return 1;
	}

	HWND window = CreateWindow(	className,
								windowName,
								WS_OVERLAPPEDWINDOW | WS_VISIBLE,
								20, 20,
								state.windowWidth, state.windowHeight, 
								nullptr, nullptr,
								hInstance,
								&state);

	if (window == nullptr)
	{
		return 2;
	}

	// SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)&state);

	// -----------------------------------------------------------------
	// OPENGL

	HDC deviceContext;
	HGLRC openGLContext;
	{
		PIXELFORMATDESCRIPTOR pixelFormatDescriptor = {};
		pixelFormatDescriptor.nSize 				= sizeof(PIXELFORMATDESCRIPTOR);
		pixelFormatDescriptor.nVersion 				= 1;
		pixelFormatDescriptor.dwFlags 				= PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pixelFormatDescriptor.iPixelType			= PFD_TYPE_RGBA;
		pixelFormatDescriptor.cColorBits 			= 32;
		pixelFormatDescriptor.cDepthBits 			= 24;
		pixelFormatDescriptor.cStencilBits 			= 8;
		
		pixelFormatDescriptor.cAlphaBits 			= 0;
		pixelFormatDescriptor.cAccumBits 			= 0;
		pixelFormatDescriptor.cAuxBuffers 			= 0;

		deviceContext = GetDC(window);

		int pixelFormat = ChoosePixelFormat(deviceContext, &pixelFormatDescriptor);
		SetPixelFormat(deviceContext, pixelFormat, &pixelFormatDescriptor);

		openGLContext = wglCreateContext(deviceContext);
		wglMakeCurrent(deviceContext, openGLContext);

		win32_am_load_opengl_functions();
	}

	glDebugMessageCallback(win32_opengl_debug_callback, nullptr);

	win32_am_get_user_data(window)->running = true;

	v3 backgroundColor = {0.1, 0.2, 0.25};
	glClearColor(backgroundColor.r, backgroundColor.g, backgroundColor.b, 1.0);
	glClearDepth(1.0);
	glClearStencil(1);

	std::cout << "renderer: " << glGetString(GL_RENDERER) << "\n";
	std::cout << "version: " << glGetString(GL_VERSION) << "\n";


	// Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    // ImGui::StyleColorsDark();
    ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(window);
    ImGui_ImplOpenGL3_Init("#version 150");
    // ImGui_ImplDX9_Init(g_pd3dDevice);

	// CREATE SHADERS
	GLint shaderProgram;
	{
		auto compile_shader = [](GLenum type, char const * source)
		{
			GLuint shader = glCreateShader(type);
			GLint sourceLength = cstring_length(source);
			glShaderSource(shader, 1, &source, nullptr);
			glCompileShader(shader);

			GLint success = GL_FALSE;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

			if(success == GL_FALSE)
			{
				char buffer [1024] = {};
				int length = array_count(buffer);
				glGetShaderInfoLog(shader, length, &length, buffer);
				buffer[array_count(buffer) - 1] = 0;

				std::cout << "[SHADER ERROR]: " << buffer << "\n";

				// Assert(false && "Shader error!");
				shader = 0;
			}
			return shader;
		};

		GLuint vertexShader = compile_shader(GL_VERTEX_SHADER, vertexShaderSource);
		GLuint fragmentShader = compile_shader(GL_FRAGMENT_SHADER, fragmentShaderSource);

		Assert(vertexShader != 0);
		Assert(fragmentShader != 0);

		shaderProgram = glCreateProgram();
		glAttachShader(shaderProgram, vertexShader);
		glAttachShader(shaderProgram, fragmentShader);
		glLinkProgram(shaderProgram);

		glUseProgram(shaderProgram);

		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		std::cout << "Hello from after shader, " << shaderProgram << "\n";
	}

	Assimp::Importer importer;
	aiScene const * scene = importer.ReadFile("planet.glb", aiProcess_Triangulate | aiProcess_GenNormals);

	std::cout << scene << "\n";
	std::cout << scene->mNumMeshes << " meshes in scene\n";

	std::cout << scene->mRootNode->mName.C_Str() << "\n";
	std::cout << scene->mRootNode->mNumChildren << " children on root node\n";

	int assimpMeshIndex = scene->mRootNode->mMeshes[0];

	aiMesh * mesh = scene->mMeshes[assimpMeshIndex];


	int assimpVertexCount = mesh->mNumVertices;
	int assimpFaceCount = mesh->mNumFaces;

	std::cout << assimpVertexCount << " vertices\n";

	int meshMemorSize 		= 1024 * 1024;
	int meshMemoryUsed 		= 0;
	char * meshMemoryBlock 	= (char*)malloc(meshMemorSize);

	v3 * positions = (v3*)(meshMemoryBlock + meshMemoryUsed);
	meshMemoryUsed += sizeof(v3) * assimpVertexCount;

	v3 * normals = (v3*)(meshMemoryBlock + meshMemoryUsed);
	meshMemoryUsed += sizeof(v3) * assimpVertexCount;

	for (int i = 0; i < assimpVertexCount; ++i)
	{
		positions[i].x 	= mesh->mVertices[i].x;
		positions[i].y 	= mesh->mVertices[i].y;
		positions[i].z 	= mesh->mVertices[i].z;

		normals[i].x 	= mesh->mNormals[i].x;
		normals[i].y 	= mesh->mNormals[i].y;
		normals[i].z 	= mesh->mNormals[i].z;
	} 

	int vertexCount = assimpVertexCount;
	std::cout << vertexCount << " vertices\n";
	std::cout << assimpVertexCount << " vertices\n";

	int vertexSize = sizeof(float) * 3;

	int indexCount = assimpFaceCount * 3;

	short * indices = (short*)(meshMemoryBlock + meshMemoryUsed);
	meshMemoryUsed += sizeof(short) * indexCount;

	for(int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
	{
		int i = 3 * faceIndex;

		indices[i] = mesh->mFaces[faceIndex].mIndices[0];
		indices[i + 1] = mesh->mFaces[faceIndex].mIndices[1];
		indices[i + 2] = mesh->mFaces[faceIndex].mIndices[2];
	}

	GLuint vertexArrayThing;
	glGenVertexArrays(1, &vertexArrayThing);
	glBindVertexArray(vertexArrayThing);


	GLuint vertexBuffer;
	glGenBuffers(1, &vertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, vertexSize * vertexCount, positions, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, false, 0, 0);

	GLuint normalBuffer;
	glGenBuffers(1, &normalBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, normalBuffer);
	glBufferData(GL_ARRAY_BUFFER, vertexSize * vertexCount, normals, GL_STATIC_DRAW);

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, false, 0, 0);

	GLuint indexBuffer;
	glGenBuffers(1, &indexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(short) * indexCount, indices, GL_STATIC_DRAW);


	bool enableNormals = true;
	v3 planetPosition = {0,0,0};
	v3 cameraPosition = {0,0,-15};
	float cameraMoveSpeed = 0.002;
	v2 cameraOffsetFromMouseInput = {0,0};

	// float fov = 60;
	float fov = 12; // Note(Leo): for now, for zoom

	float planetRenderScale 		= 1000000;
	float sunRenderScale 			= 50000;
	float distanceScale 			= 0.1;
	float timeScaleDaysPerSecond 	= 10;

	v3 sunColor 	= {1.0, 0.95, 0};
	float sunSizeAu 	= 9.29158e-06;

	struct OrbitingBody
	{
		v3 		color;

		float 	diameterAu;
		float 	distanceAu;
		float 	orbitalPeriodDays;

		// Note(Leo): These represent state
		float 	phase;
		v3 		position;
	};

	enum Planet
	{
		Planet_mercury,
		Planet_venus,
		Planet_earth,
		Planet_mars,
		Planet_jupiter,
		Planet_saturn,
		Planet_uranus,
		Planet_neptune,

		PlanetCount = 8
	};

	OrbitingBody planets [PlanetCount];

	planets[Planet_mercury] = 
	{
		.color 				= {0.6, 0.6, 0.6},

		.diameterAu 		= 3.26141e-08,
		.distanceAu 		= 0.387098,
		.orbitalPeriodDays 	= 87.9691,
	};

	planets[Planet_venus] = 
	{
		.color 				= {1.0, 0.98, 0.95},

		.diameterAu 		= 8.09102e-08,
		.distanceAu 		= 0.7233265,
		.orbitalPeriodDays 	= 224.701,
	};

	planets[Planet_earth] = 
	{
		.color 				= {0.1, 0.3, 0.9},

		.diameterAu			= 8.5175e-08,
		.distanceAu 		= 1,
		.orbitalPeriodDays 	= 365.2563363004
	};

	planets[Planet_mars] =
	{
		.color 				= {0.96, 0.6, 0.3},

		.diameterAu 		= 4.53148e-08,
		.distanceAu 		= 1.52375,
		.orbitalPeriodDays 	= 686.971	
	};

	planets[Planet_jupiter] = 
	{
		.color 				= {0.65, 0.85, 0.75},

		.diameterAu 		= 9.34652e-07,
		.distanceAu 		= 5.20445,
		.orbitalPeriodDays 	= 4332.59,
	};

	planets [Planet_saturn] =
	{
		.color 				= {0.9, 0.8, 0.6},

		.diameterAu 		= 7.78487e-07,
		.distanceAu 		= 9.5,
		.orbitalPeriodDays 	= 10759.22,
	};

	planets [Planet_uranus] =
	{
		.color = {0.8, 0.85, 1.0},

		.diameterAu 		= 3.39069e-07,
		.distanceAu 		= 19.8,
		.orbitalPeriodDays 	= 30678.9,
	};

	planets [Planet_neptune] =
	{
		.color = {0.8, 0.85, 1.0},

		.diameterAu 		= 3.29176e-07,
		.distanceAu 		= 29.9215,
		.orbitalPeriodDays 	= 60262.125,
	};


	int planetCount = array_count(planets);

	glEnable(GL_DEPTH_TEST);

	// TIME
    s64 frameStartTime 	= win32_am_get_current_time();
    float elapsedTime 	= 0;

	while(state.running)
	{
		// Note(Leo): Clear events before input peeking
		state.event_mouseUp = false;
		state.mouseScroll 	= 0;

		MSG message;
		while(PeekMessage(&message, window, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&message);
			DispatchMessage(&message);
		}

		if (state.showMenu)
		{
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplWin32_NewFrame();
			imgui::NewFrame();

			imgui::Begin("Some menu thing");

			if (imgui::Button("Close this"))
			{
				state.showMenu = false;
			}

			imgui::DragFloat3("camera position", &cameraPosition.x, 0.1f);
			imgui::DragFloat("Camera Mouse move speed", &cameraMoveSpeed);
			imgui::DragFloat("Fov", &fov);
			imgui::DragFloat("Planet Render Scale", &planetRenderScale);
			imgui::DragFloat("Sun Render Scale", &sunRenderScale);
			imgui::DragFloat("Distance Scale", &distanceScale, 0.01, 0, 1);
			imgui::DragFloat("Days Per Second", &timeScaleDaysPerSecond);

			if (imgui::ColorEdit3("Background Color", (float*)&backgroundColor))
			{
				glClearColor(backgroundColor.r, backgroundColor.g, backgroundColor.b, 1.0);				
			}


			if (imgui::Checkbox("Enable normals", &enableNormals))
			{
				if (enableNormals)
				{
					glEnableVertexAttribArray(1);
				}
				else
				{
					glDisableVertexAttribArray(1);
				}
			}

			imgui::End();
		}

		// UPDATE -----------------------------------------

		bool playerInputAvailable = io.WantCaptureMouse == false;

		if (playerInputAvailable)
		{
			fov += -state.mouseScroll;
		}

		if (state.mouseDown && playerInputAvailable)
		{
			v2 currentMousePosition 	= win32_am_get_cursor_position();
			cameraOffsetFromMouseInput 	= currentMousePosition - state.mouseDownPosition;
			cameraOffsetFromMouseInput 	= cameraOffsetFromMouseInput * cameraMoveSpeed;
		}
		else
		{
			cameraPosition.xy 			+= cameraOffsetFromMouseInput;
			cameraOffsetFromMouseInput 	= {0,0};
		}

		for (int i = 0; i < planetCount; ++i)
		{
			float speed = 1.0f / planets[i].orbitalPeriodDays * timeScaleDaysPerSecond;
			planets[i].phase += elapsedTime * speed;
			if (planets[i].phase > 1)
			{
				planets[i].phase = fmodf(planets[i].phase, 1.0f);
			}

			float positionAngle = 2 * pi * planets[i].phase;
			planets[i].position =
			{
				cosf(positionAngle) * planets[i].distanceAu * distanceScale,
				sinf(positionAngle) * planets[i].distanceAu * distanceScale,
				0
			};
		}

		// RENDER

		glViewport(0, 0, state.windowWidth, state.windowHeight);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		glUseProgram(shaderProgram);
		glBindVertexArray(vertexArrayThing);

		float aspectRatio = (float)state.windowWidth / (float)state.windowHeight;

		float farPlane 	= 100.0f;
		float nearPlane = 0.1f;

		float canvasHalfHeight = tanf(toRadians * fov / 2) * nearPlane;
		
		float bottom 	= canvasHalfHeight;
		float top 		= -canvasHalfHeight;

		float right 	= bottom * aspectRatio;
		float left 		= -right;

		float 
			b = bottom,
			t = top,
			l = left,
			r = right,
			n = nearPlane,
			f = farPlane;

		float projectionMatrix [16] =
		{
			2*n / (r - l), 		0, 					0, 						0,
			0,					-2*n / (b - t),		0, 						0,
			(r + l)/(r - l), 	(t + b)/(t - b),	-((f + n)/(f - n)), 	-1,
			0,					0, 					-(2*f*n) / (f - n),		0,
		};

		float viewMatrix [16] =
		{
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			
			(cameraPosition.x + cameraOffsetFromMouseInput.x),
			(cameraPosition.y + cameraOffsetFromMouseInput.y),
			cameraPosition.z,
			1
		};


		/*
		Todo(Leo): math
			- quaternion
				- quaternion vector rotation
			- matrix
			 	- subtraction/addition
			 	- multiplication
		*/

		glUniformMatrix4fv(projectionMatrixLocation, 1, false, projectionMatrix);
		glUniformMatrix4fv(viewMatrixLocation, 1, false, viewMatrix);

		glUniform1i(fragShaderModeLocation, FragShaderMode_planet);

		for (int i = 0; i < planetCount; ++i)
		{
			glUniform3fv(modelPositionLocation, 1, (float*)&planets[i].position);
			glUniform3fv(colorLocation, 1, (float*)&planets[i].color);
			glUniform1f(modelSizeLocation, planets[i].diameterAu * planetRenderScale);
			glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, 0);
		}

		// SUN
		glUniform1i(fragShaderModeLocation, FragShaderMode_star);
		glUniform3f(modelPositionLocation, 0, 0, 0);
		glUniform3fv(colorLocation, 1, (float*)&sunColor);
		glUniform1f(modelSizeLocation, sunSizeAu * sunRenderScale);
		glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, 0);
		// glDrawArrays(GL_TRIANGLES, 0, 9);


		if (state.showMenu)
		{
			imgui::Render();
			imgui::EndFrame();
			ImGui_ImplOpenGL3_RenderDrawData(imgui::GetDrawData());
		}


		SwapBuffers(deviceContext);

		// UPDATE TIME
		s64 frameEndTime 	= win32_am_get_current_time();
		elapsedTime 		= win32_am_elapsed_time(frameStartTime, frameEndTime);
		frameStartTime 		= frameEndTime;
	}

	free(meshMemoryBlock);
}