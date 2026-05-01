#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <learnopengl/filesystem.h>
#include <learnopengl/shader_m.h>
#include <learnopengl/camera.h>
#include <learnopengl/animator.h>
#include <learnopengl/model_animation.h>



#include <iostream>


void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// camera
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ----- character control -----
glm::vec3 charPosition(0.0f, 0.0f, 0.0f);
float     charYaw = 0.0f;        // rotation around Y, in degrees
const float MOVE_SPEED = 1.5f;   // world units per second

// jump: animation-only, no physics. We just lock the state for the duration
// of the Jump.dae clip and then return to idle.
bool  isJumping  = false;
float jumpTimer  = 0.0f;

enum CharState { CS_IDLE, CS_WALK_FWD, CS_WALK_BACK, CS_STRAFE_LEFT, CS_STRAFE_RIGHT, CS_JUMP, CS_MOONWALK };
CharState currentState = CS_IDLE;

Animation* animIdle        = nullptr;
Animation* animWalk        = nullptr;
Animation* animWalkBack    = nullptr;
Animation* animStrafeLeft  = nullptr;
Animation* animStrafeRight = nullptr;
Animation* animJump        = nullptr;
Animation* animMoonwalk    = nullptr;
Animator*  animatorPtr     = nullptr;

void switchAnim(CharState newState)
{
	if (newState == currentState) return;
	currentState = newState;
	switch (newState)
	{
		case CS_IDLE:         animatorPtr->PlayAnimation(animIdle);        break;
		case CS_WALK_FWD:     animatorPtr->PlayAnimation(animWalk);        break;
		case CS_WALK_BACK:    animatorPtr->PlayAnimation(animWalkBack);    break;
		case CS_STRAFE_LEFT:  animatorPtr->PlayAnimation(animStrafeLeft);  break;
		case CS_STRAFE_RIGHT: animatorPtr->PlayAnimation(animStrafeRight); break;
		case CS_JUMP:         animatorPtr->PlayAnimation(animJump);        break;
		case CS_MOONWALK:     animatorPtr->PlayAnimation(animMoonwalk);    break;
	}
}

int main()
{
	// glfw: initialize and configure
	// ------------------------------
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

	// glfw window creation
	// --------------------
	GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
	if (window == NULL)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetScrollCallback(window, scroll_callback);

	// tell GLFW to capture our mouse
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// glad: load all OpenGL function pointers
	// ---------------------------------------
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}

	// tell stb_image.h to flip loaded texture's on the y-axis (before loading model).
	stbi_set_flip_vertically_on_load(true);

	// configure global opengl state
	// -----------------------------
	glEnable(GL_DEPTH_TEST);

	// build and compile shaders
	// -------------------------
	Shader ourShader("anim_model.vs", "anim_model.fs");

	// ----- floor: green plane with grid lines (inline shader) -----
	const char* floorVS = R"GLSL(
		#version 330 core
		layout (location = 0) in vec3 aPos;
		uniform mat4 model;
		uniform mat4 view;
		uniform mat4 projection;
		out vec3 worldPos;
		void main() {
			vec4 wp = model * vec4(aPos, 1.0);
			worldPos = wp.xyz;
			gl_Position = projection * view * wp;
		}
	)GLSL";
	const char* floorFS = R"GLSL(
		#version 330 core
		in  vec3 worldPos;
		out vec4 FragColor;
		void main() {
			vec3 grass = vec3(0.20, 0.65, 0.25);
			vec3 line  = vec3(0.10, 0.35, 0.15);
			// distance to nearest integer grid line in X/Z
			vec2 d2 = min(fract(worldPos.xz), 1.0 - fract(worldPos.xz));
			float d = min(d2.x, d2.y);
			float gridIntensity = 1.0 - smoothstep(0.0, 0.03, d);
			FragColor = vec4(mix(grass, line, gridIntensity), 1.0);
		}
	)GLSL";
	auto compileSh = [](GLenum type, const char* src) -> unsigned int {
		unsigned int s = glCreateShader(type);
		glShaderSource(s, 1, &src, NULL);
		glCompileShader(s);
		int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, NULL, log);
		           std::cout << "Floor shader error:\n" << log << std::endl; }
		return s;
	};
	unsigned int fvs = compileSh(GL_VERTEX_SHADER,   floorVS);
	unsigned int ffs = compileSh(GL_FRAGMENT_SHADER, floorFS);
	unsigned int floorProgram = glCreateProgram();
	glAttachShader(floorProgram, fvs); glAttachShader(floorProgram, ffs);
	glLinkProgram(floorProgram);
	glDeleteShader(fvs); glDeleteShader(ffs);

	// 100x100 quad on y=0 (we translate it to y=-0.4 to meet the character's feet)
	float floorVerts[] = {
		-50.0f, 0.0f, -50.0f,   50.0f, 0.0f, -50.0f,   50.0f, 0.0f,  50.0f,
		-50.0f, 0.0f, -50.0f,   50.0f, 0.0f,  50.0f,  -50.0f, 0.0f,  50.0f,
	};
	unsigned int floorVAO, floorVBO;
	glGenVertexArrays(1, &floorVAO);
	glGenBuffers(1, &floorVBO);
	glBindVertexArray(floorVAO);
	glBindBuffer(GL_ARRAY_BUFFER, floorVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(floorVerts), floorVerts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);


	// load models
	// -----------
	Model ourModel(FileSystem::getPath("resources/objects/character/goldshi.dae"));
	Animation idleAnimation       (FileSystem::getPath("resources/objects/character/Standing Idle.dae"),          &ourModel);
	Animation walkAnimation       (FileSystem::getPath("resources/objects/character/Walking.dae"),                &ourModel);
	Animation walkBackAnimation   (FileSystem::getPath("resources/objects/character/Walking Backwards.dae"),      &ourModel);
	Animation strafeLeftAnimation (FileSystem::getPath("resources/objects/character/Left Strafe Walking.dae"),    &ourModel);
	Animation strafeRightAnimation(FileSystem::getPath("resources/objects/character/Right Strafe Walking.dae"),   &ourModel);
	Animation jumpAnimation       (FileSystem::getPath("resources/objects/character/Jump.dae"),                   &ourModel);
	Animation moonwalkAnimation   (FileSystem::getPath("resources/objects/character/Moonwalk.dae"),               &ourModel);

	animIdle        = &idleAnimation;
	animWalk        = &walkAnimation;
	animWalkBack    = &walkBackAnimation;
	animStrafeLeft  = &strafeLeftAnimation;
	animStrafeRight = &strafeRightAnimation;
	animJump        = &jumpAnimation;
	animMoonwalk    = &moonwalkAnimation;

	Animator animator(&idleAnimation);
	animatorPtr = &animator;


	// draw in wireframe
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	// render loop
	// -----------
	while (!glfwWindowShouldClose(window))
	{
		// per-frame time logic
		// --------------------
		float currentFrame = glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		// input
		// -----
		processInput(window);

		// jump: count down the animation duration, then return to idle
		if (isJumping)
		{
			jumpTimer -= deltaTime;
			if (jumpTimer <= 0.0f)
			{
				isJumping = false;
				if (currentState == CS_JUMP) switchAnim(CS_IDLE);
			}
		}

		animator.UpdateAnimation(deltaTime);

		// render
		// ------
		glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// view/projection transformations
		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
		// Camera follows the character from behind. If you see the front
		// instead of the back, flip the sign of the Z offset.
		glm::vec3 cameraPos = charPosition + glm::vec3(0.0f, 1.0f, -3.0f);
		glm::vec3 lookAt    = charPosition + glm::vec3(0.0f, 0.5f,  0.0f);
		glm::mat4 view = glm::lookAt(cameraPos, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));

		// ----- draw floor (green grid, world-aligned so it appears to scroll) -----
		glUseProgram(floorProgram);
		glm::mat4 floorModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.4f, 0.0f));
		glUniformMatrix4fv(glGetUniformLocation(floorProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(glGetUniformLocation(floorProgram, "view"),       1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(glGetUniformLocation(floorProgram, "model"),      1, GL_FALSE, glm::value_ptr(floorModel));
		glBindVertexArray(floorVAO);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		// don't forget to enable shader before setting uniforms
		ourShader.use();
		ourShader.setMat4("projection", projection);
		ourShader.setMat4("view", view);

        auto transforms = animator.GetFinalBoneMatrices();
		for (int i = 0; i < transforms.size(); ++i)
			ourShader.setMat4("finalBonesMatrices[" + std::to_string(i) + "]", transforms[i]);


		// render the loaded model at the character's current position.
		// charYaw is set by processInput (for moonwalk it tracks the move dir).
		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, charPosition + glm::vec3(0.0f, -0.4f, 0.0f));
		model = glm::rotate(model, glm::radians(charYaw), glm::vec3(0.0f, 1.0f, 0.0f));
		model = glm::scale(model, glm::vec3(500.0f, 500.0f, 500.0f));
		ourShader.setMat4("model", model);
		ourModel.Draw(ourShader);


		// glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
		// -------------------------------------------------------------------------------
		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glDeleteVertexArrays(1, &floorVAO);
	glDeleteBuffers(1, &floorVBO);
	glDeleteProgram(floorProgram);

	// glfw: terminate, clearing all previously allocated GLFW resources.
	// ------------------------------------------------------------------
	glfwTerminate();
	return 0;
}

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow* window)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	bool wPressed     = glfwGetKey(window, GLFW_KEY_W)           == GLFW_PRESS;
	bool sPressed     = glfwGetKey(window, GLFW_KEY_S)           == GLFW_PRESS;
	bool aPressed     = glfwGetKey(window, GLFW_KEY_A)           == GLFW_PRESS;
	bool dPressed     = glfwGetKey(window, GLFW_KEY_D)           == GLFW_PRESS;
	bool spacePressed = glfwGetKey(window, GLFW_KEY_SPACE)       == GLFW_PRESS;
	bool shiftPressed = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS
	                 || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

	// Start a jump on SPACE if we're on the ground. No physics -- we just lock
	// the state for the length of the clip.
	if (spacePressed && !isJumping)
	{
		isJumping = true;
		jumpTimer = animJump->GetDuration() / animJump->GetTicksPerSecond();
		switchAnim(CS_JUMP);
	}

	// Character "forward" is +Z (camera sits at -Z behind it). Strafing matches
	// the camera's left/right.
	glm::vec3 move(0.0f);
	if (wPressed) move.z += 1.0f;
	if (sPressed) move.z -= 1.0f;
	if (aPressed) move.x += 1.0f;
	if (dPressed) move.x -= 1.0f;

	// Freeze horizontal movement while jumping -- character stays in place
	// and just plays the jump animation through.
	if (!isJumping && glm::length(move) > 0.0f)
	{
		move = glm::normalize(move) * MOVE_SPEED * deltaTime;
		charPosition.x += move.x;
		charPosition.z += move.z;
	}

	// Animation choice: don't override the jump while in the air.
	// Holding SHIFT while moving plays the Moonwalk animation regardless of
	// which direction key is held.
	if (!isJumping)
	{
		bool moving = wPressed || sPressed || aPressed || dPressed;
		if      (shiftPressed && moving) switchAnim(CS_MOONWALK);
		else if (wPressed)               switchAnim(CS_WALK_FWD);
		else if (sPressed)               switchAnim(CS_WALK_BACK);
		else if (aPressed)               switchAnim(CS_STRAFE_LEFT);
		else if (dPressed)               switchAnim(CS_STRAFE_RIGHT);
		else                             switchAnim(CS_IDLE);
	}

	// Yaw: while moonwalking the character faces the OPPOSITE of the
	// movement direction (works for forward, back, left, right, and
	// diagonals). Otherwise face forward (yaw=0).
	if (currentState == CS_MOONWALK && glm::length(move) > 0.0f)
	{
		glm::vec3 facing = -glm::normalize(glm::vec3(move.x, 0.0f, move.z));
		charYaw = glm::degrees(atan2(facing.x, facing.z));
	}
	else if (currentState != CS_JUMP)
	{
		charYaw = 0.0f;
	}
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	// make sure the viewport matches the new window dimensions; note that width and
	// height will be significantly larger than specified on retina displays.
	glViewport(0, 0, width, height);
}

// glfw: whenever the mouse moves, this callback is called
// -------------------------------------------------------
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
	if (firstMouse)
	{
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	}

	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

	lastX = xpos;
	lastY = ypos;

	camera.ProcessMouseMovement(xoffset, yoffset);
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	camera.ProcessMouseScroll(yoffset);
}
